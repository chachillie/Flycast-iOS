//
//  vmem_legacy.cpp
//  flycast
//
//  Created by Chachillie on 1/20/26.
//


// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Main iOS vmem dispatcher - routes to appropriate implementation
// based on device capabilities detected at runtime

#include "types.h"

#ifdef TARGET_IPHONE

#include "../ios_jit_manager.h"
#include "oslib/virtmem.h"
#include "log/Log.h"

namespace virtmem {

// Forward declarations for implementation-specific functions
// Legacy mode (iOS < 14 or fallback)
bool prepare_jit_block_legacy(void *code_area, size_t size, void **code_area_rwx);
void release_jit_block_legacy(void *code_area, size_t size);
bool prepare_jit_block_legacy_dual(void *code_area, size_t size, 
                                   void **code_area_rw, ptrdiff_t *rx_offset);
void release_jit_block_legacy_dual(void *code_area1, void *code_area2, size_t size);

// MAP_JIT mode (iOS 14-25, non-TXM devices)
bool prepare_jit_block_map_jit(void *code_area, size_t size, void **code_area_rwx);
void release_jit_block_map_jit(void *code_area, size_t size);
bool prepare_jit_block_map_jit_dual(void *code_area, size_t size,
                                     void **code_area_rw, ptrdiff_t *rx_offset);
void release_jit_block_map_jit_dual(void *code_area_rx, void *code_area_rw, size_t size);

// TXM mode (iOS 26+ with TXM firmware and StikDebug)
bool prepare_jit_block_txm(void *code_area, size_t size, void **code_area_rwx);
void release_jit_block_txm(void *code_area, size_t size);
bool prepare_jit_block_txm_dual(void *code_area, size_t size,
                                 void **code_area_rw, ptrdiff_t *rx_offset);
void release_jit_block_txm_dual(void *code_area1, void *code_area2, size_t size);

// Global JIT type - determined once at startup
static IOSJitType g_jit_type = IOS_JIT_LEGACY;
static bool g_jit_type_determined = false;

static void determine_jit_type()
{
    if (g_jit_type_determined)
        return;
    
    g_jit_type = ios_determine_jit_type();
    g_jit_type_determined = true;
    
    INFO_LOG(VMEM, "iOS JIT Strategy: %s", ios_jit_type_description(g_jit_type));
}

// Public API - Single RWX block (for compatibility)
bool prepare_jit_block(void *code_area, size_t size, void **code_area_rwx)
{
    determine_jit_type();
    
    switch (g_jit_type) {
        case IOS_JIT_TXM:
            return prepare_jit_block_txm(code_area, size, code_area_rwx);
        
        case IOS_JIT_MAP_JIT:
            return prepare_jit_block_map_jit(code_area, size, code_area_rwx);
        
        case IOS_JIT_LEGACY:
        default:
            return prepare_jit_block_legacy(code_area, size, code_area_rwx);
    }
}

void release_jit_block(void *code_area, size_t size)
{
    if (!g_jit_type_determined) {
        WARN_LOG(VMEM, "release_jit_block called before determine_jit_type");
        return;
    }
    
    switch (g_jit_type) {
        case IOS_JIT_TXM:
            release_jit_block_txm(code_area, size);
            break;
        
        case IOS_JIT_MAP_JIT:
            release_jit_block_map_jit(code_area, size);
            break;
        
        case IOS_JIT_LEGACY:
        default:
            release_jit_block_legacy(code_area, size);
            break;
    }
}

// Public API - Dual-mapped blocks (RW + RX separate)
bool prepare_jit_block(void *code_area, size_t size, void **code_area_rw, ptrdiff_t *rx_offset)
{
    determine_jit_type();
    
    switch (g_jit_type) {
        case IOS_JIT_TXM:
            return prepare_jit_block_txm_dual(code_area, size, code_area_rw, rx_offset);
        
        case IOS_JIT_MAP_JIT:
            return prepare_jit_block_map_jit_dual(code_area, size, code_area_rw, rx_offset);
        
        case IOS_JIT_LEGACY:
        default:
            // Legacy mode doesn't support dual-mapping
            return prepare_jit_block_legacy_dual(code_area, size, code_area_rw, rx_offset);
    }
}

void release_jit_block(void *code_area1, void *code_area2, size_t size)
{
    if (!g_jit_type_determined) {
        WARN_LOG(VMEM, "release_jit_block(dual) called before determine_jit_type");
        return;
    }
    
    switch (g_jit_type) {
        case IOS_JIT_TXM:
            release_jit_block_txm_dual(code_area1, code_area2, size);
            break;
        
        case IOS_JIT_MAP_JIT:
            release_jit_block_map_jit_dual(code_area1, code_area2, size);
            break;
        
        case IOS_JIT_LEGACY:
        default:
            release_jit_block_legacy_dual(code_area1, code_area2, size);
            break;
    }
}

void jit_set_exec(void* code, size_t size, bool enable) {
    // No-op for iOS dual-mapped modes
    // Legacy mode might need this, but typically doesn't
}

} // namespace virtmem

#endif // TARGET_IPHONE
