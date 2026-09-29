// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "maul-rhi/base.h"

mrhiVersion mrhiGetVersion(void)
{
    return (mrhiVersion){MRHI_VERSION_MAJOR, MRHI_VERSION_MINOR, MRHI_VERSION_PATCH};
}

const char* mrhiResultName(mrhiResult result)
{
    switch (result)
    {
    case mrhi_success:
        return "mrhi_success";
    case mrhi_errorInvalid:
        return "mrhi_errorInvalid";
    case mrhi_errorCapacity:
        return "mrhi_errorCapacity";
    default:
        return "unknown result";
    }
}
