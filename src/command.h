// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The command stream (mrhi-0011): 32-byte records in 4 KiB chunks of a
// frame's arena, each pass linking its own chunks. A command's payload
// records follow it in the same chunk.

#ifndef MAUL_RHI_SRC_COMMAND_H
#define MAUL_RHI_SRC_COMMAND_H

#include <stdint.h>

// What a command record does. Fields a to d hold its operands.
typedef enum mrhiCommandType
{
    // a: the pipeline's slot index plus one.
    mrhiCommandGraphicsPipeline = 1,
    mrhiCommandComputePipeline,
    // a: the offset; b: the size; the bytes follow.
    mrhiCommandRootBlock,
    // The viewport follows.
    mrhiCommandViewport,
    // a, b: x and y; c, d: width and height.
    mrhiCommandScissor,
    // The color follows.
    mrhiCommandBlendConstant,
    // a: the reference.
    mrhiCommandStencilReference,
    // b: the label's bytes, which follow.
    mrhiCommandPushDebugGroup,
    mrhiCommandPopDebugGroup,
    mrhiCommandDebugMarker,
    // a: the table; b: the bindings, an mrhiCommandBinding each, which
    // follow.
    mrhiCommandBindings,
    // a: the slot or the index format; b: the frame resource's slot plus
    // one; c: the offset; d: the size.
    mrhiCommandVertexBuffer,
    mrhiCommandIndexBuffer,
    // a: the vertices; b: the instances; c: the first vertex; d: the
    // first instance.
    mrhiCommandDraw,
    // a: the indices; b: the instances; c: the first index, and the base
    // vertex in the upper half; d: the first instance.
    mrhiCommandDrawIndexed,
    // a, b, c: the workgroups in x, y and z.
    mrhiCommandDispatch,
} mrhiCommandType;

typedef struct mrhiCommand
{
    uint16_t type;
    // The payload records that follow.
    uint16_t payload;
    uint32_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
} mrhiCommand;

// One binding of a table as recorded: its slot and object (a frame
// resource's slot, or a sampler's, plus one); a buffer's offset and
// resolved size, or a texture view's first layer and layers; and a
// texture view's format, kind, aspect, first mip and mips.
typedef struct mrhiCommandBinding
{
    uint32_t slot;
    uint32_t object;
    uint64_t offset;
    uint64_t size;
    uint16_t viewFormat;
    uint8_t viewKind;
    uint8_t aspect;
    uint8_t baseMip;
    uint8_t mipCount;
    uint16_t reserved;
} mrhiCommandBinding;

// A chunk's bytes, and the records after its 32-byte header.
#define MRHI_CHUNK_BYTES    4096
#define MRHI_CHUNK_COMMANDS 127

// A chunk: the next chunk of its pass (its index plus one, 0 for none),
// its records in use, and the records.
typedef struct mrhiCommandChunk
{
    uint32_t next;
    uint32_t count;
    uint8_t reserved[24];
    mrhiCommand commands[MRHI_CHUNK_COMMANDS];
} mrhiCommandChunk;

static_assert(sizeof(mrhiCommand) == 32, "a command is one record");
static_assert(sizeof(mrhiCommandBinding) == 32, "a binding is one record");
static_assert(sizeof(mrhiCommandChunk) == MRHI_CHUNK_BYTES, "a chunk is 4 KiB");

#endif // MAUL_RHI_SRC_COMMAND_H
