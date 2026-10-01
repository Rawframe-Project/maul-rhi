// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance suite as an Android application (mrhi-0017), on the
// system's own NativeActivity: the suite runs on a thread of its own
// once the activity has a window, which its surface checks present to.

#ifndef MAUL_RHI_TEST_ANDROID_ACTIVITY_H
#define MAUL_RHI_TEST_ANDROID_ACTIVITY_H

#include <android/native_window.h>

// The activity's window while the suite runs.
ANativeWindow* mrhiTestAndroidWindow(void);

#endif // MAUL_RHI_TEST_ANDROID_ACTIVITY_H
