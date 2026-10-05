// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Diagnostic queues (mrhi-0027): a record per refusal with the check's
// code, repeats folded into one record, the first records kept when the
// queue is full, none kept by default, the texts of the codes, and
// refusals recorded from several threads at once.

#include "test_device_setup.h"

#include "maul-rhi/resources.h"

#include <string.h>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__) && !defined(__wasi__)
#define TEST_THREADS
#include <pthread.h>
#endif

static mrhiDevice* Open(uint32_t diagnostics)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.diagnostics = diagnostics;
    return OpenWith(def, true);
}

// Refuses a buffer def: of a size not a multiple of 4, or of no usage.
static void Refuse(mrhiDevice* device, bool size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size ? 6 : 64;
    def.usage = size ? mrhi_bufferVertex : 0;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_errorInvalid, "refused");
}

static void TestDevice(void)
{
    mrhiDevice* device = Open(2);
    mrhiDiagnostic record = {0};
    CHECK(mrhiNextDeviceDiagnostic(device, &record) == mrhi_empty, "none yet");
    Refuse(device, true);
    Refuse(device, true);
    Refuse(device, false);
    // The queue is full: the next check's refusal is counted, not kept,
    // while a repeat of the newest still folds into it.
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(device, nullptr, &buffer) == mrhi_errorInvalid, "a third check");
    Refuse(device, false);
    CHECK(mrhiGetDeviceMisuse(device) == 5, "every refusal counted");
    CHECK(mrhiNextDeviceDiagnostic(device, &record) == mrhi_success &&
              record.code == mrhi_diagnosticBufferSize && record.count == 2,
          "the first check's two refusals, one record");
    CHECK(mrhiNextDeviceDiagnostic(device, &record) == mrhi_success &&
              record.code == mrhi_diagnosticBufferUsage && record.count == 2,
          "the second's");
    CHECK(mrhiNextDeviceDiagnostic(device, &record) == mrhi_empty, "the third's dropped");
    // Drained, the next refusal takes a record of its own again.
    Refuse(device, false);
    CHECK(mrhiNextDeviceDiagnostic(device, &record) == mrhi_success && record.count == 1,
          "a new record after draining");
    CHECK(mrhiNextDeviceDiagnostic(device, nullptr) == mrhi_errorInvalid &&
              mrhiNextDeviceDiagnostic(device, &record) == mrhi_success &&
              record.code == mrhi_diagnosticNullArgument,
          "no record pointer, itself recorded");
    CHECK(mrhiNextDeviceDiagnostic(nullptr, &record) == mrhi_errorInvalid, "no device");
    Close(device);
}

static void TestNoneByDefault(void)
{
    mrhiDevice* device = Open(0);
    Refuse(device, true);
    mrhiDiagnostic record = {0};
    CHECK(mrhiGetDeviceMisuse(device) == 1 &&
              mrhiNextDeviceDiagnostic(device, &record) == mrhi_empty,
          "counted, not recorded");
    CHECK(mrhiDefaultDeviceDef().deviceLimits.diagnostics == 0 &&
              mrhiDefaultInstanceDef().limits.diagnostics == 0,
          "both queues off by default");
    Close(device);
}

static void TestInstance(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.limits.diagnostics = 4;
    mrhiTestDriverDef driver = {.chain = {.next = nullptr, .type = mrhi_structTestDriver}};
    def.next = &driver.chain;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.cookie = 0;
    mrhiRequestId request;
    CHECK(mrhiRequestAdapters(instance, &search, &request) == mrhi_errorInvalid, "no cookie");
    CHECK(mrhiGetAdapters(instance, nullptr, 1, nullptr) == mrhi_errorInvalid, "no count");
    mrhiDiagnostic record = {0};
    CHECK(mrhiNextInstanceDiagnostic(instance, &record) == mrhi_success &&
              record.code == mrhi_diagnosticAdapterRequestDef && record.count == 1,
          "the request def's check");
    CHECK(mrhiNextInstanceDiagnostic(instance, &record) == mrhi_success &&
              record.code == mrhi_diagnosticNullArgument,
          "then the missing pointer");
    CHECK(mrhiNextInstanceDiagnostic(instance, &record) == mrhi_empty, "drained");
    CHECK(mrhiNextInstanceDiagnostic(instance, nullptr) == mrhi_errorInvalid &&
              mrhiGetInstanceMisuse(instance) == 3,
          "no record pointer");
    CHECK(mrhiNextInstanceDiagnostic(nullptr, &record) == mrhi_errorInvalid, "no instance");
    mrhiDestroyInstance(instance);
}

static void TestTexts(void)
{
    const char* text = mrhiDiagnosticText(mrhi_diagnosticBufferSize);
    CHECK(strcmp(text, "A buffer size of zero or not a multiple of 4.") == 0, "a code's text");
    CHECK(strcmp(mrhiDiagnosticText(0), "An unknown diagnostic code.") == 0, "zero");
    CHECK(strcmp(mrhiDiagnosticText(0xFFFFu), "An unknown diagnostic code.") == 0, "past them");
}

#ifdef TEST_THREADS
enum
{
    THREADS = 4,
    REFUSALS = 2000,
};

static void* RefuseMany(void* device)
{
    for (int i = 0; i < REFUSALS; ++i)
    {
        Refuse((mrhiDevice*)device, (i & 1) != 0);
    }
    return nullptr;
}

// Refusals from several threads at once: none lost from the records'
// counts while the queue has room.
static void TestThreads(void)
{
    mrhiDevice* device = Open(THREADS * REFUSALS);
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i)
    {
        CHECK(pthread_create(&threads[i], nullptr, RefuseMany, device) == 0, "a thread");
    }
    for (int i = 0; i < THREADS; ++i)
    {
        (void)pthread_join(threads[i], nullptr);
    }
    uint64_t total = 0;
    mrhiDiagnostic record;
    while (mrhiNextDeviceDiagnostic(device, &record) == mrhi_success)
    {
        total += record.count;
    }
    CHECK(total == THREADS * REFUSALS && mrhiGetDeviceMisuse(device) == total,
          "every refusal in a record");
    Close(device);
}
#endif

int main(void)
{
    ResetAdapter();
    TestDevice();
    TestNoneByDefault();
    TestInstance();
    TestTexts();
#ifdef TEST_THREADS
    TestThreads();
#endif
    return s_failures == 0 ? 0 : 1;
}
