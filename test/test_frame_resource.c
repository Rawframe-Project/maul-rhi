// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The open frame's resources on a test driver device: declared textures
// and buffers checked as their device objects are, without usages, and
// imports that return one id per frame.

#include "test_device_setup.h"

static mrhiDevice* s_device;

static void Begin(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &def) == mrhi_success, "begun");
}

static mrhiTextureDef Texture(void)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba16Float;
    def.width = 1920;
    def.height = 1080;
    return def;
}

static mrhiBufferDef Buffer(uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    return def;
}

static void TestDeclare(void)
{
    Begin();
    mrhiTextureDef texture = Texture();
    texture.label = "scene color";
    texture.labelLength = 11;
    mrhiResourceId a;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_success, "a texture");
    CHECK(a.index1 == 1 && a.generation != 0, "the first resource");
    mrhiBufferDef buffer = Buffer(4096);
    mrhiResourceId b;
    CHECK(mrhiDeclareBuffer(s_device, &buffer, &b) == mrhi_success, "a buffer");
    CHECK(b.index1 == 2 && b.generation == a.generation, "the second, of the same frame");
    texture.usage = mrhi_textureSampled;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorInvalid, "a usage");
    texture = Texture();
    texture.width = 0;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorInvalid, "no width");
    texture = Texture();
    texture.sampleCount = 4;
    texture.mipLevels = 2;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorInvalid, "samples with mips");
    texture = Texture();
    texture.width = 8193;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorUnsupported, "too wide");
    texture = Texture();
    texture.format = mrhi_formatBc1RgbaUnorm;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorUnsupported,
          "BC without its feature");
    texture = Texture();
    texture.cookie = 0;
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorInvalid, "no cookie");
    buffer.usage = mrhi_bufferVertex;
    CHECK(mrhiDeclareBuffer(s_device, &buffer, &b) == mrhi_errorInvalid, "a buffer usage");
    buffer = Buffer(6);
    CHECK(mrhiDeclareBuffer(s_device, &buffer, &b) == mrhi_errorInvalid, "not a multiple of 4");
    buffer = Buffer(mrhiDefaultLimits().bufferBytes + 4);
    CHECK(mrhiDeclareBuffer(s_device, &buffer, &b) == mrhi_errorUnsupported, "too large");
    CHECK(mrhiDeclareTexture(nullptr, &texture, &a) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDeclareTexture(s_device, nullptr, &a) == mrhi_errorInvalid, "no def");
    CHECK(mrhiDeclareTexture(s_device, &texture, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDeclareBuffer(nullptr, &buffer, &b) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDeclareBuffer(s_device, nullptr, &b) == mrhi_errorInvalid, "no def");
    CHECK(mrhiDeclareBuffer(s_device, &buffer, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    texture = Texture();
    CHECK(mrhiDeclareTexture(s_device, &texture, &a) == mrhi_errorState, "no frame");
    buffer = Buffer(64);
    CHECK(mrhiDeclareBuffer(s_device, &buffer, &b) == mrhi_errorState, "no frame for a buffer");
}

static void TestImport(void)
{
    mrhiTextureDef textureDef = Texture();
    textureDef.usage = mrhi_textureSampled;
    mrhiTextureId texture;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &texture) == mrhi_success, "a device texture");
    mrhiBufferDef bufferDef = Buffer(256);
    bufferDef.usage = mrhi_bufferStorage;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &buffer) == mrhi_success, "a device buffer");
    mrhiResourceId first;
    CHECK(mrhiImportTexture(s_device, texture, &first) == mrhi_errorState, "no frame");
    CHECK(mrhiImportBuffer(s_device, buffer, &first) == mrhi_errorState, "no frame for a buffer");
    Begin();
    mrhiResourceId again;
    CHECK(mrhiImportTexture(s_device, texture, &first) == mrhi_success, "imported");
    CHECK(mrhiImportTexture(s_device, texture, &again) == mrhi_success, "imported again");
    CHECK(again.index1 == first.index1 && again.generation == first.generation, "the same id");
    mrhiResourceId bufferFirst;
    CHECK(mrhiImportBuffer(s_device, buffer, &bufferFirst) == mrhi_success, "a buffer imported");
    CHECK(mrhiImportBuffer(s_device, buffer, &again) == mrhi_success &&
              again.index1 == bufferFirst.index1,
          "the buffer's same id");
    CHECK(bufferFirst.index1 != first.index1, "another resource");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Begin();
    CHECK(mrhiImportTexture(s_device, texture, &again) == mrhi_success, "the next frame");
    CHECK(again.generation != first.generation && again.index1 == 1, "a new id there");
    CHECK(mrhiImportBuffer(s_device, buffer, &again) == mrhi_success && again.index1 == 2,
          "the buffer's new id");
    CHECK(mrhiImportTexture(s_device, (mrhiTextureId){0, 0}, &again) == mrhi_errorStale,
          "no texture");
    CHECK(mrhiImportBuffer(s_device, (mrhiBufferId){0, 0}, &again) == mrhi_errorStale, "no buffer");
    CHECK(mrhiImportTexture(nullptr, texture, &again) == mrhi_errorInvalid, "no device");
    CHECK(mrhiImportTexture(s_device, texture, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiImportBuffer(nullptr, buffer, &again) == mrhi_errorInvalid, "no device");
    CHECK(mrhiImportBuffer(s_device, buffer, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiImportTexture(s_device, texture, &again) == mrhi_errorState,
          "imported in a frame that closed");
    CHECK(mrhiImportBuffer(s_device, buffer, &again) == mrhi_errorState,
          "the buffer, in a frame that closed");
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "done");
    CHECK(mrhiDestroyBuffer(s_device, buffer) == mrhi_success, "done");
    Begin();
    CHECK(mrhiImportTexture(s_device, texture, &again) == mrhi_errorStale, "a destroyed texture");
    CHECK(mrhiImportBuffer(s_device, buffer, &again) == mrhi_errorStale, "a destroyed buffer");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// The frameResources limit, counted afresh each frame.
static void TestLimit(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.frameResources = 2;
    mrhiInstance* instance = s_instance;
    mrhiDevice* previous = s_device;
    s_device = OpenWith(deviceDef, true);
    mrhiTextureDef textureDef = Texture();
    textureDef.usage = mrhi_textureSampled;
    mrhiTextureId texture;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &texture) == mrhi_success, "a device texture");
    mrhiBufferDef bufferDef = Buffer(256);
    bufferDef.usage = mrhi_bufferStorage;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &buffer) == mrhi_success, "a device buffer");
    for (int frame = 0; frame < 2; ++frame)
    {
        Begin();
        mrhiResourceId resource;
        mrhiTextureDef declared = Texture();
        CHECK(mrhiImportTexture(s_device, texture, &resource) == mrhi_success, "one");
        CHECK(mrhiDeclareTexture(s_device, &declared, &resource) == mrhi_success, "two");
        CHECK(mrhiDeclareTexture(s_device, &declared, &resource) == mrhi_errorCapacity, "full");
        mrhiBufferDef buffer64 = Buffer(64);
        CHECK(mrhiDeclareBuffer(s_device, &buffer64, &resource) == mrhi_errorCapacity,
              "full for a buffer");
        CHECK(mrhiImportBuffer(s_device, buffer, &resource) == mrhi_errorCapacity,
              "full for an import");
        CHECK(mrhiImportTexture(s_device, texture, &resource) == mrhi_success,
              "an import made already");
        CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    }
    Close(s_device);
    s_device = previous;
    s_instance = instance;
}

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiInstance* instance = s_instance;
    TestDeclare();
    TestImport();
    TestLimit();
    s_instance = instance;
    CHECK(mrhiGetDeviceMisuse(s_device) == 12, "each invalid call counted");
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
