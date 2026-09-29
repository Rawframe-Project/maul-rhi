// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Textures on a test driver device: shapes per kind, mips, samples and
// usages against the format's capabilities, view formats, the device's
// limits and grants.

#include "test_device_setup.h"

#include "maul-rhi/resources.h"

static mrhiDevice* s_device;

static mrhiTextureDef Def(mrhiTextureKind kind, mrhiFormat format, uint32_t width, uint32_t height,
                          uint32_t layers, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = width;
    def.height = height;
    def.depthOrLayers = layers;
    def.usage = usage;
    return def;
}

static mrhiResult Make(mrhiTextureDef def)
{
    mrhiTextureId texture = {0};
    mrhiResult status = mrhiCreateTexture(s_device, &def, &texture);
    if (status == mrhi_success)
    {
        CHECK(texture.index1 != 0, "an id");
        CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "destroyed");
    }
    return status;
}

static const mrhiTextureUsage kSampled = mrhi_textureSampled;
static const mrhiTextureUsage kTarget = mrhi_textureRenderTarget;

static void TestKinds(void)
{
    mrhiTextureDef def = Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 256, 256, 1, 0);
    def.usage = kSampled | kTarget;
    def.mipLevels = 9;
    CHECK(Make(def) == mrhi_success, "2D with a full chain");
    def.mipLevels = 10;
    CHECK(Make(def) == mrhi_errorInvalid, "one mip too many");
    CHECK(Make(Def(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 64, 64, 10, kSampled)) ==
              mrhi_success,
          "2D array");
    CHECK(Make(Def(mrhi_textureCube, mrhi_formatRgba16Float, 128, 128, 6, kSampled)) ==
              mrhi_success,
          "cube");
    CHECK(Make(Def(mrhi_textureCubeArray, mrhi_formatRgba16Float, 64, 64, 12, kSampled)) ==
              mrhi_success,
          "cube array");
    def = Def(mrhi_texture3d, mrhi_formatR16Float, 32, 16, 64, kSampled);
    def.mipLevels = 7;
    CHECK(Make(def) == mrhi_success, "3D, its chain from its depth");
    CHECK(Make(Def(mrhi_textureCube, mrhi_formatRgba8Unorm, 128, 64, 6, kSampled)) ==
              mrhi_errorInvalid,
          "a cube that is not square");
    CHECK(Make(Def(mrhi_textureCube, mrhi_formatRgba8Unorm, 64, 64, 5, kSampled)) ==
              mrhi_errorInvalid,
          "a cube of 5 layers");
    CHECK(Make(Def(mrhi_textureCubeArray, mrhi_formatRgba8Unorm, 64, 64, 7, kSampled)) ==
              mrhi_errorInvalid,
          "a cube array of 7 layers");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 64, 64, 2, kSampled)) ==
              mrhi_errorInvalid,
          "2D with 2 layers");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 0, 64, 1, kSampled)) == mrhi_errorInvalid,
          "no width");
    CHECK(Make(Def(5, mrhi_formatRgba8Unorm, 64, 64, 1, kSampled)) == mrhi_errorInvalid,
          "an unknown kind");
}

static void TestLimits(void)
{
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 8192, 8192, 1, kSampled)) == mrhi_success,
          "at the 2D limit");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 8193, 1, 1, kSampled)) ==
              mrhi_errorUnsupported,
          "past the 2D limit");
    CHECK(Make(Def(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 4, 4, 257, kSampled)) ==
              mrhi_errorUnsupported,
          "past the layer limit");
    CHECK(Make(Def(mrhi_texture3d, mrhi_formatR8Unorm, 4, 4, 2049, kSampled)) ==
              mrhi_errorUnsupported,
          "past the 3D limit");
}

static void TestSamplesAndUsages(void)
{
    mrhiTextureDef def = Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 64, 64, 1, kTarget);
    def.sampleCount = 4;
    CHECK(Make(def) == mrhi_success, "4 samples");
    def.usage = kTarget | mrhi_textureTransient;
    CHECK(Make(def) == mrhi_success, "a transient multisampled target");
    def.usage = kTarget | mrhi_textureTransient | kSampled;
    CHECK(Make(def) == mrhi_errorInvalid, "transient and sampled");
    def.usage = kTarget;
    def.sampleCount = 2;
    CHECK(Make(def) == mrhi_errorUnsupported, "2 samples, beyond the floor");
    def.sampleCount = 3;
    CHECK(Make(def) == mrhi_errorInvalid, "3 samples");
    def.sampleCount = 4;
    def.mipLevels = 2;
    CHECK(Make(def) == mrhi_errorInvalid, "samples with mips");
    def.mipLevels = 1;
    def.usage = kTarget | mrhi_textureStorage;
    CHECK(Make(def) == mrhi_errorInvalid, "samples with storage");
    def.usage = kSampled;
    CHECK(Make(def) == mrhi_errorInvalid, "samples without a target");
    def = Def(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 64, 64, 2, kTarget);
    def.sampleCount = 4;
    CHECK(Make(def) == mrhi_errorInvalid, "samples on an array");
    def = Def(mrhi_texture2d, mrhi_formatR32Float, 64, 64, 1, kTarget);
    def.sampleCount = 4;
    CHECK(Make(def) == mrhi_errorUnsupported, "32-bit floats take 1 sample");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8UnormSrgb, 64, 64, 1, mrhi_textureStorage)) ==
              mrhi_errorUnsupported,
          "no sRGB storage");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatDepthStencil, 64, 64, 1, kTarget)) == mrhi_success,
          "a depth target");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 64, 64, 1, 0)) == mrhi_errorInvalid,
          "no usage");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 64, 64, 1, 0x40)) == mrhi_errorInvalid,
          "an unknown usage");
}

static void TestFormats(void)
{
    mrhiTextureDef def = Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 64, 64, 1, kSampled);
    def.viewFormats[0] = mrhi_formatRgba8UnormSrgb;
    CHECK(Make(def) == mrhi_success, "viewed as its sRGB twin");
    def.viewFormats[1] = mrhi_formatR8Unorm;
    CHECK(Make(def) == mrhi_errorInvalid, "viewed as another format");
    def = Def(mrhi_texture2d, mrhi_formatR8Unorm, 64, 64, 1, kSampled);
    def.viewFormats[3] = mrhi_formatRgba8UnormSrgb;
    CHECK(Make(def) == mrhi_errorInvalid, "a format without a twin");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatNone, 64, 64, 1, kSampled)) == mrhi_errorInvalid,
          "no format");
    CHECK(Make(Def(mrhi_texture2d, 999, 64, 64, 1, kSampled)) == mrhi_errorInvalid,
          "an unlisted format");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatBc7RgbaUnorm, 64, 64, 1, kSampled)) ==
              mrhi_errorUnsupported,
          "BC without the device's feature");
}

static void TestCompressedWithTheFeature(void)
{
    s_adapter.features.textureCompressionBc = true;
    s_adapter.features.textureCompressionAstc = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.textureCompressionBc = true;
    mrhiDevice* device = OpenWith(deviceDef, true);
    mrhiDevice* previous = s_device;
    s_device = device;
    mrhiTextureDef def = Def(mrhi_texture2d, mrhi_formatBc7RgbaUnorm, 256, 128, 1, kSampled);
    def.mipLevels = 9;
    def.viewFormats[0] = mrhi_formatBc7RgbaUnormSrgb;
    CHECK(Make(def) == mrhi_success, "BC with the feature, viewed as sRGB");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatBc1RgbaUnorm, 250, 128, 1, kSampled)) ==
              mrhi_errorInvalid,
          "not a whole number of blocks");
    CHECK(Make(Def(mrhi_texture3d, mrhi_formatBc1RgbaUnorm, 64, 64, 4, kSampled)) ==
              mrhi_errorInvalid,
          "a compressed volume");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatBc1RgbaUnorm, 64, 64, 1, kTarget)) ==
              mrhi_errorUnsupported,
          "a compressed target");
    CHECK(Make(Def(mrhi_texture2d, mrhi_formatAstc4x4Unorm, 64, 64, 1, kSampled)) ==
              mrhi_errorUnsupported,
          "ASTC the adapter has but the device did not ask for");
    s_device = previous;
    Close(device);
}

static void TestRefusalsStateAndLimit(void)
{
    mrhiTextureDef def = Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 16, 16, 1, kSampled);
    mrhiTextureId texture;
    def.cookie = 0;
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_errorInvalid, "no cookie");
    def = Def(mrhi_texture2d, mrhi_formatRgba8Unorm, 16, 16, 1, kSampled);
    CHECK(mrhiCreateTexture(s_device, nullptr, &texture) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateTexture(s_device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateTexture(nullptr, &def, &texture) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDestroyTexture(nullptr, texture) == mrhi_errorInvalid, "no device to destroy on");
    CHECK(mrhiDestroyTexture(s_device, (mrhiTextureId){0, 0}) == mrhi_errorStale, "null");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.textures = 1;
    s_adapter.objectsBeforeFailure = 2;
    mrhiDevice* device = OpenWith(deviceDef, false);
    mrhiTextureId a;
    mrhiTextureId b;
    CHECK(mrhiCreateTexture(device, &def, &a) == mrhi_errorState, "not ready");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    CHECK(mrhiCreateTexture(device, &def, &a) == mrhi_success, "one");
    CHECK(mrhiCreateTexture(device, &def, &b) == mrhi_errorCapacity, "the limit");
    CHECK(mrhiDestroyTexture(device, a) == mrhi_success, "freed");
    CHECK(mrhiCreateTexture(device, &def, &a) == mrhi_success, "the driver's second");
    CHECK(mrhiDestroyTexture(device, a) == mrhi_success, "freed again");
    CHECK(mrhiCreateTexture(device, &def, &a) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiCreateTexture(device, &def, &a) == mrhi_errorPlatform, "no slot lost");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiInstance* instance = s_instance;
    TestKinds();
    TestLimits();
    TestSamplesAndUsages();
    TestFormats();
    TestRefusalsStateAndLimit();
    s_instance = instance;
    CHECK(mrhiGetDeviceMisuse(s_device) > 0, "the refusals were counted");
    Close(s_device);
    TestCompressedWithTheFeature();
    return s_failures == 0 ? 0 : 1;
}
