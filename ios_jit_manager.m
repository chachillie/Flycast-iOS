// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

#import <Foundation/Foundation.h>
#include "ios_jit_manager.h"

#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR

#include <unistd.h>
#include <sys/types.h>

#define CS_OPS_STATUS 0
#define CS_DEBUGGED 0x10000000

// Properly declare csops for both C and C++
#ifdef __cplusplus
extern "C" {
#endif

int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

#ifdef __cplusplus
}
#endif

static NSString* filePathAtPath(NSString* path, NSUInteger length) {
    NSError *error = nil;
    NSArray<NSString *> *items = [[NSFileManager defaultManager]
        contentsOfDirectoryAtPath:path error:&error];
    if (!items) {
        return nil;
    }
    
    for (NSString *entry in items) {
        if (entry.length == length) {
            return [path stringByAppendingPathComponent:entry];
        }
    }
    return nil;
}

bool ios_device_has_txm(void) {
    if (@available(iOS 26, *)) {
        // Primary path: /System/Volumes/Preboot/<36>/boot/<96>/usr/.../Ap,TrustedExecutionMonitor.img4
        NSString* bootUUID = filePathAtPath(@"/System/Volumes/Preboot", 36);
        if (bootUUID) {
            NSString* bootDir = [bootUUID stringByAppendingPathComponent:@"boot"];
            NSString* ninetySixCharPath = filePathAtPath(bootDir, 96);
            if (ninetySixCharPath) {
                NSString* img = [ninetySixCharPath stringByAppendingPathComponent:
                    @"usr/standalone/firmware/FUD/Ap,TrustedExecutionMonitor.img4"];
                if (access(img.fileSystemRepresentation, F_OK) == 0) {
                    return true;
                }
            }
        }
        
        // Fallback path: /private/preboot/<96>/usr/.../Ap,TrustedExecutionMonitor.img4
        NSString* fallback = filePathAtPath(@"/private/preboot", 96);
        if (fallback) {
            NSString* img = [fallback stringByAppendingPathComponent:
                @"usr/standalone/firmware/FUD/Ap,TrustedExecutionMonitor.img4"];
            if (access(img.fileSystemRepresentation, F_OK) == 0) {
                return true;
            }
        }
    }
    
    return false;
}

bool ios_process_is_debugged(void) {
    int flags;
    if (csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) != 0) {
        return false;
    }
    return (flags & CS_DEBUGGED) != 0;
}

bool ios_can_use_txm_jit(void) {
    static bool checked = false;
    static bool result = false;
    
    if (!checked) {
        checked = true;
        
        bool has_txm = ios_device_has_txm();
        bool is_debugged = ios_process_is_debugged();
        
        if (has_txm && !is_debugged) {
            NSLog(@"[Flycast] WARNING: TXM device detected but no debugger attached.");
            NSLog(@"[Flycast] JIT compilation will not work. Please attach StikDebug to enable JIT.");
            result = false;
        } else if (has_txm && is_debugged) {
            // Check if running under Xcode (which won't work with TXM)
            NSDictionary* environment = [[NSProcessInfo processInfo] environment];
            if ([environment objectForKey:@"XCODE"] != nil) {
                NSLog(@"[Flycast] ERROR: TXM device detected but running under Xcode.");
                NSLog(@"[Flycast] Xcode debugging is not compatible with TXM JIT.");
                NSLog(@"[Flycast] Please use StikDebug instead.");
                result = false;
            } else {
                NSLog(@"[Flycast] TXM device detected with debugger attached.");
                NSLog(@"[Flycast] Enabling TXM JIT mode with dual-mapped memory pool.");
                result = true;
            }
        } else {
            NSLog(@"[Flycast] Non-TXM device or iOS < 26. Using standard MAP_JIT mode.");
            result = false;
        }
    }
    
    return result;
}

#endif // TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
