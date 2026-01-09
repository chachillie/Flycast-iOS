// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR

#ifdef __cplusplus
extern "C" {
#endif

// Returns true if device has TXM (iOS 26+ with TXM firmware)
bool ios_device_has_txm(void);

// Returns true if process is being debugged
bool ios_process_is_debugged(void);

// Returns true if TXM is present AND debugger is attached (StikDebug)
// This is the function to call to determine if TXM JIT mode should be used
bool ios_can_use_txm_jit(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
#endif // __APPLE__
