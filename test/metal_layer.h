// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A CAMetalLayer for the conformance suite and the samples on the Metal
// driver: a layer outside any window, which gives drawables all the
// same, sized as a window's view would size it.

#ifndef MAUL_RHI_TEST_METAL_LAYER_H
#define MAUL_RHI_TEST_METAL_LAYER_H

#include <stdint.h>

// A new layer of the given size in pixels, retained; NULL without one.
void* mrhiTestNewMetalLayer(uint32_t width, uint32_t height);

// Resizes a layer's drawables, as a view does when its window resizes.
void mrhiTestResizeMetalLayer(void* layer, uint32_t width, uint32_t height);

void mrhiTestReleaseMetalLayer(void* layer);

#endif // MAUL_RHI_TEST_METAL_LAYER_H
