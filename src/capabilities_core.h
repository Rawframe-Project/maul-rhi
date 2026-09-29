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

// The formats the contract lists, in order.
#define MRHI_KNOWN_FORMATS 70
extern const mrhiFormat mrhiKnownFormats[MRHI_KNOWN_FORMATS];

// Whether a value is a format the contract lists.
bool mrhiIsFormatKnown(mrhiFormat format);

// What every adapter can do with a format: WebGPU's guaranteed
// capabilities; nothing for the compressed families.
mrhiFormatCaps mrhiFloorFormatCaps(mrhiFormat format);

// Whether the feature a format's family needs is granted; true for a
// format without a family.
bool mrhiFormatFamilyGranted(mrhiFormat format, const mrhiFeatures* features);

// Whether every capability asked for is granted.
bool mrhiFormatCapsWithin(const mrhiFormatCaps* asked, const mrhiFormatCaps* granted);

#endif // MAUL_RHI_SRC_CAPABILITIES_CORE_H
