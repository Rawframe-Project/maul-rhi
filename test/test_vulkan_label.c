// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan driver's labels (vulkan_label.c) against stand-ins for
// VK_EXT_debug_utils: each label reaches Vulkan NUL-terminated at its
// length, names are skipped when empty or unnamed, and nothing is called
// without the extension.

#include "test_harness.h"
#include "vulkan_label.h"

#include "maul-rhi/base.h"

#include <string.h>

static char s_seen[MRHI_LABEL_BYTES + 2];
static int s_begins;
static int s_ends;
static int s_inserts;
static int s_names;
static VkObjectType s_type;
static uint64_t s_handle;

static void Remember(const char* text)
{
    const char* end = memchr(text, '\0', sizeof(s_seen));
    CHECK(end != nullptr, "NUL-terminated");
    size_t length = end != nullptr ? (size_t)(end - text) : 0;
    memcpy(s_seen, text, length);
    s_seen[length] = '\0';
}

static VKAPI_ATTR void VKAPI_CALL Begin(VkCommandBuffer commands, const VkDebugUtilsLabelEXT* label)
{
    (void)commands;
    CHECK(label->sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, "a label");
    ++s_begins;
    Remember(label->pLabelName);
}

static VKAPI_ATTR void VKAPI_CALL End(VkCommandBuffer commands)
{
    (void)commands;
    ++s_ends;
}

static VKAPI_ATTR void VKAPI_CALL Insert(VkCommandBuffer commands,
                                         const VkDebugUtilsLabelEXT* label)
{
    (void)commands;
    CHECK(label->sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, "a label");
    ++s_inserts;
    Remember(label->pLabelName);
}

static VKAPI_ATTR VkResult VKAPI_CALL Name(VkDevice device,
                                           const VkDebugUtilsObjectNameInfoEXT* info)
{
    (void)device;
    CHECK(info->sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, "a name");
    ++s_names;
    s_type = info->objectType;
    s_handle = info->objectHandle;
    Remember(info->pObjectName);
    return VK_SUCCESS;
}

static mrhiVulkanDevice Api(void)
{
    return (mrhiVulkanDevice){
        .vkSetDebugUtilsObjectNameEXT = Name,
        .vkCmdBeginDebugUtilsLabelEXT = Begin,
        .vkCmdEndDebugUtilsLabelEXT = End,
        .vkCmdInsertDebugUtilsLabelEXT = Insert,
    };
}

// Labels are read to their length, not to a NUL.
static void TestLabels(void)
{
    mrhiVulkanDevice api = Api();
    mrhiVulkanBeginLabel(&api, VK_NULL_HANDLE, "shadowsX", 7);
    CHECK(s_begins == 1 && strcmp(s_seen, "shadows") == 0, "a group at its length");
    mrhiVulkanInsertLabel(&api, VK_NULL_HANDLE, "cascade 1", 9);
    CHECK(s_inserts == 1 && strcmp(s_seen, "cascade 1") == 0, "a marker");
    mrhiVulkanEndLabel(&api, VK_NULL_HANDLE);
    CHECK(s_ends == 1, "the group ended");
    char longest[MRHI_LABEL_BYTES];
    memset(longest, 'x', sizeof(longest));
    mrhiVulkanBeginLabel(&api, VK_NULL_HANDLE, longest, sizeof(longest));
    CHECK(strlen(s_seen) == MRHI_LABEL_BYTES, "the longest label whole");
    mrhiVulkanBeginLabel(&api, VK_NULL_HANDLE, "", 0);
    CHECK(s_begins == 3 && s_seen[0] == '\0', "an empty group");
}

// Objects are named with their type and handle, unless the label is
// empty or the handle null.
static void TestNames(void)
{
    mrhiVulkanDevice api = Api();
    s_names = 0;
    uint32_t bits = 0x1234u;
    mrhiVulkanName(&api, VK_NULL_HANDLE, VK_OBJECT_TYPE_BUFFER, MRHI_VULKAN_HANDLE(bits),
                   "vertices!", 8);
    CHECK(s_names == 1 && s_type == VK_OBJECT_TYPE_BUFFER && s_handle == 0x1234u &&
              strcmp(s_seen, "vertices") == 0,
          "a buffer named");
    mrhiVulkanName(&api, VK_NULL_HANDLE, VK_OBJECT_TYPE_BUFFER, 0x1234u, "", 0);
    mrhiVulkanName(&api, VK_NULL_HANDLE, VK_OBJECT_TYPE_BUFFER, 0, "none", 4);
    CHECK(s_names == 1, "nothing named without a label or a handle");
}

// Without the extension nothing is called.
static void TestWithout(void)
{
    const mrhiVulkanDevice api = {0};
    int before = s_begins + s_ends + s_inserts + s_names;
    mrhiVulkanBeginLabel(&api, VK_NULL_HANDLE, "a", 1);
    mrhiVulkanInsertLabel(&api, VK_NULL_HANDLE, "b", 1);
    mrhiVulkanEndLabel(&api, VK_NULL_HANDLE);
    mrhiVulkanName(&api, VK_NULL_HANDLE, VK_OBJECT_TYPE_IMAGE, 1, "c", 1);
    CHECK(s_begins + s_ends + s_inserts + s_names == before, "nothing called");
}

int main(void)
{
    TestLabels();
    TestNames();
    TestWithout();
    return s_failures == 0 ? 0 : 1;
}
