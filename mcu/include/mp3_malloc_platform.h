#ifndef MP3_MALLOC_PLATFORM_H
#define MP3_MALLOC_PLATFORM_H

/* Force-included via -include, but ONLY into decoder_mp3.c's compilation
   (see platform_config.cmake's set_source_files_properties) -- NOT into the
   whole of src/, which would also hijack every other legitimate
   tal_psram_malloc caller (AI audio ring buffers, etc.) into this 23KB pool.
   decoder_mp3.c and minimp3.h are unmodified upstream: both branch on
   ENABLE_EXT_RAM to name MP3_MALLOC/DECODER_MP3_MALLOC as tal_psram_malloc
   (this board has ENABLE_EXT_RAM=1). Redefining that name here, before
   tal_memory.h's own declaration is even parsed, is what redirects both the
   scratch (minimp3.h) and the context (decoder_mp3.c) allocations to the
   dedicated internal-RAM pool (mcu/src/mp3_internal_pool.c) without either
   file needing to know this platform exists. */
#define tal_psram_malloc mp3_internal_malloc
#define tal_psram_free   mp3_internal_free

#endif /* MP3_MALLOC_PLATFORM_H */
