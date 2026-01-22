// Copyright 2025 Flycast Project
// SPDX-License-Identifier: GPL-2.0-or-later

// TXM JIT mode for iOS 26+ devices with TXM firmware
// Uses pre-allocated 512MB pool with lwmem suballocation
// Requires StikDebug debugger attachment

#include "types.h"

#ifdef TARGET_IPHONE

#include <sys/mman.h>
#include <mach/mach.h>
#include <unistd.h>
#include "oslib/virtmem.h"
#include "log/Log.h"

extern "C" {
#include "lwmem.h"
}

namespace virtmem {

constexpr size_t TXM_POOL_SIZE = 536870912;  // 512 MB

struct TXMPool {
    u8* rx_region = nullptr;
    u8* rw_region = nullptr;
    ptrdiff_t rw_rx_diff = 0;
    bool initialized = false;
};

static TXMPool g_txm_pool;

bool init_txm_pool()
{
    if (g_txm_pool.initialized)
        return true;
    
    INFO_LOG(VMEM, "TXM: Initializing 512MB JIT pool...");
    
    const size_t size = TXM_POOL_SIZE;
    
    // Allocate RX region
    u8* rx_ptr = static_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_EXEC,
                                       MAP_ANON | MAP_PRIVATE, -1, 0));
    
    if (rx_ptr == MAP_FAILED) {
        ERROR_LOG(VMEM, "TXM: Failed to allocate RX region: %s", strerror(errno));
        return false;
    }
    
    // Execute TXM debugger registration
    INFO_LOG(VMEM, "TXM: Executing debugger registration (brk #0x69)");
    __asm__ volatile (
        "mov x0, %0\n"
        "mov x1, %1\n"
        "brk #0x69"
        :: "r" (rx_ptr), "r" (size)
        : "x0", "x1", "memory"
    );
    INFO_LOG(VMEM, "TXM: Debugger registration completed");
    
    // Create RW mapping via vm_remap
    vm_address_t rw_region = 0;
    vm_address_t target = reinterpret_cast<vm_address_t>(rx_ptr);
    vm_prot_t cur_protection = 0;
    vm_prot_t max_protection = 0;
    
    INFO_LOG(VMEM, "TXM: Creating dual mapping with vm_remap...");
    kern_return_t retval = vm_remap(
        mach_task_self(), &rw_region, size, 0,
        VM_FLAGS_ANYWHERE, mach_task_self(), target, FALSE,
        &cur_protection, &max_protection,
        VM_INHERIT_DEFAULT
    );
    
    if (retval != KERN_SUCCESS) {
        ERROR_LOG(VMEM, "TXM: vm_remap failed with code 0x%x", retval);
        munmap(rx_ptr, size);
        return false;
    }
    
    u8* rw_ptr = reinterpret_cast<u8*>(rw_region);
    
    // Set RW permissions
    if (mprotect(rw_ptr, size, PROT_READ | PROT_WRITE) != 0) {
        ERROR_LOG(VMEM, "TXM: mprotect for RW region failed: %s", strerror(errno));
        munmap(rx_ptr, size);
        munmap(rw_ptr, size);
        return false;
    }
    
    // Initialize lwmem for suballocation
    lwmem_region_t regions[] = {
        { (void*)rw_ptr, size },
        { NULL, 0 }
    };
    
    INFO_LOG(VMEM, "TXM: Initializing lwmem memory manager...");
    size_t lwret = lwmem_assignmem(regions);
    if (lwret == 0) {
        ERROR_LOG(VMEM, "TXM: lwmem_assignmem failed");
        munmap(rx_ptr, size);
        munmap(rw_ptr, size);
        return false;
    }
    
    g_txm_pool.rx_region = rx_ptr;
    g_txm_pool.rw_region = rw_ptr;
    g_txm_pool.rw_rx_diff = rw_ptr - rx_ptr;
    g_txm_pool.initialized = true;
    
    INFO_LOG(VMEM, "TXM: Pool initialized successfully");
    INFO_LOG(VMEM, "  RX region: %p", rx_ptr);
    INFO_LOG(VMEM, "  RW region: %p", rw_ptr);
    INFO_LOG(VMEM, "  RW->RX offset: %td bytes", g_txm_pool.rw_rx_diff);
    
    return true;
}

bool prepare_jit_block_txm(void *code_area, size_t size, void **code_area_rwx)
{
    if (!g_txm_pool.initialized) {
        if (!init_txm_pool()) {
            ERROR_LOG(VMEM, "TXM: Pool initialization failed");
            return false;
        }
    }
    
    const size_t pagesize = sysconf(_SC_PAGESIZE);
    
    // Allocate from RW pool with alignment metadata
    void* raw = lwmem_malloc(size + pagesize - 1 + sizeof(void*));
    if (!raw) {
        ERROR_LOG(VMEM, "TXM: lwmem_malloc failed for size %zu", size);
        ERROR_LOG(VMEM, "TXM: Pool exhausted - 512MB limit reached");
        return false;
    }
    
    // Align to page boundary
    uintptr_t raw_addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned = (raw_addr + pagesize - 1) & ~(pagesize - 1);
    
    // Store raw pointer before aligned address for later free
    ((void**)aligned)[-1] = raw;
    
    // Return RX pointer (subtract diff to convert RW -> RX)
    *code_area_rwx = (u8*)aligned - g_txm_pool.rw_rx_diff;
    
    DEBUG_LOG(VMEM, "TXM: Allocated RW=%p RX=%p size=%zu",
              (void*)aligned, *code_area_rwx, size);
    
    return true;
}

void release_jit_block_txm(void *code_area, size_t size)
{
    if (!g_txm_pool.initialized)
        return;
    
    // Convert RX pointer back to RW to access metadata
    void* rw_ptr = (u8*)code_area + g_txm_pool.rw_rx_diff;
    void* raw = ((void**)rw_ptr)[-1];
    
    DEBUG_LOG(VMEM, "TXM: Freeing RX=%p RW=%p", code_area, rw_ptr);
    lwmem_free(raw);
}

bool prepare_jit_block_txm_dual(void *code_area, size_t size,
                                 void **code_area_rw, ptrdiff_t *rx_offset)
{
    if (!g_txm_pool.initialized) {
        if (!init_txm_pool()) {
            ERROR_LOG(VMEM, "TXM: Pool initialization failed");
            return false;
        }
    }
    
    const size_t pagesize = sysconf(_SC_PAGESIZE);
    
    void* raw = lwmem_malloc(size + pagesize - 1 + sizeof(void*));
    if (!raw) {
        ERROR_LOG(VMEM, "TXM: lwmem_malloc failed for size %zu", size);
        return false;
    }
    
    uintptr_t raw_addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned = (raw_addr + pagesize - 1) & ~(pagesize - 1);
    ((void**)aligned)[-1] = raw;
    
    // For dual-mapping mode, return RW pointer and offset
    *code_area_rw = (void*)aligned;
    *rx_offset = -g_txm_pool.rw_rx_diff;  // Negative because RW > RX
    
    INFO_LOG(VMEM, "TXM: Dual-map allocated RW=%p rx_offset=%td size=%zu",
             *code_area_rw, *rx_offset, size);
    
    return true;
}

void release_jit_block_txm_dual(void *code_area1, void *code_area2, size_t size)
{
    if (!g_txm_pool.initialized)
        return;
    
    // code_area2 is the RW pointer
    void* raw = ((void**)code_area2)[-1];
    
    DEBUG_LOG(VMEM, "TXM: Freeing dual-map RW=%p", code_area2);
    lwmem_free(raw);
}

} // namespace virtmem

#endif // TARGET_IPHONE
