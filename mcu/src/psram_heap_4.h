#ifndef PSRAM_HEAP4_H
#define PSRAM_HEAP4_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef configTOTAL_HEAP_SIZE_PSRAM
#define configTOTAL_HEAP_SIZE_PSRAM (1024 * 1024 * 4)
#endif

void  *pvPortMallocPsram(size_t xSize);
void   vPortFreePsram(void *pv);
void  *pvPortCallocPsram(size_t xNum, size_t xSize);
void  *pvPortReallocPsram(void *pv, size_t xWantedSize);
void   vPortInitialiseBlocksPsram(void);
size_t xPortGetFreeHeapSizePsram(void);
size_t xPortGetMinimumEverFreeHeapSizePsram(void);

#ifdef __cplusplus
}
#endif

#endif /* PSRAM_HEAP4_H */
