// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sealed resources on a test driver device (mrhi-0015): sealing and
// unsealing refused without bindless sampling, outside a frame's
// declarations, and for what the frame made; a sealed object ending its
// frame sealed, then read without barriers and refused anything but the
// sealed state's reads; unsealing returning it to tracking, a later seal
// winning; and a dropped frame changing nothing.

#include "test_device_setup.h"

static mrhiDevice* s_device;

static void Open(bool sampling)
{
    s_adapter.features.bindlessSampling = true;
    s_adapter.limits.heapSize = 4096;
    s_adapter.limits.samplerHeapSize = 64;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.features.bindlessSampling = sampling;
    def.limits.heapSize = sampling ? 1024 : 0;
    def.limits.samplerHeapSize = sampling ? 16 : 0;
    s_device = OpenWith(def, true);
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

static mrhiTextureId MakeTexture(mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.mipLevels = 2;
    def.usage = usage;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

static mrhiBufferId MakeBuffer(mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    def.usage = usage;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiResourceId ImportTexture(mrhiTextureId texture)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportTexture(s_device, texture, &resource) == mrhi_success, "imported");
    return resource;
}

static mrhiResourceId ImportBuffer(mrhiBufferId buffer)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportBuffer(s_device, buffer, &resource) == mrhi_success, "imported");
    return resource;
}

// Adds a kept pass of a class making one access to all of a resource:
// the answer.
static mrhiResult Use(mrhiResourceId resource, mrhiAccessKind kind, mrhiPassClass passClass)
{
    const mrhiAccess access = {
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    return mrhiAddPass(s_device, &def, &pass);
}

// The compiled frame's barriers.
static mrhiBarrier s_barriers[16];
static size_t s_count;

static void Compile(void)
{
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiGetFrameBarriers(s_device, s_barriers, 16, &s_count) == mrhi_success && s_count <= 16,
          "barriers read");
}

// Whether the barrier at an index moves a resource between two states,
// before a pass (1 for the first) or at the frame's end (0).
static bool Moves(size_t at, uint32_t pass, mrhiResourceState before, mrhiResourceState after)
{
    return at < s_count && s_barriers[at].pass.index1 == pass && s_barriers[at].before == before &&
           s_barriers[at].after == after;
}

static void TestRefusals(void)
{
    Open(false);
    mrhiTextureId texture = MakeTexture(mrhi_textureSampled);
    Begin();
    mrhiResourceId imported = ImportTexture(texture);
    CHECK(mrhiSealResource(s_device, imported) == mrhi_errorUnsupported &&
              mrhiUnsealResource(s_device, imported) == mrhi_errorUnsupported,
          "not without bindless sampling");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close(s_device);

    Open(true);
    texture = MakeTexture(mrhi_textureSampled);
    mrhiTextureId unsampled = MakeTexture(mrhi_textureCopyDestination);
    CHECK(mrhiSealResource(nullptr, imported) == mrhi_errorInvalid &&
              mrhiUnsealResource(nullptr, imported) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiSealResource(s_device, imported) == mrhi_errorState &&
              mrhiUnsealResource(s_device, imported) == mrhi_errorState,
          "no frame open");
    Begin();
    CHECK(mrhiSealResource(s_device, imported) == mrhi_errorStale &&
              mrhiUnsealResource(s_device, imported) == mrhi_errorStale,
          "a resource of another frame");
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 4;
    def.height = 4;
    mrhiResourceId declared = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &declared) == mrhi_success, "declared");
    CHECK(mrhiSealResource(s_device, declared) == mrhi_errorInvalid &&
              mrhiUnsealResource(s_device, declared) == mrhi_errorInvalid,
          "not what the frame made");
    mrhiResourceId plain = ImportTexture(unsampled);
    CHECK(mrhiSealResource(s_device, plain) == mrhi_errorInvalid, "a texture sealed is sampled");
    CHECK(mrhiUnsealResource(s_device, plain) == mrhi_success, "unsealing an unsealed one");
    mrhiTextureId doomed = MakeTexture(mrhi_textureSampled);
    mrhiResourceId gone = ImportTexture(doomed);
    CHECK(mrhiDestroyTexture(s_device, doomed) == mrhi_success, "its texture destroyed");
    CHECK(mrhiSealResource(s_device, gone) == mrhi_errorStale &&
              mrhiUnsealResource(s_device, gone) == mrhi_errorStale,
          "its object gone");
    imported = ImportTexture(texture);
    CHECK(mrhiSealResource(s_device, imported) == mrhi_success, "sealed");
    Compile();
    CHECK(mrhiSealResource(s_device, imported) == mrhi_errorState &&
              mrhiUnsealResource(s_device, imported) == mrhi_errorState,
          "not once compiled");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close(s_device);
}

// A texture written, sealed in that frame, then only sampled.
static void TestSealedTexture(void)
{
    Open(true);
    mrhiTextureId texture =
        MakeTexture(mrhi_textureSampled | mrhi_textureStorage | mrhi_textureCopyDestination |
                    mrhi_textureCopySource | mrhi_textureRenderTarget);
    Begin();
    mrhiResourceId resource = ImportTexture(texture);
    CHECK(Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_success, "uploaded");
    CHECK(mrhiSealResource(s_device, resource) == mrhi_success &&
              mrhiSealResource(s_device, resource) == mrhi_success,
          "sealed, twice");
    CHECK(Use(resource, mrhi_accessCopySource, mrhi_passTransfer) == mrhi_success,
          "a later pass of the sealing frame takes any use");
    Compile();
    CHECK(s_count == 3 && Moves(0, 1, mrhi_stateUndefined, mrhi_stateCopyDestination) &&
              Moves(1, 2, mrhi_stateCopyDestination, mrhi_stateCopySource) &&
              Moves(2, 0, mrhi_stateCopySource, mrhi_stateSealed),
          "sealed after the frame's passes");
    Submit();

    Begin();
    resource = ImportTexture(texture);
    CHECK(Use(resource, mrhi_accessSampled, mrhi_passGraphics) == mrhi_success &&
              Use(resource, mrhi_accessSampled, mrhi_passAsyncCompute) == mrhi_success,
          "sampled in any pass");
    CHECK(Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_errorInvalid &&
              Use(resource, mrhi_accessCopySource, mrhi_passTransfer) == mrhi_errorInvalid &&
              Use(resource, mrhi_accessStorageRead, mrhi_passGraphics) == mrhi_errorInvalid &&
              Use(resource, mrhi_accessStorageWrite, mrhi_passGraphics) == mrhi_errorInvalid,
          "nothing else");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = resource, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "not a target");
    Compile();
    CHECK(s_count == 0, "no barriers");
    Submit();
    Close(s_device);
}

// A buffer sealed with no pass using it, then read every way a buffer
// is read.
static void TestSealedBuffer(void)
{
    Open(true);
    mrhiBufferId buffer =
        MakeBuffer(mrhi_bufferUniform | mrhi_bufferStorage | mrhi_bufferVertex | mrhi_bufferIndex |
                   mrhi_bufferIndirect | mrhi_bufferCopySource | mrhi_bufferCopyDestination);
    Begin();
    CHECK(mrhiSealResource(s_device, ImportBuffer(buffer)) == mrhi_success, "sealed");
    Compile();
    CHECK(s_count == 1 && Moves(0, 0, mrhi_stateUndefined, mrhi_stateSealed), "sealed at the end");
    Submit();

    Begin();
    mrhiResourceId resource = ImportBuffer(buffer);
    CHECK(Use(resource, mrhi_accessUniform, mrhi_passGraphics) == mrhi_success &&
              Use(resource, mrhi_accessVertex, mrhi_passGraphics) == mrhi_success &&
              Use(resource, mrhi_accessIndex, mrhi_passGraphics) == mrhi_success &&
              Use(resource, mrhi_accessIndirect, mrhi_passGraphics) == mrhi_success &&
              Use(resource, mrhi_accessStorageRead, mrhi_passAsyncCompute) == mrhi_success,
          "its reads");
    CHECK(Use(resource, mrhi_accessStorageWrite, mrhi_passGraphics) == mrhi_errorInvalid &&
              Use(resource, mrhi_accessStorageReadWrite, mrhi_passGraphics) == mrhi_errorInvalid &&
              Use(resource, mrhi_accessCopySource, mrhi_passTransfer) == mrhi_errorInvalid &&
              Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_errorInvalid,
          "no writes or copies");
    Compile();
    CHECK(s_count == 0, "no barriers");
    Submit();
    Close(s_device);
}

// Unsealing returns an object to tracking for the passes after it; a
// later seal wins, and a later unseal undoes a seal.
static void TestUnseal(void)
{
    Open(true);
    mrhiTextureId texture = MakeTexture(mrhi_textureSampled | mrhi_textureCopyDestination);
    Begin();
    CHECK(mrhiSealResource(s_device, ImportTexture(texture)) == mrhi_success, "sealed");
    Compile();
    Submit();

    Begin();
    mrhiResourceId resource = ImportTexture(texture);
    CHECK(Use(resource, mrhi_accessSampled, mrhi_passGraphics) == mrhi_success, "read sealed");
    CHECK(mrhiUnsealResource(s_device, resource) == mrhi_success &&
              Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_success,
          "written once unsealed");
    Compile();
    CHECK(s_count == 1 && Moves(0, 2, mrhi_stateSealed, mrhi_stateCopyDestination),
          "from the sealed state, left in the last use's");
    Submit();

    Begin();
    resource = ImportTexture(texture);
    CHECK(Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_success,
          "tracked again");
    CHECK(mrhiSealResource(s_device, resource) == mrhi_success &&
              mrhiUnsealResource(s_device, resource) == mrhi_success,
          "sealed, then unsealed");
    Compile();
    CHECK(s_count == 1 && Moves(0, 1, mrhi_stateCopyDestination, mrhi_stateCopyDestination),
          "the unseal wins");
    Submit();

    Begin();
    resource = ImportTexture(texture);
    CHECK(mrhiSealResource(s_device, resource) == mrhi_success, "sealed");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "then dropped");
    Begin();
    resource = ImportTexture(texture);
    CHECK(Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_success,
          "a dropped frame seals nothing");
    CHECK(mrhiSealResource(s_device, resource) == mrhi_success, "sealed");
    Compile();
    CHECK(s_count == 2 && Moves(0, 1, mrhi_stateCopyDestination, mrhi_stateCopyDestination) &&
              Moves(1, 0, mrhi_stateCopyDestination, mrhi_stateSealed),
          "ended sealed");
    Submit();

    Begin();
    resource = ImportTexture(texture);
    CHECK(mrhiUnsealResource(s_device, resource) == mrhi_success &&
              mrhiSealResource(s_device, resource) == mrhi_success,
          "unsealed, then sealed");
    CHECK(Use(resource, mrhi_accessCopyDestination, mrhi_passTransfer) == mrhi_success,
          "written while tracked");
    Compile();
    CHECK(s_count == 2 && Moves(0, 1, mrhi_stateSealed, mrhi_stateCopyDestination) &&
              Moves(1, 0, mrhi_stateCopyDestination, mrhi_stateSealed),
          "sealed again at the end");
    Submit();
    Close(s_device);
}

int main(void)
{
    ResetAdapter();
    TestRefusals();
    TestSealedTexture();
    TestSealedBuffer();
    TestUnseal();
    return s_failures == 0 ? 0 : 1;
}
