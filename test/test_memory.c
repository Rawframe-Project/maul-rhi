// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame's memory and stores on a test driver device, whose textures
// take 4 bytes a texel and sample (twice that with mips, none when
// transient) and whose buffers round up to 256: declared resources
// placed by lifetime, and targets' stores kept only when read later.

#include "test_device_setup.h"

static mrhiDevice* s_device;

static void Begin(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &def) == mrhi_success, "begun");
}

static void Drop(void)
{
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

static mrhiResourceId Declare(mrhiFormat format, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.width = 64;
    def.height = 64;
    def.sampleCount = samples;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared");
    return resource;
}

static mrhiResourceId DeclareBuffer(uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareBuffer(s_device, &def, &resource) == mrhi_success, "a buffer declared");
    return resource;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

static mrhiPassId AddPass(mrhiPassDef def)
{
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// A pass rendering to a target, reading what it samples.
static mrhiPassId Draw(mrhiResourceId target, mrhiStoreOp store, const mrhiAccess* reads,
                       uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] =
        (mrhiColorTarget){.resource = target, .load = mrhi_loadClear, .store = store};
    def.colorTargetCount = 1;
    def.accesses = reads;
    def.accessCount = count;
    return AddPass(def);
}

static mrhiResourcePlan Plan(mrhiResourceId resource)
{
    mrhiResourcePlan plan = {0};
    CHECK(mrhiGetResourcePlan(s_device, resource, &plan) == mrhi_success, "a plan");
    return plan;
}

static mrhiPassPlan PassPlan(mrhiPassId pass)
{
    mrhiPassPlan plan = {0};
    CHECK(mrhiGetPassPlan(s_device, pass, &plan) == mrhi_success, "a pass plan");
    return plan;
}

static uint64_t Memory(void)
{
    uint64_t bytes = 0;
    CHECK(mrhiGetFrameMemory(s_device, &bytes) == mrhi_success, "the memory");
    return bytes;
}

static const uint64_t kTexture = 64 * 64 * 4;

// A chain of targets, each read by the next pass: the first and the
// third share memory, the second meets both.
static void TestChain(mrhiTextureId windowTexture)
{
    Begin();
    mrhiResourceId a = Declare(mrhi_formatRgba16Float, 1);
    mrhiResourceId b = Declare(mrhi_formatRgba16Float, 1);
    mrhiResourceId c = Declare(mrhi_formatRgba16Float, 1);
    mrhiResourceId unused = Declare(mrhi_formatRgba16Float, 1);
    mrhiResourceId window;
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "imported");
    Draw(a, mrhi_storeKeep, nullptr, 0);
    mrhiAccess readA = Access(a, mrhi_accessSampled);
    Draw(b, mrhi_storeKeep, &readA, 1);
    mrhiAccess readB = Access(b, mrhi_accessSampled);
    Draw(c, mrhi_storeKeep, &readB, 1);
    mrhiAccess readC = Access(c, mrhi_accessSampled);
    Draw(window, mrhi_storeKeep, &readC, 1);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    mrhiResourcePlan plan = Plan(a);
    CHECK(plan.memoryOffset == 0 && plan.memoryBytes == kTexture, "the first at the start");
    plan = Plan(b);
    CHECK(plan.memoryOffset == kTexture && plan.memoryBytes == kTexture, "the second after it");
    plan = Plan(c);
    CHECK(plan.memoryOffset == 0 && plan.memoryBytes == kTexture, "the third where the first was");
    CHECK(Plan(unused).memoryBytes == 0, "an unused resource takes nothing");
    CHECK(Plan(window).memoryBytes == 0, "an imported one lives elsewhere");
    CHECK(Memory() == 2 * kTexture, "two textures' worth");
    Drop();
}

// Buffers aligned to 256, a gap too small skipped.
static void TestBuffers(void)
{
    Begin();
    mrhiResourceId x = DeclareBuffer(100);
    mrhiResourceId y = DeclareBuffer(1000);
    mrhiResourceId z = DeclareBuffer(300);
    mrhiAccess first[2] = {Access(x, mrhi_accessStorageWrite), Access(y, mrhi_accessStorageWrite)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = first;
    def.accessCount = 2;
    def.neverCull = true;
    AddPass(def);
    mrhiAccess second[2] = {Access(y, mrhi_accessStorageRead), Access(z, mrhi_accessStorageWrite)};
    def.accesses = second;
    AddPass(def);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Plan(x).memoryOffset == 0 && Plan(x).memoryBytes == 256, "x at the start");
    CHECK(Plan(y).memoryOffset == 256 && Plan(y).memoryBytes == 1024, "y after it");
    CHECK(Plan(z).memoryOffset == 1280 && Plan(z).memoryBytes == 512,
          "z past y, too large for x's place");
    CHECK(Memory() == 1792, "their end");
    Drop();
    Begin();
    x = DeclareBuffer(100);
    y = DeclareBuffer(1000);
    z = DeclareBuffer(200);
    def.accesses = first;
    first[0] = Access(x, mrhi_accessStorageWrite);
    first[1] = Access(y, mrhi_accessStorageWrite);
    AddPass(def);
    second[0] = Access(y, mrhi_accessStorageRead);
    second[1] = Access(z, mrhi_accessStorageWrite);
    def.accesses = second;
    AddPass(def);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Plan(z).memoryOffset == 0, "a buffer that fits in x's place");
    CHECK(Memory() == 1280, "no more than two at once");
    Drop();
}

// Placed neighbours taken in offset order, not in the order they were
// declared.
static void TestOrder(void)
{
    Begin();
    mrhiResourceId a = DeclareBuffer(1024);
    mrhiResourceId b = DeclareBuffer(1024);
    mrhiResourceId c = DeclareBuffer(1024);
    mrhiResourceId d = DeclareBuffer(1024);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    mrhiAccess first[2] = {Access(a, mrhi_accessStorageWrite), Access(b, mrhi_accessStorageWrite)};
    def.accesses = first;
    def.accessCount = 2;
    AddPass(def);
    mrhiAccess second[2] = {Access(b, mrhi_accessStorageRead), Access(c, mrhi_accessStorageWrite)};
    def.accesses = second;
    AddPass(def);
    mrhiAccess third[3] = {Access(b, mrhi_accessStorageRead), Access(c, mrhi_accessStorageRead),
                           Access(d, mrhi_accessStorageWrite)};
    def.accesses = third;
    def.accessCount = 3;
    AddPass(def);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Plan(a).memoryOffset == 0 && Plan(b).memoryOffset == 1024, "a, then b");
    CHECK(Plan(c).memoryOffset == 0, "c where a was");
    CHECK(Plan(d).memoryOffset == 2048, "d past both c and b");
    CHECK(Memory() == 3072, "three at once at most");
    Drop();
    Begin();
    mrhiTextureDef small = mrhiDefaultTextureDef();
    small.format = mrhi_formatRgba8Unorm;
    small.width = 10;
    small.height = 10;
    mrhiResourceId texture;
    CHECK(mrhiDeclareTexture(s_device, &small, &texture) == mrhi_success, "400 bytes");
    mrhiResourceId buffer = DeclareBuffer(256);
    mrhiAccess both[2] = {Access(texture, mrhi_accessStorageWrite),
                          Access(buffer, mrhi_accessStorageWrite)};
    def.accesses = both;
    def.accessCount = 2;
    AddPass(def);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Plan(texture).memoryBytes == 400, "the texture's bytes");
    CHECK(Plan(buffer).memoryOffset == 512, "the buffer aligned past it");
    Drop();
}

// Targets that live only on chip take no memory.
static void TestTransient(void)
{
    Begin();
    mrhiResourceId samples = Declare(mrhi_formatRgba8Unorm, 4);
    mrhiResourceId resolved = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = samples,
        .load = mrhi_loadClear,
        .store = mrhi_storeDiscard,
        .resolve = resolved,
    };
    def.colorTargetCount = 1;
    AddPass(def);
    mrhiAccess read = Access(resolved, mrhi_accessSampled);
    mrhiPassDef use = mrhiDefaultPassDef();
    use.accesses = &read;
    use.accessCount = 1;
    use.neverCull = true;
    AddPass(use);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Plan(samples).memoryBytes == 0, "the samples on chip");
    CHECK(Plan(resolved).memoryOffset == 0 && Plan(resolved).memoryBytes == kTexture,
          "what they resolve to in memory");
    Drop();
}

// Stores kept only for what is read later or outlives the frame.
static void TestStores(mrhiTextureId windowTexture)
{
    Begin();
    mrhiResourceId read = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiResourceId unread = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiResourceId discarded = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiResourceId depth = Declare(mrhi_formatDepthStencil, 1);
    mrhiResourceId window;
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "imported");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = read, .load = mrhi_loadClear};
    def.colorTargets[1] = (mrhiColorTarget){.resource = unread, .load = mrhi_loadClear};
    def.colorTargets[2] = (mrhiColorTarget){
        .resource = discarded,
        .load = mrhi_loadClear,
        .store = mrhi_storeDiscard,
    };
    def.colorTargets[3] = (mrhiColorTarget){.resource = window, .load = mrhi_loadClear};
    def.colorTargetCount = 4;
    def.depthTarget = (mrhiDepthTarget){
        .resource = depth,
        .depthLoad = mrhi_loadClear,
        .stencilLoad = mrhi_loadClear,
    };
    mrhiPassId first = AddPass(def);
    mrhiAccess reads[3] = {Access(read, mrhi_accessSampled), Access(discarded, mrhi_accessSampled),
                           Access(depth, mrhi_accessSampled)};
    reads[2].range.aspect = mrhi_aspectDepthOnly;
    mrhiPassDef later = mrhiDefaultPassDef();
    later.accesses = reads;
    later.accessCount = 3;
    later.neverCull = true;
    AddPass(later);
    mrhiPassId culled = Draw(unread, mrhi_storeKeep, nullptr, 0);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    mrhiPassPlan plan = PassPlan(first);
    CHECK(plan.kept, "kept");
    CHECK(plan.colorStores[0] == mrhi_storeKeep, "read later, kept");
    CHECK(plan.colorStores[1] == mrhi_storeDiscard, "never read, discarded");
    CHECK(plan.colorStores[2] == mrhi_storeDiscard, "asked to discard, discarded");
    CHECK(plan.colorStores[3] == mrhi_storeKeep, "an imported target kept");
    CHECK(plan.depthStore == mrhi_storeKeep, "depth read later");
    CHECK(plan.stencilStore == mrhi_storeDiscard, "stencil never read");
    CHECK(!PassPlan(culled).kept, "a culled pass");
    Drop();
    Begin();
    mrhiResourceId kept = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiResourceId overwritten = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiResourceId unreadOut = Declare(mrhi_formatRgba8Unorm, 1);
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "imported");
    def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = kept, .load = mrhi_loadClear};
    def.colorTargets[1] = (mrhiColorTarget){.resource = overwritten, .load = mrhi_loadClear};
    def.colorTargets[2] = (mrhiColorTarget){.resource = window, .load = mrhi_loadClear};
    def.colorTargetCount = 3;
    mrhiPassId writer = AddPass(def);
    def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = kept, .load = mrhi_loadKeep};
    def.colorTargets[1] = (mrhiColorTarget){.resource = overwritten, .load = mrhi_loadClear};
    def.colorTargets[2] = (mrhiColorTarget){.resource = window, .load = mrhi_loadKeep};
    def.colorTargetCount = 3;
    mrhiPassId again = AddPass(def);
    mrhiAccess lateRead = Access(kept, mrhi_accessSampled);
    Draw(unreadOut, mrhi_storeKeep, &lateRead, 1);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    plan = PassPlan(writer);
    CHECK(plan.colorStores[0] == mrhi_storeKeep, "kept for the next pass's load");
    CHECK(plan.colorStores[1] == mrhi_storeDiscard, "overwritten next without a read");
    plan = PassPlan(again);
    CHECK(plan.colorStores[0] == mrhi_storeDiscard,
          "its own load is no later read, and the reader after it is culled");
    Drop();
}

static void TestRefusals(void)
{
    mrhiPassPlan plan;
    uint64_t bytes;
    CHECK(mrhiGetFrameMemory(s_device, &bytes) == mrhi_errorState, "no frame");
    Begin();
    mrhiResourceId target = Declare(mrhi_formatRgba8Unorm, 1);
    mrhiPassId pass = Draw(target, mrhi_storeKeep, nullptr, 0);
    CHECK(mrhiGetPassPlan(s_device, pass, &plan) == mrhi_errorState, "not compiled");
    CHECK(mrhiGetFrameMemory(s_device, &bytes) == mrhi_errorState, "not compiled");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Memory() == 0, "a culled frame takes nothing");
    CHECK(mrhiGetPassPlan(s_device, pass, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiGetPassPlan(nullptr, pass, &plan) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetPassPlan(s_device, (mrhiPassId){0, 0}, &plan) == mrhi_errorStale, "no pass");
    CHECK(mrhiGetPassPlan(s_device, (mrhiPassId){2, pass.generation}, &plan) == mrhi_errorStale,
          "past the passes");
    CHECK(mrhiGetFrameMemory(s_device, nullptr) == mrhi_errorInvalid, "no bytes out");
    CHECK(mrhiGetFrameMemory(nullptr, &bytes) == mrhi_errorInvalid, "no device");
    Drop();
    Begin();
    Draw(Declare(mrhi_formatRgba8Unorm, 1), mrhi_storeKeep, nullptr, 0);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiGetPassPlan(s_device, pass, &plan) == mrhi_errorStale, "the last frame's");
    Drop();
}

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 64;
    def.height = 64;
    def.usage = mrhi_textureRenderTarget;
    mrhiTextureId window;
    CHECK(mrhiCreateTexture(s_device, &def, &window) == mrhi_success, "the window's texture");
    TestChain(window);
    TestBuffers();
    TestOrder();
    TestTransient();
    TestStores(window);
    TestRefusals();
    CHECK(mrhiDestroyTexture(s_device, window) == mrhi_success, "done");
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
