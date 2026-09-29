// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opens the Vulkan loader by its platform names (mrhi-0003) and reads the
// driver's functions from it.

#include "vulkan_api.h"

#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// The loader's names on this platform, in the order they are tried.
#if defined(_WIN32)
static const char* const s_names[] = {"vulkan-1.dll"};
#elif defined(__APPLE__)
static const char* const s_names[] = {"libvulkan.1.dylib", "libvulkan.dylib"};
#else
static const char* const s_names[] = {"libvulkan.so.1", "libvulkan.so"};
#endif

static void* OpenLibrary(const char* name)
{
#ifdef _WIN32
    return (void*)LoadLibraryA(name);
#else
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void CloseLibrary(void* library)
{
#ifdef _WIN32
    FreeLibrary((HMODULE)library);
#else
    dlclose(library);
#endif
}

static PFN_vkGetInstanceProcAddr FindEntry(void* library)
{
    PFN_vkGetInstanceProcAddr entry = nullptr;
#ifdef _WIN32
    FARPROC address = GetProcAddress((HMODULE)library, "vkGetInstanceProcAddr");
#else
    void* address = dlsym(library, "vkGetInstanceProcAddr");
#endif
    // A function pointer read from the platform's untyped symbol.
    static_assert(sizeof(address) == sizeof(entry), "symbols are function pointers");
    memcpy((void*)&entry, (const void*)&address, sizeof(entry));
    return entry;
}

#define MRHI_VULKAN_READ(name)                                                                     \
    vulkan->name = (PFN_##name)vulkan->vkGetInstanceProcAddr(instance, #name);                     \
    found = found && vulkan->name != nullptr;

bool mrhiOpenVulkan(mrhiVulkan* vulkan)
{
    *vulkan = (mrhiVulkan){0};
    for (size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]) && vulkan->library == nullptr; ++i)
    {
        vulkan->library = OpenLibrary(s_names[i]);
    }
    if (vulkan->library == nullptr)
    {
        return false;
    }
    vulkan->vkGetInstanceProcAddr = FindEntry(vulkan->library);
    bool found = vulkan->vkGetInstanceProcAddr != nullptr;
    if (found)
    {
        VkInstance instance = VK_NULL_HANDLE;
        MRHI_VULKAN_GLOBAL(MRHI_VULKAN_READ)
    }
    if (!found)
    {
        mrhiCloseVulkan(vulkan);
    }
    return found;
}

bool mrhiLoadVulkanInstance(mrhiVulkan* vulkan, VkInstance instance)
{
    bool found = true;
    MRHI_VULKAN_INSTANCE(MRHI_VULKAN_READ)
    return found;
}

#define MRHI_VULKAN_READ_DEVICE(name)                                                              \
    functions->name = (PFN_##name)vulkan->vkGetDeviceProcAddr(device, #name);                      \
    found = found && functions->name != nullptr;

bool mrhiLoadVulkanDevice(const mrhiVulkan* vulkan, VkDevice device, mrhiVulkanDevice* functions)
{
    bool found = true;
    MRHI_VULKAN_DEVICE(MRHI_VULKAN_READ_DEVICE)
    return found;
}

void mrhiCloseVulkan(mrhiVulkan* vulkan)
{
    if (vulkan->library != nullptr)
    {
        CloseLibrary(vulkan->library);
    }
    *vulkan = (mrhiVulkan){0};
}
