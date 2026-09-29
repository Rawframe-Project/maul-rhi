// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Buffers on a test driver device: sizes and usages checked, the
// device's limits, and ids that end with their buffer.

#include "test_device_setup.h"

#include "maul-rhi/resources.h"

// A ready device holding at most count buffers, or an opening one; the
// driver fails after s_adapter's objectsBeforeFailure objects.
static mrhiDevice* Open(uint32_t count, bool ready)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.buffers = count;
    return OpenWith(def, ready);
}

static mrhiBufferDef Def(uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    return def;
}

static void TestMakeAndDestroy(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiBufferDef def = Def(1024, mrhi_bufferVertex | mrhi_bufferCopyDestination);
    mrhiBufferId a = {0};
    CHECK(mrhiCreateBuffer(device, &def, &a) == mrhi_success && a.index1 != 0, "a buffer");
    def = Def(mrhiDefaultLimits().bufferBytes, mrhi_bufferStorage);
    mrhiBufferId b = {0};
    CHECK(mrhiCreateBuffer(device, &def, &b) == mrhi_success, "as large as the limit");
    CHECK(mrhiDestroyBuffer(device, a) == mrhi_success, "destroyed");
    CHECK(mrhiDestroyBuffer(device, a) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiDestroyBuffer(nullptr, b) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDestroyBuffer(device, b) == mrhi_success, "the other destroyed");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

static void TestInvalidDefs(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiBufferId buffer;
    mrhiBufferDef def = Def(0, mrhi_bufferVertex);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "empty");
    def = Def(6, mrhi_bufferVertex);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "not a multiple of 4");
    def = Def(64, 0);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "no usage");
    def = Def(64, mrhi_bufferIndex | 0x100u);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "an unknown usage");
    def = Def(64, mrhi_bufferIndex);
    def.cookie = 0;
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "no cookie");
    def = Def(64, mrhi_bufferIndex);
    def.label = "\xED\xA0\x80";
    def.labelLength = 3;
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "a surrogate label");
    def = Def(64, mrhi_bufferIndex);
    CHECK(mrhiCreateBuffer(device, nullptr, &buffer) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateBuffer(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateBuffer(nullptr, &def, &buffer) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == 8, "each counted");
    def = Def(mrhiDefaultLimits().bufferBytes + 4, mrhi_bufferStorage);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorUnsupported, "past the limit");
    def = Def(64, mrhi_bufferIndex);
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorUnsupported, "an extension");
    CHECK(mrhiGetDeviceMisuse(device) == 8, "unsupported asks are not misuse");
    Close(device);
}

static void TestStateLimitAndFailure(void)
{
    mrhiDevice* device = Open(2, false);
    mrhiBufferDef def = Def(64, mrhi_bufferUniform);
    mrhiBufferId a;
    mrhiBufferId b;
    mrhiBufferId c;
    CHECK(mrhiCreateBuffer(device, &def, &a) == mrhi_errorState, "not ready yet");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    CHECK(mrhiCreateBuffer(device, &def, &a) == mrhi_success, "one");
    CHECK(mrhiCreateBuffer(device, &def, &b) == mrhi_success, "two");
    CHECK(mrhiCreateBuffer(device, &def, &c) == mrhi_errorCapacity, "the limit");
    CHECK(mrhiDestroyBuffer(device, b) == mrhi_success, "one gone");
    CHECK(mrhiCreateBuffer(device, &def, &c) == mrhi_success, "room again");
    Close(device);
    s_adapter.objectsBeforeFailure = 1;
    device = Open(2, true);
    CHECK(mrhiCreateBuffer(device, &def, &a) == mrhi_success, "the one the driver makes");
    CHECK(mrhiCreateBuffer(device, &def, &b) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiCreateBuffer(device, &def, &b) == mrhi_errorPlatform, "no slot lost");
    CHECK(mrhiDestroyBuffer(device, a) == mrhi_success, "the made one destroyed");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestMakeAndDestroy();
    TestInvalidDefs();
    TestStateLimitAndFailure();
    return s_failures == 0 ? 0 : 1;
}
