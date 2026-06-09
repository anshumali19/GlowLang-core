#include <stdio.h>
#include <stdint.h>

#define MASK_NAN      ((uint64_t)0x7FF8000000000000)
#define MASK_TYPE     ((uint64_t)0x0007000000000000)
#define MASK_PAYLOAD  ((uint64_t)0x0000FFFFFFFFFFFF)

typedef enum {
    VAL_INT=0, VAL_FLOAT=1, VAL_BOOL=2, VAL_STRING=3, VAL_LIST=4, VAL_DICT=5, VAL_OBJECT=6, VAL_NONE=7, VAL_DEOPT=15, VAL_TRACE_EXIT=16
} ValueType;

static inline uint64_t make_value(uint64_t type, uint64_t payload) {
    uint64_t v = MASK_NAN | (type << 48) | (payload & MASK_PAYLOAD);
    return v;
}

int main() {
    printf("VAL_INT:    %016lx\n", make_value(VAL_INT, 123));
    printf("VAL_STRING: %016lx\n", make_value(VAL_STRING, 0x123456));
    printf("VAL_OBJECT: %016lx\n", make_value(VAL_OBJECT, 0x123456));
    printf("VAL_NONE:   %016lx\n", make_value(VAL_NONE, 0));
    return 0;
}
