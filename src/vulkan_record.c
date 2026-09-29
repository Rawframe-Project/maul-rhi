// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Records a submitted frame on Vulkan (vulkan_frame.h): the core's
// barriers, each state standing for a stage, an access and a layout,
// then each kept pass's commands. This slice runs passes without
// targets whose commands copy, upload and read back; other work is
// refused as unsupported until the passes that draw land.

#include "capabilities_core.h"
#include "command.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_frame.h"

#include <string.h>

// The barriers one vkCmdPipelineBarrier2 takes at most.
#define BATCH 32

// Every shader stage a state may name.
#define SHADERS                                                                                    \
    (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |             \
     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)

// What a resource state is to Vulkan.
typedef struct Use
{
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    VkImageLayout layout;
} Use;

static const Use s_uses[] = {
    [mrhi_stateUndefined] = {VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateSampled] = {SHADERS, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    [mrhi_stateUniform] = {SHADERS, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateVertex] = {VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                          VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateIndex] = {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT,
                         VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateIndirect] = {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_stateStorageRead] = {SHADERS, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                               VK_IMAGE_LAYOUT_GENERAL},
    [mrhi_stateStorageWrite] = {SHADERS, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_IMAGE_LAYOUT_GENERAL},
    [mrhi_stateStorageReadWrite] = {SHADERS,
                                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                    VK_IMAGE_LAYOUT_GENERAL},
    [mrhi_stateCopySource] = {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
    [mrhi_stateCopyDestination] = {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
    [mrhi_stateColorTarget] = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    [mrhi_stateResolve] = {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    [mrhi_stateDepthTarget] = {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
    [mrhi_stateDepthRead] = {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | SHADERS,
                             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
    [mrhi_stateQueryResolve] = {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_IMAGE_LAYOUT_UNDEFINED},
    [mrhi_statePresent] = {VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
};

// A frame resource's Vulkan image and def.
typedef struct Texture
{
    VkImage image;
    const mrhiTextureDef* def;
} Texture;

static const mrhiDriverResource* Find(const mrhiDriverFrame* frame, uint32_t index1)
{
    MRHI_ASSERT(index1 != 0 && index1 <= frame->resourceCount);
    return &frame->resources[index1 - 1];
}

static bool IsTexture(const mrhiDriverResource* resource)
{
    return resource->kind == mrhiDriverDeviceTexture ||
           resource->kind == mrhiDriverTransientTexture;
}

static Texture TextureOf(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot,
                         const mrhiDriverFrame* frame, uint32_t index1)
{
    const mrhiDriverResource* resource = Find(frame, index1);
    VkImage image = resource->kind == mrhiDriverDeviceTexture
                        ? frames->objects->textures[resource->handle - 1].image
                        : slot->images[index1 - 1];
    return (Texture){.image = image, .def = resource->texture};
}

static VkBuffer BufferOf(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot,
                         const mrhiDriverFrame* frame, uint32_t index1)
{
    const mrhiDriverResource* resource = Find(frame, index1);
    return resource->kind == mrhiDriverDeviceBuffer
               ? frames->objects->buffers[resource->handle - 1].buffer
               : slot->buffers[index1 - 1];
}

static VkImageAspectFlags AspectOf(mrhiTextureAspect aspect, mrhiFormat format)
{
    if (aspect == mrhi_aspectDepthOnly)
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (aspect == mrhi_aspectStencilOnly)
    {
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VkImageAspectFlags flags = 0;
    flags |= mrhiFormatHasDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    flags |= mrhiFormatHasStencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    return flags != 0 ? flags : VK_IMAGE_ASPECT_COLOR_BIT;
}

// The barriers gathered for one vkCmdPipelineBarrier2.
typedef struct Batch
{
    VkImageMemoryBarrier2 images[BATCH];
    VkBufferMemoryBarrier2 buffers[BATCH];
    uint32_t imageCount;
    uint32_t bufferCount;
} Batch;

static void Flush(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot, Batch* batch)
{
    if (batch->imageCount + batch->bufferCount == 0)
    {
        return;
    }
    const VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = batch->bufferCount,
        .pBufferMemoryBarriers = batch->buffers,
        .imageMemoryBarrierCount = batch->imageCount,
        .pImageMemoryBarriers = batch->images,
    };
    frames->api->vkCmdPipelineBarrier2(slot->commands, &dependency);
    batch->imageCount = 0;
    batch->bufferCount = 0;
}

static void AddBarrier(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot,
                       const mrhiDriverFrame* frame, const mrhiBarrier* barrier, Batch* batch)
{
    const Use* before = &s_uses[barrier->before];
    const Use* after = &s_uses[barrier->after];
    if (!IsTexture(Find(frame, barrier->resource.index1)))
    {
        batch->buffers[batch->bufferCount++] = (VkBufferMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = before->stages,
            .srcAccessMask = before->access,
            .dstStageMask = after->stages,
            .dstAccessMask = after->access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = BufferOf(frames, slot, frame, barrier->resource.index1),
            .size = VK_WHOLE_SIZE,
        };
    }
    else
    {
        const mrhiTextureRange* range = &barrier->range;
        Texture resource = TextureOf(frames, slot, frame, barrier->resource.index1);
        batch->images[batch->imageCount++] = (VkImageMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = before->stages,
            .srcAccessMask = before->access,
            .dstStageMask = after->stages,
            .dstAccessMask = after->access,
            .oldLayout = before->layout,
            .newLayout = after->layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = resource.image,
            .subresourceRange =
                {
                    .aspectMask = AspectOf(range->aspect, resource.def->format),
                    .baseMipLevel = range->baseMip,
                    .levelCount = range->mipCount,
                    .baseArrayLayer = range->baseLayer,
                    .layerCount = range->layerCount,
                },
        };
    }
    if (batch->imageCount == BATCH || batch->bufferCount == BATCH)
    {
        Flush(frames, slot, batch);
    }
}

// Records the barriers from the cursor on that come before a pass (a
// null id for the frame's end), moving the cursor past them.
static void Barriers(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot,
                     const mrhiDriverFrame* frame, size_t* cursor, mrhiPassId pass)
{
    Batch batch = {.imageCount = 0};
    size_t at = *cursor;
    while (at < frame->barrierCount && frame->barriers[at].pass.index1 == pass.index1 &&
           frame->barriers[at].pass.generation == pass.generation)
    {
        AddBarrier(frames, slot, frame, &frame->barriers[at], &batch);
        ++at;
    }
    Flush(frames, slot, &batch);
    *cursor = at;
}

// A copy's region between a buffer side and a texture side, and the
// bytes it spans in the buffer.
static VkBufferImageCopy RegionOf(const mrhiCommandBufferSide* buffer,
                                  const mrhiCommandTextureSide* texture, const mrhiTextureDef* def,
                                  const mrhiCommand* command, uint64_t* bytesOut)
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t texelBytes = mrhiGetFormatCopy(def->format, texture->aspect).bytes;
    MRHI_ASSERT(texelBytes > 0);
    bool volume = def->kind == mrhi_texture3d;
    uint32_t width = (uint32_t)command->b;
    uint32_t height = (uint32_t)command->c;
    uint32_t depth = (uint32_t)command->d;
    uint64_t rows = height / block.height;
    uint64_t rowBytes = (uint64_t)width / block.width * texelBytes;
    *bytesOut = (uint64_t)buffer->bytesPerRow * buffer->rowsPerImage * (depth - 1) +
                (uint64_t)buffer->bytesPerRow * (rows - 1) + rowBytes;
    return (VkBufferImageCopy){
        .bufferOffset = buffer->offset,
        .bufferRowLength = buffer->bytesPerRow / texelBytes * block.width,
        .bufferImageHeight = buffer->rowsPerImage * block.height,
        .imageSubresource =
            {
                .aspectMask = AspectOf(texture->aspect, def->format),
                .mipLevel = texture->mip,
                .baseArrayLayer = volume ? 0 : texture->z,
                .layerCount = volume ? 1 : depth,
            },
        .imageOffset = {(int32_t)texture->x, (int32_t)texture->y, volume ? (int32_t)texture->z : 0},
        .imageExtent = {width, height, volume ? depth : 1},
    };
}

// A buffer side's Vulkan buffer: staging and the readback ring are
// object 0.
static VkBuffer SideBuffer(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot,
                           const mrhiDriverFrame* frame, const mrhiCommand* command,
                           uint32_t object)
{
    if (object != 0)
    {
        return BufferOf(frames, slot, frame, object);
    }
    bool upload =
        command->type == mrhiCommandWriteBuffer || command->type == mrhiCommandWriteTexture;
    return upload ? slot->staging : frames->readback;
}

static void NoteReadback(const mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, uint64_t offset,
                         uint64_t size)
{
    MRHI_ASSERT(slot->readbackCount < frames->readbackLimit);
    slot->readbacks[slot->readbackCount++] = (mrhiVulkanRange){.offset = offset, .size = size};
}

static void CopyBuffers(const mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                        const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    mrhiCommandBufferSide sides[2];
    memcpy(sides, &command[1], sizeof(sides));
    const VkBufferCopy region = {
        .srcOffset = sides[0].offset,
        .dstOffset = sides[1].offset,
        .size = command->b,
    };
    frames->api->vkCmdCopyBuffer(
        slot->commands, SideBuffer(frames, slot, frame, command, sides[0].object),
        SideBuffer(frames, slot, frame, command, sides[1].object), 1, &region);
    if (command->type == mrhiCommandReadBuffer)
    {
        NoteReadback(frames, slot, sides[1].offset, command->b);
    }
}

static void CopyWithTexture(const mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                            const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    bool fromBuffer =
        command->type == mrhiCommandCopyBufferToTexture || command->type == mrhiCommandWriteTexture;
    mrhiCommandBufferSide buffer;
    mrhiCommandTextureSide texture;
    memcpy(&buffer, fromBuffer ? &command[1] : &command[2], sizeof(buffer));
    memcpy(&texture, fromBuffer ? &command[2] : &command[1], sizeof(texture));
    Texture image = TextureOf(frames, slot, frame, texture.object);
    uint64_t bytes = 0;
    VkBufferImageCopy region = RegionOf(&buffer, &texture, image.def, command, &bytes);
    VkBuffer side = SideBuffer(frames, slot, frame, command, buffer.object);
    if (fromBuffer)
    {
        frames->api->vkCmdCopyBufferToImage(slot->commands, side, image.image,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        return;
    }
    frames->api->vkCmdCopyImageToBuffer(slot->commands, image.image,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, side, 1, &region);
    if (command->type == mrhiCommandReadTexture)
    {
        NoteReadback(frames, slot, buffer.offset, bytes);
    }
}

static void CopyTextures(const mrhiVulkanFrames* frames, const mrhiVulkanSlot* slot,
                         const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    mrhiCommandTextureSide sides[2];
    memcpy(sides, &command[1], sizeof(sides));
    const mrhiCommandTextureSide* source = &sides[0];
    const mrhiCommandTextureSide* target = &sides[1];
    Texture from = TextureOf(frames, slot, frame, source->object);
    Texture to = TextureOf(frames, slot, frame, target->object);
    bool volume = from.def->kind == mrhi_texture3d;
    uint32_t depth = (uint32_t)command->d;
    const VkImageCopy region = {
        .srcSubresource = {AspectOf(source->aspect, from.def->format), source->mip,
                           volume ? 0 : source->z, volume ? 1 : depth},
        .srcOffset = {(int32_t)source->x, (int32_t)source->y, volume ? (int32_t)source->z : 0},
        .dstSubresource = {AspectOf(target->aspect, to.def->format), target->mip,
                           volume ? 0 : target->z, volume ? 1 : depth},
        .dstOffset = {(int32_t)target->x, (int32_t)target->y, volume ? (int32_t)target->z : 0},
        .extent = {(uint32_t)command->b, (uint32_t)command->c, volume ? depth : 1},
    };
    frames->api->vkCmdCopyImage(slot->commands, from.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                to.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

static void RecordCommand(const mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                          const mrhiDriverFrame* frame, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandCopyBuffer:
    case mrhiCommandWriteBuffer:
    case mrhiCommandReadBuffer:
        CopyBuffers(frames, slot, frame, command);
        break;
    case mrhiCommandCopyBufferToTexture:
    case mrhiCommandCopyTextureToBuffer:
    case mrhiCommandWriteTexture:
    case mrhiCommandReadTexture:
        CopyWithTexture(frames, slot, frame, command);
        break;
    case mrhiCommandCopyTexture:
        CopyTextures(frames, slot, frame, command);
        break;
    default:
        // Debug groups and markers wait for VK_EXT_debug_utils.
        break;
    }
}

// Whether this slice runs a pass: no targets, and nothing but copies,
// uploads, readbacks and debug labels.
static bool IsRunnable(const mrhiDriverFrame* frame, const mrhiDriverPass* pass)
{
    if (pass->colorTargetCount > 0 || pass->depthTarget.resource.index1 != 0)
    {
        return false;
    }
    for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            uint16_t type = at->commands[i].type;
            bool transfer = type >= mrhiCommandCopyBuffer && type <= mrhiCommandReadTexture;
            bool label = type >= mrhiCommandPushDebugGroup && type <= mrhiCommandDebugMarker;
            if (!transfer && !label)
            {
                return false;
            }
        }
    }
    return true;
}

static bool IsFrameRunnable(const mrhiDriverFrame* frame)
{
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        if (frame->resources[i].kind == mrhiDriverSurfaceImage && frame->resources[i].needed)
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < frame->passCount; ++i)
    {
        if (!IsRunnable(frame, &frame->passes[i]))
        {
            return false;
        }
    }
    return true;
}

mrhiResult mrhiVulkanRecord(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                            const mrhiDriverFrame* frame)
{
    if (!IsFrameRunnable(frame))
    {
        return mrhi_errorUnsupported;
    }
    size_t barrier = 0;
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        Barriers(frames, slot, frame, &barrier, pass->id);
        for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
        {
            const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
            for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
            {
                RecordCommand(frames, slot, frame, &at->commands[i]);
            }
        }
    }
    Barriers(frames, slot, frame, &barrier, (mrhiPassId){0});
    MRHI_ASSERT(barrier == frame->barrierCount);
    return mrhi_success;
}
