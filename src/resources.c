// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The objects a device owns, as generation-checked ids over its tables:
// each def checked, the driver asked to make the object, and a
// destruction that ends the id at once.

#include "chain.h"
#include "device_core.h"
#include "instance_core.h"

#define SAMPLER_DEF_COOKIE 0x6D727361u

mrhiSamplerDef mrhiDefaultSamplerDef(void)
{
    mrhiSamplerDef def = {0};
    def.cookie = SAMPLER_DEF_COOKIE;
    def.magFilter = mrhi_filterNearest;
    def.minFilter = mrhi_filterNearest;
    def.mipFilter = mrhi_filterNearest;
    def.addressU = mrhi_addressClampToEdge;
    def.addressV = mrhi_addressClampToEdge;
    def.addressW = mrhi_addressClampToEdge;
    def.lodMin = 0.0f;
    def.lodMax = 32.0f;
    def.maxAnisotropy = 1;
    def.compare = mrhi_compareNone;
    return def;
}

// Whether a sampler def's values are in range: known enums, levels of
// detail in order (NaN refused), and anisotropy only with linear
// filtering.
static bool IsSamplerDefValid(const mrhiSamplerDef* def)
{
    bool filters = def->magFilter <= mrhi_filterLinear && def->minFilter <= mrhi_filterLinear &&
                   def->mipFilter <= mrhi_filterLinear;
    bool address = def->addressU <= mrhi_addressMirrorRepeat &&
                   def->addressV <= mrhi_addressMirrorRepeat &&
                   def->addressW <= mrhi_addressMirrorRepeat;
    bool lod = def->lodMin >= 0.0f && def->lodMax >= def->lodMin;
    bool linear = def->magFilter == mrhi_filterLinear && def->minFilter == mrhi_filterLinear &&
                  def->mipFilter == mrhi_filterLinear;
    bool anisotropy =
        def->maxAnisotropy >= 1 && def->maxAnisotropy <= 16 && (def->maxAnisotropy == 1 || linear);
    return filters && address && lod && anisotropy && def->compare <= mrhi_compareAlways;
}

// Checks a def's cookie and extension chain on a live device: success,
// or the refusal (invalid input counted as misuse).
static mrhiResult CheckDef(mrhiDevice* device, uint32_t cookie, uint32_t expected,
                           const mrhiChain* next)
{
    mrhiResult chain = mrhiCheckChain(next, nullptr, 0, device->instance->limits.chainDepth);
    if (cookie != expected || chain == mrhi_errorInvalid)
    {
        return mrhiDeviceMisuse(device);
    }
    return chain;
}

mrhiResult mrhiCreateSampler(mrhiDevice* device, const mrhiSamplerDef* def,
                             mrhiSamplerId* samplerOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || samplerOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = CheckDef(device, def->cookie, SAMPLER_DEF_COOKIE, def->next);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!IsSamplerDefValid(def))
    {
        return mrhiDeviceMisuse(device);
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->samplers, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = device->driver.vtable->createSampler(device->driver.self, def, &handle);
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->samplers, index1);
        return status;
    }
    device->samplerHandles[index1 - 1] = handle;
    *samplerOut = (mrhiSamplerId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroySampler(mrhiDevice* device, mrhiSamplerId sampler)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->samplers, sampler.index1, sampler.generation))
    {
        return mrhi_errorStale;
    }
    device->driver.vtable->destroySampler(device->driver.self,
                                          device->samplerHandles[sampler.index1 - 1]);
    mrhiPoolRelease(&device->samplers, sampler.index1);
    return mrhi_success;
}
