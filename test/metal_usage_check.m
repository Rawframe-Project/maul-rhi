// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Whether Metal's shader validation reads a sampled texture right on
// this machine, with no Maul RHI code: a 1x1 texture made for shader
// reads, sampled by a fragment function into a 1x1 target. On the macOS
// 26 runner image, shader validation's texture usage check reports a
// usage mismatch on this texture and the sample reads zero; CI runs this
// first and turns off that one check where it fails, so the suite keeps
// every other check there and the check comes back once the platform is
// fixed. Prints "usage check: ok" or "usage check: broken" and exits 0
// either way; exits 1 when there is no Metal device or a step fails.
// Built with ARC: clang -fobjc-arc -framework Metal -framework Foundation.

#import <Metal/Metal.h>
#include <stdint.h>
#include <stdio.h>

static NSString* const kSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct Out { float4 position [[position]]; };\n"
     "vertex Out vs(uint i [[vertex_id]])\n"
     "{\n"
     "    const float2 corners[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};\n"
     "    return Out{float4(corners[i], 0, 1)};\n"
     "}\n"
     "fragment float4 fs(texture2d<float> image [[texture(0)]], sampler point [[sampler(0)]])\n"
     "{\n"
     "    return image.sample(point, float2(0.5));\n"
     "}\n";

// A 1x1 RGBA8 texture in shared memory for a usage.
static id<MTLTexture> MakeTexture(id<MTLDevice> device, MTLTextureUsage usage)
{
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    descriptor.usage = usage;
    descriptor.storageMode = MTLStorageModeShared;
    return [device newTextureWithDescriptor:descriptor];
}

int main(void)
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            fprintf(stderr, "usage check: no Metal device\n");
            return 1;
        }
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:kSource options:nil error:&error];
        MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"vs"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"fs"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> pipeline =
            library == nil ? nil
                           : [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (pipeline == nil)
        {
            fprintf(stderr, "usage check: %s\n", error.localizedDescription.UTF8String);
            return 1;
        }
        const uint32_t texel = 0xff00ff00u;
        id<MTLTexture> image = MakeTexture(device, MTLTextureUsageShaderRead);
        id<MTLTexture> target = MakeTexture(device, MTLTextureUsageRenderTarget);
        [image replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                 mipmapLevel:0
                   withBytes:&texel
                 bytesPerRow:4];
        id<MTLSamplerState> sampler =
            [device newSamplerStateWithDescriptor:[MTLSamplerDescriptor new]];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> render = [commands renderCommandEncoderWithDescriptor:pass];
        [render setRenderPipelineState:pipeline];
        [render setFragmentTexture:image atIndex:0];
        [render setFragmentSamplerState:sampler atIndex:0];
        [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [render endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted)
        {
            fprintf(stderr, "usage check: the command buffer failed\n");
            return 1;
        }
        uint32_t read = 0;
        [target getBytes:&read bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
        printf("usage check: %s (read %08x)\n", read == texel ? "ok" : "broken", read);
    }
    return 0;
}
