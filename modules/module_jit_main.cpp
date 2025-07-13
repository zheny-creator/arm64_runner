#include "module_jit.h"
#include <stdio.h>
int debug_enabled = 0;
int main() {
    int res = jit_compile_simple_add(2, 3);
    printf("jit_compile_simple_add(2, 3) = %d\n", res);
    return 0;
} 