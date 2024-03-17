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
    case PR_INT:
        for (size_t i = 0; i < sizeof(int64_t); i++) {
            buffer[i] = fetch(vm);
        }
        memcpy(&word.value.as_int, buffer, sizeof(int64_t));
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

static void print_word(Word word)
{
    switch (word.type) {
    case PR_INT:
        printf("%ld\n", word.value.as_int);
        break;
    case PR_DOUBLE:
        printf("%lf\n", word.value.as_double);
        break;
    case PR_PTR:
        printf("%p\n", word.value.as_ptr);
        break;
    }
}

#define arithop_int(OP)                                             \
    {                                                               \
        Word rhs = pop(vm);                                         \
        Word lhs = pop(vm);                                         \
        assert(lhs.type == PR_INT && rhs.type == PR_INT);           \
        Word result;                                                \
        result.type = PR_INT;                                       \
        result.value.as_int = lhs.value.as_int OP rhs.value.as_int; \
        push(vm, result);                                           \
    }

#define arithop_double(OP)                                                \
    {                                                                     \
        Word rhs = pop(vm);                                               \
        Word lhs = pop(vm);                                               \
        assert(lhs.type == PR_DOUBLE && rhs.type == PR_DOUBLE);           \
        Word result;                                                      \
        result.type = PR_DOUBLE;                                          \
        result.value.as_int = lhs.value.as_double OP rhs.value.as_double; \
        push(vm, result);                                                 \
    }

#define ARITHOP(TYPE, OP) arithop_##TYPE(OP)

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
        case INS_IPUSH:
            push(vm, fetch_word(vm, PR_INT));
            break;
        case INS_DPUSH:
            push(vm, fetch_word(vm, PR_DOUBLE));
            break;
        case INS_POP:
            pop(vm);
            break;
        case INS_PRINT:
            print_word(pop(vm));
            break;
        case INS_IADD:
            ARITHOP(int, +);
            break;
        case INS_ISUB:
            ARITHOP(int, -);
            break;
        case INS_IMUL:
            ARITHOP(int, *);
            break;
        case INS_IDIV:
            ARITHOP(int, /);
            break;
        case INS_DADD:
            ARITHOP(double, +);
            break;
        case INS_DSUB:
            ARITHOP(double, -);
            break;
        case INS_DMUL:
            ARITHOP(double, *);
            break;
        case INS_DDIV:
            ARITHOP(double, /);
            break;
        }
    }
}
