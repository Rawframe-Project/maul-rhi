// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An upload and a readback: bytes and texels written into a device
// buffer and texture, copied on the GPU into resources the frame
// declares, and read back; then frames submitted back to back, each
// reading back what it wrote, their answers taken once both finish.
// Uploads are copied at the call, so the program's memory is free again
// at once; readbacks are answered when their frame has finished.

#include "harness.h"

#include <stdio.h>
#include <string.h>

// The texture's width and height, and its rows' bytes.
#define SIZE 16
#define ROW  (SIZE * 4)

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = 1, .layerCount = 1},
    };
}

static mrhiPassId CopyPass(Sample* sample, const mrhiAccess* accesses, uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = accesses;
    def.accessCount = count;
    def.neverCull = true;
    mrhiPassId pass = {0};
    SampleCheck(sample, mrhiAddPass(sample->device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// The device objects: a buffer and a texture that copies read and write.
static void MakeObjects(Sample* sample, mrhiBufferId* bufferOut, mrhiTextureId* textureOut)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = SIZE;
    textureDef.height = SIZE;
    textureDef.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    SampleCheck(sample,
                mrhiCreateBuffer(sample->device, &bufferDef, bufferOut) == mrhi_success &&
                    mrhiCreateTexture(sample->device, &textureDef, textureOut) == mrhi_success,
                "a buffer and a texture");
}

// One frame: uploads, copies into declared resources, readbacks of
// those; the bytes and texels must come back as they went.
static void RoundTrip(Sample* sample, mrhiBufferId bufferId, mrhiTextureId textureId)
{
    mrhiDevice* device = sample->device;
    uint8_t bytes[1024];
    uint8_t texels[SIZE * ROW];
    for (size_t i = 0; i < sizeof(bytes); ++i)
    {
        bytes[i] = (uint8_t)(i * 13 + 7);
    }
    for (size_t i = 0; i < sizeof(texels); ++i)
    {
        texels[i] = (uint8_t)(i / ROW * 16 + i % 4 * 64);
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffer = {0};
    mrhiResourceId texture = {0};
    mrhiResourceId bufferCopy = {0};
    mrhiResourceId textureCopy = {0};
    mrhiBufferDef copyDef = mrhiDefaultBufferDef();
    copyDef.size = sizeof(bytes);
    mrhiTextureDef imageDef = mrhiDefaultTextureDef();
    imageDef.format = mrhi_formatRgba8Unorm;
    imageDef.width = SIZE;
    imageDef.height = SIZE;
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiImportBuffer(device, bufferId, &buffer) == mrhi_success &&
                    mrhiImportTexture(device, textureId, &texture) == mrhi_success &&
                    mrhiDeclareBuffer(device, &copyDef, &bufferCopy) == mrhi_success &&
                    mrhiDeclareTexture(device, &imageDef, &textureCopy) == mrhi_success,
                "a frame and its resources");
    const mrhiAccess writes[2] = {Whole(buffer, mrhi_accessCopyDestination),
                                  Whole(texture, mrhi_accessCopyDestination)};
    mrhiPassId upload = CopyPass(sample, writes, 2);
    const mrhiAccess moves[4] = {Whole(buffer, mrhi_accessCopySource),
                                 Whole(texture, mrhi_accessCopySource),
                                 Whole(bufferCopy, mrhi_accessCopyDestination),
                                 Whole(textureCopy, mrhi_accessCopyDestination)};
    mrhiPassId move = CopyPass(sample, moves, 4);
    const mrhiAccess reads[2] = {Whole(bufferCopy, mrhi_accessCopySource),
                                 Whole(textureCopy, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(sample, reads, 2);
    SampleCheck(sample, mrhiCompileFrame(device) == mrhi_success, "the frame compiled");
    const mrhiTexelLayout layout = {.bytesPerRow = ROW, .rowsPerImage = SIZE};
    const mrhiExtent3d extent = {SIZE, SIZE, 1};
    const mrhiTextureCopy whole = {.resource = texture};
    const mrhiTextureCopy wholeCopy = {.resource = textureCopy};
    mrhiRequestId bytesBack = {0};
    mrhiRequestId texelsBack = {0};
    SampleCheck(
        sample,
        mrhiBeginPass(device, upload) == mrhi_success &&
            mrhiWriteBuffer(device, upload, buffer, 0, bytes, sizeof(bytes)) == mrhi_success &&
            mrhiWriteTexture(device, upload, &whole, texels, sizeof(texels), &layout, &extent) ==
                mrhi_success &&
            mrhiEndPass(device, upload) == mrhi_success &&
            mrhiBeginPass(device, move) == mrhi_success &&
            mrhiCopyBuffer(device, move, buffer, 0, bufferCopy, 0, sizeof(bytes)) == mrhi_success &&
            mrhiCopyTexture(device, move, &whole, &wholeCopy, &extent) == mrhi_success &&
            mrhiEndPass(device, move) == mrhi_success &&
            mrhiBeginPass(device, read) == mrhi_success &&
            mrhiReadBuffer(device, read, bufferCopy, 0, sizeof(bytes), &bytesBack) ==
                mrhi_success &&
            mrhiReadTexture(device, read, &wholeCopy, &extent, &texelsBack) == mrhi_success &&
            mrhiEndPass(device, read) == mrhi_success,
        "uploaded, copied and read");
    uint8_t back[sizeof(texels)];
    bool finished = SampleFinish(sample);
    SampleCheck(sample,
                finished && SampleTake(sample, bytesBack, back, sizeof(bytes)) &&
                    memcmp(back, bytes, sizeof(bytes)) == 0,
                "the bytes, back as they went");
    SampleCheck(sample,
                finished && SampleTake(sample, texelsBack, back, sizeof(texels)) &&
                    memcmp(back, texels, sizeof(texels)) == 0,
                "the texels, back as they went");
}

// Records a frame that writes a value into the buffer and reads it back.
static mrhiRequestId WriteAndRead(Sample* sample, mrhiBufferId bufferId, uint32_t value,
                                  mrhiRequestId* tokenOut)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffer = {0};
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiImportBuffer(device, bufferId, &buffer) == mrhi_success,
                "a frame");
    const mrhiAccess write = Whole(buffer, mrhi_accessCopyDestination);
    mrhiPassId upload = CopyPass(sample, &write, 1);
    const mrhiAccess read = Whole(buffer, mrhi_accessCopySource);
    mrhiPassId reading = CopyPass(sample, &read, 1);
    const uint32_t values[4] = {value, value + 1, value + 2, value + 3};
    mrhiRequestId request = {0};
    SampleCheck(
        sample,
        mrhiCompileFrame(device) == mrhi_success && mrhiBeginPass(device, upload) == mrhi_success &&
            mrhiWriteBuffer(device, upload, buffer, 0, values, sizeof(values)) == mrhi_success &&
            mrhiEndPass(device, upload) == mrhi_success &&
            mrhiBeginPass(device, reading) == mrhi_success &&
            mrhiReadBuffer(device, reading, buffer, 0, sizeof(values), &request) == mrhi_success &&
            mrhiEndPass(device, reading) == mrhi_success &&
            mrhiSubmitFrame(device, tokenOut) == mrhi_success,
        "written, read and submitted");
    return request;
}

// Two frames in flight on one buffer, each reading its own write: the
// second waits for nothing but the queue's order.
static void BackToBack(Sample* sample, mrhiBufferId bufferId)
{
    mrhiRequestId tokens[2];
    mrhiRequestId requests[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        requests[i] = WriteAndRead(sample, bufferId, 100 * (i + 1), &tokens[i]);
    }
    // Frames finish in order: waiting for the second waits for both.
    SampleWait(sample, tokens[1]);
    for (uint32_t i = 0; i < 2; ++i)
    {
        uint32_t values[4] = {0};
        uint32_t base = 100 * (i + 1);
        char what[48];
        snprintf(what, sizeof(what), "frame %u's own values", i + 1);
        SampleCheck(sample,
                    SampleTake(sample, requests[i], values, sizeof(values)) && values[0] == base &&
                        values[3] == base + 3,
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
    mrhiBufferId buffer = {0};
    mrhiTextureId texture = {0};
    MakeObjects(&sample, &buffer, &texture);
    RoundTrip(&sample, buffer, texture);
    BackToBack(&sample, buffer);
    SampleCheck(&sample,
                mrhiDestroyBuffer(sample.device, buffer) == mrhi_success &&
                    mrhiDestroyTexture(sample.device, texture) == mrhi_success,
                "the objects destroyed");
    return SampleClose(&sample);
}
