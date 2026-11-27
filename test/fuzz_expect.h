// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A fuzz target's promise. When one breaks, a single line names it and
// the run stops: the "FILE:LINE: expectation failed: ..." line that
// tools/fuzz_target.sh keeps in a finding's report. The line holds the
// promise's own text, never the input's bytes.

#ifndef MAUL_RHI_TEST_FUZZ_EXPECT_H
#define MAUL_RHI_TEST_FUZZ_EXPECT_H

#include <stdio.h>
#include <stdlib.h>

#define Expect(condition) FuzzExpect((condition), __FILE_NAME__, __LINE__, #condition)

static inline void FuzzExpect(bool condition, const char* file, int line, const char* promise)
{
    if (!condition)
    {
        fprintf(stderr, "test/%s:%d: expectation failed: %s\n", file, line, promise);
        fflush(stderr);
        abort();
    }
}

#endif
