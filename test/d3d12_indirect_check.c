// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Which buffer transitions an Agility SDK closes a list after, with no
// Maul RHI code: on WARP under the debug layer with GPU-based validation
// (test/d3d12_debug.c, with MAUL_RHI_D3D12_DEBUG set), each chain moves
// one new buffer, made in the default heap with unordered access as the
// driver makes its buffers, through its states on a direct list of its
// own, then closes the list. The 1.620.1 Agility SDK's validation makes
// Close return E_INVALIDARG, with no message, after the move from the
// indirect argument state alone to it and the non-pixel shader resource
// state, though the move is legal and the other chains close; 1.619.5
// closes every chain. The D3D12 driver gives a buffer read as indirect
// arguments both states from the start (src/d3d12_barrier.c), the chains
// marked "driver" here, so that it never asks for that move.
//
// usage: d3d12_indirect_check. Prints "indirect check:" with each
// chain's Close result, and exits 0 when every chain closes with no
// debug layer error, 1 when not, 2 when a step fails.

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "d3d12_debug.h"

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

#define COMMON      D3D12_RESOURCE_STATE_COMMON
#define COPY_DEST   D3D12_RESOURCE_STATE_COPY_DEST
#define COPY_SOURCE D3D12_RESOURCE_STATE_COPY_SOURCE
#define INDIRECT    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT
#define NON_PIXEL   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
#define PIXEL       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE

// A chain of states a buffer moves through, from the first.
typedef struct Chain
{
    const char* name;
    int count;
    D3D12_RESOURCE_STATES states[4];
} Chain;

static const Chain kChains[] = {
    {"indirect, then indirect and non-pixel (refused by 1.620.1)",
     4,
     {COMMON, COPY_DEST, INDIRECT, INDIRECT | NON_PIXEL}},
    {"driver: an upload, then indirect and non-pixel",
     3,
     {COMMON, COPY_DEST, INDIRECT | NON_PIXEL}},
    {"driver: indirect and non-pixel, then a copy source too",
     4,
     {COMMON, COPY_DEST, INDIRECT | NON_PIXEL, INDIRECT | NON_PIXEL | COPY_SOURCE}},
    {"driver: indirect and non-pixel from the common state", 2, {COMMON, INDIRECT | NON_PIXEL}},
    {"indirect, then indirect and a copy source",
     4,
     {COMMON, COPY_DEST, INDIRECT, INDIRECT | COPY_SOURCE}},
    {"indirect, then indirect and pixel", 4, {COMMON, COPY_DEST, INDIRECT, INDIRECT | PIXEL}},
    {"indirect, then indirect and both shader reads",
     4,
     {COMMON, COPY_DEST, INDIRECT, INDIRECT | NON_PIXEL | PIXEL}},
    {"non-pixel, then non-pixel and indirect",
     4,
     {COMMON, COPY_DEST, NON_PIXEL, NON_PIXEL | INDIRECT}},
};

// Moves a new buffer through a chain on a list of its own and closes
// it: Close's result, or E_FAIL when a step before it failed.
static HRESULT CloseAfter(ID3D12Device* device, const Chain* chain)
{
    static const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                      .Width = 256,
                                      .Height = 1,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .SampleDesc = {.Count = 1},
                                      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                      .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* buffer = nullptr;
    HRESULT closed = E_FAIL;
    if (SUCCEEDED(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      &IID_ID3D12CommandAllocator,
                                                      (void**)&allocator)) &&
        SUCCEEDED(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 allocator, nullptr, &IID_ID3D12GraphicsCommandList,
                                                 (void**)&list)) &&
        SUCCEEDED(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                       chain->states[0], nullptr,
                                                       &IID_ID3D12Resource, (void**)&buffer)))
    {
        for (int i = 1; i < chain->count; ++i)
        {
            const D3D12_RESOURCE_BARRIER barrier = {
                .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                .Transition = {.pResource = buffer,
                               .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                               .StateBefore = chain->states[i - 1],
                               .StateAfter = chain->states[i]},
            };
            ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &barrier);
        }
        closed = ID3D12GraphicsCommandList_Close(list);
    }
    if (buffer != nullptr)
    {
        ID3D12Resource_Release(buffer);
    }
    if (list != nullptr)
    {
        ID3D12GraphicsCommandList_Release(list);
    }
    if (allocator != nullptr)
    {
        ID3D12CommandAllocator_Release(allocator);
    }
    return closed;
}

int main(void)
{
    // Before any device, as the debug layer needs.
    mrhiTestWatchD3d12(true);
    IDXGIFactory4* factory = nullptr;
    IDXGIAdapter* warp = nullptr;
    ID3D12Device* device = nullptr;
    if (FAILED(CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&factory)) ||
        FAILED(IDXGIFactory4_EnumWarpAdapter(factory, &IID_IDXGIAdapter, (void**)&warp)) ||
        FAILED(D3D12CreateDevice((IUnknown*)warp, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device,
                                 (void**)&device)))
    {
        printf("indirect check: no WARP device\n");
        return 2;
    }
    bool closedAll = true;
    bool failedStep = false;
    for (size_t i = 0; i < sizeof(kChains) / sizeof(kChains[0]); ++i)
    {
        HRESULT closed = CloseAfter(device, &kChains[i]);
        printf("indirect check: Close 0x%08lx, %s\n", (unsigned long)closed, kChains[i].name);
        closedAll = closedAll && SUCCEEDED(closed);
        failedStep = failedStep || closed == E_FAIL;
    }
    uint32_t errors = mrhiTestD3d12Errors();
    printf("indirect check: %s, %u debug layer errors\n",
           closedAll ? "every chain closed" : "a chain did not close", errors);
    ID3D12Device_Release(device);
    IDXGIAdapter_Release(warp);
    IDXGIFactory4_Release(factory);
    if (failedStep)
    {
        return 2;
    }
    return closedAll && errors == 0 ? 0 : 1;
}
