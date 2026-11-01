// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// D3D12 swapchains' acquired images, from a fake DXGI swapchain that
// answers only its back buffer's index: the image handed out is the
// index plus one, and an index past the swapchain's images loses the
// device instead of naming an image it does not have.

#include "d3d12_swapchain.h"
#include "test_harness.h"

static UINT s_backBuffer;

static UINT STDMETHODCALLTYPE BackBuffer(IDXGISwapChain3* self)
{
    (void)self;
    return s_backBuffer;
}

static void TestAcquired(void)
{
    IDXGISwapChain3Vtbl table = {.GetCurrentBackBufferIndex = BackBuffer};
    IDXGISwapChain3 fake = {.lpVtbl = &table};
    // No window, which is never minimized, and a wait already spent, so
    // the acquire reads the index at once.
    mrhiD3d12Swapchain swapchain = {.swapchain = &fake, .waited = true};
    uint32_t next[1];
    mrhiD3d12Swapchains swapchains = {.swapchains = &swapchain};
    (void)mrhiD3d12InitSlots(&swapchains.slots, next, 1);
    uint64_t handle = mrhiD3d12TakeSlot(&swapchains.slots);
    s_backBuffer = MRHI_D3D12_IMAGES - 1;
    uint64_t image = 0;
    mrhiResult status = mrhiD3d12Acquire(&swapchains, handle, &image);
    CHECK((status == mrhi_success || status == mrhi_suboptimal) && image == MRHI_D3D12_IMAGES,
          "the last image");
    s_backBuffer = MRHI_D3D12_IMAGES;
    image = 1;
    CHECK(mrhiD3d12Acquire(&swapchains, handle, &image) == mrhi_errorDeviceLost && image == 0,
          "an index past the images");
}

int main(void)
{
    TestAcquired();
    return s_failures == 0 ? 0 : 1;
}
