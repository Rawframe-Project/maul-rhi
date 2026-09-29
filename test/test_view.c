// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Views on a test driver device: mip and layer ranges, kinds per texture
// kind, formats, usages and aspects, and their ends with their texture's.
// The test driver traps when a texture goes before its views.

#include "test_device_setup.h"

#include "maul-rhi/resources.h"

#include <stdint.h>
#include <string.h>

static mrhiDevice* s_device;

static const mrhiTextureUsage kSampled = mrhi_textureSampled;
static const mrhiTextureUsage kTarget = mrhi_textureRenderTarget;

static mrhiTextureId MakeTexture(mrhiDevice* device, mrhiTextureKind kind, mrhiFormat format,
                                 uint32_t layers, uint32_t mips, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = 64;
    def.height = 64;
    def.depthOrLayers = layers;
    def.mipLevels = mips;
    def.usage = usage;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "the texture");
    return texture;
}

static mrhiViewDef Def(mrhiTextureId texture, mrhiTextureKind kind)
{
    mrhiViewDef def = mrhiDefaultViewDef();
    def.texture = texture;
    def.kind = kind;
    return def;
}

// Makes a view and destroys it at once.
static mrhiResult Make(mrhiViewDef def)
{
    mrhiViewId view = {0};
    mrhiResult status = mrhiCreateView(s_device, &def, &view);
    if (status == mrhi_success)
    {
        CHECK(view.index1 != 0, "an id");
        CHECK(mrhiDestroyView(s_device, view) == mrhi_success, "destroyed");
    }
    return status;
}

// A def with a mip and layer range.
static mrhiViewDef Range(mrhiViewDef def, uint32_t baseMip, uint32_t mips, uint32_t baseLayer,
                         uint32_t layers)
{
    def.baseMip = baseMip;
    def.mipCount = mips;
    def.baseLayer = baseLayer;
    def.layerCount = layers;
    return def;
}

static void TestRanges(void)
{
    mrhiTextureId texture =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 7, kSampled);
    mrhiViewDef def = Def(texture, mrhi_texture2d);
    CHECK(Make(def) == mrhi_success, "every mip");
    CHECK(Make(Range(def, 6, MRHI_REMAINING, 0, 1)) == mrhi_success, "the last mip");
    CHECK(Make(Range(def, 2, 5, 0, MRHI_REMAINING)) == mrhi_success, "mips 2 to 6");
    CHECK(Make(Range(def, 7, MRHI_REMAINING, 0, 1)) == mrhi_errorInvalid, "no mips remain");
    CHECK(Make(Range(def, 2, 6, 0, 1)) == mrhi_errorInvalid, "past the last mip");
    CHECK(Make(Range(def, 0, 0, 0, 1)) == mrhi_errorInvalid, "no mips");
    CHECK(Make(Range(def, UINT32_MAX - 1, 2, 0, 1)) == mrhi_errorInvalid, "an overflowing range");
    CHECK(Make(Range(def, 0, 1, 1, MRHI_REMAINING)) == mrhi_errorInvalid, "no layers remain");
    CHECK(Make(Range(def, 0, 1, 0, 0)) == mrhi_errorInvalid, "no layers");
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "done");
}

static void TestKinds(void)
{
    mrhiTextureId array =
        MakeTexture(s_device, mrhi_texture2dArray, mrhi_formatRgba8Unorm, 10, 1, kSampled);
    CHECK(Make(Def(array, mrhi_texture2d)) == mrhi_errorInvalid, "2D of 10 layers");
    CHECK(Make(Range(Def(array, mrhi_texture2d), 0, 1, 9, 1)) == mrhi_success, "its last layer");
    CHECK(Make(Range(Def(array, mrhi_texture2d), 0, 1, 10, 1)) == mrhi_errorInvalid,
          "past its last layer");
    CHECK(Make(Def(array, mrhi_texture2dArray)) == mrhi_success, "2D array");
    CHECK(Make(Range(Def(array, mrhi_texture2dArray), 0, 1, 3, 4)) == mrhi_success, "a part");
    CHECK(Make(Range(Def(array, mrhi_textureCube), 0, 1, 0, 6)) == mrhi_errorInvalid,
          "a cube of an array made without cubes");
    CHECK(Make(Range(Def(array, mrhi_textureCubeArray), 0, 1, 0, 6)) == mrhi_errorInvalid,
          "a cube array of an array made without cubes");
    mrhiTextureId cube =
        MakeTexture(s_device, mrhi_textureCube, mrhi_formatRgba8Unorm, 6, 1, kSampled);
    CHECK(Make(Def(cube, mrhi_textureCube)) == mrhi_success, "cube");
    CHECK(Make(Def(cube, mrhi_textureCubeArray)) == mrhi_success, "an array of one cube");
    CHECK(Make(Def(cube, mrhi_texture2dArray)) == mrhi_success, "its faces");
    CHECK(Make(Range(Def(cube, mrhi_texture2d), 0, 1, 5, 1)) == mrhi_success, "one face");
    CHECK(Make(Range(Def(cube, mrhi_textureCube), 0, 1, 1, MRHI_REMAINING)) == mrhi_errorInvalid,
          "a cube of 5 faces");
    mrhiTextureId cubes =
        MakeTexture(s_device, mrhi_textureCubeArray, mrhi_formatRgba8Unorm, 12, 1, kSampled);
    CHECK(Make(Def(cubes, mrhi_textureCubeArray)) == mrhi_success, "cube array");
    CHECK(Make(Range(Def(cubes, mrhi_textureCube), 0, 1, 6, 6)) == mrhi_success, "its second");
    CHECK(Make(Range(Def(cubes, mrhi_textureCubeArray), 0, 1, 0, 7)) == mrhi_errorInvalid,
          "7 layers of cubes");
    CHECK(Make(Range(Def(cubes, mrhi_textureCube), 0, 1, 0, 12)) == mrhi_errorInvalid,
          "a cube of 12 layers");
    mrhiTextureId volume =
        MakeTexture(s_device, mrhi_texture3d, mrhi_formatRgba8Unorm, 16, 1, kSampled);
    CHECK(Make(Def(volume, mrhi_texture3d)) == mrhi_success, "3D");
    CHECK(Make(Range(Def(volume, mrhi_texture3d), 0, 1, 1, 1)) == mrhi_errorInvalid,
          "a 3D texture has one layer");
    CHECK(Make(Def(volume, mrhi_texture2d)) == mrhi_errorInvalid, "2D of a volume");
    CHECK(Make(Def(volume, mrhi_texture2dArray)) == mrhi_errorInvalid, "a 2D array of a volume");
    CHECK(Make(Def(array, mrhi_texture3d)) == mrhi_errorInvalid, "3D of an array");
    CHECK(Make(Def(cube, mrhi_texture3d)) == mrhi_errorInvalid, "3D of a cube");
    CHECK(Make(Def(volume, 5)) == mrhi_errorInvalid, "an unknown kind");
    mrhiTextureId textures[] = {array, cube, cubes, volume};
    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i)
    {
        CHECK(mrhiDestroyTexture(s_device, textures[i]) == mrhi_success, "done");
    }
}

static void TestFormatsAndUsages(void)
{
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 32;
    textureDef.height = 32;
    textureDef.usage = kSampled | mrhi_textureStorage;
    textureDef.viewFormats[1] = mrhi_formatRgba8UnormSrgb;
    mrhiTextureId texture;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &texture) == mrhi_success, "mutable");
    mrhiViewDef def = Def(texture, mrhi_texture2d);
    CHECK(Make(def) == mrhi_success, "its own format and uses");
    def.format = mrhi_formatRgba8UnormSrgb;
    CHECK(Make(def) == mrhi_errorUnsupported, "sRGB cannot be storage");
    def.usage = kSampled;
    CHECK(Make(def) == mrhi_success, "sampled as sRGB");
    def.format = mrhi_formatBgra8Unorm;
    CHECK(Make(def) == mrhi_errorInvalid, "a format it was not given");
    def.format = mrhi_formatNone;
    def.usage = kTarget;
    CHECK(Make(def) == mrhi_errorInvalid, "a use it lacks");
    def.usage = 0x40;
    CHECK(Make(def) == mrhi_errorInvalid, "an unknown use");
    mrhiTextureId plain =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    def = Def(plain, mrhi_texture2d);
    def.format = mrhi_formatRgba8UnormSrgb;
    CHECK(Make(def) == mrhi_errorInvalid, "a twin it was not given");
    mrhiTextureId transient = MakeTexture(s_device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1,
                                          mrhi_textureTransient | kTarget);
    def = Def(transient, mrhi_texture2d);
    CHECK(Make(def) == mrhi_success, "a transient target");
    def.usage = kTarget;
    CHECK(Make(def) == mrhi_success, "as a target alone");
    def.usage = mrhi_textureTransient;
    CHECK(Make(def) == mrhi_errorInvalid, "transient but no target");
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "done");
    CHECK(mrhiDestroyTexture(s_device, plain) == mrhi_success, "done");
    CHECK(mrhiDestroyTexture(s_device, transient) == mrhi_success, "done");
}

static void TestAspects(void)
{
    mrhiTextureId depth =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatDepth32Float, 1, 1, kSampled | kTarget);
    mrhiViewDef def = Def(depth, mrhi_texture2d);
    CHECK(Make(def) == mrhi_success, "depth, every aspect");
    def.aspect = mrhi_aspectDepthOnly;
    CHECK(Make(def) == mrhi_success, "depth only");
    def.aspect = mrhi_aspectStencilOnly;
    CHECK(Make(def) == mrhi_errorInvalid, "no stencil to see");
    def.aspect = 3;
    CHECK(Make(def) == mrhi_errorInvalid, "an unknown aspect");
    mrhiTextureId both =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatDepthStencil, 1, 1, kSampled | kTarget);
    def = Def(both, mrhi_texture2d);
    def.aspect = mrhi_aspectStencilOnly;
    CHECK(Make(def) == mrhi_success, "stencil only");
    def.aspect = mrhi_aspectDepthOnly;
    CHECK(Make(def) == mrhi_success, "its depth only");
    mrhiTextureId color =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    def = Def(color, mrhi_texture2d);
    def.aspect = mrhi_aspectDepthOnly;
    CHECK(Make(def) == mrhi_errorInvalid, "no depth to see");
    CHECK(mrhiDestroyTexture(s_device, depth) == mrhi_success, "done");
    CHECK(mrhiDestroyTexture(s_device, both) == mrhi_success, "done");
    CHECK(mrhiDestroyTexture(s_device, color) == mrhi_success, "done");
}

// Views end with their texture, from any place in its list; their slots
// come back.
static void TestEnds(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.views = 3;
    mrhiDevice* device = OpenWith(deviceDef, true);
    mrhiTextureId texture =
        MakeTexture(device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    mrhiViewDef def = Def(texture, mrhi_texture2d);
    mrhiViewId views[4];
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(mrhiCreateView(device, &def, &views[i]) == mrhi_success, "a view");
    }
    CHECK(mrhiCreateView(device, &def, &views[3]) == mrhi_errorCapacity, "the limit");
    CHECK(mrhiDestroyView(device, views[1]) == mrhi_success, "the middle one");
    CHECK(mrhiDestroyView(device, views[1]) == mrhi_errorStale, "ended");
    CHECK(mrhiDestroyView(device, views[0]) == mrhi_success, "the oldest, after the middle");
    CHECK(mrhiCreateView(device, &def, &views[1]) == mrhi_success, "another");
    CHECK(mrhiDestroyView(device, views[1]) == mrhi_success, "the newest");
    CHECK(mrhiCreateView(device, &def, &views[1]) == mrhi_success, "and another");
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture and its views");
    CHECK(mrhiDestroyView(device, views[1]) == mrhi_errorStale, "ended with their texture");
    CHECK(mrhiDestroyView(device, views[2]) == mrhi_errorStale, "both");
    mrhiTextureId next = MakeTexture(device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    CHECK(mrhiCreateView(device, &def, &views[0]) == mrhi_errorStale, "a texture that ended");
    def.texture = next;
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(mrhiCreateView(device, &def, &views[i]) == mrhi_success, "every slot back");
    }
    CHECK(mrhiDestroyTexture(device, next) == mrhi_success, "three views with it");
    for (size_t i = 0; i < 3; ++i)
    {
        CHECK(mrhiDestroyView(device, views[i]) == mrhi_errorStale, "all ended");
    }
    Close(device);
}

// The test driver holds 64 views; a texture with all of them ends them
// all.
static void TestManyViews(void)
{
    mrhiTextureId texture =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    mrhiViewDef def = Def(texture, mrhi_texture2d);
    mrhiViewId view;
    for (int i = 0; i < 64; ++i)
    {
        CHECK(mrhiCreateView(s_device, &def, &view) == mrhi_success, "a view");
    }
    CHECK(mrhiCreateView(s_device, &def, &view) == mrhi_errorPlatform, "the driver's bound");
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "all 64 with it");
    CHECK(mrhiDestroyView(s_device, view) == mrhi_errorStale, "the last one ended");
}

static void TestRefusalsAndState(void)
{
    mrhiTextureId texture =
        MakeTexture(s_device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    mrhiViewDef def = Def(texture, mrhi_texture2d);
    mrhiViewId view;
    CHECK(mrhiCreateView(nullptr, &def, &view) == mrhi_errorInvalid, "no device");
    CHECK(mrhiCreateView(s_device, nullptr, &view) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateView(s_device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDestroyView(nullptr, view) == mrhi_errorInvalid, "no device to destroy on");
    CHECK(mrhiDestroyView(s_device, (mrhiViewId){0, 0}) == mrhi_errorStale, "null");
    def.cookie = 0;
    CHECK(Make(def) == mrhi_errorInvalid, "no cookie");
    def = Def(texture, mrhi_texture2d);
    char label[MRHI_LABEL_BYTES + 1];
    memset(label, 'v', sizeof(label));
    def.label = label;
    def.labelLength = MRHI_LABEL_BYTES;
    CHECK(Make(def) == mrhi_success, "a label at the bound");
    def.labelLength = MRHI_LABEL_BYTES + 1;
    CHECK(Make(def) == mrhi_errorInvalid, "a label past the bound");
    def = Def((mrhiTextureId){0, 0}, mrhi_texture2d);
    CHECK(Make(def) == mrhi_errorStale, "no texture");
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "done");
    s_adapter.objectsBeforeFailure = 1;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.views = 1;
    mrhiInstance* instance = s_instance;
    mrhiDevice* device = OpenWith(deviceDef, false);
    def.texture = (mrhiTextureId){1, 1};
    CHECK(mrhiCreateView(device, &def, &view) == mrhi_errorState, "not ready");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    def.texture = MakeTexture(device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, kSampled);
    CHECK(mrhiCreateView(device, &def, &view) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiCreateView(device, &def, &view) == mrhi_errorPlatform, "no slot lost");
    Close(device);
    s_instance = instance;
}

int main(void)
{
    ResetAdapter();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.label = "views";
    deviceDef.labelLength = 5;
    s_device = OpenWith(deviceDef, true);
    mrhiInstance* instance = s_instance;
    TestRanges();
    TestKinds();
    TestFormatsAndUsages();
    TestAspects();
    TestManyViews();
    TestRefusalsAndState();
    s_instance = instance;
    CHECK(mrhiGetDeviceMisuse(s_device) > 0, "the refusals were counted");
    Close(s_device);
    TestEnds();
    return s_failures == 0 ? 0 : 1;
}
