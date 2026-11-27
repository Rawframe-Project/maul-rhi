// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The linear container's WGSL: the entries of linear.comp and
// linear.gi.comp, each reading one flattened index as the CTS's
// dispatch,linear_indexing_range shader does.

requires linear_indexing;

@compute @workgroup_size(1, 1, 1)
fn wi(@builtin(workgroup_index) index: u32) {
    _ = index;
}

@compute @workgroup_size(2, 1, 1)
fn gi(@builtin(global_invocation_index) index: u32) {
    _ = index;
}
