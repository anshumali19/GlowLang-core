#include "glowlang.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

#define ARENA_CHUNK_SIZE (1024 * 1024) /* 1MB chunks */

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    size_t used;
    char data[ARENA_CHUNK_SIZE];
} ArenaChunk;

static THREAD_LOCAL ArenaChunk *arena_head = NULL;

typedef struct LargeAllocation {
    struct LargeAllocation *next;
    void *ptr;
} LargeAllocation;

static THREAD_LOCAL LargeAllocation *large_allocations = NULL;

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
        void *ptr = calloc(1, size);
        if (ptr) {
            LargeAllocation *la = malloc(sizeof(LargeAllocation));
            if (la) {
                la->ptr = ptr;
                la->next = large_allocations;
                large_allocations = la;
            }
        }
        return ptr;
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

    LargeAllocation *la = large_allocations;
    while (la) {
        LargeAllocation *next = la->next;
        free(la->ptr);
        free(la);
        la = next;
    }
    large_allocations = NULL;
}

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

void *glow_alloc_guarded(size_t size) {
    if (size == 0) return NULL;
    size_t page_sz = 4096;
#ifndef _WIN32
    long sc = sysconf(_SC_PAGESIZE);
    if (sc > 0) page_sz = (size_t)sc;
#endif
    size_t payload_pages = (size + page_sz - 1) / page_sz;
    size_t total_pages = payload_pages + 1; // +1 guard page
    size_t total_size = total_pages * page_sz;

#ifdef _WIN32
    char *base = (char*)VirtualAlloc(NULL, total_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return NULL;
    DWORD old_protect;
    VirtualProtect(base + payload_pages * page_sz, page_sz, PAGE_NOACCESS, &old_protect);
#else
    char *base = (char*)mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (base == MAP_FAILED) return NULL;
    mprotect(base + payload_pages * page_sz, page_sz, PROT_NONE);
#endif

    // Place payload right against the guard page so any sequential overrun immediately triggers an MMU fault
    char *ret = base + (payload_pages * page_sz) - size;
    return (void*)ret;
}

void glow_free_guarded(void *ptr, size_t size) {
    if (!ptr || size == 0) return;
    size_t page_sz = 4096;
#ifndef _WIN32
    long sc = sysconf(_SC_PAGESIZE);
    if (sc > 0) page_sz = (size_t)sc;
#endif
    size_t payload_pages = (size + page_sz - 1) / page_sz;
    size_t total_size = (payload_pages + 1) * page_sz;
    char *base = (char*)ptr + size - (payload_pages * page_sz);

#ifdef _WIN32
    VirtualFree(base, 0, MEM_RELEASE);
#else
    munmap(base, total_size);
#endif
}
