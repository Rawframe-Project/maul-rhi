// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The driver SPI at the instance level (record R15), as the core calls
// it. A driver reports finished work only when the core polls it, and
// never calls the core.

#ifndef MAUL_RHI_SRC_DRIVER_H
#define MAUL_RHI_SRC_DRIVER_H

#include "maul-rhi/capabilities.h"

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
