#pragma once

#include <math.h>
#include <stdint.h>

#define STACK_CAP 2048

typedef enum {
    INS_HALT,
    INS_I32PUSH,
    INS_I64PUSH,
    INS_DPUSH,
    INS_POP,
} PyriteInstruction;

typedef enum {
    PR_I32,
    PR_I64,
    PR_DOUBLE,
    PR_PTR,
} PyriteValueType;

typedef union {
    int32_t as_i32;
    int64_t as_i64;
    double_t as_double;
    void* as_ptr;
} PyriteValue;

typedef struct {
    PyriteValue value;
    PyriteValueType type;
} Word;

typedef struct {
    uint8_t* program;
    int32_t program_length;
    int32_t program_counter;

    Word stack[STACK_CAP];
    int32_t stack_pointer;
    int32_t base_pointer;
} VirtualMachine;

void vm_init(VirtualMachine* vm, uint8_t* program, uint32_t program_length);
void vm_execute(VirtualMachine* vm);
