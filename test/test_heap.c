// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bindless heaps on a test driver device: the def's checks, the two
// features and the size limits; entries of each kind with the usages
// and ranges they need; entries written only while empty and written
// again only once the frames submitted before their emptying finish;
// objects whose destruction empties the entries naming them; and the
// heap a pass names.

#include "device_core.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/heap.h"

// A ready device holding at most heaps heaps, with the bindless
// features asked for, both granted by the adapter, whose heap sizes and
// storage alignment are twice and a quarter of the device's.
static mrhiDevice* Open(uint32_t heaps, bool sampling, bool heterogeneous)
{
    s_adapter.features.bindlessSampling = true;
    s_adapter.features.bindlessHeterogeneous = true;
    s_adapter.limits.heapSize = 4096;
    s_adapter.limits.samplerHeapSize = 64;
    s_adapter.limits.storageOffsetAlignment = 64;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.heaps = heaps;
    def.features.bindlessSampling = sampling;
    def.features.bindlessHeterogeneous = heterogeneous;
    def.limits.heapSize = sampling ? 2048 : 0;
    def.limits.samplerHeapSize = sampling ? 32 : 0;
    return OpenWith(def, true);
}

static mrhiHeapId MakeHeap(mrhiDevice* device, uint32_t entries, uint32_t samplers)
{
    mrhiHeapDef def = mrhiDefaultHeapDef();
    def.entries = entries;
    def.samplers = samplers;
    mrhiHeapId heap = {0};
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_success, "a heap");
    return heap;
}

static mrhiTextureId MakeTexture(mrhiDevice* device, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 4;
    def.height = 4;
    def.usage = usage;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

static mrhiViewId MakeView(mrhiDevice* device, mrhiTextureId texture)
{
    mrhiViewDef def = mrhiDefaultViewDef();
    def.texture = texture;
    mrhiViewId view = {0};
    CHECK(mrhiCreateView(device, &def, &view) == mrhi_success, "a view");
    return view;
}

static mrhiBufferId MakeBuffer(mrhiDevice* device, uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiSamplerId MakeSampler(mrhiDevice* device)
{
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    mrhiSamplerId sampler = {0};
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_success, "a sampler");
    return sampler;
}

static mrhiHeapEntry Sampled(mrhiViewId view)
{
    return (mrhiHeapEntry){.kind = mrhi_heapSampledTexture, .view = view};
}

static mrhiHeapEntry Storage(mrhiBufferId buffer, uint64_t offset, uint64_t size)
{
    return (mrhiHeapEntry){
        .kind = mrhi_heapStorageBuffer,
        .buffer = buffer,
        .offset = offset,
        .size = size,
        .writable = true,
    };
}

static void TestMakeAndDestroy(void)
{
    mrhiDevice* device = Open(2, false, false);
    mrhiHeapDef def = mrhiDefaultHeapDef();
    CHECK(def.entries == 1024 && def.samplers == 16, "the defaults");
    mrhiHeapId heap = {0};
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorUnsupported, "no bindless feature");
    Close(device);
    device = Open(2, true, false);
    CHECK(mrhiDefaultDeviceDef().budgets.heaps == 4, "four heaps by default");
    def.entries = 0;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorInvalid, "no entries");
    def = mrhiDefaultHeapDef();
    def.cookie = 0;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorInvalid, "no cookie");
    CHECK(mrhiCreateHeap(device, nullptr, &heap) == mrhi_errorInvalid, "no def");
    def = mrhiDefaultHeapDef();
    CHECK(mrhiCreateHeap(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateHeap(nullptr, &def, &heap) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDevice_GetMisuse(device) == 4, "each counted on the device");
    def.entries = 2049;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorCapacity,
          "past heapSize, within the adapter's");
    def.entries = 4097;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorUnsupported, "past the adapter's");
    def.entries = 2048;
    def.samplers = 33;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorCapacity,
          "past samplerHeapSize, within the adapter's");
    def.samplers = 65;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorUnsupported, "past the adapter's");
    def.entries = 2049;
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_errorUnsupported,
          "past the adapter's beside the device's");
    def.entries = 2048;
    def.samplers = 32;
    def.label = "materials";
    def.labelLength = 9;
    mrhiHeapId second = {0};
    CHECK(mrhiCreateHeap(device, &def, &heap) == mrhi_success &&
              mrhiCreateHeap(device, &def, &second) == mrhi_success,
          "two full heaps");
    mrhiHeapId third = {0};
    CHECK(mrhiCreateHeap(device, &def, &third) == mrhi_errorCapacity, "the heaps budget");
    CHECK(mrhiDestroyHeap(device, heap) == mrhi_success, "destroyed");
    CHECK(mrhiDestroyHeap(device, heap) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiDestroyHeap(device, (mrhiHeapId){0}) == mrhi_errorStale, "the null id");
    CHECK(mrhiDestroyHeap(nullptr, second) == mrhi_errorInvalid, "no device");
    CHECK(mrhiCreateHeap(device, &def, &third) == mrhi_success, "its slot taken again");
    CHECK(mrhiDevice_GetMisuse(device) == 4, "no more misuse");
    // The heaps left end with the device.
    Close(device);
}

static void TestEntries(void)
{
    mrhiDevice* device = Open(1, true, false);
    mrhiHeapId heap = MakeHeap(device, 8, 0);
    mrhiTextureId texture = MakeTexture(device, mrhi_textureSampled | mrhi_textureStorage);
    mrhiViewId view = MakeView(device, texture);
    mrhiHeapEntry entry = Sampled(view);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success, "a sampled texture");
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_errorState, "not empty");
    CHECK(mrhiHeap_SetEntry(device, heap, 7, &entry) == mrhi_success, "the same view again");
    CHECK(mrhiHeap_SetEntry(device, heap, 8, &entry) == mrhi_errorInvalid, "past the entries");
    CHECK(mrhiHeap_ClearEntry(device, heap, 0) == mrhi_success &&
              mrhiHeap_ClearEntry(device, heap, 0) == mrhi_success,
          "cleared, twice");
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success,
          "free again with no frame running");
    CHECK(mrhiHeap_ClearEntry(device, heap, 8) == mrhi_errorInvalid, "a clear past the entries");
    entry.writable = true;
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_errorInvalid, "a written sampled");
    entry = Sampled(view);
    entry.kind = 3;
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_errorInvalid, "an unknown kind");
    // Kinds are int32_t: a check against the last kind alone would let a
    // negative one through.
    static const int32_t negative[] = {-1, INT32_MIN};
    for (size_t i = 0; i < sizeof(negative) / sizeof(negative[0]); ++i)
    {
        entry.kind = negative[i];
        CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_errorInvalid, "a negative kind");
    }
    CHECK(mrhiHeap_SetEntry(device, heap, 1, nullptr) == mrhi_errorInvalid, "no entry");
    entry = (mrhiHeapEntry){.kind = mrhi_heapStorageTexture, .view = view};
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_errorUnsupported,
          "a storage texture without bindless_heterogeneous");
    mrhiTextureId target = MakeTexture(device, mrhi_textureRenderTarget);
    entry = Sampled(MakeView(device, target));
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_errorInvalid, "a view not sampled");
    entry = Sampled((mrhiViewId){99, 1});
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_errorStale, "a stale view");
    CHECK(mrhiHeap_SetEntry(device, (mrhiHeapId){1, 99}, 1, &entry) == mrhi_errorStale,
          "a stale heap");
    CHECK(mrhiHeap_SetEntry(nullptr, heap, 1, &entry) == mrhi_errorInvalid, "no device");
    Close(device);
}

static void TestStorage(void)
{
    mrhiDevice* device = Open(1, true, true);
    mrhiHeapId heap = MakeHeap(device, 16, 0);
    uint32_t alignment = device->limits.storageOffsetAlignment;
    mrhiBufferId buffer = MakeBuffer(device, 4096, mrhi_bufferStorage);
    mrhiHeapEntry entry = Storage(buffer, 0, MRHI_WHOLE_SIZE);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success, "a whole buffer");
    entry = Storage(buffer, alignment, 64);
    entry.writable = false;
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_success, "a range, read only");
    entry = Storage(buffer, 64, 64);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorCapacity,
          "on the adapter's alignment only");
    entry = Storage(buffer, 4, 64);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorUnsupported, "off the adapter's");
    entry = Storage(buffer, 0, 6);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorInvalid, "not 4-byte sized");
    entry = Storage(buffer, 0, 0);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorInvalid, "empty");
    entry = Storage(buffer, 0, 4100);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorInvalid, "past its end");
    entry = Storage(buffer, 8192, MRHI_WHOLE_SIZE);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorInvalid, "from past its end");
    mrhiBufferId uniform = MakeBuffer(device, 256, mrhi_bufferUniform);
    entry = Storage(uniform, 0, MRHI_WHOLE_SIZE);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorInvalid, "no storage usage");
    entry = Storage((mrhiBufferId){77, 1}, 0, 4);
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_errorStale, "a stale buffer");
    mrhiTextureId texture = MakeTexture(device, mrhi_textureStorage);
    entry = (mrhiHeapEntry){
        .kind = mrhi_heapStorageTexture, .view = MakeView(device, texture), .writable = true};
    CHECK(mrhiHeap_SetEntry(device, heap, 2, &entry) == mrhi_success, "a storage texture");
    mrhiTextureId sampled = MakeTexture(device, mrhi_textureSampled);
    entry.view = MakeView(device, sampled);
    CHECK(mrhiHeap_SetEntry(device, heap, 3, &entry) == mrhi_errorInvalid, "no storage usage");
    mrhiTextureDef mipped = mrhiDefaultTextureDef();
    mipped.format = mrhi_formatRgba8Unorm;
    mipped.width = 4;
    mipped.height = 4;
    mipped.mipLevels = 2;
    mipped.usage = mrhi_textureStorage;
    CHECK(mrhiCreateTexture(device, &mipped, &texture) == mrhi_success, "two mips");
    entry.view = MakeView(device, texture);
    CHECK(mrhiHeap_SetEntry(device, heap, 3, &entry) == mrhi_errorInvalid,
          "a storage view of more than one mip");
    Close(device);
}

static void TestSamplers(void)
{
    mrhiDevice* device = Open(2, true, false);
    mrhiHeapId heap = MakeHeap(device, 1, 2);
    mrhiSamplerId sampler = MakeSampler(device);
    CHECK(mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_success, "a sampler");
    CHECK(mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_errorState, "not empty");
    CHECK(mrhiHeap_SetSampler(device, heap, 2, sampler) == mrhi_errorInvalid, "past the samplers");
    CHECK(mrhiHeap_SetSampler(device, heap, 1, (mrhiSamplerId){9, 9}) == mrhi_errorStale,
          "a stale sampler");
    CHECK(mrhiHeap_ClearSampler(device, heap, 0) == mrhi_success &&
              mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_success,
          "cleared and set again");
    CHECK(mrhiHeap_ClearSampler(device, heap, 2) == mrhi_errorInvalid, "a clear past them");
    CHECK(mrhiDestroySampler(device, sampler) == mrhi_success, "the sampler destroyed");
    sampler = MakeSampler(device);
    CHECK(mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_success,
          "its entry emptied by its destruction");
    mrhiHeapId none = MakeHeap(device, 1, 0);
    CHECK(mrhiHeap_SetSampler(device, none, 0, sampler) == mrhi_errorInvalid, "no sampler part");
    Close(device);
}

// Submits a frame of one pass that names the heap, and leaves it
// running: its token.
static mrhiRequestId SubmitWith(mrhiDevice* device, mrhiHeapId heap)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.heap = heap;
    mrhiPassId pass = {0};
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiAddPass(device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, pass) == mrhi_success &&
              mrhiPass_End(device, pass) == mrhi_success &&
              mrhiDevice_SubmitFrame(device, &token) == mrhi_success,
          "a frame with the heap");
    return token;
}

// Takes the device's notifications: every frame submitted is done.
static void Drain(mrhiDevice* device)
{
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
        CHECK(record.kind == mrhi_deviceFrameDone && record.outcome == mrhi_success, "done");
    }
}

static void TestFreedAfterFrames(void)
{
    mrhiDevice* device = Open(1, true, false);
    mrhiHeapId heap = MakeHeap(device, 4, 1);
    mrhiTextureId texture = MakeTexture(device, mrhi_textureSampled);
    mrhiHeapEntry entry = Sampled(MakeView(device, texture));
    mrhiSamplerId sampler = MakeSampler(device);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success &&
              mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_success,
          "set");
    (void)SubmitWith(device, heap);
    CHECK(mrhiHeap_SetEntry(device, heap, 1, &entry) == mrhi_success,
          "an entry empty while the frame was made");
    CHECK(mrhiHeap_ClearEntry(device, heap, 0) == mrhi_success &&
              mrhiHeap_ClearSampler(device, heap, 0) == mrhi_success,
          "cleared while the frame runs");
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_errorState &&
              mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_errorState,
          "not until the frame finishes");
    Drain(device);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success &&
              mrhiHeap_SetSampler(device, heap, 0, sampler) == mrhi_success,
          "free once it has");
    Close(device);
}

static void TestDestroyEmpties(void)
{
    mrhiDevice* device = Open(2, true, true);
    mrhiHeapId heap = MakeHeap(device, 4, 0);
    mrhiHeapId other = MakeHeap(device, 4, 0);
    mrhiTextureId texture = MakeTexture(device, mrhi_textureSampled);
    mrhiViewId view = MakeView(device, texture);
    mrhiHeapEntry entry = Sampled(view);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success &&
              mrhiHeap_SetEntry(device, other, 3, &entry) == mrhi_success,
          "one view in two heaps");
    CHECK(mrhiDestroyView(device, view) == mrhi_success, "the view destroyed");
    entry = Sampled(MakeView(device, texture));
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success &&
              mrhiHeap_SetEntry(device, other, 3, &entry) == mrhi_success,
          "both entries emptied");
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture and its view");
    mrhiBufferId buffer = MakeBuffer(device, 256, mrhi_bufferStorage);
    entry = Storage(buffer, 0, MRHI_WHOLE_SIZE);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success,
          "emptied by the texture's end");
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "the buffer destroyed");
    texture = MakeTexture(device, mrhi_textureSampled);
    entry = Sampled(MakeView(device, texture));
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success,
          "emptied by the buffer's end");
    // A heap's end lets go of what it names: destroying them later
    // finds nothing to empty.
    CHECK(mrhiDestroyHeap(device, heap) == mrhi_success &&
              mrhiDestroyHeap(device, other) == mrhi_success &&
              mrhiDestroyTexture(device, texture) == mrhi_success,
          "destroyed in any order");
    Close(device);
}

// A view and a buffer in the same slots of their tables: destroying one
// empties only its own entries.
static void TestSameSlots(void)
{
    mrhiDevice* device = Open(1, true, true);
    mrhiHeapId heap = MakeHeap(device, 2, 0);
    mrhiTextureId texture = MakeTexture(device, mrhi_textureSampled);
    mrhiViewId view = MakeView(device, texture);
    mrhiBufferId buffer = MakeBuffer(device, 256, mrhi_bufferStorage);
    CHECK(view.index1 == buffer.index1, "the same slot");
    mrhiHeapEntry entry = Sampled(view);
    mrhiHeapEntry range = Storage(buffer, 0, MRHI_WHOLE_SIZE);
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_success &&
              mrhiHeap_SetEntry(device, heap, 1, &range) == mrhi_success,
          "both set");
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "the buffer destroyed");
    CHECK(mrhiHeap_SetEntry(device, heap, 0, &entry) == mrhi_errorState, "the view's entry kept");
    Close(device);
}

static void TestPassHeap(void)
{
    mrhiDevice* device = Open(1, true, false);
    mrhiHeapId heap = MakeHeap(device, 4, 0);
    (void)SubmitWith(device, heap);
    Drain(device);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiPassDef def = mrhiDefaultPassDef();
    def.heap = (mrhiHeapId){1, 99};
    mrhiPassId pass = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiAddPass(device, &def, &pass) == mrhi_errorStale,
          "a stale heap");
    CHECK(mrhiDestroyHeap(device, heap) == mrhi_success, "destroyed while the frame is open");
    def.heap = heap;
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_errorStale, "its id ended");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_success, "dropped");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestMakeAndDestroy();
    TestEntries();
    TestStorage();
    TestSamplers();
    TestFreedAfterFrames();
    TestDestroyEmpties();
    TestSameSlots();
    TestPassHeap();
    return s_failures == 0 ? 0 : 1;
}
