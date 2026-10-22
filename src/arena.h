#ifndef GLOWLANG_ARENA_H
#define GLOWLANG_ARENA_H

#include <stddef.h>

void arena_init(void);
void *arena_alloc(size_t size);
void *arena_realloc(void *ptr, size_t old_size, size_t new_size);
void arena_free_all(void);
void *glow_alloc_guarded(size_t size);
void glow_free_guarded(void *ptr, size_t size);

#endif
