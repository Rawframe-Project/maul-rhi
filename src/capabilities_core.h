// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The core's checks of features and limits, generated from the
// contract in generated/capabilities.c.

#ifndef MAUL_RHI_SRC_CAPABILITIES_CORE_H
#define MAUL_RHI_SRC_CAPABILITIES_CORE_H

#include "maul-rhi/capabilities.h"

// Whether every feature asked for is granted.
bool mrhiFeaturesWithin(const mrhiFeatures* asked, const mrhiFeatures* granted);

// Whether every limit asked for is within the grant: at most it, or at
// least it for a limit that is better lower (the alignments).
bool mrhiLimitsWithin(const mrhiLimits* asked, const mrhiLimits* granted);

// Clears the features a driver's API cannot grant: the rows the
// contract classes absent-rejected.
void mrhiMaskFeatures(mrhiFeatures* features, mrhiDriverKind driver);

#endif // MAUL_RHI_SRC_CAPABILITIES_CORE_H
