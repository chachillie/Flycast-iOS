//
//  vmem_legacy.cpp
//  flycast
//
//  Created by Chachillie on 1/20/26.
//


// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Legacy JIT mode for older iOS devices or when debugger is required
// Uses traditional debugger-based JIT with simple memory allocation

#include "types.h"

#ifdef TARGET_IPHONE

#include <sys/mman.h>
#include <unistd.h>
#include "oslib/virtmem.h"
#include "log/Log.h"

namespace virtmem {

bool prepare_jit_block_legacy(void *code_area, size_t size, void **code_area_rwx)
{
    INFO_LOG(VMEM, "Legacy: Allocating JIT block size=%zu", size);
    
    // Simple RWX allocation for legacy mode
    void *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANON | MAP_PRIVATE, -1, 0);
    
    if (ptr == MAP_FAILED) {
        ERROR_LOG(VMEM, "Legacy: mmap failed: %s", strerror(errno));
        return false;
    }
    
    *code_area_rwx = ptr;
    INFO_LOG(VMEM, "Legacy: Allocated at %p", ptr);
    return true;
}

void release_jit_block_legacy(void *code_area, size_t size)
{
    INFO_LOG(VMEM, "Legacy: Releasing JIT block at %p size=%zu", code_area, size);
    munmap(code_area, size);
}

bool prepare_jit_block_legacy_dual(void *code_area, size_t size, 
                                   void **code_area_rw, ptrdiff_t *rx_offset)
{
    // Legacy mode doesn't support dual-mapping
    // Return failure so caller falls back to single RWX
    WARN_LOG(VMEM, "Legacy: Dual-mapping not supported, use single RWX mode");
    return false;
}

void release_jit_block_legacy_dual(void *code_area1, void *code_area2, size_t size)
{
    // Should not be called in legacy mode
    WARN_LOG(VMEM, "Legacy: Unexpected dual-mapping release call");
}

} // namespace virtmem

#endif // TARGET_IPHONE