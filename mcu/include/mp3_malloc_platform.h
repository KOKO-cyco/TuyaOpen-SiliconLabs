#ifndef MP3_MALLOC_PLATFORM_H
#define MP3_MALLOC_PLATFORM_H

/* Force-included (see platform_config.cmake's -include) before every src/
   translation unit compiles, same effect as the old -DMP3_MALLOC=... command
   line define but readable/greppable as an actual file. minimp3.h's own
   #ifndef MP3_MALLOC / #else branch (unmodified from upstream's structure)
   still supplies the void*(size_t)/void(void*) prototypes -- this file only
   needs to name the real allocator: the dedicated internal-RAM pool
   (mcu/src/mp3_internal_pool.c). */
#define MP3_MALLOC mp3_internal_malloc
#define MP3_FREE   mp3_internal_free

#endif /* MP3_MALLOC_PLATFORM_H */
