// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A D3D12 device's frames (d3d12_frame.h). Frames run in submission
// order on one queue, each signalling the fence with its serial, so a
// poll reads one counter to know every frame finished, and a slot is
// reused only once the core has been told its frame finished. A frame's
// transients are committed resources of their own, made at its submit
// and released when it finishes; the core's placement of them is not
// used yet.

#include "d3d12_frame.h"

#include "allocator.h"
#include "d3d12_barrier.h"
#include "d3d12_record.h"
#include "invariant.h"

#include <stdalign.h>
#include <string.h>

// Makes a buffer in an upload or readback heap, in the state that heap
// keeps, and maps it: nullptr when D3D12 makes none.
static ID3D12Resource* MakeMapped(ID3D12Device* device, D3D12_HEAP_TYPE type, uint64_t size,
                                  uint8_t** bytesOut)
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
    };
    D3D12_RESOURCE_STATES state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                                                 : D3D12_RESOURCE_STATE_COPY_DEST;
    ID3D12Resource* resource = nullptr;
    if (FAILED(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    state, nullptr, &IID_ID3D12Resource,
                                                    (void**)&resource)))
    {
        return nullptr;
    }
    // The host never reads an upload buffer, and reads the readback
    // buffer whole.
    const D3D12_RANGE none = {0, 0};
    void* mapped = nullptr;
    if (FAILED(ID3D12Resource_Map(resource, 0, type == D3D12_HEAP_TYPE_UPLOAD ? &none : nullptr,
                                  &mapped)))
    {
        ID3D12Resource_Release(resource);
        return nullptr;
    }
    *bytesOut = mapped;
    return resource;
}

static ID3D12DescriptorHeap* MakeHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                      uint32_t count)
{
    const D3D12_DESCRIPTOR_HEAP_DESC desc = {.Type = type, .NumDescriptors = count};
    ID3D12DescriptorHeap* heap = nullptr;
    return SUCCEEDED(ID3D12Device_CreateDescriptorHeap(device, &desc, &IID_ID3D12DescriptorHeap,
                                                       (void**)&heap))
               ? heap
               : nullptr;
}

mrhiD3d12FrameRoom mrhiD3d12PlanFrames(mrhiLayout* layout, const mrhiDeviceLimits* limits,
                                       uint32_t slots)
{
    mrhiD3d12FrameRoom room = {0};
    room.slots = mrhiLayoutAdd(layout, slots, sizeof(mrhiD3d12Slot), alignof(mrhiD3d12Slot));
    room.table = mrhiLayoutAdd(layout, limits->frameResources, sizeof(mrhiD3d12Object),
                               alignof(mrhiD3d12Object));
    room.transients = mrhiLayoutAdd(layout, (size_t)slots * limits->frameResources,
                                    sizeof(ID3D12Resource*), alignof(ID3D12Resource*));
    room.readbacks = mrhiLayoutAdd(layout, (size_t)slots * limits->readbacks,
                                   sizeof(mrhiD3d12Range), alignof(mrhiD3d12Range));
    uint64_t retirees = (uint64_t)limits->buffers + limits->textures + limits->views +
                        limits->samplers + limits->querySets + limits->pipelines;
    room.retireCapacity = retirees < UINT32_MAX ? (uint32_t)retirees : UINT32_MAX;
    room.retirees = mrhiLayoutAdd(layout, room.retireCapacity, sizeof(mrhiD3d12Retiree),
                                  alignof(mrhiD3d12Retiree));
    return room;
}

void mrhiD3d12LayFrames(mrhiD3d12Frames* frames, unsigned char* block,
                        const mrhiD3d12FrameRoom* room, const mrhiDeviceLimits* limits,
                        uint32_t slots)
{
    frames->slots = (mrhiD3d12Slot*)(block + room->slots);
    frames->slotCount = slots;
    frames->table = (mrhiD3d12Object*)(block + room->table);
    frames->resourceLimit = limits->frameResources;
    // Each pass takes a view of each color target and one of its depth
    // target at most.
    frames->targetLimit = limits->framePasses * MRHI_COLOR_TARGETS;
    frames->depthLimit = limits->framePasses;
    frames->readbackLimit = limits->readbacks;
    frames->uploadBytes = limits->frameUploadBytes;
    frames->readbackSize = limits->readbackBytes;
    frames->retirees = (mrhiD3d12Retiree*)(block + room->retirees);
    frames->retireCapacity = room->retireCapacity;
    ID3D12Resource** transients = (ID3D12Resource**)(block + room->transients);
    mrhiD3d12Range* readbacks = (mrhiD3d12Range*)(block + room->readbacks);
    for (uint32_t i = 0; i < slots; ++i)
    {
        frames->slots[i] = (mrhiD3d12Slot){
            .transients = transients + (size_t)i * limits->frameResources,
            .readbacks = readbacks + (size_t)i * limits->readbacks,
        };
    }
}

static bool OpenSlot(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot)
{
    ID3D12Device* device = frames->device;
    if (FAILED(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   &IID_ID3D12CommandAllocator,
                                                   (void**)&slot->allocator)) ||
        FAILED(ID3D12Device_CreateCommandList(
            device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot->allocator, nullptr,
            &IID_ID3D12GraphicsCommandList, (void**)&slot->list)) ||
        FAILED(ID3D12GraphicsCommandList_Close(slot->list)))
    {
        return false;
    }
    slot->targetHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                frames->targetLimit > 0 ? frames->targetLimit : 1);
    slot->depthHeap = MakeHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                               frames->depthLimit > 0 ? frames->depthLimit : 1);
    if (frames->uploadBytes > 0)
    {
        slot->staging =
            MakeMapped(device, D3D12_HEAP_TYPE_UPLOAD, frames->uploadBytes, &slot->stagingBytes);
    }
    return slot->targetHeap != nullptr && slot->depthHeap != nullptr &&
           (frames->uploadBytes == 0 || slot->staging != nullptr);
}

mrhiResult mrhiD3d12OpenFrames(mrhiD3d12Frames* frames)
{
    if (FAILED(ID3D12Device_CreateFence(frames->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
                                        (void**)&frames->fence)))
    {
        return mrhi_errorCapacity;
    }
    frames->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool made = frames->event != nullptr;
    for (uint32_t i = 0; i < frames->slotCount && made; ++i)
    {
        made = OpenSlot(frames, &frames->slots[i]);
    }
    if (made && frames->readbackSize > 0)
    {
        frames->readback = MakeMapped(frames->device, D3D12_HEAP_TYPE_READBACK,
                                      frames->readbackSize, &frames->readbackBytes);
        made = frames->readback != nullptr;
    }
    return made ? mrhi_success : mrhi_errorCapacity;
}

// Releases a slot's transients.
static void DropTransients(mrhiD3d12Slot* slot)
{
    for (uint32_t i = 0; i < slot->transientCount; ++i)
    {
        if (slot->transients[i] != nullptr)
        {
            ID3D12Resource_Release(slot->transients[i]);
            slot->transients[i] = nullptr;
        }
    }
    slot->transientCount = 0;
}

static void Retire(mrhiD3d12Frames* frames, const mrhiD3d12Retiree* retiree)
{
    if (retiree->kind == mrhiD3d12KindPipeline)
    {
        mrhiD3d12ReleasePipeline(frames->pipelines, retiree->handle);
    }
    else
    {
        mrhiD3d12ReleaseObject(frames->objects, retiree->kind, retiree->handle);
    }
}

// Releases the objects whose frame has finished: all of them with no
// frame left to wait for.
static void RetireUpTo(mrhiD3d12Frames* frames, uint64_t serial)
{
    while (frames->retireCount > 0 && frames->retirees[frames->retireFirst].serial <= serial)
    {
        Retire(frames, &frames->retirees[frames->retireFirst]);
        frames->retireFirst = (frames->retireFirst + 1) % frames->retireCapacity;
        --frames->retireCount;
    }
}

void mrhiD3d12RetireLater(mrhiD3d12Frames* frames, mrhiD3d12Kind kind, uint64_t handle)
{
    // Every handle waits at most once, so the queue never fills.
    MRHI_ASSERT(frames->retireCount < frames->retireCapacity);
    uint32_t at = (frames->retireFirst + frames->retireCount) % frames->retireCapacity;
    frames->retirees[at] =
        (mrhiD3d12Retiree){.serial = frames->submitted + 1, .handle = handle, .kind = kind};
    ++frames->retireCount;
}

// Waits, without end, until the fence reaches a value or the device is
// removed.
static void WaitFor(mrhiD3d12Frames* frames, uint64_t serial)
{
    if (ID3D12Fence_GetCompletedValue(frames->fence) >= serial)
    {
        return;
    }
    (void)ResetEvent(frames->event);
    if (SUCCEEDED(ID3D12Fence_SetEventOnCompletion(frames->fence, serial, frames->event)))
    {
        (void)WaitForSingleObject(frames->event, INFINITE);
    }
}

void mrhiD3d12CloseFrames(mrhiD3d12Frames* frames)
{
    if (frames->fence != nullptr && frames->event != nullptr)
    {
        WaitFor(frames, frames->submitted);
    }
    RetireUpTo(frames, UINT64_MAX);
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        mrhiD3d12Slot* slot = &frames->slots[i];
        DropTransients(slot);
        if (slot->staging != nullptr)
        {
            ID3D12Resource_Release(slot->staging);
        }
        if (slot->targetHeap != nullptr)
        {
            ID3D12DescriptorHeap_Release(slot->targetHeap);
        }
        if (slot->depthHeap != nullptr)
        {
            ID3D12DescriptorHeap_Release(slot->depthHeap);
        }
        if (slot->list != nullptr)
        {
            ID3D12GraphicsCommandList_Release(slot->list);
        }
        if (slot->allocator != nullptr)
        {
            ID3D12CommandAllocator_Release(slot->allocator);
        }
    }
    if (frames->readback != nullptr)
    {
        ID3D12Resource_Release(frames->readback);
    }
    if (frames->event != nullptr)
    {
        (void)CloseHandle(frames->event);
    }
    if (frames->fence != nullptr)
    {
        ID3D12Fence_Release(frames->fence);
    }
}

// Makes a frame's transients in its slot and fills the frame's table:
// whether D3D12 made them all.
static bool TakeObjects(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot, const mrhiDriverFrame* frame)
{
    MRHI_ASSERT(frame->resourceCount <= frames->resourceLimit);
    slot->transientCount = frame->resourceCount;
    bool made = true;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        mrhiD3d12Object* object = &frames->table[i];
        *object = (mrhiD3d12Object){.state = D3D12_RESOURCE_STATE_COMMON};
        slot->transients[i] = nullptr;
        if (!resource->needed || !made)
        {
            continue;
        }
        switch (resource->kind)
        {
        case mrhiDriverTransientTexture:
        {
            mrhiTextureDef def = *resource->texture;
            def.usage = resource->usage;
            slot->transients[i] = mrhiD3d12CommitTexture(frames->objects, &def);
            object->resource = slot->transients[i];
            object->texture = resource->texture;
            break;
        }
        case mrhiDriverTransientBuffer:
        {
            const mrhiBufferDef def = {.size = resource->size, .usage = resource->usage};
            slot->transients[i] = mrhiD3d12CommitBuffer(frames->objects, &def);
            object->resource = slot->transients[i];
            break;
        }
        case mrhiDriverDeviceTexture:
        {
            const mrhiD3d12Texture* texture = &frames->objects->textures[resource->handle - 1];
            object->resource = texture->resource;
            object->texture = &texture->def;
            break;
        }
        case mrhiDriverDeviceBuffer:
            object->resource = frames->objects->buffers[resource->handle - 1].resource;
            break;
        default:
            // Surfaces are never configured on D3D12 yet.
            MRHI_ASSERT(false);
            break;
        }
        made = object->resource != nullptr;
    }
    return made;
}

static mrhiD3d12Ring RingOf(ID3D12Device* device, ID3D12DescriptorHeap* heap,
                            D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity)
{
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    (void)ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap, &start);
    return (mrhiD3d12Ring){
        .start = start,
        .step = ID3D12Device_GetDescriptorHandleIncrementSize(device, type),
        .capacity = capacity,
    };
}

// Records a frame into its slot's list: its passes, each after its
// barriers, then the barriers at its end.
static HRESULT Record(mrhiD3d12Frames* frames, mrhiD3d12Slot* slot, const mrhiDriverFrame* frame)
{
    HRESULT result = ID3D12CommandAllocator_Reset(slot->allocator);
    if (SUCCEEDED(result))
    {
        result = ID3D12GraphicsCommandList_Reset(slot->list, slot->allocator, nullptr);
    }
    if (FAILED(result))
    {
        return result;
    }
    mrhiD3d12Recorder recorder = {
        .device = frames->device,
        .list = slot->list,
        .objects = frames->objects,
        .frame = frame,
        .table = frames->table,
        .staging = slot->staging,
        .readback = frames->readback,
        .targets = RingOf(frames->device, slot->targetHeap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                          frames->targetLimit),
        .depths = RingOf(frames->device, slot->depthHeap, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                         frames->depthLimit),
        .readbacks = slot->readbacks,
        .readbackLimit = frames->readbackLimit,
    };
    for (uint32_t i = 0; i < frame->passCount; ++i)
    {
        mrhiD3d12RecordPass(&recorder, &frame->passes[i]);
    }
    mrhiD3d12RecordBarriers(&recorder, (mrhiPassId){0});
    MRHI_ASSERT(recorder.barrierAt == frame->barrierCount);
    slot->readbackCount = recorder.readbackCount;
    return ID3D12GraphicsCommandList_Close(slot->list);
}

// Notes the device removed when D3D12 says so: whether it has.
static bool Removed(mrhiD3d12Frames* frames)
{
    HRESULT reason = ID3D12Device_GetDeviceRemovedReason(frames->device);
    if (reason != S_OK)
    {
        frames->removed = reason;
        frames->lost = true;
    }
    return frames->lost;
}

mrhiResult mrhiD3d12Submit(mrhiD3d12Frames* frames, const mrhiDriverFrame* frame, uint64_t tag)
{
    if (frames->lost)
    {
        return mrhi_errorDeviceLost;
    }
    mrhiD3d12Slot* slot = &frames->slots[frames->submitted % frames->slotCount];
    MRHI_ASSERT(slot->tag == 0 && tag != 0);
    if (!TakeObjects(frames, slot, frame))
    {
        DropTransients(slot);
        return Removed(frames) ? mrhi_errorDeviceLost : mrhi_errorCapacity;
    }
    if (frame->stagingBytes > 0)
    {
        MRHI_ASSERT(frame->stagingBytes <= frames->uploadBytes);
        memcpy(slot->stagingBytes, frame->staging, frame->stagingBytes);
    }
    HRESULT result = Record(frames, slot, frame);
    if (SUCCEEDED(result))
    {
        ID3D12CommandList* lists[] = {(ID3D12CommandList*)slot->list};
        ID3D12CommandQueue_ExecuteCommandLists(frames->queue, 1, lists);
        result = ID3D12CommandQueue_Signal(frames->queue, frames->fence, frames->submitted + 1);
    }
    if (FAILED(result))
    {
        // A list D3D12 refuses to close is a recording the core never
        // makes; either way the device is gone to the program.
        DropTransients(slot);
        slot->readbackCount = 0;
        (void)Removed(frames);
        frames->lost = true;
        return mrhi_errorDeviceLost;
    }
    ++frames->submitted;
    slot->serial = frames->submitted;
    slot->tag = tag;
    slot->ring = frame->readbackRing;
    return mrhi_success;
}

// Copies a finished frame's readbacks into the core's ring.
static void FillReadbacks(const mrhiD3d12Frames* frames, mrhiD3d12Slot* slot)
{
    for (uint32_t i = 0; i < slot->readbackCount; ++i)
    {
        const mrhiD3d12Range* range = &slot->readbacks[i];
        memcpy(slot->ring + range->offset, frames->readbackBytes + range->offset, range->size);
    }
    slot->readbackCount = 0;
}

size_t mrhiD3d12PollFrames(mrhiD3d12Frames* frames, mrhiDriverEvent* events, size_t capacity)
{
    if (frames->lost || capacity == 0)
    {
        return 0;
    }
    // A removed device's fence reads all ones.
    uint64_t done = ID3D12Fence_GetCompletedValue(frames->fence);
    if (done == UINT64_MAX && Removed(frames))
    {
        events[0] = (mrhiDriverEvent){.tag = 0, .outcome = mrhi_errorDeviceLost};
        return 1;
    }
    size_t moved = 0;
    while (moved < capacity && frames->finished < frames->submitted && frames->finished < done)
    {
        mrhiD3d12Slot* slot = &frames->slots[frames->finished % frames->slotCount];
        FillReadbacks(frames, slot);
        DropTransients(slot);
        RetireUpTo(frames, slot->serial);
        events[moved++] = (mrhiDriverEvent){.tag = slot->tag, .outcome = mrhi_success};
        slot->tag = 0;
        ++frames->finished;
    }
    return moved;
}

// Milliseconds, rounded up, for a wait of timeoutNs; INFINITE for the
// longest.
static DWORD Milliseconds(uint64_t timeoutNs)
{
    if (timeoutNs == UINT64_MAX)
    {
        return INFINITE;
    }
    uint64_t ms = timeoutNs / 1000000 + (timeoutNs % 1000000 != 0 ? 1 : 0);
    return ms < INFINITE ? (DWORD)ms : INFINITE - 1;
}

bool mrhiD3d12WaitFrame(mrhiD3d12Frames* frames, uint64_t tag, uint64_t timeoutNs)
{
    uint64_t serial = 0;
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        serial = frames->slots[i].tag == tag ? frames->slots[i].serial : serial;
    }
    MRHI_ASSERT(serial != 0);
    DWORD total = Milliseconds(timeoutNs);
    ULONGLONG start = GetTickCount64();
    // The event is the device's one, so an earlier wait's completion may
    // wake this one early; it waits again for what time is left.
    for (;;)
    {
        if (ID3D12Fence_GetCompletedValue(frames->fence) >= serial)
        {
            return true;
        }
        ULONGLONG spent = GetTickCount64() - start;
        if (total != INFINITE && spent >= total)
        {
            return false;
        }
        (void)ResetEvent(frames->event);
        if (FAILED(ID3D12Fence_SetEventOnCompletion(frames->fence, serial, frames->event)))
        {
            return false;
        }
        (void)WaitForSingleObject(frames->event,
                                  total == INFINITE ? INFINITE : (DWORD)(total - spent));
    }
}

// The message of a loss: D3D12's reason as eight hex digits.
static uint32_t Describe(char* message, HRESULT result)
{
    static const char text[] = "D3D12 removed the device: HRESULT 0x";
    static const char digits[] = "0123456789ABCDEF";
    static_assert(sizeof(text) - 1 + 8 <= MRHI_LOSS_MESSAGE_BYTES, "the message fits");
    memcpy(message, text, sizeof(text) - 1);
    uint32_t bits = (uint32_t)result;
    for (size_t i = 0; i < 8; ++i)
    {
        message[sizeof(text) - 1 + i] = digits[(bits >> (28 - 4 * i)) & 0xF];
    }
    return (uint32_t)(sizeof(text) - 1 + 8);
}

void mrhiD3d12LossReport(const mrhiD3d12Frames* frames, mrhiDeviceLossReport* reportOut)
{
    mrhiDeviceLossReason reason = mrhi_lossUnknown;
    switch (frames->removed)
    {
    case DXGI_ERROR_DEVICE_HUNG:
        reason = mrhi_lossHung;
        break;
    case DXGI_ERROR_DEVICE_RESET:
        reason = mrhi_lossReset;
        break;
    case DXGI_ERROR_DEVICE_REMOVED:
        reason = mrhi_lossRemoved;
        break;
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
        reason = mrhi_lossDriverFault;
        break;
    default:
        break;
    }
    *reportOut = (mrhiDeviceLossReport){.reason = reason};
    reportOut->messageLength = Describe(reportOut->message, frames->removed);
}
