// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A textured scene under the binding model: a quad from vertex and index
// buffers, placed by a uniform buffer bound in table 0 and colored by a
// texture and a sampler bound in table 1. The scene's objects are made
// and uploaded in one frame and drawn in the next, as a program keeps
// them across frames. The texture is two by two texels sampled nearest,
// so each quarter of the quad shows one texel.

#include "harness.h"

#include "shaders/textured_container.h"

#include <stdio.h>

// The target's width and height.
#define SIZE 64

// Each vertex's position and texture coordinates.
static const float kVertices[4][4] = {
    {-1.0f, 1.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
    {-1.0f, -1.0f, 0.0f, 1.0f},
    {1.0f, -1.0f, 1.0f, 1.0f},
};
static const uint16_t kIndices[6] = {0, 1, 2, 2, 1, 3};
// The quad at half size in the target's middle.
static const float kTransform[4] = {0.5f, 0.5f, 0.0f, 0.0f};
// Red and green above, blue and white below.
static const uint8_t kTexels[2][2][4] = {
    {{255, 0, 0, 255}, {0, 255, 0, 255}},
    {{0, 0, 255, 255}, {255, 255, 255, 255}},
};

typedef struct Scene
{
    mrhiShaderId shader;
    mrhiGraphicsPipelineId pipeline;
    mrhiBufferId vertices;
    mrhiBufferId indices;
    mrhiBufferId transform;
    mrhiTextureId texture;
    mrhiSamplerId sampler;
} Scene;

static mrhiBufferId MakeBuffer(Sample* sample, uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    SampleCheck(sample, mrhiCreateBuffer(sample->device, &def, &buffer) == mrhi_success,
                "a buffer");
    return buffer;
}

static void MakeScene(Sample* sample, Scene* scene)
{
    scene->shader = SampleShader(sample, s_texturedContainer, sizeof(s_texturedContainer));
    static const mrhiVertexBufferLayout buffer = {.stride = sizeof(kVertices[0])};
    static const mrhiVertexAttribute attributes[2] = {
        {.buffer = 0, .location = 0, .format = mrhi_vertexFloat32x2, .offset = 0},
        {.buffer = 0, .location = 1, .format = mrhi_vertexFloat32x2, .offset = 8},
    };
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = scene->shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.vertexBuffers = &buffer;
    def.vertexBufferCount = 1;
    def.vertexAttributes = attributes;
    def.vertexAttributeCount = 2;
    def.colorTargetCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    scene->pipeline = SampleGraphics(sample, &def);
    scene->vertices = MakeBuffer(sample, sizeof(kVertices), mrhi_bufferVertex);
    scene->indices = MakeBuffer(sample, sizeof(kIndices), mrhi_bufferIndex);
    scene->transform = MakeBuffer(sample, sizeof(kTransform), mrhi_bufferUniform);
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 2;
    textureDef.height = 2;
    textureDef.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
    mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
    samplerDef.magFilter = mrhi_filterNearest;
    samplerDef.minFilter = mrhi_filterNearest;
    SampleCheck(sample,
                mrhiCreateTexture(sample->device, &textureDef, &scene->texture) == mrhi_success &&
                    mrhiCreateSampler(sample->device, &samplerDef, &scene->sampler) == mrhi_success,
                "a texture and a sampler");
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = 1, .layerCount = 1},
    };
}

// The first frame: every scene object's contents uploaded.
static void Upload(Sample* sample, const Scene* scene)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId v = {0};
    mrhiResourceId i = {0};
    mrhiResourceId u = {0};
    mrhiResourceId t = {0};
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiImportBuffer(device, scene->vertices, &v) == mrhi_success &&
                    mrhiImportBuffer(device, scene->indices, &i) == mrhi_success &&
                    mrhiImportBuffer(device, scene->transform, &u) == mrhi_success &&
                    mrhiImportTexture(device, scene->texture, &t) == mrhi_success,
                "an uploading frame");
    const mrhiAccess writes[4] = {
        Whole(v, mrhi_accessCopyDestination), Whole(i, mrhi_accessCopyDestination),
        Whole(u, mrhi_accessCopyDestination), Whole(t, mrhi_accessCopyDestination)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = writes;
    def.accessCount = 4;
    def.neverCull = true;
    mrhiPassId pass = {0};
    const mrhiTextureCopy texels = {.resource = t};
    const mrhiTexelLayout layout = {.bytesPerRow = 8, .rowsPerImage = 2};
    const mrhiExtent3d extent = {2, 2, 1};
    SampleCheck(
        sample,
        mrhiAddPass(device, &def, &pass) == mrhi_success &&
            mrhiCompileFrame(device) == mrhi_success &&
            mrhiBeginPass(device, pass) == mrhi_success &&
            mrhiWriteBuffer(device, pass, v, 0, kVertices, sizeof(kVertices)) == mrhi_success &&
            mrhiWriteBuffer(device, pass, i, 0, kIndices, sizeof(kIndices)) == mrhi_success &&
            mrhiWriteBuffer(device, pass, u, 0, kTransform, sizeof(kTransform)) == mrhi_success &&
            mrhiWriteTexture(device, pass, &texels, kTexels, sizeof(kTexels), &layout, &extent) ==
                mrhi_success &&
            mrhiEndPass(device, pass) == mrhi_success,
        "uploaded");
    SampleFinish(sample);
}

// Draws the quad with both tables set.
static void DrawQuad(Sample* sample, const Scene* scene, mrhiPassId pass, mrhiResourceId v,
                     mrhiResourceId i, mrhiResourceId u, mrhiResourceId t)
{
    mrhiDevice* device = sample->device;
    const mrhiBinding table0[1] = {{.slot = 0, .resource = u, .size = MRHI_WHOLE_SIZE}};
    const mrhiBinding table1[2] = {
        {.slot = 0, .resource = t, .range = {.mipCount = 1, .layerCount = 1}},
        {.slot = 1, .sampler = scene->sampler},
    };
    SampleCheck(sample,
                mrhiBeginPass(device, pass) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, pass, scene->pipeline) == mrhi_success &&
                    mrhiSetBindings(device, pass, 0, table0, 1) == mrhi_success &&
                    mrhiSetBindings(device, pass, 1, table1, 2) == mrhi_success &&
                    mrhiSetVertexBuffer(device, pass, 0, v, 0, sizeof(kVertices)) == mrhi_success &&
                    mrhiSetIndexBuffer(device, pass, i, mrhi_indexUint16, 0, sizeof(kIndices)) ==
                        mrhi_success &&
                    mrhiDrawIndexed(device, pass, 6, 1, 0, 0, 0) == mrhi_success &&
                    mrhiEndPass(device, pass) == mrhi_success,
                "the quad drawn");
}

// The second frame: the quad drawn into a declared target, read back.
static mrhiRequestId Draw(Sample* sample, const Scene* scene)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId v = {0};
    mrhiResourceId i = {0};
    mrhiResourceId u = {0};
    mrhiResourceId t = {0};
    mrhiResourceId target = {0};
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = SIZE;
    targetDef.height = SIZE;
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiImportBuffer(device, scene->vertices, &v) == mrhi_success &&
                    mrhiImportBuffer(device, scene->indices, &i) == mrhi_success &&
                    mrhiImportBuffer(device, scene->transform, &u) == mrhi_success &&
                    mrhiImportTexture(device, scene->texture, &t) == mrhi_success &&
                    mrhiDeclareTexture(device, &targetDef, &target) == mrhi_success,
                "a drawing frame");
    const mrhiAccess reads[4] = {Whole(v, mrhi_accessVertex), Whole(i, mrhi_accessIndex),
                                 Whole(u, mrhi_accessUniform), Whole(t, mrhi_accessSampled)};
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    drawDef.colorTargetCount = 1;
    drawDef.accesses = reads;
    drawDef.accessCount = 4;
    const mrhiAccess read = Whole(target, mrhi_accessCopySource);
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId draw = {0};
    mrhiPassId reading = {0};
    SampleCheck(sample,
                mrhiAddPass(device, &drawDef, &draw) == mrhi_success &&
                    mrhiAddPass(device, &readDef, &reading) == mrhi_success &&
                    mrhiCompileFrame(device) == mrhi_success,
                "the frame compiled");
    DrawQuad(sample, scene, draw, v, i, u, t);
    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {SIZE, SIZE, 1};
    mrhiRequestId pixels = {0};
    SampleCheck(sample,
                mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, &pixels) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success,
                "the target read");
    return pixels;
}

// Each quarter of the quad shows its texel, up to two pixels from its
// edges; three pixels past them, the clear color.
static void Check(Sample* sample, const uint8_t* image)
{
    static const uint8_t black[4] = {0, 0, 0, 255};
    static const struct
    {
        int x;
        int y;
        const uint8_t* color;
    } probes[] = {
        {24, 24, kTexels[0][0]}, {40, 24, kTexels[0][1]}, {24, 40, kTexels[1][0]},
        {40, 40, kTexels[1][1]}, {8, 8, black},           {56, 32, black},
        {32, 56, black},         {18, 18, kTexels[0][0]}, {45, 45, kTexels[1][1]},
        {13, 24, black},         {50, 40, black},         {24, 13, black},
        {40, 50, black},
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
    {
        char what[48];
        snprintf(what, sizeof(what), "pixel (%d, %d)", probes[i].x, probes[i].y);
        SampleCheck(sample,
                    SampleNear(&image[(probes[i].y * SIZE + probes[i].x) * 4], probes[i].color, 1),
                    what);
    }
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    Scene scene = {0};
    MakeScene(&sample, &scene);
    Upload(&sample, &scene);
    mrhiRequestId pixels = Draw(&sample, &scene);
    static uint8_t image[SIZE * SIZE * 4];
    if (SampleFinish(&sample) && SampleTake(&sample, pixels, image, sizeof(image)))
    {
        Check(&sample, image);
    }
    mrhiDevice* device = sample.device;
    SampleCheck(&sample,
                mrhiDestroyGraphicsPipeline(device, scene.pipeline) == mrhi_success &&
                    mrhiDestroyShader(device, scene.shader) == mrhi_success &&
                    mrhiDestroyBuffer(device, scene.vertices) == mrhi_success &&
                    mrhiDestroyBuffer(device, scene.indices) == mrhi_success &&
                    mrhiDestroyBuffer(device, scene.transform) == mrhi_success &&
                    mrhiDestroyTexture(device, scene.texture) == mrhi_success &&
                    mrhiDestroySampler(device, scene.sampler) == mrhi_success,
                "the scene destroyed");
    return SampleClose(&sample);
}
