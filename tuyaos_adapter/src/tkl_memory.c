/*****************************************************************************//**
 * @file tkl_memory.c
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------

#include <stdlib.h>
#include "FreeRTOS.h"
#include "psram_heap_4.h"
#include "tuya_error_code.h"
#include "tkl_memory.h"
#include "sl_tuya_log.h"

// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------

#define MEMORY_FREERTOS_HEAP_PSRAM 1
#define MEMORY_FREERTOS            2
#define MEMORY_LIBC                3

#ifndef TKL_MEMORY
#define TKL_MEMORY MEMORY_FREERTOS
#endif

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------

/**
 * @brief Alloc memory of system
 *
 * @param[in] size: memory size
 *
 * @note This API is used to alloc memory of system.
 *
 * @return the memory address malloced
 */
void *tkl_system_malloc(size_t size)
{
#if TKL_MEMORY == MEMORY_LIBC
    void *p = malloc(size);
#elif TKL_MEMORY == MEMORY_FREERTOS_HEAP_PSRAM
    void *p = tkl_system_psram_malloc(size);
#elif TKL_MEMORY == MEMORY_FREERTOS
    void *p = pvPortMalloc(size);
#else
    void *p = NULL;
#endif

    TKL_LOGV("malloc 0x%08lx %u ", (uint32_t)p, size);

    return p;
}

/**
 * @brief Free memory of system
 *
 * @param[in] ptr: memory point
 *
 * @note This API is used to free memory of system.
 *
 * @return void
 */
void tkl_system_free(void *ptr)
{
    TKL_LOGV("free 0x%08lx", (uint32_t)ptr);

#if TKL_MEMORY == MEMORY_LIBC
    free(ptr);
#elif TKL_MEMORY == MEMORY_FREERTOS_HEAP_PSRAM
    tkl_system_psram_free(ptr);
#elif TKL_MEMORY == MEMORY_FREERTOS
    vPortFree(ptr);
#else
    return;
#endif
}

/**
 * @brief set memory
 *
 * @param[in] size: memory size
 *
 * @note This API is used to alloc memory of system.
 *
 * @return the memory address malloced
 */
void *tkl_system_memset(void *src, int ch, const size_t n)
{
    return memset(src, ch, n);
}

/**
 * @brief Alloc memory of system
 *
 * @param[in] size: memory size
 *
 * @note This API is used to alloc memory of system.
 *
 * @return the memory address malloced
 */
void *tkl_system_memcpy(void *src, const void *dst, const size_t n)
{
    return memcpy(src, dst, n);
}

/**
 * @brief Allocate and clear the memory
 *
 * @param[in]       nitems      the numbers of memory block
 * @param[in]       size        the size of the memory block
 *
 * @return the memory address calloced
 */
void *tkl_system_calloc(size_t nitems, size_t size)
{
    void *addr;

    addr = tkl_system_malloc(nitems * size);
    if (addr == NULL) {
        return addr;
    }

    memset(addr, 0, nitems * size);

    return addr;
}

/**
 * @brief Re-allocate the memory
 *
 * @param[in]       nitems      source memory address
 * @param[in]       size        the size after re-allocate
 *
 * @return void
 */
void *tkl_system_realloc(void *ptr, size_t size)
{
    void *new;

    if (size == 0) {
        tkl_system_free(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        return tkl_system_malloc(size);
    }

    new = tkl_system_malloc(size);
    if (new == NULL) {
        return NULL;
    }

    memcpy(new, ptr, size);
    tkl_system_free(ptr);

    return new;
}

/**
 * @brief Get system free heap size
 *
 * @param none
 *
 * @return heap size
 */
int tkl_system_get_free_heap_size(void)
{
#if TKL_MEMORY == MEMORY_LIBC
    return 0;
#elif TKL_MEMORY == MEMORY_FREERTOS_HEAP_PSRAM
    return (int)xPortGetFreeHeapSizePsram();
#elif TKL_MEMORY == MEMORY_FREERTOS
    return (int)xPortGetFreeHeapSize();
#else
    return 0;
#endif
}

/**
 * @brief Compare two pieces of memory
 *
 * @param[in]       str1        memory 1
 * @param[in]       str2        memory 2
 * @param[in]       n           number of comparisons
 *
 * @return 0 means that the two pieces of memory are the same
 */
int tkl_system_memcmp(const void *str1, const void *str2, size_t n)
{
    return memcmp(str1, str2, n);
}

#if defined(CONFIG_SPIRAM)
/**
 * @brief Alloc memory of PSRAM
 *
 * @param[in] size: memory size
 *
 * @note This API is used to alloc memory of PSRAM.
 *
 * @return the memory address malloced
 */
void *tkl_system_psram_malloc(size_t size)
{
    void *p = pvPortMallocPsram(size);
    TKL_LOGV("malloc 0x%08lx %u ", (uint32_t)p, size);

    return p;
}

/**
 * @brief Free memory of PSRAM
 *
 * @param[in] ptr: memory point
 *
 * @note This API is used to free memory of PSRAM.
 *
 * @return void
 */
void tkl_system_psram_free(void *ptr)
{
    TKL_LOGV("free 0x%08lx", (uint32_t)ptr);
    vPortFreePsram(ptr);
}

/**
 * @brief Allocate and clear memory from PSRAM
 * @param[in] nitems number of elements
 * @param[in] size size of each element in bytes
 * @return allocated zeroed memory address, or NULL on failure
 */
void *tkl_system_psram_calloc(size_t nitems, size_t size)
{
    void *p = pvPortCallocPsram(nitems, size);
    TKL_LOGV("calloc 0x%08lx %u x %u", (uint32_t)p, nitems, size);

    return p;
}

/**
 * @brief Reallocate memory in PSRAM
 * @param[in] ptr previously allocated pointer, may be NULL
 * @param[in] size new size in bytes
 * @return reallocated memory address, or NULL on failure
 * @note Delegated to the heap, which reads the real block size from the block
 *       header. Copying here instead would have to assume the new size and read
 *       past the end of the old block whenever the block grows.
 */
void *tkl_system_psram_realloc(void *ptr, size_t size)
{
    void *p = pvPortReallocPsram(ptr, size);
    TKL_LOGV("realloc 0x%08lx -> 0x%08lx %u", (uint32_t)ptr, (uint32_t)p, size);

    return p;
}

/**
 * @brief Get PSRAM free heap size
 *
 * @param none
 *
 * @return PSRAM heap size
 */
int tkl_system_psram_get_free_heap_size(void)
{
    return (int)xPortGetFreeHeapSizePsram();
}

#endif // CONFIG_SPIRAM
