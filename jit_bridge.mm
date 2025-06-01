// Bridge between C++ and Objective-C JitManager
#import "jit_bridge.h"

#if defined(TARGET_IPHONE)
#import "JitManager.h"

bool flycast_jit_check_availability(void) {
    if (@available(iOS 26, *)) {
        [[JitManager shared] recheckIfJitIsAcquired];
        return [[JitManager shared] acquiredJit];
    }
    // For iOS < 26, assume JIT is available (will use AltKit fallback)
    return true;
}

bool flycast_jit_is_txm_device(void) {
    if (@available(iOS 26, *)) {
        return [[JitManager shared] deviceHasTxm];
    }
    return false;
}

const char* flycast_jit_get_error(void) {
    NSString *error = [[JitManager shared] acquisitionError];
    if (error) {
        return [error UTF8String];
    }
    return nullptr;
}

#else

// Non-iOS platforms
bool flycast_jit_check_availability(void) {
    return true;
}

bool flycast_jit_is_txm_device(void) {
    return false;
}

const char* flycast_jit_get_error(void) {
    return nullptr;
}

#endif