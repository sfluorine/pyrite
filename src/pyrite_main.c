#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pyrite.h"

int main()
{
    VirtualMachine vm;
    vm_init_from_file(&vm, "output.pyrite");
    vm_execute(&vm);
    vm_free(&vm);
}
