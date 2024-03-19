#pragma once

#include <math.h>
#include <stdint.h>

#define STACK_CAP 2048

typedef enum {
    INS_HALT,
    INS_IPUSH,
    INS_DPUSH,
    INS_POP,
    INS_PRINT,
    INS_IADD,
    INS_ISUB,
    INS_IMUL,
    INS_IDIV,
    INS_DADD,
    INS_DSUB,
    INS_DMUL,
    INS_DDIV,
} PyriteInstruction;

typedef enum {
    PR_INT,
    PR_DOUBLE,
    PR_PTR,
} PyriteValueType;

typedef union {
    int64_t as_int;
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
void vm_init_from_file(VirtualMachine* vm, const char* file);
void vm_free(VirtualMachine* vm);
void vm_execute(VirtualMachine* vm);
