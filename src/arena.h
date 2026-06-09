#ifndef GLOWLANG_ARENA_H
#define GLOWLANG_ARENA_H

#include <stddef.h>

void arena_init(void);
void *arena_alloc(size_t size);
void *arena_realloc(void *ptr, size_t old_size, size_t new_size);
void arena_free_all(void);

#endif
