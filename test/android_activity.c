// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance suite as an Android application (android_activity.h).
// The first window the activity gets starts the suite on a thread, with
// the host's MAUL_RHI_* variables, which tools/run_android_app.sh writes
// to files/environment (an application has no shell to inherit them
// from), and its output in files/out, ended by "result: N failures",
// which the runner reads. Android waits for a window's destruction
// callback before taking the window away, so the callback waits for the
// suite.

#include "android_activity.h"

#include <android/native_activity.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void);

static ANativeWindow* s_window;
static pthread_t s_suite;
static bool s_running;
static char s_files[512];

ANativeWindow* mrhiTestAndroidWindow(void)
{
    return s_window;
}

// Each line "NAME=value" of files/environment set.
static void ReadEnvironment(void)
{
    char path[600];
    (void)snprintf(path, sizeof(path), "%s/environment", s_files);
    FILE* file = fopen(path, "r");
    char line[256];
    while (file != nullptr && fgets(line, sizeof(line), file) != nullptr)
    {
        line[strcspn(line, "\r\n")] = '\0';
        char* equals = strchr(line, '=');
        if (equals != nullptr && strncmp(line, "MAUL_RHI_", 9) == 0)
        {
            *equals = '\0';
            (void)setenv(line, equals + 1, 1);
        }
    }
    if (file != nullptr)
    {
        (void)fclose(file);
    }
}

static void* RunSuite(void* user)
{
    (void)user;
    ReadEnvironment();
    char path[600];
    (void)snprintf(path, sizeof(path), "%s/out", s_files);
    if (freopen(path, "w", stdout) != nullptr)
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
    int status = main();
    printf("result: %d failures\n", status);
    return nullptr;
}

static void OnWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    if (s_running)
    {
        return;
    }
    ANativeWindow_acquire(window);
    s_window = window;
    s_running = pthread_create(&s_suite, nullptr, RunSuite, nullptr) == 0;
}

// The suite ends before the window or the activity goes.
static void WaitForSuite(void)
{
    if (s_running)
    {
        (void)pthread_join(s_suite, nullptr);
        s_running = false;
    }
    if (s_window != nullptr)
    {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }
}

static void OnWindowDestroyed(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    (void)window;
    WaitForSuite();
}

static void OnDestroy(ANativeActivity* activity)
{
    (void)activity;
    WaitForSuite();
}

__attribute__((visibility("default"))) void
ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;
    (void)snprintf(s_files, sizeof(s_files), "%s", activity->internalDataPath);
    activity->callbacks->onNativeWindowCreated = OnWindowCreated;
    activity->callbacks->onNativeWindowDestroyed = OnWindowDestroyed;
    activity->callbacks->onDestroy = OnDestroy;
}
