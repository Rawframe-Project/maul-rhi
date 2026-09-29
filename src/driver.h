// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The driver SPI at the instance level (mrhi-0003), as the core calls
// it. A driver reports finished work only when the core polls it, and
// never calls the core.

#ifndef MAUL_RHI_SRC_DRIVER_H
#define MAUL_RHI_SRC_DRIVER_H

#include "maul-rhi/surface.h"

// The SPI version a driver's vtable must carry.
#define MRHI_SPI_VERSION 1

// An adapter as a driver reports it: its handle, never zero, its facts,
// and the features and limits it can grant.
typedef struct mrhiDriverAdapter
{
    uint64_t handle;
    mrhiAdapterInfo info;
    mrhiFeatures features;
    mrhiLimits limits;
} mrhiDriverAdapter;

// Finished work: the tag the core gave the request, and its outcome.
typedef struct mrhiDriverEvent
{
    uint64_t tag;
    mrhiResult outcome;
} mrhiDriverEvent;

// The driver side of a device: its vtable and pointer. A device driver
// is made at once and opens in the background; its instance driver
// answers the opening through a poll event. A def's label is valid
// (label.h) and read only during the call that passes it.
typedef struct mrhiDeviceDriverVtable
{
    uint32_t spiVersion;
    uint32_t size;
    void (*destroy)(void* self);
    // Makes a sampler the core has checked; its handle, never zero.
    mrhiResult (*createSampler)(void* self, const mrhiSamplerDef* def, uint64_t* handleOut);
    void (*destroySampler)(void* self, uint64_t handle);
    // Makes a buffer the core has checked; its handle, never zero.
    mrhiResult (*createBuffer)(void* self, const mrhiBufferDef* def, uint64_t* handleOut);
    void (*destroyBuffer)(void* self, uint64_t handle);
    // Makes a texture the core has checked; its handle, never zero.
    mrhiResult (*createTexture)(void* self, const mrhiTextureDef* def, uint64_t* handleOut);
    // Destroys a texture whose views the core has destroyed.
    void (*destroyTexture)(void* self, uint64_t handle);
    // Makes a view of a texture from a def the core has checked and
    // resolved: its format, usage and counts filled in.
    mrhiResult (*createView)(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut);
    void (*destroyView)(void* self, uint64_t handle);
} mrhiDeviceDriverVtable;

typedef struct mrhiDeviceDriver
{
    const mrhiDeviceDriverVtable* vtable;
    void* self;
} mrhiDeviceDriver;

typedef struct mrhiInstanceDriverVtable
{
    uint32_t spiVersion;
    uint32_t size;
    // Starts a search for adapters, answered by an event with the tag.
    mrhiResult (*requestAdapters)(void* self, uint64_t tag);
    // Moves up to capacity finished requests into events and returns
    // how many it moved.
    size_t (*poll)(void* self, mrhiDriverEvent* events, size_t capacity);
    // Copies up to capacity adapters the last finished search found and
    // returns how many it found.
    size_t (*getAdapters)(const void* self, mrhiDriverAdapter* adapters, size_t capacity);
    // Fills what a format can do on an adapter.
    void (*getFormatCaps)(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut);
    // Makes a surface from the one source chained on a def the core has
    // checked; its handle, never zero. mrhi_errorUnsupported for a
    // source the driver cannot use.
    mrhiResult (*createSurface)(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut);
    void (*destroySurface)(void* self, uint64_t handle);
    // Fills what a surface can do on an adapter, the floors included
    // when the adapter presents there.
    void (*getSurfaceCaps)(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut);
    // Makes a device on an adapter from a def the core has checked (its
    // features and limits are the grant, its label read only during the
    // call), opening it in the background: the open is answered by an
    // event with the tag. An immediate failure is returned instead.
    mrhiResult (*createDevice)(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut);
    void (*destroy)(void* self);
} mrhiInstanceDriverVtable;

// A driver as an instance holds it.
typedef struct mrhiInstanceDriver
{
    const mrhiInstanceDriverVtable* vtable;
    void* self;
} mrhiInstanceDriver;

// Whether a driver's vtable passes the handshake: this SPI version, and
// at least the size the core knows.
bool mrhiIsDriverVtableValid(const mrhiInstanceDriverVtable* vtable);

#endif // MAUL_RHI_SRC_DRIVER_H
