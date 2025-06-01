// Bridge between C++ and Objective-C JitManager
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool flycast_jit_check_availability(void);
bool flycast_jit_is_txm_device(void);
const char* flycast_jit_get_error(void);

#ifdef __cplusplus
}
#endif