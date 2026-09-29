// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A CAMetalLayer outside any window (metal_layer.h).

#include "metal_layer.h"

#import <QuartzCore/CAMetalLayer.h>

void* mrhiTestNewMetalLayer(uint32_t width, uint32_t height)
{
    CAMetalLayer* layer = nil;
    @autoreleasepool
    {
        layer = [[CAMetalLayer alloc] init];
        layer.bounds = CGRectMake(0, 0, width, height);
        layer.contentsScale = 1.0;
        layer.drawableSize = CGSizeMake(width, height);
    }
    return (void*)layer;
}

void mrhiTestResizeMetalLayer(void* layer, uint32_t width, uint32_t height)
{
    @autoreleasepool
    {
        CAMetalLayer* metal = (CAMetalLayer*)layer;
        metal.bounds = CGRectMake(0, 0, width, height);
        metal.drawableSize = CGSizeMake(width, height);
    }
}

void mrhiTestReleaseMetalLayer(void* layer)
{
    [(CAMetalLayer*)layer release];
}
