// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU conformance test suite's validation cases whose calls have
// Maul RHI counterparts (mrhi-0021), on the test driver: each case names
// its CTS test, the call it becomes and the outcome the CTS expects,
// success or the library's refusal. Cases the library decides otherwise
// are listed with the reason. Translated from the CTS at a5e5d74
// (https://github.com/gpuweb/cts), whose cases are under this notice:
//
//   Copyright 2019 WebGPU CTS Contributors
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions
//   are met:
//   1. Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//   2. Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in
//      the documentation and/or other materials provided with the
//      distribution.
//   3. Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived
//      from this software without specific prior written permission.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
//   WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//   POSSIBILITY OF SUCH DAMAGE.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/resources.h"

#include <stdio.h>

static mrhiDevice* s_device;

// The CTS's kMaxSafeMultipleOf8: JavaScript's largest safe integer
// rounded down to a multiple of 8.
#define MAX_SAFE_MULTIPLE_OF_8 UINT64_C(9007199254740984)

// Reports a case whose outcome differs from the CTS's.
static void Expect(const char* test, mrhiResult result, bool succeeds)
{
    bool ok = succeeds ? result == mrhi_success : result == mrhi_errorInvalid;
    if (!ok)
    {
        printf("FAIL: %s: %d, the CTS expects %s\n", test, (int)result,
               succeeds ? "success" : "a validation error");
        ++s_failures;
    }
}

static mrhiBufferId MakeBuffer(uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// A copyBufferToBuffer case: the buffers' sizes and usages, the offsets
// and size, whether one buffer is both ends, and the CTS's outcome.
typedef struct BufferCopy
{
    const char* test;
    uint64_t sourceSize;
    mrhiBufferUsage sourceUsage;
    uint64_t destinationSize;
    mrhiBufferUsage destinationUsage;
    uint64_t sourceOffset;
    uint64_t destinationOffset;
    uint64_t size;
    bool same;
    bool succeeds;
} BufferCopy;

// The case in a frame: its buffers imported, a transfer pass declaring
// the source's copy source and the destination's copy destination, and
// the copy; the first refusal, of the pass or the copy, or success.
static mrhiResult CopyBuffers(const BufferCopy* c)
{
    mrhiBufferId from = MakeBuffer(c->sourceSize, c->sourceUsage);
    mrhiBufferId to = c->same ? from : MakeBuffer(c->destinationSize, c->destinationUsage);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId source = {0};
    mrhiResourceId destination = {0};
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, from, &source) == mrhi_success &&
              (c->same || mrhiImportBuffer(s_device, to, &destination) == mrhi_success),
          "the buffers in a frame");
    destination = c->same ? source : destination;
    const mrhiAccess accesses[2] = {Whole(source, mrhi_accessCopySource),
                                    Whole(destination, mrhi_accessCopyDestination)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 2;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
                  mrhiBeginPass(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiCopyBuffer(s_device, pass, source, c->sourceOffset, destination,
                                c->destinationOffset, c->size);
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, from) == mrhi_success &&
              (c->same || mrhiDestroyBuffer(s_device, to) == mrhi_success),
          "dropped");
    return status;
}

#define COPY_SRC mrhi_bufferCopySource
#define COPY_DST mrhi_bufferCopyDestination
#define BOTH     (mrhi_bufferCopySource | mrhi_bufferCopyDestination)

// api,validation,encoding,cmds,copyBufferToBuffer.
static const BufferCopy s_bufferCopies[] = {
    {"copy_size_alignment copySize=0", 16, COPY_SRC, 16, COPY_DST, 0, 0, 0, false, true},
    {"copy_size_alignment copySize=2", 16, COPY_SRC, 16, COPY_DST, 0, 0, 2, false, false},
    {"copy_size_alignment copySize=4", 16, COPY_SRC, 16, COPY_DST, 0, 0, 4, false, true},
    {"copy_size_alignment copySize=5", 16, COPY_SRC, 16, COPY_DST, 0, 0, 5, false, false},
    {"copy_size_alignment copySize=8", 16, COPY_SRC, 16, COPY_DST, 0, 0, 8, false, true},
    {"copy_offset_alignment 0,0", 16, COPY_SRC, 16, COPY_DST, 0, 0, 8, false, true},
    {"copy_offset_alignment 2,0", 16, COPY_SRC, 16, COPY_DST, 2, 0, 8, false, false},
    {"copy_offset_alignment 4,0", 16, COPY_SRC, 16, COPY_DST, 4, 0, 8, false, true},
    {"copy_offset_alignment 5,0", 16, COPY_SRC, 16, COPY_DST, 5, 0, 8, false, false},
    {"copy_offset_alignment 8,0", 16, COPY_SRC, 16, COPY_DST, 8, 0, 8, false, true},
    {"copy_offset_alignment 0,2", 16, COPY_SRC, 16, COPY_DST, 0, 2, 8, false, false},
    {"copy_offset_alignment 0,4", 16, COPY_SRC, 16, COPY_DST, 0, 4, 8, false, true},
    {"copy_offset_alignment 0,5", 16, COPY_SRC, 16, COPY_DST, 0, 5, 8, false, false},
    {"copy_offset_alignment 0,8", 16, COPY_SRC, 16, COPY_DST, 0, 8, 8, false, true},
    {"copy_offset_alignment 4,4", 16, COPY_SRC, 16, COPY_DST, 4, 4, 8, false, true},
    {"copy_overflow 0,0,max", 16, COPY_SRC, 16, COPY_DST, 0, 0, MAX_SAFE_MULTIPLE_OF_8, false,
     false},
    {"copy_overflow 16,0,max", 16, COPY_SRC, 16, COPY_DST, 16, 0, MAX_SAFE_MULTIPLE_OF_8, false,
     false},
    {"copy_overflow 0,16,max", 16, COPY_SRC, 16, COPY_DST, 0, 16, MAX_SAFE_MULTIPLE_OF_8, false,
     false},
    {"copy_overflow max,0,16", 16, COPY_SRC, 16, COPY_DST, MAX_SAFE_MULTIPLE_OF_8, 0, 16, false,
     false},
    {"copy_overflow 0,max,16", 16, COPY_SRC, 16, COPY_DST, 0, MAX_SAFE_MULTIPLE_OF_8, 16, false,
     false},
    {"copy_overflow max,0,max", 16, COPY_SRC, 16, COPY_DST, MAX_SAFE_MULTIPLE_OF_8, 0,
     MAX_SAFE_MULTIPLE_OF_8, false, false},
    {"copy_overflow 0,max,max", 16, COPY_SRC, 16, COPY_DST, 0, MAX_SAFE_MULTIPLE_OF_8,
     MAX_SAFE_MULTIPLE_OF_8, false, false},
    {"copy_overflow max,max,max", 16, COPY_SRC, 16, COPY_DST, MAX_SAFE_MULTIPLE_OF_8,
     MAX_SAFE_MULTIPLE_OF_8, MAX_SAFE_MULTIPLE_OF_8, false, false},
    {"copy_out_of_bounds 0,0,32", 32, COPY_SRC, 32, COPY_DST, 0, 0, 32, false, true},
    {"copy_out_of_bounds 0,0,36", 32, COPY_SRC, 32, COPY_DST, 0, 0, 36, false, false},
    {"copy_out_of_bounds 36,0,4", 32, COPY_SRC, 32, COPY_DST, 36, 0, 4, false, false},
    {"copy_out_of_bounds 0,36,4", 32, COPY_SRC, 32, COPY_DST, 0, 36, 4, false, false},
    {"copy_out_of_bounds 36,0,0", 32, COPY_SRC, 32, COPY_DST, 36, 0, 0, false, false},
    {"copy_out_of_bounds 0,36,0", 32, COPY_SRC, 32, COPY_DST, 0, 36, 0, false, false},
    {"copy_out_of_bounds 20,0,16", 32, COPY_SRC, 32, COPY_DST, 20, 0, 16, false, false},
    {"copy_out_of_bounds 20,0,12", 32, COPY_SRC, 32, COPY_DST, 20, 0, 12, false, true},
    {"copy_out_of_bounds 0,20,16", 32, COPY_SRC, 32, COPY_DST, 0, 20, 16, false, false},
    {"copy_out_of_bounds 0,20,12", 32, COPY_SRC, 32, COPY_DST, 0, 20, 12, false, true},
    {"copy_within_same_buffer 0,8,4", 16, BOTH, 16, BOTH, 0, 8, 4, true, false},
    {"copy_within_same_buffer 8,0,4", 16, BOTH, 16, BOTH, 8, 0, 4, true, false},
    {"copy_within_same_buffer 0,4,8", 16, BOTH, 16, BOTH, 0, 4, 8, true, false},
    {"copy_within_same_buffer 4,0,8", 16, BOTH, 16, BOTH, 4, 0, 8, true, false},
};

// The usages a buffer is made with, as the CTS's kBufferUsages less
// MAP_READ and MAP_WRITE, which Maul RHI has no counterpart of (its
// uploads and readbacks are the frame's).
static const mrhiBufferUsage s_usages[] = {
    mrhi_bufferCopySource, mrhi_bufferCopyDestination, mrhi_bufferIndex,    mrhi_bufferVertex,
    mrhi_bufferUniform,    mrhi_bufferStorage,         mrhi_bufferIndirect, mrhi_bufferQueryResolve,
};

static void TestBufferCopies(void)
{
    char name[96];
    for (size_t i = 0; i < sizeof(s_bufferCopies) / sizeof(s_bufferCopies[0]); ++i)
    {
        const BufferCopy* c = &s_bufferCopies[i];
        snprintf(name, sizeof(name), "copyBufferToBuffer:%s", c->test);
        Expect(name, CopyBuffers(c), c->succeeds);
    }
    // buffer_usage: success only from a copy source to a copy
    // destination.
    size_t count = sizeof(s_usages) / sizeof(s_usages[0]);
    for (size_t from = 0; from < count; ++from)
    {
        for (size_t to = 0; to < count; ++to)
        {
            const BufferCopy c = {"", 16, s_usages[from], 16, s_usages[to], 0, 0, 8, false, false};
            snprintf(name, sizeof(name), "copyBufferToBuffer:buffer_usage %#x,%#x",
                     (unsigned)s_usages[from], (unsigned)s_usages[to]);
            Expect(name, CopyBuffers(&c), s_usages[from] == COPY_SRC && s_usages[to] == COPY_DST);
        }
    }
}

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    TestBufferCopies();
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
