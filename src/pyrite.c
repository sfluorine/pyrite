#include "pyrite.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint8_t fetch(VirtualMachine* vm)
{
    return vm->program[++vm->program_counter];
}

static Word fetch_word(VirtualMachine* vm, PyriteValueType type)
{
    uint8_t buffer[sizeof(PyriteValue)];

    Word word;
    word.type = type;

    switch (type) {
    case PR_I32:
        for (size_t i = 0; i < sizeof(int32_t); i++) {
            buffer[i] = fetch(vm);
        }
        memcpy(&word.value.as_i32, buffer, sizeof(int32_t));
        break;
    case PR_I64:
        for (size_t i = 0; i < sizeof(int64_t); i++) {
            buffer[i] = fetch(vm);
        }
        memcpy(&word.value.as_i64, buffer, sizeof(int64_t));
        break;
    case PR_DOUBLE:
        for (size_t i = 0; i < sizeof(double_t); i++) {
            buffer[i] = fetch(vm);
        }
        memcpy(&word.value.as_double, buffer, sizeof(double_t));
        break;
    case PR_PTR:
        for (size_t i = 0; i < sizeof(void*); i++) {
            buffer[i] = fetch(vm);
        }
        memcpy(&word.value.as_ptr, buffer, sizeof(void*));
        break;
    }

    return word;
}

static void push(VirtualMachine* vm, Word word)
{
    assert(vm->stack_pointer < STACK_CAP && "STACK OVERFLOW!");

    vm->stack[++vm->stack_pointer] = word;
}

static Word pop(VirtualMachine* vm)
{
    assert(vm->stack_pointer >= 0 && "STACK UNDERFLOW!");

    return vm->stack[vm->stack_pointer--];
}

void vm_init(VirtualMachine* vm, uint8_t* program, uint32_t program_length)
{
    vm->program = program;
    vm->program_length = program_length;
    vm->program_counter = -1;

    vm->stack_pointer = -1;
    vm->base_pointer = -1;
}

void vm_execute(VirtualMachine* vm)
{
    bool halt = false;
    while (vm->program_counter < vm->program_length && !halt) {
        switch (fetch(vm)) {
        case INS_HALT:
            halt = true;
            break;
        case INS_I32PUSH:
            push(vm, fetch_word(vm, PR_I32));
            break;
        case INS_I64PUSH:
            push(vm, fetch_word(vm, PR_I64));
            break;
        case INS_DPUSH:
            push(vm, fetch_word(vm, PR_DOUBLE));
            break;
        case INS_POP:
            pop(vm);
            break;
        }
    }
}
