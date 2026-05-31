#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
typedef struct GCObject {
    struct GCObject *next;
    uint8_t type;
    uint8_t marked;
    size_t sz;
} GCObject;
typedef struct { GCObject obj; char chars[1]; } GCString;
int main() { printf("%zu\n", offsetof(GCString, chars)); return 0; }
