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
    static const struct
    {
        mrhiResult result;
        const char* name;
    } names[] = {
        {mrhi_success, "mrhi_success"},
        {mrhi_errorInvalid, "mrhi_errorInvalid"},
        {mrhi_errorCapacity, "mrhi_errorCapacity"},
        {mrhi_errorStale, "mrhi_errorStale"},
        {mrhi_errorUnsupported, "mrhi_errorUnsupported"},
        {mrhi_errorPlatform, "mrhi_errorPlatform"},
        {mrhi_errorState, "mrhi_errorState"},
        {mrhi_errorVersion, "mrhi_errorVersion"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        CHECK(strcmp(mrhiResultName(names[i].result), names[i].name) == 0, names[i].name);
    }
    CHECK(strcmp(mrhiResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
