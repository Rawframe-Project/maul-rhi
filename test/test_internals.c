// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Internals at limits no public call reaches in a test: a diagnostic's
// repeat count held at UINT32_MAX, and the C library's allocation
// serving max_align_t's own alignment.

#include "allocator.h"
#include "diagnostics.h"
#include "test_harness.h"

#include <stdalign.h>
#include <stddef.h>

static void TestRepeatCount(void)
{
    mrhiDiagnostic records[2];
    mrhiDiagnosticQueue queue;
    mrhiInitDiagnostics(&queue, records, 2);
    mrhiRecordDiagnostic(&queue, mrhi_diagnosticNullArgument);
    records[0].count = UINT32_MAX - 1;
    mrhiRecordDiagnostic(&queue, mrhi_diagnosticNullArgument);
    mrhiRecordDiagnostic(&queue, mrhi_diagnosticNullArgument);
    mrhiDiagnostic record = {0};
    CHECK(mrhiTakeDiagnostic(&queue, &record) == mrhi_success &&
              record.code == mrhi_diagnosticNullArgument && record.count == UINT32_MAX,
          "repeats counted up to UINT32_MAX and held there");
    CHECK(mrhiTakeDiagnostic(&queue, &record) == mrhi_empty, "folded into one record");
}

static void TestLibraryAlignment(void)
{
    mrhiAllocator allocator = {0};
    void* memory = mrhiAllocate(&allocator, 64, alignof(max_align_t));
    CHECK(memory != nullptr, "max_align_t's alignment served");
    mrhiRelease(&allocator, memory, 64, alignof(max_align_t));
    CHECK(mrhiAllocate(&allocator, 64, alignof(max_align_t) * 2) == nullptr,
          "a larger one refused");
}

int main(void)
{
    TestRepeatCount();
    TestLibraryAlignment();
    return s_failures == 0 ? 0 : 1;
}
