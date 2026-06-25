#include <stdio.h>
extern int active_stack_count;
void test_active_stack() {
    printf("Active stacks: %d\n", active_stack_count);
}
