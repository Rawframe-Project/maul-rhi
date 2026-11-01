// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Passes and the compile on a test driver device: accesses and targets
// checked against their resources and the pass's class, WebGPU's usage
// scopes, writes before reads, culling from the frame's outputs, and the
// usages derived for declared textures.

#include "test_device_setup.h"

#include <math.h>

static mrhiDevice* s_device;

static void Begin(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &def) == mrhi_success, "begun");
}

static void Drop(void)
{
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
}

static mrhiResourceId Declare(mrhiFormat format, uint32_t width, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.width = width;
    def.height = width;
    def.sampleCount = samples;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared");
    return resource;
}

static mrhiResourceId DeclareSized(mrhiTextureKind kind, uint32_t width, uint32_t height,
                                   uint32_t layers, uint32_t mips)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = mrhi_formatRgba8Unorm;
    def.width = width;
    def.height = height;
    def.depthOrLayers = layers;
    def.mipLevels = mips;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared with a size");
    return resource;
}

static mrhiResourceId DeclareMips(uint32_t mips)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba16Float;
    def.width = 64;
    def.height = 64;
    def.mipLevels = mips;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared with mips");
    return resource;
}

static mrhiResourceId DeclareBuffer(void)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 1024;
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

static mrhiColorTarget Color(mrhiResourceId resource, mrhiLoadOp load)
{
    return (mrhiColorTarget){.resource = resource, .load = load};
}

// A pass rendering to one color target, with its accesses.
static mrhiPassDef Pass(mrhiColorTarget target, const mrhiAccess* accesses, uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = target;
    def.colorTargetCount = target.resource.index1 != 0 ? 1 : 0;
    def.accesses = accesses;
    def.accessCount = count;
    return def;
}

static mrhiResult Add(mrhiPassDef def)
{
    mrhiPassId pass;
    return mrhiAddPass(s_device, &def, &pass);
}

static bool Kept(mrhiPassId pass)
{
    bool kept = false;
    CHECK(mrhiPass_IsKept(s_device, pass, &kept) == mrhi_success, "read");
    return kept;
}

// An imported texture made with the usages.
static mrhiTextureId MakeTexture(mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 64;
    def.height = 64;
    def.usage = usage;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a device texture");
    return texture;
}

static void TestCulling(void)
{
    mrhiTextureId output = MakeTexture(mrhi_textureRenderTarget);
    Begin();
    mrhiResourceId scene = Declare(mrhi_formatRgba16Float, 64, 1);
    mrhiResourceId unused = Declare(mrhi_formatRgba16Float, 64, 1);
    mrhiResourceId chain = Declare(mrhi_formatRgba16Float, 64, 1);
    mrhiResourceId window;
    CHECK(mrhiImportTexture(s_device, output, &window) == mrhi_success, "imported");
    mrhiPassId draw, dead, feeds, fed, compose, side;
    mrhiPassDef def = Pass(Color(scene, mrhi_loadClear), nullptr, 0);
    CHECK(mrhiAddPass(s_device, &def, &draw) == mrhi_success, "the scene");
    def = Pass(Color(unused, mrhi_loadClear), nullptr, 0);
    CHECK(mrhiAddPass(s_device, &def, &dead) == mrhi_success, "a target nobody reads");
    def = Pass(Color(chain, mrhi_loadClear), nullptr, 0);
    CHECK(mrhiAddPass(s_device, &def, &feeds) == mrhi_success, "feeds a dead end");
    mrhiAccess readChain = Access(chain, mrhi_accessSampled);
    def = Pass(Color(unused, mrhi_loadKeep), &readChain, 1);
    CHECK(mrhiAddPass(s_device, &def, &fed) == mrhi_success, "the dead end");
    mrhiAccess readScene = Access(scene, mrhi_accessSampled);
    def = Pass(Color(window, mrhi_loadDiscard), &readScene, 1);
    CHECK(mrhiAddPass(s_device, &def, &compose) == mrhi_success, "to the imported target");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    CHECK(mrhiAddPass(s_device, &def, &side) == mrhi_success, "never culled");
    bool kept;
    CHECK(mrhiPass_IsKept(s_device, draw, &kept) == mrhi_errorState, "not compiled");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(Kept(draw) && Kept(compose) && Kept(side), "what the output needs, and the side pass");
    CHECK(!Kept(dead) && !Kept(feeds) && !Kept(fed), "what nothing kept needs");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_errorState, "compiled once");
    CHECK(Add(Pass(Color(scene, mrhi_loadClear), nullptr, 0)) == mrhi_errorState,
          "no pass after the compile");
    mrhiTextureDef late = mrhiDefaultTextureDef();
    late.format = mrhi_formatRgba8Unorm;
    late.width = 4;
    late.height = 4;
    mrhiResourceId resource;
    CHECK(mrhiDeclareTexture(s_device, &late, &resource) == mrhi_errorState,
          "no resource after the compile");
    CHECK(mrhiPass_IsKept(s_device, (mrhiPassId){0, 0}, &kept) == mrhi_errorStale, "no pass");
    CHECK(mrhiPass_IsKept(s_device, (mrhiPassId){side.index1 + 1, draw.generation}, &kept) ==
              mrhi_errorStale,
          "past the passes");
    CHECK(mrhiPass_IsKept(s_device, draw, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiPass_IsKept(nullptr, draw, &kept) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDevice_CompileFrame(nullptr) == mrhi_errorInvalid, "no device to compile");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "submitted");
    CHECK(mrhiPass_IsKept(s_device, draw, &kept) == mrhi_errorState, "the frame closed");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_errorState, "nothing to compile");
    Begin();
    CHECK(mrhiPass_IsKept(s_device, draw, &kept) == mrhi_errorState, "not compiled yet");
    // The last frame's scene slot, needed there, is only written here.
    mrhiResourceId reused = Declare(mrhi_formatRgba16Float, 64, 1);
    CHECK(reused.index1 == scene.index1, "the scene's slot");
    mrhiPassId lone;
    def = Pass(Color(reused, mrhi_loadClear), nullptr, 0);
    CHECK(mrhiAddPass(s_device, &def, &lone) == mrhi_success, "a pass nothing reads");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(!Kept(lone), "culled, whatever the last frame needed");
    CHECK(mrhiPass_IsKept(s_device, draw, &kept) == mrhi_errorStale, "a pass of the last frame");
    Drop();
    Begin();
    mrhiResourceId overwritten = Declare(mrhi_formatRgba16Float, 64, 1);
    CHECK(mrhiImportTexture(s_device, output, &window) == mrhi_success, "imported");
    mrhiPassId first, second;
    def = Pass(Color(overwritten, mrhi_loadClear), nullptr, 0);
    CHECK(mrhiAddPass(s_device, &def, &first) == mrhi_success, "written");
    def = Pass(Color(overwritten, mrhi_loadClear), nullptr, 0);
    def.colorTargets[1] = Color(window, mrhi_loadClear);
    def.colorTargetCount = 2;
    CHECK(mrhiAddPass(s_device, &def, &second) == mrhi_success, "written again, not read");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(!Kept(first) && Kept(second), "a write nobody reads before the next write");
    Drop();
    CHECK(mrhiDestroyTexture(s_device, output) == mrhi_success, "done");
}

// Submitting compiles, and a compile that fails keeps the frame open.
static void TestDerivedUsages(void)
{
    Begin();
    mrhiResourceId srgb = Declare(mrhi_formatRgba8UnormSrgb, 64, 1);
    mrhiAccess write = Access(srgb, mrhi_accessStorageWrite);
    mrhiPassDef def = Pass((mrhiColorTarget){0}, &write, 1);
    def.neverCull = true;
    CHECK(Add(def) == mrhi_success, "sRGB written as storage");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorUnsupported, "sRGB has no storage");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_errorUnsupported,
          "still open, and still refused");
    Drop();
    Begin();
    srgb = Declare(mrhi_formatRgba8UnormSrgb, 64, 1);
    write = Access(srgb, mrhi_accessStorageWrite);
    CHECK(Add(Pass((mrhiColorTarget){0}, &write, 1)) == mrhi_success, "culled");
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success,
          "a culled pass derives nothing");
    Begin();
    mrhiResourceId samples = Declare(mrhi_formatRgba8Unorm, 64, 4);
    CHECK(Add(Pass(Color(samples, mrhi_loadClear), nullptr, 0)) == mrhi_success, "drawn");
    mrhiAccess read = Access(samples, mrhi_accessSampled);
    def = Pass((mrhiColorTarget){0}, &read, 1);
    def.neverCull = true;
    CHECK(Add(def) == mrhi_success, "then sampled");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "a target also sampled");
    Drop();
    Begin();
    samples = Declare(mrhi_formatRgba8Unorm, 64, 4);
    mrhiAccess copy = Access(samples, mrhi_accessCopyDestination);
    def = Pass((mrhiColorTarget){0}, &copy, 1);
    def.neverCull = true;
    CHECK(Add(def) == mrhi_success, "samples only copied to");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_errorInvalid,
          "several samples not as a target");
    Drop();
}

// Accesses a pass may not make. The check that refused the
// device's latest refusal, draining its diagnostics; 0 when there
// is none.
static mrhiDiagnosticCode Refusal(void)
{
    mrhiDiagnosticCode code = 0;
    mrhiDiagnostic record;
    while (mrhiDevice_TakeDiagnostic(s_device, &record) == mrhi_success)
    {
        code = record.code;
    }
    return code;
}

static void TestAccesses(void)
{
    mrhiTextureId copyOnly = MakeTexture(mrhi_textureCopyDestination);
    Begin();
    mrhiResourceId texture = DeclareMips(4);
    mrhiResourceId buffer = DeclareBuffer();
    mrhiResourceId imported;
    CHECK(mrhiImportTexture(s_device, copyOnly, &imported) == mrhi_success, "imported");
    mrhiAccess init[2] = {Access(texture, mrhi_accessCopyDestination),
                          Access(buffer, mrhi_accessCopyDestination)};
    mrhiPassDef def = Pass((mrhiColorTarget){0}, init, 2);
    def.passClass = mrhi_passTransfer;
    CHECK(Add(def) == mrhi_success, "an upload");
    mrhiAccess one;
    one = Access(buffer, mrhi_accessSampled);
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticPassUses,
          "a sampled buffer");
    one = Access(texture, mrhi_accessUniform);
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid, "a uniform texture");
    one = Access(texture, 10);
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid, "an unknown kind");
    one = Access(imported, mrhi_accessSampled);
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid,
          "a use the imported texture was not made with");
    one = Access(texture, mrhi_accessSampled);
    one.range.aspect = mrhi_aspectDepthOnly;
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid, "no depth to use");
    one.range.aspect = 3;
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid, "an unknown aspect");
    one = Access(texture, mrhi_accessSampled);
    one.range.baseMip = 4;
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid, "past the mips");
    one = Access(texture, mrhi_accessSampled);
    one.range.baseLayer = 1;
    CHECK(Add(Pass((mrhiColorTarget){0}, &one, 1)) == mrhi_errorInvalid, "past the layers");
    one = Access(buffer, mrhi_accessStorageRead);
    def = Pass((mrhiColorTarget){0}, &one, 1);
    def.passClass = mrhi_passTransfer;
    CHECK(Add(def) == mrhi_errorInvalid, "storage in a transfer pass");
    one = Access(buffer, mrhi_accessVertex);
    def = Pass((mrhiColorTarget){0}, &one, 1);
    def.passClass = mrhi_passAsyncCompute;
    CHECK(Add(def) == mrhi_errorInvalid, "vertices in async compute");
    one = Access(buffer, mrhi_accessIndex);
    def.accesses = &one;
    CHECK(Add(def) == mrhi_errorInvalid, "indices in async compute");
    one = Access(buffer, mrhi_accessIndirect);
    CHECK(Add(def) == mrhi_success, "indirect arguments in async compute");
    one = Access(texture, mrhi_accessCopySource);
    def.passClass = mrhi_passTransfer;
    CHECK(Add(def) == mrhi_success, "a copy from, in a transfer pass");
    def = Pass(Color(texture, mrhi_loadClear), nullptr, 0);
    def.passClass = mrhi_passAsyncCompute;
    CHECK(Add(def) == mrhi_errorInvalid, "a target in async compute");
    def = mrhiDefaultPassDef();
    def.passClass = 3;
    CHECK(Add(def) == mrhi_errorInvalid && Refusal() == mrhi_diagnosticPassClass,
          "an unknown class");
    def = mrhiDefaultPassDef();
    def.accessCount = 1;
    CHECK(Add(def) == mrhi_errorInvalid && Refusal() == mrhi_diagnosticNullArgument,
          "a count without accesses");
    def = mrhiDefaultPassDef();
    def.colorTargetCount = MRHI_MAX_COLOR_TARGETS + 1;
    CHECK(Add(def) == mrhi_errorLimit && Refusal() == mrhi_diagnosticPassColorTargets,
          "too many color targets");
    def.cookie = 0;
    CHECK(Add(def) == mrhi_errorInvalid, "no cookie");
    Drop();
    CHECK(mrhiDestroyTexture(s_device, copyOnly) == mrhi_success, "done");
}

// WebGPU's usage scopes, and writes before reads.
static void TestScopes(void)
{
    Begin();
    mrhiResourceId texture = DeclareMips(4);
    mrhiResourceId buffer = DeclareBuffer();
    mrhiAccess first = Access(texture, mrhi_accessSampled);
    CHECK(Add(Pass((mrhiColorTarget){0}, &first, 1)) == mrhi_errorInvalid,
          "read before any pass wrote it");
    mrhiAccess unwritten = Access(buffer, mrhi_accessCopySource);
    CHECK(Add(Pass((mrhiColorTarget){0}, &unwritten, 1)) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticPassUses,
          "a buffer read before any pass wrote it");
    mrhiAccess init[2] = {Access(texture, mrhi_accessStorageWrite),
                          Access(buffer, mrhi_accessStorageWrite)};
    CHECK(Add(Pass((mrhiColorTarget){0}, init, 2)) == mrhi_success, "written");
    mrhiAccess pair[2] = {Access(texture, mrhi_accessSampled),
                          Access(texture, mrhi_accessStorageWrite)};
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_errorInvalid,
          "the same mips read and written");
    pair[0].range = (mrhiTextureRange){.baseMip = 0, .mipCount = 1, .layerCount = 1};
    pair[1].range = (mrhiTextureRange){.baseMip = 1, .mipCount = 1, .layerCount = 1};
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_success,
          "one mip read, the next written");
    pair[1].range.baseMip = 0;
    pair[1].range.mipCount = 2;
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_errorInvalid, "overlapping mips");
    pair[0] = Access(texture, mrhi_accessSampled);
    pair[1] = Access(texture, mrhi_accessCopySource);
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_errorInvalid,
          "a texture's part in two read states");
    pair[1] = Access(texture, mrhi_accessSampled);
    pair[1].range = (mrhiTextureRange){.baseMip = 1, .mipCount = 2, .layerCount = 1};
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_success, "two reads in one state");
    mrhiAccess reads[2] = {Access(buffer, mrhi_accessStorageRead),
                           Access(buffer, mrhi_accessIndirect)};
    CHECK(Add(Pass((mrhiColorTarget){0}, reads, 2)) == mrhi_success, "a buffer read two ways");
    mrhiResourceId layers = DeclareSized(mrhi_texture2dArray, 16, 16, 4, 1);
    mrhiAccess fill = Access(layers, mrhi_accessStorageWrite);
    CHECK(Add(Pass((mrhiColorTarget){0}, &fill, 1)) == mrhi_success, "layers written");
    pair[0] = Access(layers, mrhi_accessSampled);
    pair[0].range = (mrhiTextureRange){.mipCount = 1, .baseLayer = 0, .layerCount = 1};
    pair[1] = Access(layers, mrhi_accessStorageWrite);
    pair[1].range = (mrhiTextureRange){.mipCount = 1, .baseLayer = 1, .layerCount = 3};
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_success,
          "one layer read, the others written");
    pair[1].range.baseLayer = 0;
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_errorInvalid, "overlapping layers");
    mrhiResourceId both = Declare(mrhi_formatDepthStencil, 64, 1);
    mrhiAccess fillBoth = Access(both, mrhi_accessCopyDestination);
    CHECK(Add(Pass((mrhiColorTarget){0}, &fillBoth, 1)) == mrhi_success, "depth and stencil");
    pair[0] = Access(both, mrhi_accessSampled);
    pair[0].range.aspect = mrhi_aspectDepthOnly;
    pair[1] = Access(both, mrhi_accessCopyDestination);
    pair[1].range.aspect = mrhi_aspectStencilOnly;
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_success, "depth read, stencil written");
    pair[1].range.aspect = mrhi_aspectAll;
    CHECK(Add(Pass((mrhiColorTarget){0}, pair, 2)) == mrhi_errorInvalid,
          "depth read, both written");
    mrhiAccess buffers[2] = {Access(buffer, mrhi_accessStorageRead),
                             Access(buffer, mrhi_accessStorageReadWrite)};
    CHECK(Add(Pass((mrhiColorTarget){0}, buffers, 2)) == mrhi_errorInvalid,
          "a buffer read and written");
    mrhiAccess target = Access(texture, mrhi_accessSampled);
    target.range = (mrhiTextureRange){.baseMip = 1, .mipCount = 1, .layerCount = 1};
    CHECK(Add(Pass(Color(texture, mrhi_loadKeep), &target, 1)) == mrhi_success,
          "mip 1 sampled while mip 0 is the target");
    target.range.baseMip = 0;
    CHECK(Add(Pass(Color(texture, mrhi_loadKeep), &target, 1)) == mrhi_errorInvalid,
          "the target's mip sampled");
    mrhiResourceId fresh = Declare(mrhi_formatRgba8Unorm, 64, 1);
    CHECK(Add(Pass(Color(fresh, mrhi_loadKeep), nullptr, 0)) == mrhi_errorInvalid,
          "kept contents never written");
    CHECK(Add(Pass(Color(fresh, mrhi_loadDiscard), nullptr, 0)) == mrhi_success, "discarded");
    CHECK(Add(Pass(Color(fresh, mrhi_loadKeep), nullptr, 0)) == mrhi_success, "then kept");
    Drop();
}

// Targets that agree, resolve and test depth, and those that do not.
static void TestTargets(void)
{
    mrhiTextureId sampledOnly = MakeTexture(mrhi_textureSampled);
    Begin();
    mrhiResourceId color = Declare(mrhi_formatRgba8Unorm, 64, 1);
    mrhiResourceId small = Declare(mrhi_formatRgba8Unorm, 32, 1);
    mrhiResourceId msaa = Declare(mrhi_formatRgba8Unorm, 64, 4);
    mrhiResourceId msaaDepth = Declare(mrhi_formatDepthStencil, 64, 4);
    mrhiResourceId depth = Declare(mrhi_formatDepth32Float, 64, 1);
    mrhiResourceId other = Declare(mrhi_formatBgra8Unorm, 64, 1);
    mrhiResourceId buffer = DeclareBuffer();
    mrhiResourceId imported;
    CHECK(mrhiImportTexture(s_device, sampledOnly, &imported) == mrhi_success, "imported");
    mrhiPassDef def = Pass(Color(msaa, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].resolve = color;
    def.depthTarget = (mrhiDepthTarget){
        .resource = msaaDepth,
        .depthLoad = mrhi_loadClear,
        .stencilLoad = mrhi_loadClear,
        .clearDepth = 1.0f,
    };
    CHECK(Add(def) == mrhi_success, "4 samples resolved, with depth and stencil");
    def = Pass(Color(color, mrhi_loadKeep), nullptr, 0);
    def.colorTargets[1] = Color(small, mrhi_loadClear);
    def.colorTargetCount = 2;
    CHECK(Add(def) == mrhi_errorInvalid, "targets of two sizes");
    def.colorTargets[1] = Color(msaa, mrhi_loadClear);
    CHECK(Add(def) == mrhi_errorInvalid, "targets of two sample counts");
    mrhiResourceId single = Declare(mrhi_formatRgba8Unorm, 64, 1);
    def = Pass(Color(single, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].resolve = color;
    CHECK(Add(def) == mrhi_errorInvalid, "a single sample resolved");
    mrhiResourceId msaaOther = Declare(mrhi_formatRgba8Unorm, 64, 4);
    def = Pass(Color(msaa, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].resolve = msaaOther;
    CHECK(Add(def) == mrhi_errorInvalid, "resolved into samples");
    mrhiResourceId wide = DeclareSized(mrhi_texture2d, 64, 32, 1, 1);
    mrhiResourceId tall = DeclareSized(mrhi_texture2d, 32, 64, 1, 1);
    def = Pass(Color(msaa, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].resolve = wide;
    CHECK(Add(def) == mrhi_errorInvalid, "resolved into another height");
    def.colorTargets[0].resolve = tall;
    CHECK(Add(def) == mrhi_errorInvalid, "resolved into another width");
    def = Pass(Color(color, mrhi_loadClear), nullptr, 0);
    def.colorTargets[1] = Color(wide, mrhi_loadClear);
    def.colorTargetCount = 2;
    CHECK(Add(def) == mrhi_errorInvalid, "targets of two heights");
    def.colorTargets[1] = Color(tall, mrhi_loadClear);
    CHECK(Add(def) == mrhi_errorInvalid, "targets of two widths");
    mrhiResourceId volume = DeclareSized(mrhi_texture3d, 64, 64, 8, 2);
    mrhiColorTarget slice = Color(volume, mrhi_loadClear);
    slice.layer = 7;
    CHECK(Add(Pass(slice, nullptr, 0)) == mrhi_success, "a 3D texture's last slice");
    slice.mip = 1;
    slice.layer = 4;
    CHECK(Add(Pass(slice, nullptr, 0)) == mrhi_errorInvalid, "a slice past the depth at mip 1");
    slice.layer = 3;
    CHECK(Add(Pass(slice, nullptr, 0)) == mrhi_success, "the last slice at mip 1");
    mrhiAccess volumeRead = Access(volume, mrhi_accessSampled);
    volumeRead.range.baseLayer = 1;
    CHECK(Add(Pass((mrhiColorTarget){0}, &volumeRead, 1)) == mrhi_errorInvalid,
          "a 3D texture has one layer to read");
    volumeRead.range.baseLayer = 0;
    CHECK(Add(Pass((mrhiColorTarget){0}, &volumeRead, 1)) == mrhi_success, "the whole volume");
    def.colorTargets[0].resolve = other;
    CHECK(Add(def) == mrhi_errorInvalid, "resolved into another format");
    def.colorTargets[0].resolve = small;
    CHECK(Add(def) == mrhi_errorInvalid, "resolved into another size");
    def.colorTargets[0].resolve = color;
    def.colorTargets[0].resolveMip = 1;
    CHECK(Add(def) == mrhi_errorInvalid, "resolved into a mip it lacks");
    CHECK(Add(Pass(Color(buffer, mrhi_loadClear), nullptr, 0)) == mrhi_errorInvalid,
          "a buffer as a target");
    CHECK(Add(Pass(Color(depth, mrhi_loadClear), nullptr, 0)) == mrhi_errorInvalid,
          "depth as a color target");
    CHECK(Add(Pass(Color(imported, mrhi_loadClear), nullptr, 0)) == mrhi_errorInvalid,
          "an imported texture made without rendering");
    mrhiColorTarget bad = Color(color, mrhi_loadClear);
    bad.mip = 1;
    CHECK(Add(Pass(bad, nullptr, 0)) == mrhi_errorInvalid, "a mip it lacks");
    bad = Color(color, mrhi_loadClear);
    bad.layer = 1;
    CHECK(Add(Pass(bad, nullptr, 0)) == mrhi_errorInvalid, "a layer it lacks");
    bad = Color(color, 3);
    CHECK(Add(Pass(bad, nullptr, 0)) == mrhi_errorInvalid, "an unknown load");
    bad = Color(color, mrhi_loadClear);
    bad.store = 2;
    CHECK(Add(Pass(bad, nullptr, 0)) == mrhi_errorInvalid, "an unknown store");
    def = mrhiDefaultPassDef();
    def.depthTarget = (mrhiDepthTarget){.resource = color, .depthLoad = mrhi_loadClear};
    CHECK(Add(def) == mrhi_errorInvalid, "a color format as depth");
    def.depthTarget = (mrhiDepthTarget){.resource = depth, .depthLoad = mrhi_loadClear};
    CHECK(Add(def) == mrhi_success, "depth only");
    def.depthTarget.depthLoad = 3;
    CHECK(Add(def) == mrhi_errorInvalid, "an unknown depth load");
    def.depthTarget.depthLoad = mrhi_loadClear;
    def.depthTarget.depthStore = 2;
    CHECK(Add(def) == mrhi_errorInvalid, "an unknown depth store");
    def.depthTarget.depthStore = mrhi_storeKeep;
    def.depthTarget.stencilLoad = 3;
    CHECK(Add(def) == mrhi_errorInvalid, "an unknown stencil load");
    def.depthTarget.stencilLoad = mrhi_loadClear;
    def.depthTarget.stencilStore = 2;
    CHECK(Add(def) == mrhi_errorInvalid, "an unknown stencil store");
    def = mrhiDefaultPassDef();
    def.depthTarget =
        (mrhiDepthTarget){.resource = depth, .depthStore = mrhi_storeDiscard, .readOnly = true};
    CHECK(Add(def) == mrhi_errorInvalid, "read-only depth that discards");
    mrhiResourceId both = Declare(mrhi_formatDepthStencil, 64, 1);
    def.depthTarget = (mrhiDepthTarget){
        .resource = both, .depthLoad = mrhi_loadClear, .stencilLoad = mrhi_loadKeep};
    CHECK(Add(def) == mrhi_errorInvalid, "stencil kept before any pass wrote it");
    def.depthTarget.stencilLoad = mrhi_loadClear;
    CHECK(Add(def) == mrhi_success, "depth and stencil cleared");
    def.depthTarget =
        (mrhiDepthTarget){.resource = both, .stencilLoad = mrhi_loadClear, .readOnly = true};
    CHECK(Add(def) == mrhi_errorInvalid, "read-only stencil that clears");
    def.depthTarget.stencilLoad = mrhi_loadKeep;
    def.depthTarget.stencilStore = mrhi_storeDiscard;
    CHECK(Add(def) == mrhi_errorInvalid, "read-only stencil that discards");
    def.depthTarget.stencilStore = mrhi_storeKeep;
    CHECK(Add(def) == mrhi_success, "read-only depth and stencil");
    def.passClass = mrhi_passAsyncCompute;
    CHECK(Add(def) == mrhi_errorInvalid, "a depth target in async compute");
    def = mrhiDefaultPassDef();
    def.depthTarget = (mrhiDepthTarget){.resource = depth, .readOnly = true};
    mrhiAccess sampled = Access(depth, mrhi_accessSampled);
    def.accesses = &sampled;
    def.accessCount = 1;
    CHECK(Add(def) == mrhi_success, "read-only depth, also sampled");
    def.depthTarget.readOnly = false;
    CHECK(Add(def) == mrhi_errorInvalid, "written depth, also sampled");
    def.depthTarget =
        (mrhiDepthTarget){.resource = depth, .depthLoad = mrhi_loadClear, .readOnly = true};
    def.accessCount = 0;
    CHECK(Add(def) == mrhi_errorInvalid, "read-only depth that clears");
    def = Pass(Color(color, mrhi_loadClear), nullptr, 0);
    def.depthTarget = (mrhiDepthTarget){.resource = depth, .depthLoad = mrhi_loadClear, .mip = 1};
    CHECK(Add(def) == mrhi_errorInvalid, "depth at a mip it lacks");
    Drop();
    CHECK(mrhiDestroyTexture(s_device, sampledOnly) == mrhi_success, "done");
}

// A declared rgba8 texture of the samples, viewable in its sRGB twin
// when asked.
static mrhiResourceId DeclareTwin(uint32_t samples, bool twin)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 64;
    def.height = 64;
    def.sampleCount = samples;
    def.viewFormats[0] = twin ? mrhi_formatRgba8UnormSrgb : mrhi_formatNone;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared");
    return resource;
}

// Color targets in a view format: the texture's own or one it lists,
// a resolve texture listing it too.
static void TestViewFormats(void)
{
    Begin();
    mrhiResourceId twin = DeclareTwin(1, true);
    mrhiResourceId plain = DeclareTwin(1, false);
    mrhiResourceId samples = DeclareTwin(4, true);
    mrhiColorTarget target = Color(twin, mrhi_loadClear);
    target.viewFormat = mrhi_formatRgba8UnormSrgb;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_success, "its listed twin");
    target.viewFormat = mrhi_formatRgba8Unorm;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_success, "its own format named");
    target.viewFormat = mrhi_formatBgra8Unorm;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_errorInvalid, "a format it does not list");
    target = Color(plain, mrhi_loadClear);
    target.viewFormat = mrhi_formatRgba8UnormSrgb;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_errorInvalid, "a twin it does not list");
    target = Color(samples, mrhi_loadClear);
    target.viewFormat = mrhi_formatRgba8UnormSrgb;
    target.resolve = twin;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_success, "resolved into a texture listing it");
    target.resolve = plain;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_errorInvalid,
          "resolved into a texture not listing it");
    target.viewFormat = mrhi_formatNone;
    CHECK(Add(Pass(target, nullptr, 0)) == mrhi_success, "both in their own format");
    Drop();
}

// Kinds are int32_t, so a check against a kind's last value alone would
// let a negative one through: each kind of a pass def refuses -1 and
// INT32_MIN, each case from a def that is added.
static void TestNegativeKinds(void)
{
    Begin();
    mrhiResourceId texture = DeclareMips(4);
    mrhiResourceId color = Declare(mrhi_formatRgba8Unorm, 64, 1);
    mrhiResourceId both = Declare(mrhi_formatDepthStencil, 64, 1);
    mrhiAccess upload = Access(texture, mrhi_accessCopyDestination);
    mrhiPassDef access = Pass((mrhiColorTarget){0}, &upload, 1);
    access.passClass = mrhi_passTransfer;
    mrhiPassDef target = Pass(Color(color, mrhi_loadClear), nullptr, 0);
    // A graphics pass reading what the upload wrote: every class allows
    // its access, so only the kind can refuse it.
    mrhiAccess read = Access(texture, mrhi_accessSampled);
    mrhiPassDef reading = Pass(Color(color, mrhi_loadClear), &read, 1);
    mrhiPassDef empty = mrhiDefaultPassDef();
    mrhiPassDef depth = mrhiDefaultPassDef();
    depth.depthTarget = (mrhiDepthTarget){
        .resource = both, .depthLoad = mrhi_loadClear, .stencilLoad = mrhi_loadClear};
    CHECK(Add(access) == mrhi_success && Add(target) == mrhi_success &&
              Add(reading) == mrhi_success && Add(empty) == mrhi_success &&
              Add(depth) == mrhi_success,
          "the valid defs added");
    static const int32_t negative[] = {-1, INT32_MIN};
    for (size_t i = 0; i < sizeof(negative) / sizeof(negative[0]); ++i)
    {
        mrhiPassDef def = empty;
        def.passClass = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative class");
        read.kind = negative[i];
        CHECK(Add(reading) == mrhi_errorInvalid, "a negative access kind");
        read = Access(texture, mrhi_accessSampled);
        upload = Access(texture, mrhi_accessCopyDestination);
        upload.range.aspect = negative[i];
        CHECK(Add(access) == mrhi_errorInvalid, "a negative aspect");
        upload = Access(texture, mrhi_accessCopyDestination);
        def = target;
        def.colorTargets[0].load = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative load");
        def = target;
        def.colorTargets[0].store = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative store");
        def = target;
        def.colorTargets[0].viewFormat = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative view format");
        def = depth;
        def.depthTarget.depthLoad = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative depth load");
        def = depth;
        def.depthTarget.depthStore = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative depth store");
        def = depth;
        def.depthTarget.stencilLoad = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative stencil load");
        def = depth;
        def.depthTarget.stencilStore = negative[i];
        CHECK(Add(def) == mrhi_errorInvalid, "a negative stencil store");
    }
    Drop();
}

// Imported buffers and textures: their usages, written as outputs and
// read without a pass writing them first.
static void TestImports(void)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource;
    mrhiBufferId storage;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &storage) == mrhi_success, "a storage buffer");
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId copyOnly;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &copyOnly) == mrhi_success, "a copy buffer");
    mrhiTextureId sampled = MakeTexture(mrhi_textureSampled);
    Begin();
    mrhiResourceId a, b, t;
    CHECK(mrhiImportBuffer(s_device, storage, &a) == mrhi_success, "imported");
    CHECK(mrhiImportBuffer(s_device, copyOnly, &b) == mrhi_success, "imported");
    CHECK(mrhiImportTexture(s_device, sampled, &t) == mrhi_success, "imported");
    mrhiAccess one = Access(a, mrhi_accessStorageRead);
    mrhiPassId read, written, copied;
    mrhiPassDef def = Pass((mrhiColorTarget){0}, &one, 1);
    CHECK(mrhiAddPass(s_device, &def, &read) == mrhi_success, "read with nothing written");
    one = Access(a, mrhi_accessStorageWrite);
    CHECK(mrhiAddPass(s_device, &def, &written) == mrhi_success, "written as storage");
    one = Access(a, mrhi_accessCopySource);
    def.passClass = mrhi_passTransfer;
    CHECK(Add(def) == mrhi_success, "copied from");
    one = Access(b, mrhi_accessCopyDestination);
    CHECK(mrhiAddPass(s_device, &def, &copied) == mrhi_success, "copied to");
    one = Access(b, mrhi_accessCopySource);
    CHECK(Add(def) == mrhi_errorInvalid, "a copy from a buffer made only to copy to");
    def.passClass = mrhi_passGraphics;
    one = Access(b, mrhi_accessStorageRead);
    CHECK(Add(def) == mrhi_errorInvalid, "storage of a buffer made without it");
    one = Access(a, mrhi_accessUniform);
    CHECK(Add(def) == mrhi_errorInvalid, "uniforms of a buffer made without them");
    one = Access(t, mrhi_accessSampled);
    CHECK(Add(def) == mrhi_success, "a sampled texture sampled");
    one = Access(t, mrhi_accessStorageRead);
    CHECK(Add(def) == mrhi_errorInvalid, "storage of a texture made without it");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(!Kept(read), "a read only pass is culled");
    CHECK(Kept(written) && Kept(copied), "writes to imported buffers are kept");
    Drop();
    CHECK(mrhiDestroyBuffer(s_device, storage) == mrhi_success, "done");
    CHECK(mrhiDestroyBuffer(s_device, copyOnly) == mrhi_success, "done");
    CHECK(mrhiDestroyTexture(s_device, sampled) == mrhi_success, "done");
}

// Ids of other frames, destroyed imports and the frame's budgets.
static void TestStaleAndLimits(void)
{
    mrhiTextureId gone = MakeTexture(mrhi_textureSampled | mrhi_textureRenderTarget);
    Begin();
    mrhiResourceId old = Declare(mrhi_formatRgba8Unorm, 64, 1);
    Declare(mrhi_formatRgba8Unorm, 64, 1);
    Drop();
    Begin();
    mrhiResourceId current = Declare(mrhi_formatRgba8Unorm, 64, 1);
    CHECK(current.index1 == old.index1, "the same slot as the last frame's");
    CHECK(Add(Pass(Color(old, mrhi_loadClear), nullptr, 0)) == mrhi_errorStale,
          "a resource of the last frame");
    mrhiResourceId past = {current.index1 + 1, current.generation};
    CHECK(Add(Pass(Color(past, mrhi_loadClear), nullptr, 0)) == mrhi_errorStale,
          "past the frame's resources");
    mrhiAccess access = Access(old, mrhi_accessSampled);
    CHECK(Add(Pass((mrhiColorTarget){0}, &access, 1)) == mrhi_errorStale, "accessed");
    mrhiResourceId imported;
    CHECK(mrhiImportTexture(s_device, gone, &imported) == mrhi_success, "imported");
    CHECK(mrhiDestroyTexture(s_device, gone) == mrhi_success, "then destroyed");
    CHECK(Add(Pass(Color(imported, mrhi_loadClear), nullptr, 0)) == mrhi_errorStale,
          "an import destroyed since");
    mrhiColorTarget resolveGone = Color(Declare(mrhi_formatRgba8Unorm, 64, 4), mrhi_loadClear);
    resolveGone.resolve = old;
    CHECK(Add(Pass(resolveGone, nullptr, 0)) == mrhi_errorStale, "a resolve of the last frame");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.depthTarget.resource = old;
    CHECK(Add(def) == mrhi_errorStale, "depth of the last frame");
    Drop();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.budgets.framePasses = 1;
    deviceDef.budgets.frameAccesses = 4;
    mrhiInstance* instance = s_instance;
    mrhiDevice* previous = s_device;
    s_device = OpenWith(deviceDef, true);
    Begin();
    mrhiResourceId target = Declare(mrhi_formatRgba8Unorm, 64, 1);
    mrhiAccess many[3] = {Access(target, mrhi_accessCopyDestination)};
    CHECK(Add(Pass(Color(target, mrhi_loadClear), many, 3)) == mrhi_errorCapacity,
          "more than the frame's accesses");
    // Three accesses and the depth target's room: the frame's four exactly.
    mrhiAccess three[3] = {
        Access(Declare(mrhi_formatRgba8Unorm, 64, 1), mrhi_accessCopyDestination),
        Access(Declare(mrhi_formatRgba8Unorm, 64, 1), mrhi_accessCopyDestination),
        Access(Declare(mrhi_formatRgba8Unorm, 64, 1), mrhi_accessCopyDestination),
    };
    mrhiPassDef exact = mrhiDefaultPassDef();
    exact.passClass = mrhi_passTransfer;
    exact.accesses = three;
    exact.accessCount = 3;
    CHECK(Add(exact) == mrhi_success, "one pass of the frame's accesses exactly");
    CHECK(Add(Pass(Color(target, mrhi_loadKeep), nullptr, 0)) == mrhi_errorCapacity,
          "the frame's passes");
    Drop();
    Close(s_device);
    s_device = previous;
    s_instance = instance;
}

// Multiview passes: unsupported without the feature or past its limit;
// refused without targets, on targets without a layer per view from
// theirs, and on 3D ones; a target's use spans a layer per view, so
// that sampling the second layer beside it is refused.
static void TestViews(void)
{
    Begin();
    mrhiPassDef def =
        Pass(Color(DeclareSized(mrhi_texture2dArray, 16, 16, 2, 1), mrhi_loadClear), nullptr, 0);
    def.viewCount = 2;
    CHECK(Add(def) == mrhi_errorUnsupported, "no multiview");
    Drop();
    mrhiInstance* instance = s_instance;
    mrhiDevice* previous = s_device;
    // Views in the limit, but not the feature.
    s_adapter.limits.multiviewViews = 2;
    mrhiDeviceDef limited = mrhiDefaultDeviceDef();
    limited.limits.multiviewViews = 2;
    s_device = OpenWith(limited, true);
    Begin();
    def.colorTargets[0].resource = DeclareSized(mrhi_texture2dArray, 16, 16, 2, 1);
    CHECK(Add(def) == mrhi_errorUnsupported, "views without the feature");
    Drop();
    Close(s_device);
    s_adapter.features.multiview = true;
    s_adapter.limits.multiviewViews = 4;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.multiview = true;
    deviceDef.limits.multiviewViews = 2;
    s_device = OpenWith(deviceDef, true);
    Begin();
    mrhiResourceId two = DeclareSized(mrhi_texture2dArray, 16, 16, 2, 1);
    mrhiResourceId three = DeclareSized(mrhi_texture2dArray, 16, 16, 3, 1);
    mrhiResourceId one = Declare(mrhi_formatRgba8Unorm, 16, 1);
    mrhiResourceId volume = DeclareSized(mrhi_texture3d, 16, 16, 2, 1);
    def = Pass(Color(two, mrhi_loadClear), nullptr, 0);
    def.viewCount = 3;
    CHECK(Add(def) == mrhi_errorCapacity, "past the device's limit, within the adapter's");
    def.viewCount = 5;
    CHECK(Add(def) == mrhi_errorUnsupported, "past the adapter's");
    def.viewCount = 2;
    CHECK(Add(def) == mrhi_success, "two views on two layers");
    def.colorTargets[0].layer = 1;
    CHECK(Add(def) == mrhi_errorInvalid, "a view past the layers");
    def.colorTargets[0].resource = three;
    CHECK(Add(def) == mrhi_success, "two views from the second of three layers");
    def.colorTargets[0] = Color(one, mrhi_loadClear);
    CHECK(Add(def) == mrhi_errorInvalid, "one layer");
    def.colorTargets[0] = Color(volume, mrhi_loadClear);
    CHECK(Add(def) == mrhi_errorInvalid, "a 3D target");
    mrhiAccess second = Access(two, mrhi_accessSampled);
    second.range.baseLayer = 1;
    second.range.layerCount = 1;
    def = Pass(Color(two, mrhi_loadClear), &second, 1);
    CHECK(Add(def) == mrhi_success, "the second layer sampled beside one view");
    def.viewCount = 2;
    CHECK(Add(def) == mrhi_errorInvalid, "but not beside two");
    mrhiAccess copy = Access(DeclareBuffer(), mrhi_accessCopyDestination);
    def = Pass(Color((mrhiResourceId){0}, mrhi_loadClear), &copy, 1);
    def.viewCount = 2;
    CHECK(Add(def) == mrhi_errorInvalid, "no targets");
    Drop();
    Close(s_device);
    s_device = previous;
    s_instance = instance;
}

static void TestRefusals(void)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    mrhiPassId pass;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorState, "no frame");
    CHECK(mrhiAddPass(nullptr, &def, &pass) == mrhi_errorInvalid, "no device");
    CHECK(mrhiAddPass(s_device, nullptr, &pass) == mrhi_errorInvalid, "no def");
    CHECK(mrhiAddPass(s_device, &def, nullptr) == mrhi_errorInvalid, "no out");
    Begin();
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorUnsupported,
          "an unknown critical extension");
    def = mrhiDefaultPassDef();
    def.label = "shadows";
    def.labelLength = 7;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success && pass.index1 != 0, "labelled");
    Drop();
}

// A cleared color target's clear value converts to its format's texels
// as WebGPU converts a clearValue, which is finite: an integer format's
// components, all four, may carry fractions, which are dropped, but
// their integer parts must fit its 32-bit type. A target loaded or
// discarded takes any clear value.
// Null color targets: locations without a target, as WebGPU's null
// attachments, beside one target or a depth target.
static void TestNullTargets(void)
{
    Begin();
    mrhiResourceId color = Declare(mrhi_formatRgba8Unorm, 64, 1);
    mrhiResourceId msaa = Declare(mrhi_formatRgba8Unorm, 64, 4);
    mrhiResourceId depth = Declare(mrhi_formatDepth32Float, 64, 1);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[1] = Color(color, mrhi_loadClear);
    def.colorTargetCount = 2;
    CHECK(Add(def) == mrhi_success, "a null target before one");
    def.colorTargets[1] = (mrhiColorTarget){0};
    CHECK(Add(def) == mrhi_errorInvalid && Refusal() == mrhi_diagnosticPassUses,
          "only null targets");
    def.depthTarget = (mrhiDepthTarget){.resource = depth, .depthLoad = mrhi_loadClear};
    CHECK(Add(def) == mrhi_success, "only null targets, with depth");
    def = mrhiDefaultPassDef();
    def.colorTargets[0] = Color(msaa, mrhi_loadClear);
    def.colorTargets[0].resolve = color;
    def.colorTargetCount = 2;
    CHECK(Add(def) == mrhi_success, "a resolved target before a null one");
    def.colorTargets[1].resolve = Declare(mrhi_formatRgba8Unorm, 64, 1);
    CHECK(Add(def) == mrhi_errorInvalid && Refusal() == mrhi_diagnosticPassUses,
          "a null target resolved");
    Drop();
}

static void TestClearValues(void)
{
    Begin();
    mrhiResourceId u = Declare(mrhi_formatR32Uint, 64, 1);
    mrhiResourceId s = Declare(mrhi_formatR32Sint, 64, 1);
    mrhiResourceId f = Declare(mrhi_formatRgba16Float, 64, 1);
    mrhiPassDef def = Pass(Color(u, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].clear = (mrhiColor){0.0f, 4294967040.0f, 7.5f, -0.5f};
    CHECK(Add(def) == mrhi_success, "unsigned, to the largest float below 2^32, and fractions");
    def.colorTargets[0].clear.alpha = -1.0f;
    CHECK(Add(def) == mrhi_errorInvalid && Refusal() == mrhi_diagnosticPassUses,
          "negative, for an unsigned format");
    def.colorTargets[0].clear.alpha = 4294967296.0f;
    CHECK(Add(def) == mrhi_errorInvalid, "past 32 bits");
    def.colorTargets[0].clear.alpha = NAN;
    CHECK(Add(def) == mrhi_errorInvalid, "not a number");
    def.colorTargets[0].load = mrhi_loadKeep;
    CHECK(Add(def) == mrhi_success, "not read when loaded");
    def.colorTargets[0].load = mrhi_loadDiscard;
    CHECK(Add(def) == mrhi_success, "nor when discarded");
    def = Pass(Color(s, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].clear = (mrhiColor){-2147483648.0f, 2147483520.0f, -1.5f, 0.0f};
    CHECK(Add(def) == mrhi_success, "signed, from -2^31 to the largest float below 2^31");
    def.colorTargets[0].clear.green = 2147483648.0f;
    CHECK(Add(def) == mrhi_errorInvalid, "2^31, for a signed format");
    def.colorTargets[0].clear.green = -INFINITY;
    CHECK(Add(def) == mrhi_errorInvalid, "infinite");
    def = Pass(Color(f, mrhi_loadClear), nullptr, 0);
    def.colorTargets[0].clear = (mrhiColor){-70000.0f, 1e30f, 0.5f, 1.0f};
    CHECK(Add(def) == mrhi_success, "any finite value, for a float format");
    def.colorTargets[0].clear.blue = INFINITY;
    CHECK(Add(def) == mrhi_errorInvalid, "but not an infinite one");
    def.colorTargets[0].clear.blue = NAN;
    CHECK(Add(def) == mrhi_errorInvalid, "nor not a number");
    Drop();
}

int main(void)
{
    ResetAdapter();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.budgets.diagnostics = 8;
    s_device = OpenWith(deviceDef, true);
    mrhiInstance* instance = s_instance;
    TestCulling();
    TestDerivedUsages();
    TestAccesses();
    TestScopes();
    TestTargets();
    TestViewFormats();
    TestNegativeKinds();
    TestImports();
    TestStaleAndLimits();
    TestViews();
    TestClearValues();
    TestNullTargets();
    TestRefusals();
    s_instance = instance;
    CHECK(mrhiDevice_GetMisuse(s_device) > 30, "the refusals were counted");
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
