// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Format capabilities from the floor and the features.

#include "format_caps.h"

#include "capabilities_core.h"

mrhiFormatCaps mrhiGrantedFormatCaps(mrhiFormat format, const mrhiFeatures* features)
{
    mrhiFormatCaps caps = mrhiFloorFormatCaps(format);
    bool float32 = format == mrhi_formatR32Float || format == mrhi_formatRg32Float ||
                   format == mrhi_formatRgba32Float;
    caps.filtering = caps.filtering || (float32 && features->float32Filterable);
    // rg11b10ufloat-renderable renders, blends, multisamples and resolves
    // the format, as WebGPU's feature does.
    if (format == mrhi_formatRg11b10Ufloat && features->rg11b10Renderable)
    {
        caps.rendering = true;
        caps.blending = true;
        caps.sampleCounts |= mrhi_samples4Bit;
    }
    if (!caps.sampling && mrhiIsFormatKnown(format) && mrhiFormatFamilyGranted(format, features))
    {
        caps.sampling = true;
        caps.filtering = true;
        caps.sampleCounts = mrhi_samples1Bit;
    }
    return caps;
}
