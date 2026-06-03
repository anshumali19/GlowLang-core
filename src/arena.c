#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_CHUNK_SIZE (1024 * 1024) /* 1MB chunks */

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    size_t used;
    char data[ARENA_CHUNK_SIZE];
} ArenaChunk;

static __thread ArenaChunk *arena_head = NULL;

void arena_init(void) {
    if (!arena_head) {
        arena_head = calloc(1, sizeof(ArenaChunk));
    }
}

void *arena_alloc(size_t size) {
    /* 8-byte alignment */
    size = (size + 7) & ~7;
    
    if (size > ARENA_CHUNK_SIZE) {
        /* Large allocations bypass the bump pointer and are not strictly managed by the block 
           in a real system, but for now we just allocate them raw. To avoid leaks in a pure arena, 
           we'd link them. For simplicity, we assume AST nodes and strings are small. */
        return calloc(1, size);
    }
    
    if (!arena_head) {
        arena_init();
    }
    
    if (arena_head->used + size > ARENA_CHUNK_SIZE) {
        ArenaChunk *new_chunk = calloc(1, sizeof(ArenaChunk));
        new_chunk->next = arena_head;
        arena_head = new_chunk;
    }
    
    void *ptr = arena_head->data + arena_head->used;
    arena_head->used += size;
    return ptr;
}

void *arena_realloc(void *ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) return NULL;
    void *new_ptr = arena_alloc(new_size);
    if (ptr && old_size > 0) {
        memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    }
    return new_ptr;
}

void arena_free_all(void) {
    ArenaChunk *curr = arena_head;
    while (curr) {
        ArenaChunk *next = curr->next;
        free(curr);
        curr = next;
    }
    arena_head = NULL;
}
