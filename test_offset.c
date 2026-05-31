#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef struct GCObject {
    struct GCObject *next;
    unsigned char type;
    unsigned char marked;
    size_t sz;
} GCObject;

struct List {
    GCObject obj;
    int refcount;
    int size;
    int capacity;
    uint64_t *items;
};

int main() {
    printf("GCObject size: %zu\n", sizeof(GCObject));
    printf("List offset size: %zu\n", offsetof(struct List, size));
    printf("List offset items: %zu\n", offsetof(struct List, items));
    return 0;
}
