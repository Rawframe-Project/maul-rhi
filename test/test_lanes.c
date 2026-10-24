// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The order across pass classes a driver is handed: each kept pass names,
// by class, the latest pass of that class it must follow. A read after
// another class's write; a write after reads in two other classes; a read
// that needs no barrier, which follows the pass that set the part's state;
// parts that do not meet; memory a placed resource takes over from another
// class's resources; native passes; and passes of one class, which follow
// one another by order alone. Each frame is captured as the test driver is
// handed it.

#include "device_core.h"
#include "test_device_setup.h"

static mrhiDevice* s_device;
static mrhiDeviceDriverVtable s_capture;
static const mrhiDeviceDriverVtable* s_handedTo;

// The last submitted frame's waits, by pass index; a pass it did not
// hand the driver keeps none.
#define MAX_PASSES 16
static mrhiPassId s_waits[MAX_PASSES + 1][MRHI_PASS_CLASSES];
static bool s_handed[MAX_PASSES + 1];

static mrhiResult Capture(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    for (uint32_t i = 0; i <= MAX_PASSES; ++i)
    {
        s_handed[i] = false;
    }
    for (uint32_t i = 0; i < frame->passCount; ++i)
    {
        const mrhiDriverPass* pass = &frame->passes[i];
        CHECK(pass->id.index1 <= MAX_PASSES, "a pass the test made");
        uint32_t index = pass->id.index1 <= MAX_PASSES ? pass->id.index1 : 0;
        s_handed[index] = true;
        for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
        {
            s_waits[index][c] = pass->waitFor[c];
        }
    }
    return s_handedTo->submitFrame(self, frame, tag);
}

static void Open(void)
{
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    s_handedTo = s_device->driver.vtable;
    s_capture = *s_handedTo;
    s_capture.submitFrame = Capture;
    s_device->driver.vtable = &s_capture;
}

static void Begin(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &def) == mrhi_success, "begun");
}

static void Submit(void)
{
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
}

// Whether a pass was handed the driver following exactly the given pass of
// each class (null ids for none).
static bool Waits(mrhiPassId pass, mrhiPassId graphics, mrhiPassId compute, mrhiPassId transfer)
{
    const mrhiPassId expected[MRHI_PASS_CLASSES] = {graphics, compute, transfer};
    bool same = pass.index1 <= MAX_PASSES && s_handed[pass.index1];
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES && same; ++c)
    {
        same = s_waits[pass.index1][c].index1 == expected[c].index1 &&
               s_waits[pass.index1][c].generation == expected[c].generation;
    }
    return same;
}

static mrhiResourceId Buffer(void)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    mrhiResourceId buffer = {0};
    CHECK(mrhiDeclareBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind, uint32_t baseMip)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = baseMip, .mipCount = 1, .layerCount = MRHI_REMAINING},
    };
}

static mrhiPassId Pass(mrhiPassClass passClass, mrhiAccess access, bool neverCull)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.accesses = &access;
    def.accessCount = 1;
    def.neverCull = neverCull;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

static const mrhiPassId kNone = {0};

// A read after another class's write follows it; the writer follows
// nothing.
static void TestReadAfterWrite(void)
{
    Open();
    Begin();
    mrhiResourceId buffer = Buffer();
    mrhiPassId write =
        Pass(mrhi_passAsyncCompute, Access(buffer, mrhi_accessStorageWrite, 0), false);
    mrhiPassId read = Pass(mrhi_passGraphics, Access(buffer, mrhi_accessStorageRead, 0), true);
    Submit();
    CHECK(Waits(write, kNone, kNone, kNone), "the write follows nothing");
    CHECK(Waits(read, kNone, write, kNone), "the read follows the write");
    Close(s_device);
}

// A write after reads in two other classes follows the last of each. The
// first read follows the first write; the second needs no barrier after
// the first, so it follows the pass whose barrier set the state.
static void TestWriteAfterReads(void)
{
    Open();
    Begin();
    mrhiResourceId buffer = Buffer();
    mrhiPassId first = Pass(mrhi_passGraphics, Access(buffer, mrhi_accessStorageWrite, 0), false);
    mrhiPassId compute =
        Pass(mrhi_passAsyncCompute, Access(buffer, mrhi_accessStorageRead, 0), true);
    mrhiPassId copy = Pass(mrhi_passTransfer, Access(buffer, mrhi_accessCopySource, 0), true);
    mrhiPassId second = Pass(mrhi_passGraphics, Access(buffer, mrhi_accessStorageWrite, 0), true);
    Submit();
    CHECK(Waits(compute, first, kNone, kNone), "the first read follows the first write");
    CHECK(Waits(copy, kNone, compute, kNone), "the second read follows the first");
    CHECK(Waits(second, kNone, compute, copy), "the second write follows both reads");
    Close(s_device);
}

// A read that needs no barrier follows the pass whose barrier set the
// part's state, not the write before it: the order carries through.
static void TestReadAfterRead(void)
{
    Open();
    Begin();
    mrhiResourceId buffer = Buffer();
    mrhiPassId write =
        Pass(mrhi_passAsyncCompute, Access(buffer, mrhi_accessStorageWrite, 0), false);
    mrhiPassId read = Pass(mrhi_passGraphics, Access(buffer, mrhi_accessStorageRead, 0), true);
    mrhiPassId copy = Pass(mrhi_passTransfer, Access(buffer, mrhi_accessCopySource, 0), true);
    Submit();
    CHECK(Waits(read, kNone, write, kNone), "the first read follows the write");
    CHECK(Waits(copy, read, kNone, kNone), "the second read follows the first");
    Close(s_device);
}

// Parts that do not meet order nothing: another class's write to one mip
// and a read of another.
static void TestDisjointParts(void)
{
    Open();
    Begin();
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 64;
    def.height = 64;
    def.mipLevels = 2;
    mrhiResourceId texture = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    mrhiPassId write =
        Pass(mrhi_passAsyncCompute, Access(texture, mrhi_accessStorageWrite, 0), true);
    mrhiPassId read = Pass(mrhi_passGraphics, Access(texture, mrhi_accessSampled, 1), true);
    Submit();
    CHECK(Waits(write, kNone, kNone, kNone) && Waits(read, kNone, kNone, kNone),
          "no order between different mips");
    Close(s_device);
}

// Planes keep their order apart: after a write to both, another class's
// read of the depth plane leaves the stencil plane's writer as it was, so
// a third class's read of the stencil plane follows the writer alone.
static void TestPlanes(void)
{
    Open();
    Begin();
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatDepthStencil;
    textureDef.width = 64;
    textureDef.height = 64;
    mrhiResourceId depth = {0};
    CHECK(mrhiDeclareTexture(s_device, &textureDef, &depth) == mrhi_success, "a depth texture");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.depthTarget = (mrhiDepthTarget){
        .resource = depth,
        .depthLoad = mrhi_loadClear,
        .stencilLoad = mrhi_loadClear,
    };
    mrhiPassId write = {0};
    CHECK(mrhiAddPass(s_device, &def, &write) == mrhi_success, "the write");
    mrhiAccess depthOnly = Access(depth, mrhi_accessSampled, 0);
    depthOnly.range.aspect = mrhi_aspectDepthOnly;
    mrhiPassId sample = Pass(mrhi_passAsyncCompute, depthOnly, true);
    mrhiAccess stencilOnly = Access(depth, mrhi_accessCopySource, 0);
    stencilOnly.range.aspect = mrhi_aspectStencilOnly;
    mrhiPassId copy = Pass(mrhi_passTransfer, stencilOnly, true);
    Submit();
    CHECK(Waits(sample, write, kNone, kNone), "the depth read follows the write");
    CHECK(Waits(copy, write, kNone, kNone), "the stencil read follows the write alone");
    Close(s_device);
}

// Memory a declared buffer takes over from one another class used: every
// use of it follows that class's last use of the memory.
static void TestAliasing(void)
{
    Open();
    Begin();
    mrhiResourceId early = Buffer();
    mrhiResourceId late = Buffer();
    mrhiPassId write =
        Pass(mrhi_passAsyncCompute, Access(early, mrhi_accessStorageWrite, 0), false);
    mrhiPassId read = Pass(mrhi_passAsyncCompute, Access(early, mrhi_accessStorageRead, 0), true);
    mrhiPassId copy = Pass(mrhi_passTransfer, Access(late, mrhi_accessCopyDestination, 0), false);
    mrhiPassId graphics = Pass(mrhi_passGraphics, Access(late, mrhi_accessStorageRead, 0), true);
    mrhiResourcePlan earlyPlan;
    mrhiResourcePlan latePlan;
    CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
              mrhiGetResourcePlan(s_device, early, &earlyPlan) == mrhi_success &&
              mrhiGetResourcePlan(s_device, late, &latePlan) == mrhi_success &&
              latePlan.memoryOffset == earlyPlan.memoryOffset,
          "the later buffer takes the earlier one's memory");
    Submit();
    CHECK(Waits(write, kNone, kNone, kNone), "the first use follows nothing");
    CHECK(Waits(copy, kNone, read, kNone),
          "the new resource's first use follows the old one's last");
    CHECK(Waits(graphics, kNone, read, copy),
          "its read follows that use too, beside the write before it");
    Close(s_device);
}

// A native pass follows every class's passes before it, and every class's
// passes after it follow it.
static void TestNative(void)
{
    s_adapter.info.driver = mrhi_driverVulkan;
    Open();
    Begin();
    mrhiResourceId buffer = Buffer();
    mrhiPassId compute =
        Pass(mrhi_passAsyncCompute, Access(buffer, mrhi_accessStorageWrite, 0), true);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.native = true;
    def.neverCull = true;
    mrhiPassId native = {0};
    CHECK(mrhiAddPass(s_device, &def, &native) == mrhi_success, "a native pass");
    // An imported buffer, which takes no frame memory to order it.
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId object = {0};
    mrhiResourceId other = {0};
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &object) == mrhi_success &&
              mrhiImportBuffer(s_device, object, &other) == mrhi_success,
          "an imported buffer");
    mrhiPassId copy = Pass(mrhi_passTransfer, Access(other, mrhi_accessCopyDestination, 0), true);
    Submit();
    CHECK(Waits(native, kNone, compute, kNone), "the native pass follows the passes before it");
    CHECK(Waits(copy, native, kNone, kNone), "a later pass follows the native pass");
    Close(s_device);
}

// Passes of one class follow one another by order alone.
static void TestOneClass(void)
{
    Open();
    Begin();
    mrhiResourceId buffer = Buffer();
    mrhiPassId write =
        Pass(mrhi_passAsyncCompute, Access(buffer, mrhi_accessStorageWrite, 0), false);
    mrhiPassId read = Pass(mrhi_passAsyncCompute, Access(buffer, mrhi_accessStorageRead, 0), true);
    Submit();
    CHECK(Waits(write, kNone, kNone, kNone) && Waits(read, kNone, kNone, kNone),
          "no wait within a class");
    Close(s_device);
}

int main(void)
{
    ResetAdapter();
    TestReadAfterWrite();
    TestWriteAfterReads();
    TestReadAfterRead();
    TestDisjointParts();
    TestPlanes();
    TestAliasing();
    TestNative();
    TestOneClass();
    return s_failures == 0 ? 0 : 1;
}
