// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Buffers on a test driver device: sizes and usages checked, the
// device's limits, and ids that end with their buffer.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/test.h"

// A ready device holding at most count buffers, or an opening one; the
// driver fails after s_adapter's objectsBeforeFailure objects.
static mrhiDevice* Open(uint32_t count, bool ready)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.buffers = count;
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
    CHECK(mrhiDevice_GetMisuse(device) == 0, "no misuse");
    Close(device);
}

static void TestInvalidDefs(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiBufferId buffer;
    mrhiBufferDef def = Def(64, 0);
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
    CHECK(mrhiDevice_GetMisuse(device) == 6, "each counted");
    def = Def(mrhiDefaultLimits().bufferBytes + 4, mrhi_bufferStorage);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorUnsupported, "past the limit");
    def = Def(64, mrhi_bufferIndex);
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorUnsupported, "an extension");
    CHECK(mrhiDevice_GetMisuse(device) == 6, "unsupported asks are not misuse");
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
    CHECK(mrhiInstance_TakeNotification(s_instance, &record) == mrhi_success, "ready");
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

// A pass of the frame declaring each access: success, or the refusal.
static mrhiResult AddPass(mrhiDevice* device, const mrhiAccess* accesses, uint32_t count,
                          mrhiPassId* passOut)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = count;
    return mrhiAddPass(device, &def, passOut);
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// WebGPU's buffers take any size, 0 included. A driver is handed whole
// fours, at least 4, which the frame's zeroing clears; every range is
// checked against the program's size, never the driver's: a range
// ending past 6 bytes, or a whole range of 6 bytes where fours are
// needed, is refused though the driver's buffer holds 8.
static void TestAnySizes(void)
{
    mrhiTestFrameLog log = {0};
    s_adapter.frameLog = &log;
    mrhiDevice* device = Open(4, true);
    mrhiBufferUsage usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferDef def = Def(0, usage);
    mrhiBufferId empty = {0};
    CHECK(mrhiCreateBuffer(device, &def, &empty) == mrhi_success, "0 bytes");
    def = Def(6, usage);
    mrhiBufferId six = {0};
    CHECK(mrhiCreateBuffer(device, &def, &six) == mrhi_success, "6 bytes");
    def = Def(64, usage);
    mrhiBufferId source = {0};
    CHECK(mrhiCreateBuffer(device, &def, &source) == mrhi_success, "a source");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId r[5] = {0};
    mrhiBufferDef declared = mrhiDefaultBufferDef();
    declared.size = 6;
    mrhiBufferDef nothing = mrhiDefaultBufferDef();
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, empty, &r[0]) == mrhi_success &&
              mrhiImportBuffer(device, six, &r[1]) == mrhi_success &&
              mrhiImportBuffer(device, source, &r[2]) == mrhi_success &&
              mrhiDeclareBuffer(device, &declared, &r[3]) == mrhi_success &&
              mrhiDeclareBuffer(device, &nothing, &r[4]) == mrhi_success,
          "a frame of them, and declared buffers of 6 and 0 bytes");
    const mrhiAccess writes[] = {
        Whole(r[0], mrhi_accessCopyDestination), Whole(r[1], mrhi_accessCopyDestination),
        Whole(r[2], mrhi_accessCopySource), Whole(r[3], mrhi_accessCopyDestination),
        Whole(r[4], mrhi_accessCopyDestination)};
    const mrhiAccess reads[] = {Whole(r[1], mrhi_accessCopySource),
                                Whole(r[3], mrhi_accessCopySource)};
    mrhiPassId write = {0};
    mrhiPassId read = {0};
    CHECK(AddPass(device, writes, 5, &write) == mrhi_success &&
              AddPass(device, reads, 2, &read) == mrhi_success &&
              mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, write) == mrhi_success &&
              mrhiPass_Begin(device, read) == mrhi_success,
          "two passes begun");
    CHECK(mrhiPass_ClearBuffer(device, write, r[0], 0, MRHI_WHOLE_SIZE) == mrhi_success &&
              mrhiPass_ClearBuffer(device, write, r[4], 0, MRHI_WHOLE_SIZE) == mrhi_success,
          "all of 0 bytes cleared, made and declared");
    CHECK(mrhiPass_CopyBuffer(device, write, r[2], 0, r[0], 0, 4) == mrhi_errorInvalid,
          "4 bytes into 0");
    for (uint32_t i = 1; i <= 3; i += 2)
    {
        CHECK(mrhiPass_ClearBuffer(device, write, r[i], 0, MRHI_WHOLE_SIZE) == mrhi_errorInvalid,
              "all of 6 bytes, not a multiple of 4");
        CHECK(mrhiPass_ClearBuffer(device, write, r[i], 4, 4) == mrhi_errorInvalid,
              "a clear past 6 bytes");
        CHECK(mrhiPass_CopyBuffer(device, write, r[2], 0, r[i], 4, 4) == mrhi_errorInvalid,
              "a copy past 6 bytes");
        CHECK(mrhiPass_ClearBuffer(device, write, r[i], 0, 4) == mrhi_success &&
                  mrhiPass_CopyBuffer(device, write, r[2], 0, r[i], 0, 4) == mrhi_success,
              "a clear and a copy within 6 bytes");
        mrhiRequestId request = {0};
        CHECK(mrhiPass_ReadBuffer(device, read, r[i], 4, 4, &request) == mrhi_errorInvalid,
              "a read past 6 bytes");
        CHECK(mrhiPass_ReadBuffer(device, read, r[i], 0, 4, &request) == mrhi_success,
              "a read within them");
    }
    mrhiRequestId token = {0};
    CHECK(mrhiPass_End(device, write) == mrhi_success &&
              mrhiPass_End(device, read) == mrhi_success &&
              mrhiDevice_SubmitFrame(device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(device, token, 1) == mrhi_success,
          "submitted and finished");
    CHECK(log.frames == 1 && log.transients == 2 && log.memoryBytes > 0,
          "the driver walked it, the declared buffers in its memory");
    Close(device);
    ResetAdapter();
}

// A size past the device's bufferBytes but within the adapter's is
// capacity, which a device asking for more would take; past the
// adapter's it is unsupported. Neither is misuse.
static void TestPastDeviceLimit(void)
{
    uint64_t most = mrhiDefaultLimits().bufferBytes;
    s_adapter.limits.bufferBytes = 2 * most;
    mrhiDevice* device = Open(4, true);
    mrhiBufferId buffer = {0};
    mrhiBufferDef def = Def(most + 4, mrhi_bufferStorage);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorCapacity,
          "past the device's limit, within the adapter's");
    def = Def(2 * most + 4, mrhi_bufferStorage);
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorUnsupported, "past the adapter's");
    CHECK(mrhiDevice_GetMisuse(device) == 0, "neither is misuse");
    Close(device);
    ResetAdapter();
}

int main(void)
{
    ResetAdapter();
    TestMakeAndDestroy();
    TestPastDeviceLimit();
    TestInvalidDefs();
    TestStateLimitAndFailure();
    TestAnySizes();
    return s_failures == 0 ? 0 : 1;
}
