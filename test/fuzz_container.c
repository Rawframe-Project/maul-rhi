// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes shader containers (mrhi-0009), the bytes a program loads from
// disk and hands the library. The input's size field is set to its size
// and its digest to the SHA-256 of what follows it before the parse, so
// that mutations reach the section table and records instead of
// stopping at those two checks, which the unit tests cover. A
// container that parses must keep every section inside its bytes, name
// its entries inside its strings, decode into a reflection that finds
// each entry by its name and stage and frees what it took, and parse the
// same again.

#include "container.h"
#include "reflection.h"
#include "sha256.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// The bytes the reflection holds, which must come back to 0.
static size_t s_held;

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    void* memory = aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
    s_held += memory != nullptr ? size : 0;
    return memory;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_held -= size;
    free(memory);
}

// Whether a part lies in the container: an absent one anywhere, a
// present one, empty or not, from inside it to no further than its end.
static bool Inside(const uint8_t* bytes, size_t size, const uint8_t* part, uint64_t partBytes)
{
    return part == nullptr ||
           (part >= bytes && part <= bytes + size && partBytes <= (uint64_t)(bytes + size - part));
}

// Whether two byte parts share no byte.
static bool Apart(const uint8_t* a, uint64_t aBytes, const uint8_t* b, uint64_t bBytes)
{
    return aBytes == 0 || bBytes == 0 || a + aBytes <= b || b + bBytes <= a;
}

static void CheckSections(const uint8_t* bytes, size_t size, const mrhiContainer* container)
{
    Expect(Inside(bytes, size, container->strings, container->stringBytes));
    Expect(Inside(bytes, size, container->spirv, container->spirvBytes));
    Expect(Inside(bytes, size, container->wgsl, container->wgslBytes));
    // WGSL exactly when no entry uses a heap.
    Expect((container->wgslBytes > 0) == (container->heapUses == 0));
    // Every record array holds its count of fixed-size records; their
    // sizes are the parse's, so only the first byte of each is checked
    // here and ASan checks the rest as the reflection decodes them.
    Expect(Inside(bytes, size, container->entries, container->entryCount));
    Expect(Inside(bytes, size, container->bindings, container->bindingCount));
    Expect(Inside(bytes, size, container->inputs, container->inputCount));
    Expect(Inside(bytes, size, container->outputs, container->outputCount));
    Expect(Inside(bytes, size, container->variables, container->variableCount));
    Expect(Inside(bytes, size, container->constants, container->constantCount));
    Expect(
        Apart(container->strings, container->stringBytes, container->spirv,
              container->spirvBytes) &&
        Apart(container->strings, container->stringBytes, container->wgsl, container->wgslBytes) &&
        Apart(container->spirv, container->spirvBytes, container->wgsl, container->wgslBytes));
}

static void CheckReflection(const mrhiContainer* container)
{
    const mrhiAllocator allocator = {.alloc = Alloc, .free = Free};
    mrhiReflection* reflection = mrhiKeepReflection(&allocator, container);
    Expect(reflection != nullptr);
    Expect(reflection->entryCount == container->entryCount &&
           reflection->bindingCount == container->bindingCount &&
           reflection->constantCount == container->constantCount &&
           reflection->rootBlockBytes == container->rootBlockBytes);
    for (uint32_t i = 0; i < reflection->entryCount; ++i)
    {
        mrhiShaderEntry entry = mrhiContainerEntry(container, i);
        Expect(entry.nameOffset <= container->stringBytes &&
               entry.nameLength <= container->stringBytes - entry.nameOffset);
        const mrhiShaderEntry* kept = &reflection->entries[i];
        Expect(kept->stage == entry.stage && kept->nameLength == entry.nameLength &&
               memcmp(reflection->names + kept->nameOffset, container->strings + entry.nameOffset,
                      entry.nameLength) == 0);
        uint32_t found = mrhiFindEntry(reflection, reflection->names + kept->nameOffset,
                                       kept->nameLength, kept->stage);
        Expect(found < reflection->entryCount);
        const mrhiShaderEntry* other = &reflection->entries[found];
        Expect(other->stage == kept->stage && other->nameLength == kept->nameLength &&
               memcmp(reflection->names + other->nameOffset, reflection->names + kept->nameOffset,
                      kept->nameLength) == 0);
    }
    mrhiReleaseReflection(&allocator, reflection);
    Expect(s_held == 0);
}

// Parses the first size bytes of the input, its size field and digest
// set, and checks what parses.
static void Run(const uint8_t* data, size_t size)
{
    // The exact size, so that ASan sees any read past the end.
    uint8_t* bytes = malloc(size > 0 ? size : 1);
    Expect(bytes != nullptr);
    if (size > 0)
    {
        memcpy(bytes, data, size);
    }
    if (size >= 48)
    {
        for (int i = 0; i < 8; ++i)
        {
            bytes[8 + i] = (uint8_t)((uint64_t)size >> (8 * i));
        }
        mrhiSha256(bytes + 48, size - 48, bytes + 16);
    }
    mrhiContainer container;
    mrhiResult status = mrhiParseContainer(bytes, size, &container);
    Expect(status == mrhi_success || status == mrhi_errorInvalid || status == mrhi_errorVersion);
    if (status == mrhi_success)
    {
        CheckSections(bytes, size, &container);
        CheckReflection(&container);
        mrhiContainer again;
        Expect(mrhiParseContainer(bytes, size, &again) == mrhi_success &&
               memcmp(&again, &container, sizeof(container)) == 0);
    }
    free(bytes);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

// The input whole, and cut short by 1 to 64 bytes as its last byte
// says: mutations seldom cut a container at its end, where a section
// that runs past it is found.
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Run(data, size);
    if (size > 64)
    {
        Run(data, size - 1 - data[size - 1] % 64);
    }
    return 0;
}
