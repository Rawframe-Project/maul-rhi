// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Whether GPU-based validation tracks a texture's state right on this
// machine, with no Maul RHI code (#7): the commands the D3D12 driver
// records for the conformance suite's storage bindings, in raw D3D12 on
// the first adapter under the debug layer with GPU-based validation
// (test/d3d12_debug.c, with MAUL_RHI_D3D12_DEBUG set). Each frame makes a 4 by 1 rgba8 texture in
// the common state, moves it to unordered access, writes it in one dispatch through a descriptor
// table of three data-volatile ranges and copies it out. Rounds make a queue, heaps, a root
// signature, a pipeline and two frame slots on the adapter's one D3D12 device and release them
// again, as each device the library opens does. With rounds, the state tracking of the 1.619
// Agility SDK on WARP sometimes reports the texture still in the common state at the dispatch,
// though every value comes out right: about 1 run in 70 of "d3d12_uav_check 2 4".
// MAUL_RHI_D3D12_GBV_STATES=0 turns the state tracking off, and the
// error with it.
//
// usage: d3d12_uav_check [frames [rounds]]: one frame in one round by
// default. Prints "uav check:" with whether the values came out right
// and the debug layer's error count, and exits 0 when both are clean, 1
// when not, 2 when a step fails.

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "d3d12_debug.h"

#include "shaders/storage_container.h"

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define CHECKED(call)                                                                              \
    do                                                                                             \
    {                                                                                              \
        HRESULT hr_ = (call);                                                                      \
        if (FAILED(hr_))                                                                           \
        {                                                                                          \
            printf("uav check: %s failed 0x%08lx\n", #call, (unsigned long)hr_);                   \
            return 2;                                                                              \
        }                                                                                          \
    } while (0)

static ID3D12Resource* Buffer(ID3D12Device* device, D3D12_HEAP_TYPE type, UINT64 size,
                              D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state)
{
    const D3D12_HEAP_PROPERTIES heap = {.Type = type};
    const D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = flags,
    };
    ID3D12Resource* resource = nullptr;
    if (FAILED(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    state, nullptr, &IID_ID3D12Resource,
                                                    (void**)&resource)))
    {
        return nullptr;
    }
    return resource;
}

static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after)
{
    return (D3D12_RESOURCE_BARRIER){
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = resource,
                       .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                       .StateBefore = before,
                       .StateAfter = after},
    };
}

// What a frame slot keeps: its allocator and list, its shader-visible
// heaps, and the frame's texture, made at each submit and released once
// the slot's frame finished, as the driver's transients are.
typedef struct Slot
{
    ID3D12CommandAllocator* allocator;
    ID3D12GraphicsCommandList* list;
    ID3D12DescriptorHeap* views;
    ID3D12DescriptorHeap* samplers;
    ID3D12Resource* texture;
    ID3D12Resource* inputs;
    ID3D12Resource* outputs;
    UINT64 serial;
} Slot;

static ID3D12Device* s_device;
static ID3D12RootSignature* s_root;
static ID3D12PipelineState* s_pipeline;
static ID3D12Resource* s_upload;
static ID3D12Resource* s_readback;

static int MakeSlot(Slot* slot)
{
    CHECKED(ID3D12Device_CreateCommandAllocator(s_device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                &IID_ID3D12CommandAllocator,
                                                (void**)&slot->allocator));
    CHECKED(ID3D12Device_CreateCommandList(s_device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           slot->allocator, nullptr, &IID_ID3D12GraphicsCommandList,
                                           (void**)&slot->list));
    CHECKED(ID3D12GraphicsCommandList_Close(slot->list));
    const D3D12_DESCRIPTOR_HEAP_DESC viewDesc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                 .NumDescriptors = 64,
                                                 .Flags =
                                                     D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    const D3D12_DESCRIPTOR_HEAP_DESC samplerDesc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                    .NumDescriptors = 16,
                                                    .Flags =
                                                        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    CHECKED(ID3D12Device_CreateDescriptorHeap(s_device, &viewDesc, &IID_ID3D12DescriptorHeap,
                                              (void**)&slot->views));
    CHECKED(ID3D12Device_CreateDescriptorHeap(s_device, &samplerDesc, &IID_ID3D12DescriptorHeap,
                                              (void**)&slot->samplers));
    return 0;
}

static void Release(ID3D12Resource** resource)
{
    if (*resource != nullptr)
    {
        ID3D12Resource_Release(*resource);
        *resource = nullptr;
    }
}

// Records and submits one frame in a slot: the copy pass, the dispatch
// pass (the texture's barrier at its start, the buffers' at the
// dispatch), then the read pass, as the driver records the suite's
// storage bindings.
static int Frame(ID3D12CommandQueue* queue, ID3D12Fence* fence, Slot* slot, UINT64 serial)
{
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
    const D3D12_RESOURCE_DESC textureDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width = 4,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1},
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
    };
    CHECKED(ID3D12Device_CreateCommittedResource(s_device, &heap, D3D12_HEAP_FLAG_NONE,
                                                 &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 &IID_ID3D12Resource, (void**)&slot->texture));
    slot->inputs = Buffer(s_device, D3D12_HEAP_TYPE_DEFAULT, 16,
                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    slot->outputs = Buffer(s_device, D3D12_HEAP_TYPE_DEFAULT, 16,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    if (slot->inputs == nullptr || slot->outputs == nullptr)
    {
        printf("uav check: no buffers\n");
        return 2;
    }
    ID3D12GraphicsCommandList* list = slot->list;
    CHECKED(ID3D12CommandAllocator_Reset(slot->allocator));
    CHECKED(ID3D12GraphicsCommandList_Reset(list, slot->allocator, nullptr));
    ID3D12DescriptorHeap* heaps[2] = {slot->views, slot->samplers};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 2, heaps);
    D3D12_RESOURCE_BARRIER barriers[2] = {
        Transition(slot->inputs, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)};
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, barriers);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, slot->inputs, 0, s_upload, 0, 16);
    barriers[0] = Transition(slot->texture, D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, barriers);
    ID3D12GraphicsCommandList_SetPipelineState(list, s_pipeline);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, s_root);
    UINT step = ID3D12Device_GetDescriptorHandleIncrementSize(
        s_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    (void)ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot->views, &cpu);
    (void)ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot->views, &gpu);
    const D3D12_SHADER_RESOURCE_VIEW_DESC srv = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {.NumElements = 4, .Flags = D3D12_BUFFER_SRV_FLAG_RAW},
    };
    ID3D12Device_CreateShaderResourceView(s_device, slot->inputs, &srv, cpu);
    const D3D12_UNORDERED_ACCESS_VIEW_DESC raw = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {.NumElements = 4, .Flags = D3D12_BUFFER_UAV_FLAG_RAW},
    };
    ID3D12Device_CreateUnorderedAccessView(s_device, slot->outputs, nullptr, &raw,
                                           (D3D12_CPU_DESCRIPTOR_HANDLE){cpu.ptr + step});
    const D3D12_UNORDERED_ACCESS_VIEW_DESC image = {
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
    };
    ID3D12Device_CreateUnorderedAccessView(s_device, slot->texture, nullptr, &image,
                                           (D3D12_CPU_DESCRIPTOR_HANDLE){cpu.ptr + 2 * step});
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpu);
    barriers[0] = Transition(slot->inputs, D3D12_RESOURCE_STATE_COPY_DEST,
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = Transition(slot->outputs, D3D12_RESOURCE_STATE_COMMON,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_ResourceBarrier(list, 2, barriers);
    ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
    barriers[0] = Transition(slot->texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);
    barriers[1] = Transition(slot->outputs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_ResourceBarrier(list, 2, barriers);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, s_readback, 0, slot->outputs, 0, 16);
    const D3D12_TEXTURE_COPY_LOCATION to = {
        .pResource = s_readback,
        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = {.Offset = 256,
                            .Footprint = {.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                          .Width = 4,
                                          .Height = 1,
                                          .Depth = 1,
                                          .RowPitch = 256}},
    };
    const D3D12_TEXTURE_COPY_LOCATION from = {
        .pResource = slot->texture,
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
    };
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &to, 0, 0, 0, &from, nullptr);
    CHECKED(ID3D12GraphicsCommandList_Close(list));
    ID3D12CommandList* lists[1] = {(ID3D12CommandList*)list};
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    CHECKED(ID3D12CommandQueue_Signal(queue, fence, serial));
    slot->serial = serial;
    return 0;
}

static void Wait(ID3D12Fence* fence, HANDLE event, UINT64 serial)
{
    if (ID3D12Fence_GetCompletedValue(fence) < serial)
    {
        (void)ID3D12Fence_SetEventOnCompletion(fence, serial, event);
        (void)WaitForSingleObject(event, INFINITE);
    }
}

static void ReleaseSlot(Slot* slot)
{
    Release(&slot->texture);
    Release(&slot->inputs);
    Release(&slot->outputs);
    ID3D12GraphicsCommandList_Release(slot->list);
    ID3D12CommandAllocator_Release(slot->allocator);
    ID3D12DescriptorHeap_Release(slot->views);
    ID3D12DescriptorHeap_Release(slot->samplers);
}

// The storage container's compute DXIL: its one DXBC part, which opens
// with that tag and gives its size at byte 24. Empty when there is none.
static D3D12_SHADER_BYTECODE StorageDxil(void)
{
    const uint8_t* bytes = s_storageContainer;
    size_t size = sizeof(s_storageContainer);
    for (size_t at = 0; at + 28 <= size; at += 4)
    {
        if (memcmp(bytes + at, "DXBC", 4) == 0)
        {
            uint32_t length = 0;
            memcpy(&length, bytes + at + 24, sizeof(length));
            if (length <= size - at)
            {
                return (D3D12_SHADER_BYTECODE){bytes + at, length};
            }
        }
    }
    return (D3D12_SHADER_BYTECODE){0};
}

// One round: what a library device makes on the adapter's one D3D12
// device, its frames, and everything released again but the device.
static int Round(int frames)
{
    const D3D12_COMMAND_QUEUE_DESC queueDesc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandQueue* queue = nullptr;
    CHECKED(ID3D12Device_CreateCommandQueue(s_device, &queueDesc, &IID_ID3D12CommandQueue,
                                            (void**)&queue));

    // The root signature the driver makes for the storage container: one
    // table of three volatile-data ranges, t0, u1 and u2.
    const D3D12_DESCRIPTOR_RANGE1 ranges[3] = {
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
         .NumDescriptors = 1,
         .BaseShaderRegister = 0,
         .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
         .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
         .NumDescriptors = 1,
         .BaseShaderRegister = 1,
         .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
         .OffsetInDescriptorsFromTableStart = 1},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
         .NumDescriptors = 1,
         .BaseShaderRegister = 2,
         .Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
         .OffsetInDescriptorsFromTableStart = 2},
    };
    const D3D12_ROOT_PARAMETER1 parameter = {
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable = {.NumDescriptorRanges = 3, .pDescriptorRanges = ranges},
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
    };
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {
        .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1 = {.NumParameters = 1, .pParameters = &parameter},
    };
    ID3DBlob* serialized = nullptr;
    CHECKED(D3D12SerializeVersionedRootSignature(&rootDesc, &serialized, nullptr));
    CHECKED(ID3D12Device_CreateRootSignature(s_device, 0, ID3D10Blob_GetBufferPointer(serialized),
                                             ID3D10Blob_GetBufferSize(serialized),
                                             &IID_ID3D12RootSignature, (void**)&s_root));
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {
        .pRootSignature = s_root,
        .CS = StorageDxil(),
    };
    CHECKED(ID3D12Device_CreateComputePipelineState(s_device, &pipelineDesc,
                                                    &IID_ID3D12PipelineState, (void**)&s_pipeline));
    s_upload = Buffer(s_device, D3D12_HEAP_TYPE_UPLOAD, 16, D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ);
    s_readback = Buffer(s_device, D3D12_HEAP_TYPE_READBACK, 512, D3D12_RESOURCE_FLAG_NONE,
                        D3D12_RESOURCE_STATE_COPY_DEST);
    if (s_upload == nullptr || s_readback == nullptr)
    {
        printf("uav check: no buffers\n");
        return 2;
    }
    const UINT values[4] = {0, 51, 102, 255};
    void* mapped = nullptr;
    CHECKED(ID3D12Resource_Map(s_upload, 0, nullptr, &mapped));
    memcpy(mapped, values, sizeof(values));
    ID3D12Resource_Unmap(s_upload, 0, nullptr);

    ID3D12Fence* fence = nullptr;
    CHECKED(ID3D12Device_CreateFence(s_device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                     (void**)&fence));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Slot slots[2] = {0};
    for (int i = 0; i < 2; ++i)
    {
        int made = MakeSlot(&slots[i]);
        if (made != 0)
        {
            return made;
        }
    }
    for (int f = 0; f < frames; ++f)
    {
        Slot* slot = &slots[f % 2];
        // A slot is reused only once its frame finished; its resources go
        // then, as the driver's transients do.
        Wait(fence, event, slot->serial);
        Release(&slot->texture);
        Release(&slot->inputs);
        Release(&slot->outputs);
        int done = Frame(queue, fence, slot, (UINT64)f + 1);
        if (done != 0)
        {
            return done;
        }
    }
    Wait(fence, event, (UINT64)frames);

    CHECKED(ID3D12Resource_Map(s_readback, 0, nullptr, &mapped));
    const UINT* doubled = mapped;
    const unsigned char* red = (const unsigned char*)mapped + 256;
    bool right = doubled[0] == 1 && doubled[1] == 103 && doubled[2] == 205 && doubled[3] == 511 &&
                 red[0] == 0 && red[4] == 51 && red[8] == 102 && red[12] == 255;
    ID3D12Resource_Unmap(s_readback, 0, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        ReleaseSlot(&slots[i]);
    }
    CloseHandle(event);
    ID3D12Fence_Release(fence);
    Release(&s_upload);
    Release(&s_readback);
    ID3D12PipelineState_Release(s_pipeline);
    ID3D12RootSignature_Release(s_root);
    ID3D10Blob_Release(serialized);
    ID3D12CommandQueue_Release(queue);
    return right ? 0 : 1;
}

// Runs the rounds of frames the usage above names. Within a round,
// frames alternate between two slots and each is submitted while the
// one before it may still run, as the driver submits.
int main(int argc, char** argv)
{
    int frames = argc > 1 ? (int)strtol(argv[1], nullptr, 10) : 1;
    int rounds = argc > 2 ? (int)strtol(argv[2], nullptr, 10) : 1;
    mrhiTestWatchD3d12(true);
    IDXGIFactory4* factory = nullptr;
    CHECKED(CreateDXGIFactory2(0, &IID_IDXGIFactory4, (void**)&factory));
    IDXGIAdapter1* adapter = nullptr;
    CHECKED(IDXGIFactory4_EnumAdapters1(factory, 0, &adapter));
    CHECKED(D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device,
                              (void**)&s_device));
    bool right = true;
    for (int r = 0; r < rounds; ++r)
    {
        int done = Round(frames);
        if (done > 1)
        {
            return done;
        }
        right = right && done == 0;
    }
    uint32_t errors = mrhiTestD3d12Errors();
    printf("uav check: %s, %u debug layer errors\n", right ? "values right" : "values wrong",
           (unsigned)errors);
    return errors == 0 && right ? 0 : 1;
}
