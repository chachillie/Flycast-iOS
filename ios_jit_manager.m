// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

#import <Foundation/Foundation.h>
#include "ios_jit_manager.h"

#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR

#include <unistd.h>
#include <sys/types.h>

#define CS_OPS_STATUS 0
#define CS_DEBUGGED 0x10000000

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
    // Only check on iOS 26+
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

bool ios_running_under_xcode(void) {
    NSDictionary* environment = [[NSProcessInfo processInfo] environment];
    
    // Check for Xcode-specific environment variables
    // Xcode sets these when running/debugging an app
    NSString* xcodeVersion = [environment objectForKey:@"XCODE_PRODUCT_BUILD_VERSION"];
    if (xcodeVersion != nil) {
        return true;
    }
    
    // Check for DYLD settings that Xcode uses
    NSString* dyldInsert = [environment objectForKey:@"DYLD_INSERT_LIBRARIES"];
    if (dyldInsert != nil && [dyldInsert containsString:@"/Xcode.app/"]) {
        return true;
    }
    
    // When debugging with Xcode, it often sets these
    if ([environment objectForKey:@"IDE_DISABLED_OS_ACTIVITY_DT_MODE"] != nil) {
        return true;
    }
    
    // For simulator builds, Xcode sets SIMULATOR_*
    if ([environment objectForKey:@"SIMULATOR_DEVICE_NAME"] != nil) {
        // Check if it's actually Xcode (not just simulator runtime)
        NSString* dyldLibraryPath = [environment objectForKey:@"DYLD_LIBRARY_PATH"];
        if (dyldLibraryPath != nil && [dyldLibraryPath containsString:@"/Xcode.app/"]) {
            return true;
        }
    }
    
    return false;
}

IOSJitType ios_determine_jit_type(void) {
    static bool initialized = false;
    static IOSJitType result = IOS_JIT_LEGACY;
    
    if (!initialized) {
        initialized = true;
        
        bool has_txm = ios_device_has_txm();
        bool is_debugged = ios_process_is_debugged();
        bool under_xcode = ios_running_under_xcode();
        
        NSLog(@"[Flycast JIT] Device detection:");
        NSLog(@"  - TXM firmware: %@", has_txm ? @"YES" : @"NO");
        NSLog(@"  - Debugger attached: %@", is_debugged ? @"YES" : @"NO");
        NSLog(@"  - Running under Xcode: %@", under_xcode ? @"YES" : @"NO");
        
        if (has_txm) {
            // iOS 26+ device with TXM
            if (under_xcode) {
                NSLog(@"[Flycast JIT] ERROR: TXM device detected but running under Xcode.");
                NSLog(@"[Flycast JIT] Xcode debugging is not compatible with TXM JIT.");
                NSLog(@"[Flycast JIT] Please use StikDebug instead.");
                NSLog(@"[Flycast JIT] Falling back to LEGACY mode (will likely fail).");
                result = IOS_JIT_LEGACY;
            } else if (!is_debugged) {
                NSLog(@"[Flycast JIT] WARNING: TXM device detected but no debugger attached.");
                NSLog(@"[Flycast JIT] JIT will not work. Please attach StikDebug to enable JIT.");
                NSLog(@"[Flycast JIT] Falling back to LEGACY mode (will fail without debugger).");
                result = IOS_JIT_LEGACY;
            } else {
                NSLog(@"[Flycast JIT] TXM device with debugger attached (not Xcode).");
                NSLog(@"[Flycast JIT] Using TXM mode with dual-mapped memory pool.");
                result = IOS_JIT_TXM;
            }
        } else {
            // Non-TXM device or iOS < 26
            if (@available(iOS 14, *)) {
                NSLog(@"[Flycast JIT] Non-TXM device (iOS < 26 or no TXM firmware).");
                NSLog(@"[Flycast JIT] Using MAP_JIT mode (standard dual-mapping).");
                result = IOS_JIT_MAP_JIT;
            } else {
                NSLog(@"[Flycast JIT] Older iOS version (< 14).");
                NSLog(@"[Flycast JIT] Using LEGACY mode (debugger-based).");
                result = IOS_JIT_LEGACY;
            }
        }
        
        NSLog(@"[Flycast JIT] Selected strategy: %s", ios_jit_type_description(result));
    }
    
    return result;
}

const char* ios_jit_type_description(IOSJitType type) {
    switch (type) {
        case IOS_JIT_LEGACY:
            return "LEGACY (debugger-based JIT)";
        case IOS_JIT_MAP_JIT:
            return "MAP_JIT (standard dual-mapping)";
        case IOS_JIT_TXM:
            return "TXM (iOS 26+ dual-mapped pool)";
        default:
            return "UNKNOWN";
    }
}

#endif // TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
