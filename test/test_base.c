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
        {mrhi_errorFormat, "mrhi_errorFormat"},
        {mrhi_errorLimit, "mrhi_errorLimit"},
        {mrhi_errorBusy, "mrhi_errorBusy"},
        {mrhi_errorOutOfDate, "mrhi_errorOutOfDate"},
        {mrhi_errorDeviceLost, "mrhi_errorDeviceLost"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        CHECK(strcmp(mrhiGetResultName(names[i].result), names[i].name) == 0, names[i].name);
    }
    CHECK(strcmp(mrhiGetResultName(12345), "unknown result") == 0, "unknown name");
}

// The family's numbering: the shared errors at their values, so a raw
// status reads the same in every Maul library, and the library's own
// from -32 down.
static void TestResultValues(void)
{
    static const struct
    {
        mrhiResult result;
        int value;
    } values[] = {
        {mrhi_errorInvalid, -1},     {mrhi_errorCapacity, -2},   {mrhi_errorStale, -3},
        {mrhi_errorUnsupported, -4}, {mrhi_errorPlatform, -5},   {mrhi_errorState, -6},
        {mrhi_errorVersion, -7},     {mrhi_errorFormat, -8},     {mrhi_errorLimit, -9},
        {mrhi_errorBusy, -10},       {mrhi_errorOutOfDate, -32}, {mrhi_errorDeviceLost, -33},
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        CHECK(values[i].result == values[i].value, mrhiGetResultName(values[i].result));
    }
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultValues();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
