// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 debug layer for the suites (d3d12_debug.h). The exe exports
// the Agility SDK it runs on, from D3D12/ beside it where CI places
// the SDK's NuGet package (the version the directx/ headers are, 619),
// falling back to the system's runtime without it. The watched devices
// are held until the process ends, so that each adapter keeps the one
// device the library then opens.

// getenv is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS
#define INITGUID
#include "d3d12_debug.h"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <directx/d3d12.h>
#include <directx/d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

__declspec(dllexport) const UINT D3D12SDKVersion = 619;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";

static uint32_t s_errors;

static void __stdcall OnMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity,
                                D3D12_MESSAGE_ID id, LPCSTR description, void* context)
{
    (void)category;
    (void)context;
    if (severity <= D3D12_MESSAGE_SEVERITY_ERROR)
    {
        printf("FAIL: D3D12 debug layer, message %d: %s\n", (int)id, description);
        ++s_errors;
    }
}

// Watches a device's messages: through a callback where the runtime has
// one, otherwise by breaking on errors, which ends the run.
static void Watch(ID3D12Device* device)
{
    ID3D12InfoQueue1* callbacks = nullptr;
    if (SUCCEEDED(ID3D12Device_QueryInterface(device, &IID_ID3D12InfoQueue1, (void**)&callbacks)))
    {
        DWORD cookie = 0;
        (void)ID3D12InfoQueue1_RegisterMessageCallback(
            callbacks, OnMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
        ID3D12InfoQueue1_Release(callbacks);
        return;
    }
    ID3D12InfoQueue* queue = nullptr;
    if (SUCCEEDED(ID3D12Device_QueryInterface(device, &IID_ID3D12InfoQueue, (void**)&queue)))
    {
        (void)ID3D12InfoQueue_SetBreakOnSeverity(queue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        (void)ID3D12InfoQueue_SetBreakOnSeverity(queue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D12InfoQueue_Release(queue);
    }
}

void mrhiTestWatchD3d12(void)
{
    const char* asked = getenv("MAUL_RHI_D3D12_DEBUG");
    if (asked == nullptr || asked[0] == '\0')
    {
        return;
    }
    ID3D12Debug* debug = nullptr;
    if (FAILED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void**)&debug)))
    {
        printf("FAIL: no D3D12 debug layer\n");
        ++s_errors;
        return;
    }
    ID3D12Debug_EnableDebugLayer(debug);
    ID3D12Debug1* validation = nullptr;
    if (SUCCEEDED(ID3D12Debug_QueryInterface(debug, &IID_ID3D12Debug1, (void**)&validation)))
    {
        ID3D12Debug1_SetEnableGPUBasedValidation(validation, TRUE);
        ID3D12Debug1_Release(validation);
    }
    ID3D12Debug_Release(debug);
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&factory)))
    {
        return;
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; IDXGIFactory4_EnumAdapters1(factory, i, &adapter) == S_OK; ++i)
    {
        ID3D12Device* device = nullptr;
        if (SUCCEEDED(D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_12_0,
                                        &IID_ID3D12Device, (void**)&device)))
        {
            Watch(device);
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory4_Release(factory);
}

uint32_t mrhiTestD3d12Errors(void)
{
    return s_errors;
}
