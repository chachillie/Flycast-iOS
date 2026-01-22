//
//  vmem_no_txm.cpp
//  flycast
//
//  Created by Chachillie on 1/20/26.
//


// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

// MAP_JIT mode for non-TXM iOS devices (iOS 14-25)
// Uses vm_remap for dual-mapped memory (RW + RX pointing to same physical pages)

#include "types.h"

#ifdef TARGET_IPHONE

#include <sys/mman.h>
#include <mach/mach.h>
#include <unistd.h>
#include "oslib/virtmem.h"
#include "log/Log.h"

namespace virtmem {

bool prepare_jit_block_map_jit(void *code_area, size_t size, void **code_area_rwx)
{
    INFO_LOG(VMEM, "MAP_JIT: Allocating single RX block size=%zu", size);
    
    // Allocate RX region
    void *rx_ptr = mmap(nullptr, size, PROT_READ | PROT_EXEC,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
    
    if (rx_ptr == MAP_FAILED) {
        ERROR_LOG(VMEM, "MAP_JIT: Failed to allocate RX region: %s", strerror(errno));
        return false;
    }
    
    *code_area_rwx = rx_ptr;
    INFO_LOG(VMEM, "MAP_JIT: Allocated RX at %p", rx_ptr);
    return true;
}

void release_jit_block_map_jit(void *code_area, size_t size)
{
    INFO_LOG(VMEM, "MAP_JIT: Releasing single block at %p size=%zu", code_area, size);
    munmap(code_area, size);
}

bool prepare_jit_block_map_jit_dual(void *code_area, size_t size,
                                     void **code_area_rw, ptrdiff_t *rx_offset)
{
    INFO_LOG(VMEM, "MAP_JIT: Creating dual-mapped block size=%zu", size);
    
    // Allocate RX region
    u8* rx_ptr = static_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_EXEC,
                                       MAP_ANON | MAP_PRIVATE, -1, 0));
    
    if (rx_ptr == MAP_FAILED) {
        ERROR_LOG(VMEM, "MAP_JIT: Failed to allocate RX region: %s", strerror(errno));
        return false;
    }
    
    // Create RW mapping via vm_remap
    vm_address_t rw_region = 0;
    vm_address_t target = reinterpret_cast<vm_address_t>(rx_ptr);
    vm_prot_t cur_protection = 0;
    vm_prot_t max_protection = 0;
    
    kern_return_t retval = vm_remap(
        mach_task_self(), &rw_region, size, 0,
        VM_FLAGS_ANYWHERE, mach_task_self(), target, FALSE,
        &cur_protection, &max_protection,
        VM_INHERIT_DEFAULT
    );
    
    if (retval != KERN_SUCCESS) {
        ERROR_LOG(VMEM, "MAP_JIT: vm_remap failed with code 0x%x", retval);
        munmap(rx_ptr, size);
        return false;
    }
    
    u8* rw_ptr = reinterpret_cast<u8*>(rw_region);
    
    // Set RW permissions
    if (mprotect(rw_ptr, size, PROT_READ | PROT_WRITE) != 0) {
        ERROR_LOG(VMEM, "MAP_JIT: mprotect failed: %s", strerror(errno));
        munmap(rx_ptr, size);
        munmap(rw_ptr, size);
        return false;
    }
    
    *code_area_rw = rw_ptr;
    *rx_offset = rx_ptr - rw_ptr;  // Positive offset from RW to RX
    
    INFO_LOG(VMEM, "MAP_JIT: Dual-map created - RX=%p RW=%p offset=%td",
             rx_ptr, rw_ptr, *rx_offset);
    
    return true;
}

void release_jit_block_map_jit_dual(void *code_area_rx, void *code_area_rw, size_t size)
{
    INFO_LOG(VMEM, "MAP_JIT: Releasing dual-mapped block RX=%p RW=%p size=%zu",
             code_area_rx, code_area_rw, size);
    
    // Unmap both regions
    if (code_area_rx)
        munmap(code_area_rx, size);
    if (code_area_rw)
        munmap(code_area_rw, size);
}

} // namespace virtmem

#endif // TARGET_IPHONE
