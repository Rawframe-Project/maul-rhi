// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CPU cost of the library on the build's native driver: per draw
// recorded, per pass declared and
// recorded, the compile of a fixed frame, and a frame's start to its
// submission; each the best of five runs. Then, exactly, the bytes an
// instance and a device with the default defs take from their
// allocators. Given a baseline file, such as bench/baseline.txt, it
// prints each number's ratio to the recorded one as well. Skipped where
// there is no adapter.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "harness.h"

#include "shaders/triangle_container.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// The draws one frame records, and the passes of the pass frame.
#define DRAWS  10000
#define PASSES 200
#define RUNS   5

// The width of a result's name, as printed and as read back.
#define NAME_WIDTH 32

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// The baseline's lines, read whole.
static char s_baseline[16384];

// The value the baseline records for a name, or 0.
static double Recorded(const char* name)
{
    for (const char* line = s_baseline; *line != '\0';)
    {
        const char* end = strchr(line, '\n');
        size_t length = end != nullptr ? (size_t)(end - line) : strlen(line);
        size_t named = strlen(name);
        if (line[0] != '#' && length > NAME_WIDTH && strncmp(line, name, named) == 0 &&
            line[named] == ' ')
        {
            return strtod(line + NAME_WIDTH, nullptr);
        }
        line += length + (end != nullptr ? 1 : 0);
    }
    return 0.0;
}

// Prints a result, and its ratio to the baseline's where it has one.
static void Report(const char* name, double value, const char* unit)
{
    double recorded = Recorded(name);
    if (recorded > 0.0)
    {
        printf("%-*s %12.1f %-6s (%.2fx the baseline)\n", NAME_WIDTH, name, value, unit,
               value / recorded);
    }
    else
    {
        printf("%-*s %12.1f %s\n", NAME_WIDTH, name, value, unit);
    }
}

static mrhiGraphicsPipelineId MakePipeline(Sample* sample)
{
    mrhiShaderId shader = SampleShader(sample, s_triangleContainer, sizeof(s_triangleContainer));
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargetCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    return SampleGraphics(sample, &def);
}

// One frame of DRAWS draws into a declared target, each with its own
// root block: the seconds the draws' recording took, and the frame's
// from its start to its submission.
static void DrawFrame(Sample* sample, mrhiGraphicsPipelineId pipeline, double* drawsOut,
                      double* frameOut)
{
    mrhiDevice* device = sample->device;
    double start = Seconds();
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = 64;
    targetDef.height = 64;
    mrhiResourceId target = {0};
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    mrhiPassId draw = {0};
    bool ready = mrhiBeginFrame(device, &frame) == mrhi_success &&
                 mrhiDeclareTexture(device, &targetDef, &target) == mrhi_success;
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
    };
    drawDef.colorTargetCount = 1;
    drawDef.neverCull = true;
    ready = ready && mrhiAddPass(device, &drawDef, &draw) == mrhi_success &&
            mrhiCompileFrame(device) == mrhi_success &&
            mrhiBeginPass(device, draw) == mrhi_success &&
            mrhiSetGraphicsPipeline(device, draw, pipeline) == mrhi_success;
    double drawing = Seconds();
    for (uint32_t i = 0; i < DRAWS && ready; ++i)
    {
        const float color[4] = {(float)(i % 7) / 7.0f, 0.5f, 0.25f, 1.0f};
        ready = mrhiSetRootBlock(device, draw, 0, color, sizeof(color)) == mrhi_success &&
                mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success;
    }
    double drawn = Seconds();
    mrhiRequestId token = {0};
    ready = ready && mrhiEndPass(device, draw) == mrhi_success &&
            mrhiSubmitFrame(device, &token) == mrhi_success;
    double submitted = Seconds();
    SampleCheck(sample, ready && SampleWait(sample, token), "the draw frame");
    *drawsOut = drawn - drawing;
    *frameOut = submitted - start;
}

// One frame of PASSES transfer passes: the first writes a buffer, and
// each after it copies the one before it wrote into its own. The
// seconds from the frame's start to its submission, and the compile's.
static void PassFrame(Sample* sample, double* passesOut, double* compileOut)
{
    mrhiDevice* device = sample->device;
    double start = Seconds();
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    static mrhiResourceId buffers[PASSES];
    static mrhiPassId passes[PASSES];
    bool ready = mrhiBeginFrame(device, &frame) == mrhi_success;
    for (uint32_t i = 0; i < PASSES && ready; ++i)
    {
        ready = mrhiDeclareBuffer(device, &bufferDef, &buffers[i]) == mrhi_success;
    }
    for (uint32_t i = 0; i < PASSES && ready; ++i)
    {
        const mrhiAccess accesses[2] = {
            {.resource = buffers[i], .kind = mrhi_accessCopyDestination},
            {.resource = buffers[i == 0 ? 0 : i - 1], .kind = mrhi_accessCopySource},
        };
        mrhiPassDef def = mrhiDefaultPassDef();
        def.passClass = mrhi_passTransfer;
        def.accesses = accesses;
        def.accessCount = i == 0 ? 1 : 2;
        def.neverCull = i + 1 == PASSES;
        ready = mrhiAddPass(device, &def, &passes[i]) == mrhi_success;
    }
    double compiling = Seconds();
    ready = ready && mrhiCompileFrame(device) == mrhi_success;
    double compiled = Seconds();
    static const uint8_t bytes[16] = {1, 2, 3, 4};
    for (uint32_t i = 0; i < PASSES && ready; ++i)
    {
        ready = mrhiBeginPass(device, passes[i]) == mrhi_success &&
                (i == 0 ? mrhiWriteBuffer(device, passes[i], buffers[0], 0, bytes, sizeof(bytes))
                        : mrhiCopyBuffer(device, passes[i], buffers[i - 1], 0, buffers[i], 0,
                                         sizeof(bytes))) == mrhi_success &&
                mrhiEndPass(device, passes[i]) == mrhi_success;
    }
    mrhiRequestId token = {0};
    ready = ready && mrhiSubmitFrame(device, &token) == mrhi_success;
    double submitted = Seconds();
    SampleCheck(sample, ready && SampleWait(sample, token), "the pass frame");
    *passesOut = submitted - start;
    *compileOut = compiled - compiling;
}

// The bytes an allocator has handed out and not taken back.
typedef struct Counted
{
    size_t live;
} Counted;

// Memory with its size and offset before it, so that free needs no
// platform's aligned allocation.
static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    size_t head = alignment > sizeof(size_t) * 2 ? alignment : sizeof(size_t) * 2;
    unsigned char* block = malloc(size + head + alignment);
    if (block == nullptr)
    {
        return nullptr;
    }
    uintptr_t at = ((uintptr_t)block + head + alignment - 1) & ~(uintptr_t)(alignment - 1);
    unsigned char* memory = (unsigned char*)at;
    size_t offset = (size_t)(memory - block);
    memcpy(memory - sizeof(size_t) * 2, &offset, sizeof(size_t));
    ((Counted*)context)->live += size;
    return memory;
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    if (memory == nullptr)
    {
        return;
    }
    size_t offset = 0;
    memcpy(&offset, (unsigned char*)memory - sizeof(size_t) * 2, sizeof(size_t));
    ((Counted*)context)->live -= size;
    free((unsigned char*)memory - offset);
}

// The bytes an instance and a device with the default defs take, on the
// first adapter: false where either cannot be made.
static bool Footprint(size_t* instanceOut, size_t* deviceOut)
{
    static Counted instanceBytes;
    static Counted deviceBytes;
    mrhiInstanceDef instanceDef = mrhiDefaultInstanceDef();
    instanceDef.allocator = (mrhiAllocator){CountedAlloc, CountedFree, &instanceBytes};
    mrhiInstance* instance = nullptr;
    if (mrhiCreateInstance(&instanceDef, &instance) != mrhi_success)
    {
        return false;
    }
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched = {0};
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    bool found = mrhiRequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
                 mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count >= 1;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    deviceDef.allocator = (mrhiAllocator){CountedAlloc, CountedFree, &deviceBytes};
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    found = found && mrhiCreateDevice(instance, &deviceDef, &device, &opened) == mrhi_success &&
            mrhiNextInstanceNotification(instance, &record) == mrhi_success;
    *instanceOut = instanceBytes.live;
    *deviceOut = deviceBytes.live;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
    return found;
}

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        FILE* file = fopen(argv[1], "rb");
        size_t read = file != nullptr ? fread(s_baseline, 1, sizeof(s_baseline) - 1, file) : 0;
        s_baseline[read] = '\0';
        if (file != nullptr)
        {
            fclose(file);
        }
    }
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        printf("skip: no adapter\n");
        return opened == SAMPLE_SKIPPED ? 0 : opened;
    }
    mrhiAdapterInfo info;
    if (mrhiGetAdapterInfo(sample.instance, sample.adapter, &info) == mrhi_success)
    {
        printf("# %.*s, best of %d runs\n", (int)info.nameLength, info.name, RUNS);
    }
    mrhiGraphicsPipelineId pipeline = MakePipeline(&sample);
    double draws = 1e9;
    double drawFrame = 1e9;
    double passes = 1e9;
    double compile = 1e9;
    for (int run = 0; run < RUNS && sample.failures == 0; ++run)
    {
        double a = 0.0;
        double b = 0.0;
        DrawFrame(&sample, pipeline, &a, &b);
        draws = a < draws ? a : draws;
        drawFrame = b < drawFrame ? b : drawFrame;
        PassFrame(&sample, &a, &b);
        passes = a < passes ? a : passes;
        compile = b < compile ? b : compile;
    }
    if (sample.failures == 0)
    {
        Report("draw, recorded", draws * 1e9 / DRAWS, "ns");
        Report("draw frame, start to submit", drawFrame * 1e6, "us");
        Report("pass, declared and recorded", passes * 1e9 / PASSES, "ns");
        Report("compile, 200 chained passes", compile * 1e6, "us");
    }
    int status = SampleClose(&sample);
    size_t instanceBytes = 0;
    size_t deviceBytes = 0;
    if (status == 0 && Footprint(&instanceBytes, &deviceBytes))
    {
        Report("instance, default def", (double)instanceBytes, "bytes");
        Report("device, default def", (double)deviceBytes, "bytes");
    }
    return status;
}
