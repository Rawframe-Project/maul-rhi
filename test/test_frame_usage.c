// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's usage of the device's frame budgets on test driver devices:
// what the frame begun last took, the library's own pass left out, kept
// after a refusal and a submission until the next frame begins; and the
// budget each capacity refusal names in the device's diagnostics, counted
// as no misuse, a driver's budgets at the submission among them.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

#include <string.h>

// A command arena's chunk: the unit frameCommandBytes is taken in.
#define CHUNK_BYTES 4096

static mrhiDevice* s_device;
static uint8_t s_bytes[2048];

// Opens a ready device keeping diagnostics, with frames held until
// waited on.
static void Open(mrhiDeviceDef def)
{
    s_adapter.holdFrames = true;
    def.budgets.diagnostics = 8;
    s_device = OpenWith(def, true);
}

static void CloseDevice(void)
{
    CHECK(mrhiDevice_GetMisuse(s_device) == 0, "no limit is misuse");
    Close(s_device);
    s_device = nullptr;
}

static void Begin(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &def) == mrhi_success, "begun");
}

static mrhiFrameUsage Usage(void)
{
    mrhiFrameUsage usage;
    memset(&usage, 0xFF, sizeof(usage));
    CHECK(mrhiDevice_GetFrameUsage(s_device, &usage) == mrhi_success, "the usage");
    return usage;
}

// Whether two usages hold the same counts. Field by field, because a
// struct returned by value may carry any bytes in its padding.
static bool SameUsage(const mrhiFrameUsage* a, const mrhiFrameUsage* b)
{
    return a->frameResourceCount == b->frameResourceCount &&
           a->framePassCount == b->framePassCount && a->frameAccessCount == b->frameAccessCount &&
           a->frameBarrierCount == b->frameBarrierCount &&
           a->frameCommandBytes == b->frameCommandBytes &&
           a->frameUploadBytes == b->frameUploadBytes &&
           a->frameIndirectDrawCount == b->frameIndirectDrawCount &&
           a->frameMemoryBytes == b->frameMemoryBytes;
}

// Whether the device's next diagnostic is the code, standing for count
// refusals, and the last.
static bool Refused(mrhiDiagnosticCode code, uint32_t count)
{
    mrhiDiagnostic record = {0};
    bool taken = mrhiDevice_TakeDiagnostic(s_device, &record) == mrhi_success;
    mrhiDiagnostic after;
    return taken && record.code == code && record.callsRefused == count &&
           mrhiDevice_TakeDiagnostic(s_device, &after) == mrhi_empty;
}

static mrhiResourceId DeclareBuffer(uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareBuffer(s_device, &def, &resource) == mrhi_success, "declared");
    return resource;
}

static mrhiResourceId ImportBuffer(mrhiBufferId buffer)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportBuffer(s_device, buffer, &resource) == mrhi_success, "imported");
    return resource;
}

static mrhiBufferId MakeBuffer(uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// Adds a transfer pass that is never culled: the refusal, success with
// the pass.
static mrhiResult AddPass(const mrhiAccess* accesses, uint32_t count, mrhiPassId* passOut)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = count;
    return mrhiAddPass(s_device, &def, passOut);
}

// Adds a pass of one access, compiles the frame and begins the pass.
static mrhiPassId Recording(mrhiResourceId resource, mrhiAccessKind kind)
{
    mrhiAccess access = Access(resource, kind);
    mrhiPassId pass = {0};
    CHECK(AddPass(&access, 1, &pass) == mrhi_success, "a pass");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiPass_Begin(s_device, pass) == mrhi_success, "begun");
    return pass;
}

static void Drop(void)
{
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
}

static void TestBeforeFrames(void)
{
    Open(mrhiDefaultDeviceDef());
    mrhiFrameUsage usage = Usage();
    mrhiFrameUsage zero = {0};
    CHECK(SameUsage(&usage, &zero), "nothing before the first frame");
    CHECK(mrhiDevice_GetFrameUsage(nullptr, &usage) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDevice_GetFrameUsage(s_device, nullptr) == mrhi_errorInvalid &&
              Refused(mrhi_diagnosticNullArgument, 1) && mrhiDevice_GetMisuse(s_device) == 1,
          "no usage, a misuse");
    Close(s_device);
    s_device = nullptr;
}

// Each count, kept past the submission until the next frame begins.
static void TestCounts(void)
{
    Open(mrhiDefaultDeviceDef());
    Begin();
    mrhiResourceId first = DeclareBuffer(1024);
    mrhiResourceId second = DeclareBuffer(1024);
    mrhiAccess accesses[2] = {
        Access(first, mrhi_accessCopyDestination),
        Access(second, mrhi_accessCopyDestination),
    };
    mrhiPassId writes = {0};
    mrhiPassId reads = {0};
    mrhiAccess read = Access(first, mrhi_accessCopySource);
    CHECK(AddPass(accesses, 2, &writes) == mrhi_success &&
              AddPass(&read, 1, &reads) == mrhi_success,
          "two passes");
    mrhiFrameUsage usage = Usage();
    CHECK(usage.frameResourceCount == 2 && usage.framePassCount == 2 &&
              usage.frameAccessCount == 3 && usage.frameBarrierCount == 0 &&
              usage.frameCommandBytes == 0 && usage.frameUploadBytes == 0 &&
              usage.frameIndirectDrawCount == 0 && usage.frameMemoryBytes == 0,
          "resources, passes and accesses before the compile");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    // The two buffers are both written in the first pass, so they share
    // no bytes.
    CHECK(Usage().frameMemoryBytes >= 2048, "the declared buffers' memory once compiled");
    size_t barriers = 0;
    CHECK(mrhiDevice_GetFrameBarriers(s_device, nullptr, 0, &barriers) == mrhi_success &&
              barriers > 0,
          "barriers planned");
    CHECK(Usage().frameBarrierCount == barriers, "the compile's barriers, all the program's");
    CHECK(mrhiPass_Begin(s_device, writes) == mrhi_success, "begun");
    CHECK(mrhiPass_ClearBuffer(s_device, writes, first, 0, 4) == mrhi_success, "a command");
    CHECK(mrhiPass_WriteBuffer(s_device, writes, second, 0, s_bytes, 4) == mrhi_success &&
              mrhiPass_WriteBuffer(s_device, writes, second, 0, s_bytes, 516) == mrhi_success,
          "two uploads");
    usage = Usage();
    CHECK(usage.frameCommandBytes == CHUNK_BYTES, "one chunk of commands");
    CHECK(usage.frameUploadBytes == 512 + 1024, "each upload at a 512-byte boundary");
    CHECK(mrhiPass_End(s_device, writes) == mrhi_success, "ended");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "submitted");
    mrhiFrameUsage submitted = Usage();
    CHECK(SameUsage(&submitted, &usage), "kept once submitted");
    Begin();
    usage = Usage();
    mrhiFrameUsage zero = {0};
    CHECK(SameUsage(&usage, &zero), "nothing in the next frame");
    Drop();
    CloseDevice();
}

// A new buffer's clear and its barrier are the library's.
static void TestLibraryLeftOut(void)
{
    Open(mrhiDefaultDeviceDef());
    mrhiBufferId buffer = MakeBuffer(1024);
    Begin();
    mrhiPassId pass = Recording(ImportBuffer(buffer), mrhi_accessCopySource);
    size_t barriers = 0;
    CHECK(mrhiDevice_GetFrameBarriers(s_device, nullptr, 0, &barriers) == mrhi_success &&
              barriers > 0,
          "the clear's barriers");
    mrhiFrameUsage usage = Usage();
    CHECK(usage.framePassCount == 1 && usage.frameBarrierCount == 0 && usage.frameCommandBytes == 0,
          "the library's pass, barrier and commands left out");
    CHECK(mrhiPass_End(s_device, pass) == mrhi_success, "ended");
    Drop();
    CloseDevice();
}

static void TestResources(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.frameResources = 2;
    Open(def);
    mrhiBufferId buffer = MakeBuffer(1024);
    Begin();
    DeclareBuffer(4);
    ImportBuffer(buffer);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 4;
    mrhiResourceId resource;
    CHECK(mrhiDeclareBuffer(s_device, &bufferDef, &resource) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameResources, 1),
          "a third resource");
    CHECK(Usage().frameResourceCount == 2, "the frame's resources");
    Drop();
    CloseDevice();
}

static void TestPasses(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.framePasses = 2;
    def.budgets.frameAccesses = 4;
    Open(def);
    Begin();
    mrhiAccess accesses[3] = {
        Access(DeclareBuffer(4), mrhi_accessCopyDestination),
        Access(DeclareBuffer(4), mrhi_accessCopyDestination),
        Access(DeclareBuffer(4), mrhi_accessCopyDestination),
    };
    mrhiPassId pass = {0};
    // Three accesses and a depth target's room fill the frame's four.
    CHECK(AddPass(accesses, 3, &pass) == mrhi_success, "three accesses");
    CHECK(AddPass(accesses, 1, &pass) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameAccesses, 1),
          "a fourth access and its room");
    mrhiFrameUsage usage = Usage();
    CHECK(usage.framePassCount == 1 && usage.frameAccessCount == 3, "below the limit");
    CHECK(AddPass(nullptr, 0, &pass) == mrhi_success, "a pass without accesses");
    CHECK(AddPass(nullptr, 0, &pass) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFramePasses, 1),
          "a third pass");
    CHECK(Usage().framePassCount == 2, "the frame's passes");
    Drop();
    CloseDevice();
}

// A write, a read and a write again of one buffer: a barrier before
// each pass after the first.
static void ReadBetweenWrites(void)
{
    mrhiResourceId buffer = DeclareBuffer(4);
    mrhiResourceId other = DeclareBuffer(4);
    mrhiAccess write = Access(buffer, mrhi_accessCopyDestination);
    mrhiAccess read[2] = {
        Access(buffer, mrhi_accessCopySource),
        Access(other, mrhi_accessCopyDestination),
    };
    mrhiPassId pass = {0};
    CHECK(AddPass(&write, 1, &pass) == mrhi_success && AddPass(read, 2, &pass) == mrhi_success &&
              AddPass(&write, 1, &pass) == mrhi_success,
          "three passes");
}

// Two buffers written and read in turn, the second placed over the
// first's memory: its first use waits for the first's uses with a
// barrier of its own, planned after the others.
static void AliasedAfter(void)
{
    mrhiResourceId first = DeclareBuffer(4);
    mrhiResourceId second = DeclareBuffer(4);
    mrhiAccess accesses[4] = {
        Access(first, mrhi_accessCopyDestination),
        Access(first, mrhi_accessCopySource),
        Access(second, mrhi_accessCopyDestination),
        Access(second, mrhi_accessCopySource),
    };
    mrhiPassId pass = {0};
    for (uint32_t i = 0; i < 4; ++i)
    {
        CHECK(AddPass(&accesses[i], 1, &pass) == mrhi_success, "a pass");
    }
}

// Whether the compiled frame plans an aliasing barrier.
static bool PlansAliasing(void)
{
    mrhiBarrier barriers[16];
    size_t count = 0;
    bool aliasing = false;
    CHECK(mrhiDevice_GetFrameBarriers(s_device, barriers, 16, &count) == mrhi_success &&
              count <= 16,
          "the barriers");
    for (size_t i = 0; i < count; ++i)
    {
        aliasing = aliasing || barriers[i].aliasing;
    }
    return aliasing;
}

// A frame's barriers one past the limit, the last one planned by the
// resource or by the aliasing that follows.
static void CheckBarriers(void (*declare)(void), bool aliasing)
{
    Open(mrhiDefaultDeviceDef());
    Begin();
    declare();
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    uint32_t planned = Usage().frameBarrierCount;
    CHECK(planned >= 2 && PlansAliasing() == aliasing, "planned");
    Drop();
    CloseDevice();
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.frameBarriers = planned - 1;
    Open(def);
    Begin();
    declare();
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameBarriers, 1),
          "one barrier too many");
    CHECK(Usage().frameBarrierCount == planned - 1, "those planned before the refusal");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameBarriers, 1),
          "the submission's compile too");
    Drop();
    CloseDevice();
}

static void TestBarriers(void)
{
    CheckBarriers(ReadBetweenWrites, false);
    CheckBarriers(AliasedAfter, true);
}

// A full arena refuses the command, every later one of its pass and the
// submission, each under its limit.
static void TestCommands(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.frameCommandBytes = CHUNK_BYTES;
    Open(def);
    Begin();
    mrhiResourceId buffer = DeclareBuffer(4);
    mrhiPassId pass = Recording(buffer, mrhi_accessCopyDestination);
    mrhiResult status = mrhi_success;
    uint32_t cleared = 0;
    while (status == mrhi_success && cleared <= CHUNK_BYTES)
    {
        status = mrhiPass_ClearBuffer(s_device, pass, buffer, 0, 4);
        cleared += status == mrhi_success ? 1 : 0;
    }
    CHECK(status == mrhi_errorCapacity && cleared > 0 &&
              Refused(mrhi_diagnosticFrameCommandBytes, 1),
          "the arena full");
    CHECK(Usage().frameCommandBytes == CHUNK_BYTES, "all of it");
    CHECK(mrhiPass_ClearBuffer(s_device, pass, buffer, 0, 4) == mrhi_errorCapacity &&
              mrhiPass_WriteBuffer(s_device, pass, buffer, 0, s_bytes, 0) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameCommandBytes, 2),
          "the pass's later calls");
    CHECK(mrhiPass_End(s_device, pass) == mrhi_success, "ended");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameCommandBytes, 1),
          "the submission");
    CHECK(Usage().frameCommandBytes == CHUNK_BYTES, "after it");
    Drop();
    CloseDevice();
}

// A refused upload leaves the staging as it was for other passes; the
// last upload's boundary past the limit is not counted.
static void TestUploads(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.frameUploadBytes = 2052;
    Open(def);
    Begin();
    mrhiResourceId first = DeclareBuffer(2048);
    mrhiResourceId second = DeclareBuffer(2048);
    mrhiAccess firstAccess = Access(first, mrhi_accessCopyDestination);
    mrhiAccess secondAccess = Access(second, mrhi_accessCopyDestination);
    mrhiPassId firstPass = {0};
    mrhiPassId secondPass = {0};
    CHECK(AddPass(&firstAccess, 1, &firstPass) == mrhi_success &&
              AddPass(&secondAccess, 1, &secondPass) == mrhi_success,
          "two passes");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, firstPass) == mrhi_success &&
              mrhiPass_Begin(s_device, secondPass) == mrhi_success,
          "both recording");
    CHECK(mrhiPass_WriteBuffer(s_device, firstPass, first, 0, s_bytes, 1024) == mrhi_success,
          "half");
    CHECK(mrhiPass_WriteBuffer(s_device, firstPass, first, 0, s_bytes, 1032) ==
                  mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameUploadBytes, 1),
          "more than the rest");
    CHECK(Usage().frameUploadBytes == 1024, "the staging as it was");
    CHECK(mrhiPass_WriteBuffer(s_device, firstPass, first, 0, s_bytes, 4) == mrhi_errorCapacity &&
              mrhiPass_ClearBuffer(s_device, firstPass, first, 0, 4) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameUploadBytes, 2),
          "the pass's later calls");
    CHECK(mrhiPass_WriteBuffer(s_device, secondPass, second, 0, s_bytes, 1028) == mrhi_success,
          "the rest, for another pass");
    CHECK(Usage().frameUploadBytes == 2052, "all of it");
    CHECK(mrhiPass_End(s_device, firstPass) == mrhi_success &&
              mrhiPass_End(s_device, secondPass) == mrhi_success,
          "ended");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFrameUploadBytes, 1),
          "the submission");
    Drop();
    CloseDevice();
}

static void TestReadbacks(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.budgets.readbacks = 1;
    def.budgets.readbackBytes = 512;
    Open(def);
    mrhiBufferId buffer = MakeBuffer(1024);
    Begin();
    mrhiResourceId resource = ImportBuffer(buffer);
    mrhiPassId pass = Recording(resource, mrhi_accessCopySource);
    mrhiRequestId request;
    CHECK(mrhiPass_ReadBuffer(s_device, pass, resource, 0, 1024, &request) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticReadbackBytes, 1),
          "more than the ring");
    CHECK(mrhiPass_ReadBuffer(s_device, pass, resource, 0, 4, &request) == mrhi_success, "one");
    CHECK(mrhiPass_ReadBuffer(s_device, pass, resource, 0, 4, &request) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticReadbacks, 1),
          "a second");
    CHECK(mrhiPass_End(s_device, pass) == mrhi_success, "ended");
    Drop();
    CloseDevice();
}

static void TestFramesInFlight(void)
{
    Open(mrhiDefaultDeviceDef());
    mrhiRequestId token;
    for (int i = 0; i < 2; ++i)
    {
        Begin();
        CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "submitted");
    }
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticFramesInFlight, 1),
          "a third in flight");
    CloseDevice();
}

// The answers a frame and a readback are owed need room in the device's
// queue.
static void TestNotifications(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    // Two answers, and the record kept for the loss notice.
    def.budgets.notifications = 3;
    Open(def);
    mrhiBufferId buffer = MakeBuffer(1024);
    mrhiRequestId token;
    Begin();
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(s_device, token, 1) == mrhi_success,
          "one answer owed");
    Begin();
    mrhiResourceId resource = ImportBuffer(buffer);
    mrhiPassId pass = Recording(resource, mrhi_accessCopySource);
    mrhiRequestId request;
    CHECK(mrhiPass_ReadBuffer(s_device, pass, resource, 0, 4, &request) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticNotifications, 1),
          "no room for a readback and its frame");
    CHECK(mrhiPass_End(s_device, pass) == mrhi_success &&
              mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(s_device, token, 1) == mrhi_success,
          "two owed");
    Begin();
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticNotifications, 1),
          "no room for a third");
    Drop();
    CloseDevice();
}

// A driver that runs out of a budget at the submission names it; the
// frame closes, as at any refusal of its driver.
static void TestDriverBudget(void)
{
    s_adapter.submitBudget = mrhi_diagnosticSubmitDescriptors;
    Open(mrhiDefaultDeviceDef());
    Begin();
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity &&
              Refused(mrhi_diagnosticSubmitDescriptors, 1),
          "the driver's budget");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_errorState, "the frame closed");
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestBeforeFrames();
    TestCounts();
    TestLibraryLeftOut();
    TestResources();
    TestPasses();
    TestBarriers();
    TestCommands();
    TestUploads();
    TestReadbacks();
    TestFramesInFlight();
    TestNotifications();
    TestDriverBudget();
    return s_failures == 0 ? 0 : 1;
}
