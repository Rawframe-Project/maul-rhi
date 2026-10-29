// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// New textures on a test driver device: the first frame that imports one
// clears it whole in the library's pass, from the undefined state, and no
// later frame does. A texture of one mip and layer whose first use is a
// target that clears it and keeps the result is left out, as is any
// transient. The clear's barrier comes from no budget of the program's,
// and neither do its use and its command.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/resources.h"

static mrhiTestFrameLog s_log;

static mrhiDevice* Open(mrhiDeviceDef def)
{
    s_adapter.frameLog = &s_log;
    return OpenWith(def, true);
}

static mrhiTextureId MakeTexture(mrhiDevice* device, mrhiFormat format, mrhiTextureKind kind,
                                 uint32_t mips, uint32_t layers)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.kind = kind;
    def.width = 16;
    def.height = 16;
    def.depthOrLayers = layers;
    def.mipLevels = mips;
    def.usage = mrhi_textureSampled | mrhi_textureRenderTarget | mrhi_textureCopySource |
                mrhi_textureCopyDestination;
    if (format == mrhi_formatDepth32Float || format == mrhi_formatDepthStencil)
    {
        def.usage = mrhi_textureSampled | mrhi_textureRenderTarget | mrhi_textureCopySource;
    }
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

static mrhiResourceId Begin(mrhiDevice* device, mrhiTextureId texture)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiDevice_BeginFrame(device, &def) == mrhi_success &&
              mrhiImportTexture(device, texture, &resource) == mrhi_success,
          "a frame with the texture");
    return resource;
}

// A pass using the whole texture in one way.
static mrhiPassId Use(mrhiDevice* device, mrhiResourceId resource, mrhiAccessKind kind)
{
    mrhiAccess access = {
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass using it");
    return pass;
}

// A pass rendering to the texture's first mip and layer, as a color or a
// depth target, with the loads and stores given (the stencil's too).
static void Render(mrhiDevice* device, mrhiResourceId resource, bool depth, mrhiLoadOp load,
                   mrhiStoreOp store, mrhiLoadOp stencilLoad)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    if (depth)
    {
        def.depthTarget = (mrhiDepthTarget){
            .resource = resource,
            .depthLoad = load,
            .depthStore = store,
            .stencilLoad = stencilLoad,
            .stencilStore = stencilLoad == mrhi_loadKeep ? mrhi_storeKeep : store,
        };
    }
    else
    {
        def.colorTargets[0] = (mrhiColorTarget){.resource = resource, .load = load, .store = store};
        def.colorTargetCount = 1;
    }
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass rendering to it");
}

// Submits the open frame, the log counting it alone.
static void Submit(mrhiDevice* device)
{
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
    s_log = (mrhiTestFrameLog){0};
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success, "submitted");
}

// The first frame that imports a texture clears it, every mip and layer
// at once, in the library's pass, from the undefined state; the next
// frame does not.
static void TestFirstFrame(void)
{
    mrhiDevice* device = Open(mrhiDefaultDeviceDef());
    mrhiTextureId texture = MakeTexture(device, mrhi_formatRgba8Unorm, mrhi_texture2dArray, 3, 2);
    mrhiResourceId resource = Begin(device, texture);
    mrhiPassId pass = Use(device, resource, mrhi_accessCopySource);
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
    mrhiBarrier barriers[4];
    size_t count = 0;
    CHECK(mrhiDevice_GetFrameBarriers(device, barriers, 4, &count) == mrhi_success && count == 2 &&
              barriers[0].pass.index1 == 1 && barriers[0].before == mrhi_stateUndefined &&
              barriers[0].after == mrhi_stateCopyDestination && barriers[0].range.mipCount == 3 &&
              barriers[0].range.layerCount == 2 && barriers[1].pass.index1 == pass.index1 &&
              barriers[1].before == mrhi_stateCopyDestination,
          "the whole texture cleared, then read");
    s_log = (mrhiTestFrameLog){0};
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success && s_log.passes == 2 &&
              s_log.commands == 1,
          "one clear in the library's pass");
    resource = Begin(device, texture);
    (void)Use(device, resource, mrhi_accessCopySource);
    Submit(device);
    CHECK(s_log.passes == 1 && s_log.commands == 0, "the next frame clears nothing");
    Close(device);
}

// A first use that clears and keeps the one mip and layer of a texture,
// as a color or depth target and the stencil too, takes the place of the
// clear; any other first use, or a texture of more mips or layers, keeps
// it. A texture its first use cleared is not cleared later either.
static void TestElision(void)
{
    typedef struct Case
    {
        const char* what;
        mrhiFormat format;
        mrhiTextureKind kind;
        uint32_t mips;
        uint32_t layers;
        mrhiLoadOp load;
        mrhiStoreOp store;
        mrhiLoadOp stencilLoad;
        bool sampledFirst;
        bool cleared;
    } Case;
    const Case cases[] = {
        {"a color target cleared and kept", mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1,
         mrhi_loadClear, mrhi_storeKeep, mrhi_loadClear, false, false},
        {"a depth target cleared and kept", mrhi_formatDepth32Float, mrhi_texture2d, 1, 1,
         mrhi_loadClear, mrhi_storeKeep, mrhi_loadClear, false, false},
        {"depth and stencil cleared and kept", mrhi_formatDepthStencil, mrhi_texture2d, 1, 1,
         mrhi_loadClear, mrhi_storeKeep, mrhi_loadClear, false, false},
        {"the stencil kept as it was", mrhi_formatDepthStencil, mrhi_texture2d, 1, 1,
         mrhi_loadClear, mrhi_storeKeep, mrhi_loadKeep, false, true},
        {"cleared but not kept", mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1, mrhi_loadClear,
         mrhi_storeDiscard, mrhi_loadClear, false, true},
        {"kept as it was", mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1, mrhi_loadKeep,
         mrhi_storeKeep, mrhi_loadClear, false, true},
        {"discarded", mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1, mrhi_loadDiscard, mrhi_storeKeep,
         mrhi_loadClear, false, true},
        {"one mip of two", mrhi_formatRgba8Unorm, mrhi_texture2d, 2, 1, mrhi_loadClear,
         mrhi_storeKeep, mrhi_loadClear, false, true},
        {"one layer of two", mrhi_formatRgba8Unorm, mrhi_texture2dArray, 1, 2, mrhi_loadClear,
         mrhi_storeKeep, mrhi_loadClear, false, true},
        {"sampled before it is cleared", mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1,
         mrhi_loadClear, mrhi_storeKeep, mrhi_loadClear, true, true},
    };
    mrhiDevice* device = Open(mrhiDefaultDeviceDef());
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const Case* c = &cases[i];
        mrhiTextureId texture = MakeTexture(device, c->format, c->kind, c->mips, c->layers);
        mrhiResourceId resource = Begin(device, texture);
        if (c->sampledFirst)
        {
            (void)Use(device, resource, mrhi_accessCopySource);
        }
        bool depth = c->format != mrhi_formatRgba8Unorm;
        Render(device, resource, depth, c->load, c->store,
               c->format == mrhi_formatDepthStencil ? c->stencilLoad : mrhi_loadKeep);
        Submit(device);
        CHECK(s_log.commands == (c->cleared ? 1u : 0u), c->what);
        resource = Begin(device, texture);
        (void)Use(device, resource, mrhi_accessCopySource);
        Submit(device);
        CHECK(s_log.commands == 0, "and no later frame clears it");
        CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "ended");
    }
    Close(device);
}

// A transient texture is never cleared: a pass reads one the frame
// declares only after one wrote it, and a device texture made transient
// keeps its contents only inside a render pass.
static void TestTransient(void)
{
    mrhiDevice* device = Open(mrhiDefaultDeviceDef());
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    mrhiResourceId transient = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiDeclareTexture(device, &def, &transient) == mrhi_success,
          "a transient");
    Render(device, transient, false, mrhi_loadDiscard, mrhi_storeKeep, mrhi_loadClear);
    (void)Use(device, transient, mrhi_accessCopySource);
    Submit(device);
    CHECK(s_log.passes == 2 && s_log.commands == 0, "rendered and read, never cleared");
    def.usage = mrhi_textureRenderTarget | mrhi_textureTransient;
    mrhiTextureId onChip = {0};
    CHECK(mrhiCreateTexture(device, &def, &onChip) == mrhi_success, "a transient device texture");
    Render(device, Begin(device, onChip), false, mrhi_loadDiscard, mrhi_storeKeep, mrhi_loadKeep);
    Submit(device);
    CHECK(s_log.passes == 1 && s_log.commands == 0, "its contents live in a pass, never cleared");
    Close(device);
}

// A device whose frames plan one barrier: a new texture read in the
// first frame that imports it compiles, its clear's barrier beside the
// program's one; a frame that needs two of the program's own is refused.
static void TestBarrierBudget(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.budgets.frameBarriers = 1;
    mrhiDevice* device = Open(deviceDef);
    mrhiTextureId old = MakeTexture(device, mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1);
    (void)Use(device, Begin(device, old), mrhi_accessCopySource);
    Submit(device);
    CHECK(s_log.barriers == 2 && s_log.commands == 1, "the clear's barrier beside the read's");
    mrhiTextureId fresh = MakeTexture(device, mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resources[2] = {{0}, {0}};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, old, &resources[0]) == mrhi_success &&
              mrhiImportTexture(device, fresh, &resources[1]) == mrhi_success,
          "a frame with both");
    (void)Use(device, resources[0], mrhi_accessCopyDestination);
    (void)Use(device, resources[1], mrhi_accessCopySource);
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_errorCapacity &&
              mrhiDevice_DropFrame(device) == mrhi_success,
          "two barriers of the program's own refused");
    Close(device);
}

// Records clears of a buffer into the frame's one pass until the arena is
// full, beside the texture imported and unused: how many fit. The frame
// is dropped, as a pass out of room cannot be submitted.
static uint32_t Fill(mrhiDevice* device, mrhiTextureId texture, mrhiBufferId buffer)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId t = {0};
    mrhiResourceId b = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success,
          "a frame with both");
    mrhiPassId pass = Use(device, b, mrhi_accessCopyDestination);
    mrhiPassDef def = mrhiDefaultPassDef();
    mrhiPassId more = {0};
    CHECK(mrhiAddPass(device, &def, &more) == mrhi_errorCapacity, "no other pass");
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, pass) == mrhi_success,
          "begun");
    uint32_t fitted = 0;
    while (fitted < 100000 && mrhiPass_ClearBuffer(device, pass, b, 0, 4) == mrhi_success)
    {
        ++fitted;
    }
    CHECK(mrhiPass_End(device, pass) == mrhi_success &&
              mrhiDevice_DropFrame(device) == mrhi_success,
          "filled and dropped");
    return fitted;
}

// A device whose frames take one pass, the two accesses AddPass holds
// for it and 4096 bytes of commands gives the program as much of each in
// a frame that clears a new texture as in one that does not.
static void TestBudgets(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.budgets.framePasses = 1;
    deviceDef.budgets.frameAccesses = 2;
    deviceDef.budgets.frameCommandBytes = 4096;
    mrhiDevice* device = Open(deviceDef);
    mrhiTextureId texture = MakeTexture(device, mrhi_formatRgba8Unorm, mrhi_texture2d, 1, 1);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success, "a buffer");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &resource) == mrhi_success,
          "the buffer's own first frame");
    Submit(device);
    uint32_t first = Fill(device, texture, buffer);
    (void)Begin(device, texture);
    Submit(device);
    CHECK(s_log.commands == 1, "the texture cleared in a frame of its own");
    uint32_t later = Fill(device, texture, buffer);
    CHECK(first > 0 && first == later,
          "the program's whole arena beside the clear of a new texture, as without it");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestFirstFrame();
    TestElision();
    TestTransient();
    TestBarrierBudget();
    TestBudgets();
    return s_failures == 0 ? 0 : 1;
}
