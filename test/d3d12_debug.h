// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 debug layer for the suites that run the D3D12 driver
// (mrhi-0003): turned on, with GPU-based validation, when
// MAUL_RHI_D3D12_DEBUG is set and not empty, before the library opens a
// device. D3D12 keeps one device per adapter, so watching each
// adapter's device first watches the library's too.

#ifndef MAUL_RHI_TEST_D3D12_DEBUG_H
#define MAUL_RHI_TEST_D3D12_DEBUG_H

#include <stdint.h>

// Turns the debug layer on where asked and watches every adapter's
// device, printing each error it reports.
void mrhiTestWatchD3d12(void);

// The errors the debug layer has reported so far.
uint32_t mrhiTestD3d12Errors(void);

#endif // MAUL_RHI_TEST_D3D12_DEBUG_H
