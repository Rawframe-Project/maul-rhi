// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "test_harness.h"

#include "maul-rhi/base.h"

#include <string.h>

static void TestVersionMatchesHeader(void)
{
    mrhiVersion version = mrhiGetVersion();
    CHECK(version.major == MRHI_VERSION_MAJOR, "major version");
    CHECK(version.minor == MRHI_VERSION_MINOR, "minor version");
    CHECK(version.patch == MRHI_VERSION_PATCH, "patch version");
}

static void TestResultNames(void)
{
    CHECK(strcmp(mrhiResultName(mrhi_success), "mrhi_success") == 0, "success name");
    CHECK(strcmp(mrhiResultName(mrhi_errorInvalid), "mrhi_errorInvalid") == 0, "invalid name");
    CHECK(strcmp(mrhiResultName(mrhi_errorCapacity), "mrhi_errorCapacity") == 0, "capacity name");
    CHECK(strcmp(mrhiResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
