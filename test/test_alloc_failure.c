// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every call that allocates, run on the test driver with its allocation
// n failing for n = 1, 2, ... up to what the call needs: once with only
// allocation n failing, once with every allocation from n on failing.
// Each failure answers mrhi_errorCapacity, changes nothing a program can
// see, leaks nothing and leaves the objects usable: the call run again
// answers as a clean run does. A call that absorbs the failure answers
// as a clean run does. A failed reconfiguration leaves the surface
// unconfigured, as its driver retires the old swapchain whatever happens.
// A driver refusing an object for memory changes nothing either: the
// next one made takes the id a clean run gives it.

#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/heap.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"
#include "maul-rhi/test.h"
#include "shaders/noop_container.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// The memory of every object: allocation failAt fails (counting from
// when it was set; 0 for none), and every one after it when failFrom.
typedef struct Memory
{
    uint64_t count;
    uint64_t failAt;
    bool failFrom;
    int64_t live;
} Memory;

static void* Alloc(size_t size, size_t alignment, void* context)
{
    Memory* memory = context;
    ++memory->count;
    if (memory->failAt != 0 &&
        (memory->count == memory->failAt || (memory->failFrom && memory->count > memory->failAt)))
    {
        return nullptr;
    }
    // The library asks for no more than malloc's alignment.
    void* block = alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
    memory->live += block != nullptr;
    return block;
}

static void Free(void* block, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    Memory* memory = context;
    memory->live -= block != nullptr;
    free(block);
}

typedef struct World
{
    Memory memory;
    mrhiTestAdapter adapter;
    mrhiTestDriverDef driver;
    mrhiInstance* instance;
    mrhiDevice* device;
    // A device on the C library's memory that tells whether the surface
    // is configured on another device.
    mrhiDevice* probe;
    mrhiSurfaceId surface;
    mrhiShaderId shader;
    mrhiTextureId texture;
    // Set to have the driver refuse every object.
    bool refuse;
    // What a step made, destroyed with the world.
    mrhiInstance* madeInstance;
    mrhiDevice* madeDevice;
    // What the step answered, beside its status.
    uint64_t output;
} World;

// Mixes a value into a hash, field by field: a struct's padding holds
// what its storage held before.
static uint64_t Mix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * 1099511628211u;
}

static uint64_t MixRequest(uint64_t hash, mrhiRequestId request)
{
    return Mix(Mix(hash, request.index1), request.generation);
}

static mrhiAllocator AllocatorOf(World* world)
{
    return (mrhiAllocator){.alloc = Alloc, .free = Free, .context = &world->memory};
}

static mrhiInstanceDef InstanceDef(World* world)
{
    world->driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &world->adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &world->driver.chain;
    def.allocator = AllocatorOf(world);
    def.limits.diagnostics = 8;
    return def;
}

static mrhiDeviceDef DeviceDef(World* world)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    size_t count = 0;
    CHECK(mrhiGetAdapters(world->instance, &def.adapter, 1, &count) == mrhi_success && count == 1,
          "the adapter");
    def.allocator = AllocatorOf(world);
    def.features.bindlessSampling = true;
    def.limits.heapSize = 64;
    def.limits.samplerHeapSize = 16;
    def.deviceLimits.diagnostics = 8;
    return def;
}

// Takes the instance's answers in; the device's open with them.
static void Answer(World* world)
{
    mrhiInstanceNotification notification;
    while (mrhiNextInstanceNotification(world->instance, &notification) == mrhi_success)
    {
    }
}

// An instance on the test driver with its adapter found, which has
// heaps; nothing else made.
static void MakeInstance(World* world)
{
    mrhiInstanceDef def = InstanceDef(world);
    CHECK(mrhiCreateInstance(&def, &world->instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    CHECK(mrhiRequestAdapters(world->instance, &search, &request) == mrhi_success, "the search");
    Answer(world);
}

static void MakeWorld(World* world)
{
    *world = (World){0};
    world->adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    world->adapter.features.bindlessSampling = true;
    world->adapter.refuseObjects = &world->refuse;
    world->adapter.limits.heapSize = 64;
    world->adapter.limits.samplerHeapSize = 16;
}

static void Opened(World* world)
{
    MakeInstance(world);
    mrhiDeviceDef def = DeviceDef(world);
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(world->instance, &def, &world->device, &request) == mrhi_success,
          "the device");
    def.allocator = (mrhiAllocator){0};
    CHECK(mrhiCreateDevice(world->instance, &def, &world->probe, &request) == mrhi_success,
          "the probe");
    Answer(world);
}

static void FreeWorld(World* world)
{
    world->memory.failAt = 0;
    mrhiDestroyDevice(world->madeDevice);
    mrhiDestroyDevice(world->probe);
    mrhiDestroyDevice(world->device);
    mrhiDestroyInstance(world->instance);
    mrhiDestroyInstance(world->madeInstance);
}

static mrhiSurfaceConfig Config(const World* world, uint32_t width)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = world->surface;
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.width = width;
    config.height = 64;
    return config;
}

// The opened world with a surface presenting on its adapter.
static void WithSurface(World* world)
{
    Opened(world);
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .caps = {.colors = {{.format = mrhi_formatBgra8Unorm}}, .colorCount = 1},
        .presentingAdapters = 1,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    CHECK(mrhiCreateSurface(world->instance, &def, &world->surface) == mrhi_success, "the surface");
}

// The surface configured on the device.
static void Configured(World* world)
{
    WithSurface(world);
    mrhiSurfaceConfig config = Config(world, 64);
    CHECK(mrhiConfigureSurface(world->device, &config) == mrhi_success, "configured");
}

typedef struct Step
{
    const char* name;
    // Makes a world for the step, outside the failing allocations.
    void (*prepare)(World* world);
    mrhiResult (*run)(World* world);
    // Whether a refusal leaves the surface unconfigured.
    bool retires;
} Step;

static mrhiResult CreateInstance(World* world)
{
    mrhiInstanceDef def = InstanceDef(world);
    mrhiResult result = mrhiCreateInstance(&def, &world->madeInstance);
    world->output = world->madeInstance != nullptr;
    return result;
}

static mrhiResult CreateDevice(World* world)
{
    mrhiDeviceDef def = DeviceDef(world);
    mrhiRequestId request = {0};
    mrhiResult result = mrhiCreateDevice(world->instance, &def, &world->madeDevice, &request);
    world->output = MixRequest(world->madeDevice != nullptr, request);
    return result;
}

static mrhiResult CreateShader(World* world)
{
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = s_noopContainer;
    def.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    mrhiResult result = mrhiCreateShader(world->device, &def, &shader);
    mrhiShaderInfo info = {0};
    bool live = mrhiGetShaderInfo(world->device, shader, &info) == mrhi_success;
    world->output = Mix(Mix(Mix(live, shader.index1), shader.generation), info.entryCount);
    return result;
}

static mrhiResult CreateHeap(World* world)
{
    mrhiHeapDef def = mrhiDefaultHeapDef();
    def.entries = 16;
    def.samplers = 4;
    mrhiHeapId heap = {0};
    mrhiResult result = mrhiCreateHeap(world->device, &def, &heap);
    world->output = Mix(Mix(0, heap.index1), heap.generation);
    return result;
}

static mrhiResult CreateSampler(World* world)
{
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    mrhiSamplerId sampler = {0};
    mrhiResult result = mrhiCreateSampler(world->device, &def, &sampler);
    world->output = Mix(Mix(0, sampler.index1), sampler.generation);
    return result;
}

static mrhiResult CreateBuffer(World* world)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    def.usage = mrhi_bufferStorage;
    mrhiBufferId buffer = {0};
    mrhiResult result = mrhiCreateBuffer(world->device, &def, &buffer);
    world->output = Mix(Mix(0, buffer.index1), buffer.generation);
    return result;
}

static mrhiResult CreateTexture(World* world)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureSampled;
    mrhiTextureId texture = {0};
    mrhiResult result = mrhiCreateTexture(world->device, &def, &texture);
    world->output = Mix(Mix(0, texture.index1), texture.generation);
    return result;
}

static mrhiResult CreateView(World* world)
{
    mrhiViewDef def = mrhiDefaultViewDef();
    def.texture = world->texture;
    mrhiViewId view = {0};
    mrhiResult result = mrhiCreateView(world->device, &def, &view);
    world->output = Mix(Mix(0, view.index1), view.generation);
    return result;
}

static mrhiResult CreateQuerySet(World* world)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.count = 4;
    mrhiQuerySetId set = {0};
    mrhiResult result = mrhiCreateQuerySet(world->device, &def, &set);
    world->output = Mix(Mix(0, set.index1), set.generation);
    return result;
}

static mrhiResult CreateComputePipeline(World* world)
{
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = world->shader;
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiResult result = mrhiCreateComputePipeline(world->device, &def, &pipeline, &request);
    world->output = MixRequest(Mix(Mix(0, pipeline.index1), pipeline.generation), request);
    return result;
}

static mrhiResult CreateGraphicsPipeline(World* world)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = world->shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiResult result = mrhiCreateGraphicsPipeline(world->device, &def, &pipeline, &request);
    world->output = MixRequest(Mix(Mix(0, pipeline.index1), pipeline.generation), request);
    return result;
}

static mrhiResult Configure(World* world)
{
    mrhiSurfaceConfig config = Config(world, 128);
    return mrhiConfigureSurface(world->device, &config);
}

// A surface whose driver fails to make it while the world refuses.
static mrhiResult CreateSurface(World* world)
{
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .presentingAdapters = 1,
        .fail = world->refuse,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    mrhiSurfaceId surface = {0};
    mrhiResult result = mrhiCreateSurface(world->instance, &def, &surface);
    world->output = Mix(Mix(0, surface.index1), surface.generation);
    if (result == mrhi_success)
    {
        CHECK(mrhiDestroySurface(world->instance, surface) == mrhi_success, "the surface ended");
    }
    return result;
}

// The opened world with a shader of the noop container.
static void WithShader(World* world)
{
    Opened(world);
    CHECK(CreateShader(world) == mrhi_success, "the shader");
    world->shader = (mrhiShaderId){(uint32_t)1, (uint32_t)1};
}

// The opened world with a texture to view.
static void WithTexture(World* world)
{
    Opened(world);
    CHECK(CreateTexture(world) == mrhi_success, "the texture");
    world->texture = (mrhiTextureId){(uint32_t)1, (uint32_t)1};
}

static const Step s_steps[] = {
    {"create instance", nullptr, CreateInstance, false},
    {"create device", MakeInstance, CreateDevice, false},
    {"create shader", Opened, CreateShader, false},
    {"create heap", Opened, CreateHeap, false},
    {"configure surface", WithSurface, Configure, false},
    {"reconfigure surface", Configured, Configure, true},
};

// Every creation whose driver can refuse it.
static const Step s_refused[] = {
    {"create surface", MakeInstance, CreateSurface, false},
    {"create shader", Opened, CreateShader, false},
    {"create heap", Opened, CreateHeap, false},
    {"create sampler", Opened, CreateSampler, false},
    {"create buffer", Opened, CreateBuffer, false},
    {"create texture", Opened, CreateTexture, false},
    {"create view", WithTexture, CreateView, false},
    {"create query set", Opened, CreateQuerySet, false},
    {"create compute pipeline", WithShader, CreateComputePipeline, false},
    {"create graphics pipeline", WithShader, CreateGraphicsPipeline, false},
};

static uint64_t InstanceDigest(World* world)
{
    uint64_t hash = Mix(14695981039346656037u, mrhiGetInstanceMisuse(world->instance));
    mrhiInstanceNotification notification;
    while (mrhiNextInstanceNotification(world->instance, &notification) == mrhi_success)
    {
        hash = Mix(MixRequest(Mix(hash, notification.kind), notification.request),
                   (uint64_t)notification.outcome);
    }
    mrhiDiagnostic diagnostic;
    while (mrhiNextInstanceDiagnostic(world->instance, &diagnostic) == mrhi_success)
    {
        hash = Mix(Mix(hash, diagnostic.code), diagnostic.count);
    }
    return hash;
}

static uint64_t DeviceDigest(mrhiDevice* device, uint64_t hash)
{
    hash = Mix(Mix(hash, mrhiGetDeviceMisuse(device)), mrhiGetDeviceState(device));
    mrhiDeviceNotification notification;
    while (mrhiNextDeviceNotification(device, &notification) == mrhi_success)
    {
        hash = Mix(MixRequest(Mix(hash, notification.kind), notification.request),
                   (uint64_t)notification.outcome);
    }
    mrhiDiagnostic diagnostic;
    while (mrhiNextDeviceDiagnostic(device, &diagnostic) == mrhi_success)
    {
        hash = Mix(Mix(hash, diagnostic.code), diagnostic.count);
    }
    return hash;
}

// What a program sees of the world, taking its queues' records in.
static uint64_t Digest(World* world)
{
    if (world->instance == nullptr)
    {
        return 0;
    }
    uint64_t hash = InstanceDigest(world);
    return world->device != nullptr ? DeviceDigest(world->device, hash) : hash;
}

// Whether the surface is configured on the device: then the probe's
// configuration is refused as the device's surface before the caps
// refuse its mode, and nothing is counted either way.
static bool IsConfigured(World* world)
{
    if (world->surface.index1 == 0)
    {
        return false;
    }
    mrhiSurfaceConfig config = Config(world, 64);
    config.presentMode = mrhi_presentMailbox;
    return mrhiConfigureSurface(world->probe, &config) == mrhi_errorState;
}

// What the step changed and answered.
static uint64_t Outcome(World* world)
{
    return Mix(Mix(Digest(world), IsConfigured(world)), world->output);
}

static void Prepare(World* world, const Step* step)
{
    MakeWorld(world);
    if (step->prepare != nullptr)
    {
        step->prepare(world);
    }
    // The setup's own records, taken before the step.
    (void)Digest(world);
    world->memory.count = 0;
}

// The step's outcome with nothing failing, and the allocations it made.
static uint64_t Clean(const Step* step, uint64_t* countOut)
{
    World world;
    Prepare(&world, step);
    mrhiResult result = step->run(&world);
    *countOut = world.memory.count;
    uint64_t done = Outcome(&world);
    FreeWorld(&world);
    CHECK(result == mrhi_success, step->name);
    CHECK(world.memory.live == 0, "nothing leaked");
    return done;
}

// Runs the step with allocation failAt failing, and every one after it
// when failFrom; with the driver refusing every object when failAt is 0.
static bool Fail(const Step* step, uint64_t failAt, bool failFrom, uint64_t done, uint64_t count)
{
    World world;
    Prepare(&world, step);
    uint64_t before = Mix(Digest(&world), !step->retires && IsConfigured(&world));
    world.memory.failAt = failAt;
    world.memory.failFrom = failFrom;
    world.refuse = failAt == 0;
    mrhiResult result = step->run(&world);
    world.memory.failAt = 0;
    world.refuse = false;
    uint64_t answered = world.output;
    uint64_t after = Mix(Digest(&world), IsConfigured(&world));
    mrhiResult again = result != mrhi_success ? step->run(&world) : result;
    uint64_t last = result != mrhi_success ? Outcome(&world) : Mix(after, answered);
    FreeWorld(&world);
    // Refused and unchanged, then done as a clean run does it; or an
    // allocation's failure absorbed with the clean run's outcome. A driver
    // refuses a surface as the platform, and its objects for memory.
    bool refused = failAt == 0 ? result != mrhi_success : result == mrhi_errorCapacity;
    bool good = refused ? after == before && again == mrhi_success && last == done
                        : failAt != 0 && result == mrhi_success && last == done;
    if (!good || world.memory.live != 0)
    {
        fprintf(stderr, "%s: allocation %u%s of %u failing: %d, %s, then %d, %s, %lld leaked\n",
                step->name, (unsigned)failAt, failFrom ? " on" : "", (unsigned)count, (int)result,
                after == before ? "unchanged" : "changed", (int)again,
                last == done ? "as done" : "not as done", (long long)world.memory.live);
    }
    CHECK(world.memory.live == 0, "nothing leaked");
    return good;
}

static void Sweep(const Step* step)
{
    uint64_t count = 0;
    uint64_t done = Clean(step, &count);
    CHECK(count > 0, "the step allocates");
    for (int failFrom = 0; failFrom <= 1; ++failFrom)
    {
        for (uint64_t failAt = 1; failAt <= count; ++failAt)
        {
            CHECK(Fail(step, failAt, failFrom != 0, done, count), step->name);
        }
    }
}

int main(void)
{
    for (size_t i = 0; i < sizeof s_steps / sizeof s_steps[0]; ++i)
    {
        Sweep(&s_steps[i]);
    }
    for (size_t i = 0; i < sizeof s_refused / sizeof s_refused[0]; ++i)
    {
        uint64_t count = 0;
        uint64_t done = Clean(&s_refused[i], &count);
        CHECK(Fail(&s_refused[i], 0, false, done, count), s_refused[i].name);
    }
    return s_failures == 0 ? 0 : 1;
}
