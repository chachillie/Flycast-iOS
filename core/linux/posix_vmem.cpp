// Implementation of the vmem related function for POSIX-like platforms.
// There's some minimal amount of platform specific hacks to support
// Android and OSX since they are slightly different in some areas.
#include "types.h"


#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#include <unistd.h>

#include "hw/mem/addrspace.h"
#include "hw/sh4/sh4_if.h"
#include "oslib/virtmem.h"
#include "log/Log.h"

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0
#endif

#ifdef __ANDROID__
#include <linux/ashmem.h>

// Only available in SDK 26+. Required in SDK 29+ (android 10)
extern "C" int __attribute__((weak)) ASharedMemory_create(const char*, size_t);

// Android specific ashmem-device stuff for creating shared memory regions
static int ashmem_create_region(const char *name, size_t size)
{
    int fd = -1;
    if (ASharedMemory_create != nullptr)
    {
        fd = ASharedMemory_create(name, size);
        if (fd < 0)
            WARN_LOG(VMEM, "ASharedMemory_create failed: errno %d", errno);
    }

    if (fd < 0)
    {
        fd = open("/" ASHMEM_NAME_DEF, O_RDWR);
        if (fd >= 0 && ioctl(fd, ASHMEM_SET_SIZE, size) < 0)
        {
            close(fd);
            fd = -1;
        }
    }

    return fd;
}
#endif  // #ifdef __ANDROID__

namespace virtmem
{

bool region_lock(void *start, size_t len)
{
    size_t inpage = (uintptr_t)start & PAGE_MASK;
    if (mprotect((u8*)start - inpage, len + inpage, PROT_READ))
        die("mprotect failed...");
    return true;
}

bool region_unlock(void *start, size_t len)
{
    size_t inpage = (uintptr_t)start & PAGE_MASK;
    if (mprotect((u8*)start - inpage, len + inpage, PROT_READ | PROT_WRITE))
        // Add some way to see why it failed? gdb> info proc mappings
        die("mprotect  failed...");
    return true;
}

bool region_set_exec(void *start, size_t len)
{
    size_t inpage = (uintptr_t)start & PAGE_MASK;
    int protFlags = PROT_READ | PROT_EXEC;
#ifndef TARGET_IPHONE
    protFlags |= PROT_WRITE;
#endif
    if (mprotect((u8*)start - inpage, len + inpage, protFlags))
    {
        WARN_LOG(VMEM, "region_set_exec: mprotect failed. errno %d", errno);
        return false;
    }
    return true;
}

static void *mem_region_reserve(void *start, size_t len)
{
    void *p = mmap(start, len, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }
    else
        return p;
}

static bool mem_region_release(void *start, size_t len)
{
    return munmap(start, len) == 0;
}

static void *mem_region_map_file(void *file_handle, void *dest, size_t len, size_t offset, bool readwrite)
{
    int flags = MAP_SHARED | MAP_NOSYNC | (dest != NULL ? MAP_FIXED : 0);
    void *p = mmap(dest, len, PROT_READ | (readwrite ? PROT_WRITE : 0), flags, (int)(uintptr_t)file_handle, offset);
    if (p == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }
    else
        return p;
}

// Allocates memory via a fd on shmem/ahmem or even a file on disk
static int allocate_shared_filemem(unsigned size)
{
    int fd = -1;
#if defined(__ANDROID__)
    // Use Android's specific shmem stuff.
    fd = ashmem_create_region("RAM", size);
#else
    #if !defined(__APPLE__)
        fd = shm_open("/dcnzorz_mem", O_CREAT | O_EXCL | O_RDWR, S_IREAD | S_IWRITE);
        shm_unlink("/dcnzorz_mem");
    #endif

    // if shmem does not work (or using OSX) fallback to a regular file on disk
    if (fd < 0) {
        std::string path = get_writable_data_path("dcnzorz_mem");
        fd = open(path.c_str(), O_CREAT|O_RDWR|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
        unlink(path.c_str());
    }
    if (fd >= 0)
    {
        // Finally make the file as big as we need!
        if (ftruncate(fd, size)) {
            // Can't get as much memory as needed, fallback.
            close(fd);
            fd = -1;
        }
    }
#endif
    if (fd < 0)
        WARN_LOG(VMEM, "Virtual memory file allocation failed: errno %d", errno);

    return fd;
}

// Implement vmem initialization for RAM, ARAM, VRAM and SH4 context, fpcb etc.

int vmem_fd = -1;
static void *reserved_base;
static size_t reserved_size;

// vmem_base_addr points to an address space of 512MB that can be used for fast memory ops.
// In negative offsets of the pointer (up to FPCB size, usually 65/129MB) the context and jump table
// can be found. If the platform init returns error, the user is responsible for initializing the
// memory using a fallback (that is, regular mallocs and falling back to slow memory JIT).
bool init(void **vmem_base_addr, void **sh4rcb_addr, size_t ramSize)
{
    // Firt let's try to allocate the shm-backed memory
    vmem_fd = allocate_shared_filemem(ramSize);
    if (vmem_fd < 0)
        return false;

    // Now try to allocate a contiguous piece of memory.
    reserved_size = 512_MB + sizeof(Sh4RCB) + ARAM_SIZE_MAX + 0x10000;
    reserved_base = mem_region_reserve(NULL, reserved_size);
    if (!reserved_base) {
        close(vmem_fd);
        return false;
    }

    // Align pointer to 64KB too, some Linaro bug (no idea but let's just be safe I guess).
    uintptr_t ptrint = (uintptr_t)reserved_base;
    ptrint = (ptrint + 0x10000 - 1) & (~0xffff);
    *sh4rcb_addr = (void*)ptrint;
    *vmem_base_addr = (void*)(ptrint + sizeof(Sh4RCB));
    const size_t fpcb_size = sizeof(((Sh4RCB *)NULL)->fpcb);
    void *sh4rcb_base_ptr  = (void*)(ptrint + fpcb_size);

    // Now map the memory for the SH4 context, do not include FPCB on purpose (paged on demand).
    region_unlock(sh4rcb_base_ptr, sizeof(Sh4RCB) - fpcb_size);

    return true;
}

// Just tries to wipe as much as possible in the relevant area.
void destroy()
{
    if (reserved_base != nullptr)
    {
        mem_region_release(reserved_base, reserved_size);
        reserved_base = nullptr;
    }
    if (vmem_fd >= 0)
    {
        close(vmem_fd);
        vmem_fd = -1;
    }
}

// Resets a chunk of memory by deleting its data and setting its protection back.
void reset_mem(void *ptr, unsigned size_bytes) {
    // Mark them as non accessible.
    mprotect(ptr, size_bytes, PROT_NONE);
    // Tell the kernel to flush'em all (FIXME: perhaps unmap+mmap 'd be better?)
    madvise(ptr, size_bytes, MADV_DONTNEED);
    #if defined(MADV_REMOVE)
    madvise(ptr, size_bytes, MADV_REMOVE);
    #elif defined(MADV_FREE)
    madvise(ptr, size_bytes, MADV_FREE);
    #endif
}

// Allocates a bunch of memory (page aligned and page-sized)
void ondemand_page(void *address, unsigned size_bytes) {
    bool rc = region_unlock(address, size_bytes);
    verify(rc);
}

// Creates mappings to the underlying file including mirroring sections
void create_mappings(const Mapping *vmem_maps, unsigned nummaps) {
    for (unsigned i = 0; i < nummaps; i++) {
        // Ignore unmapped stuff, it is already reserved as PROT_NONE
        if (!vmem_maps[i].memsize)
            continue;

        // Calculate the number of mirrors
        u64 address_range_size = vmem_maps[i].end_address - vmem_maps[i].start_address;
        unsigned num_mirrors = (address_range_size) / vmem_maps[i].memsize;
        verify((address_range_size % vmem_maps[i].memsize) == 0 && num_mirrors >= 1);

        for (unsigned j = 0; j < num_mirrors; j++) {
            u64 offset = vmem_maps[i].start_address + j * vmem_maps[i].memsize;
            void *p = mem_region_map_file((void*)(uintptr_t)vmem_fd, &addrspace::ram_base[offset],
                    vmem_maps[i].memsize, vmem_maps[i].memoffset, vmem_maps[i].allow_writes);
            verify(p != nullptr);
        }
    }
}




} // namespace virtmem

#ifdef TARGET_IPHONE
#include "/Users/chachillie/flycast/ios_jit_manager.h"
#include <mach/mach.h>

extern "C" {
#include "lwmem.h"
}

constexpr size_t TXM_EXECUTABLE_REGION_SIZE = 536870912;

static bool init_txm_jit_pool();  // Forward declaration
#endif

namespace virtmem {

// Define TXM pool info inside virtmem namespace so it can be accessed as virtmem::g_txm_pool
struct TXMPoolInfo {
    u8* rx_region = nullptr;
    u8* rw_region = nullptr;
    ptrdiff_t rw_rx_diff = 0;
    bool initialized = false;
    bool uses_txm = false;
};

TXMPoolInfo g_txm_pool;  // Now in virtmem namespace

#ifdef TARGET_IPHONE
static bool init_txm_jit_pool()
{
    if (g_txm_pool.initialized)
        return g_txm_pool.uses_txm;
    
    g_txm_pool.initialized = true;
    
    if (!ios_device_has_txm())
    {
        g_txm_pool.uses_txm = false;
        return false;
    }

    g_txm_pool.uses_txm = true;
    const size_t size = TXM_EXECUTABLE_REGION_SIZE;

    INFO_LOG(VMEM, "Initializing TXM JIT pool (%zu MB)...", size / (1024 * 1024));
    
    u8* rx_ptr = static_cast<u8*>(mmap(nullptr, size, PROT_READ | PROT_EXEC,
                                       MAP_ANON | MAP_PRIVATE, -1, 0));
    if (rx_ptr == MAP_FAILED)
    {
        ERROR_LOG(VMEM, "TXM: Failed to allocate RX region: %s", strerror(errno));
        g_txm_pool.uses_txm = false;
        return false;
    }
    
    bool has_txm = ios_device_has_txm();  // Add this line before the asm block

    if (has_txm) {
        __asm__ volatile (
            "mov x0, %0\n"
            "mov x1, %1\n"
            "brk #0x69"
            :: "r" (rx_ptr), "r" (size)
            : "x0", "x1", "memory"
        );
    }
    
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
    
    if (retval != KERN_SUCCESS)
    {
        ERROR_LOG(VMEM, "TXM: vm_remap failed with code 0x%x", retval);
        munmap(rx_ptr, size);
        g_txm_pool.uses_txm = false;
        return false;
    }
    
    u8* rw_ptr = reinterpret_cast<u8*>(rw_region);
    
    if (mprotect(rw_ptr, size, PROT_READ | PROT_WRITE) != 0)
    {
        ERROR_LOG(VMEM, "TXM: mprotect for RW region failed: %s", strerror(errno));
        munmap(rx_ptr, size);
        munmap(rw_ptr, size);
        g_txm_pool.uses_txm = false;
        return false;
    }
    
    lwmem_region_t regions[] = {
        { (void*)rw_ptr, size },
        { NULL, 0 }
    };
    
    size_t lwret = lwmem_assignmem(regions);
    if (lwret == 0)
    {
        ERROR_LOG(VMEM, "TXM: lwmem_assignmem failed");
        munmap(rx_ptr, size);
        munmap(rw_ptr, size);
        g_txm_pool.uses_txm = false;
        return false;
    }
    
    g_txm_pool.rx_region = rx_ptr;
    g_txm_pool.rw_region = rw_ptr;
    g_txm_pool.rw_rx_diff = rw_ptr - rx_ptr;
    
    INFO_LOG(VMEM, "TXM JIT pool initialized successfully");
    INFO_LOG(VMEM, "  RX region: %p", rx_ptr);
    INFO_LOG(VMEM, "  RW region: %p", rw_ptr);
    INFO_LOG(VMEM, "  RW->RX offset: %td bytes", g_txm_pool.rw_rx_diff);
    
    return true;
}
#endif



void jit_set_exec(void* code, size_t size, bool enable) {
}

} // namespace virtmem

// Some OSes restrict cache flushing, cause why not right? :D

#if HOST_CPU == CPU_ARM64

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#endif

namespace virtmem
{

// Code borrowed from Dolphin https://github.com/dolphin-emu/dolphin
static void Arm64_CacheFlush(void* start, void* end) {
    if (start == end)
        return;

#if defined(__APPLE__)
    // Header file says this is equivalent to: sys_icache_invalidate(start, end - start);
    sys_cache_control(kCacheFunctionPrepareForExecution, start, (uintptr_t)end - (uintptr_t)start);
#else
    // Don't rely on GCC's __clear_cache implementation, as it caches
    // icache/dcache cache line sizes, that can vary between cores on
    // big.LITTLE architectures.
    u64 addr, ctr_el0;
    static size_t icache_line_size = 0xffff, dcache_line_size = 0xffff;
    size_t isize, dsize;

    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
    isize = 4 << ((ctr_el0 >> 0) & 0xf);
    dsize = 4 << ((ctr_el0 >> 16) & 0xf);

    // use the global minimum cache line size
    icache_line_size = isize = icache_line_size < isize ? icache_line_size : isize;
    dcache_line_size = dsize = dcache_line_size < dsize ? dcache_line_size : dsize;

    addr = (u64)start & ~(u64)(dsize - 1);
    for (; addr < (u64)end; addr += dsize)
        // use "civac" instead of "cvau", as this is the suggested workaround for
        // Cortex-A53 errata 819472, 826319, 827319 and 824069.
        __asm__ volatile("dc civac, %0" : : "r"(addr) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");

    addr = (u64)start & ~(u64)(isize - 1);
    for (; addr < (u64)end; addr += isize)
        __asm__ volatile("ic ivau, %0" : : "r"(addr) : "memory");

    __asm__ volatile("dsb ish" : : : "memory");
    __asm__ volatile("isb" : : : "memory");
#endif
}


void flush_cache(void *icache_start, void *icache_end, void *dcache_start, void *dcache_end) {
    Arm64_CacheFlush(dcache_start, dcache_end);

    // Dont risk it and flush and invalidate icache&dcache for both ranges just in case.
    if (icache_start != dcache_start)
        Arm64_CacheFlush(icache_start, icache_end);
}

} // namespace virtmem

#elif HOST_CPU == CPU_ARM

#if defined(__APPLE__)

#include <libkern/OSCacheControl.h>

static void CacheFlush(void* code, void* pEnd)
{
    sys_dcache_flush(code, (u8*)pEnd - (u8*)code + 1);
    sys_icache_invalidate(code, (u8*)pEnd - (u8*)code + 1);
}

#elif !defined(ARMCC)

#ifdef __ANDROID__
#include <sys/syscall.h>  // for cache flushing.
#endif

static void CacheFlush(void* code, void* pEnd)
{
#if !defined(__ANDROID__)
#ifdef __GNUC__
    __builtin___clear_cache((char *)code, (char *)pEnd);
#else
    __clear_cache((void*)code, pEnd);
#endif
#else // defined(__ANDROID__)
    void* start=code;
    size_t size=(u8*)pEnd-(u8*)start+4;

  // Ideally, we would call
  //   syscall(__ARM_NR_cacheflush, start,
  //           reinterpret_cast<intptr_t>(start) + size, 0);
  // however, syscall(int, ...) is not supported on all platforms, especially
  // not when using EABI, so we call the __ARM_NR_cacheflush syscall directly.

  register uint32_t beg asm("a1") = reinterpret_cast<uint32_t>(start);
  register uint32_t end asm("a2") = reinterpret_cast<uint32_t>(start) + size;
  register uint32_t flg asm("a3") = 0;

  #ifdef __ARM_EABI__
    #if defined (__arm__) && !defined(__thumb__)
      // __arm__ may be defined in thumb mode.
      register uint32_t scno asm("r7") = __ARM_NR_cacheflush;
      asm volatile(
          "svc 0x0"
          : "=r" (beg)
          : "0" (beg), "r" (end), "r" (flg), "r" (scno));
    #else
      // r7 is reserved by the EABI in thumb mode.
      asm volatile(
      "@   Enter ARM Mode  \n\t"
          "adr r3, 1f      \n\t"
          "bx  r3          \n\t"
          ".ALIGN 4        \n\t"
          ".ARM            \n"
      "1:  push {r7}       \n\t"
          "mov r7, %4      \n\t"
          "svc 0x0         \n\t"
          "pop {r7}        \n\t"
      "@   Enter THUMB Mode\n\t"
          "adr r3, 2f+1    \n\t"
          "bx  r3          \n\t"
          ".THUMB          \n"
      "2:                  \n\t"
          : "=r" (beg)
          : "0" (beg), "r" (end), "r" (flg), "r" (__ARM_NR_cacheflush)
          : "r3");
    #endif // !defined (__arm__) || defined(__thumb__)
  #else // ! __ARM_EABI__
    #if defined (__arm__) && !defined(__thumb__)
      // __arm__ may be defined in thumb mode.
      asm volatile(
          "svc %1"
          : "=r" (beg)
          : "i" (__ARM_NR_cacheflush), "0" (beg), "r" (end), "r" (flg));
    #else
      // Do not use the value of __ARM_NR_cacheflush in the inline assembly
      // below, because the thumb mode value would be used, which would be
      // wrong, since we switch to ARM mode before executing the svc instruction
      asm volatile(
      "@   Enter ARM Mode  \n\t"
          "adr r3, 1f      \n\t"
          "bx  r3          \n\t"
          ".ALIGN 4        \n\t"
          ".ARM            \n"
      "1:  svc 0x9f0002    \n"
      "@   Enter THUMB Mode\n\t"
          "adr r3, 2f+1    \n\t"
          "bx  r3          \n\t"
          ".THUMB          \n"
      "2:                  \n\t"
          : "=r" (beg)
          : "0" (beg), "r" (end), "r" (flg)
          : "r3");
    #endif // !defined (__arm__) || defined(__thumb__)
  #endif // !__ARM_EABI__
    #if 0
        const int syscall = 0xf0002;
        __asm __volatile (
            "mov     r0, %0\n"
            "mov     r1, %1\n"
            "mov     r7, %2\n"
            "mov     r2, #0x0\n"
            "svc     0x00000000\n"
            :
            :   "r" (code), "r" (pEnd), "r" (syscall)
            :   "r0", "r1", "r7"
            );
    #endif
#endif // defined(__ANDROID__)
}
#else // defined(ARMCC)
asm static void CacheFlush(void* code, void* pEnd)
{
    ARM
    push {r7}
    //add r1, r1, r0
    mov r7, #0xf0000
    add r7, r7, #0x2
    mov r2, #0x0
    svc #0x0
    pop {r7}
    bx lr
}
#endif

namespace virtmem
{

void flush_cache(void *icache_start, void *icache_end, void *dcache_start, void *dcache_end)
{
    CacheFlush(icache_start, icache_end);
}

} // namespace virtmem

#endif // #if HOST_CPU == CPU_ARM
