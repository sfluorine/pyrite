#pragma once

#include <stddef.h>
#include <stdlib.h>

typedef struct {
    size_t length;
    size_t cap;
    size_t elem_size;
} DynArrHeader;

#define DYNARRAY_MAKE(___TYPE)                                                      \
    ({                                                                              \
         DynArrHeader* ___header = malloc(sizeof(*___header) + sizeof(___TYPE));    \
         if (!___header) {                                                          \
             perror("Memory allocation failed");                                    \
             exit(EXIT_FAILURE);                                                    \
         }                                                                          \
         ___header->length = 0;                                                     \
         ___header->cap = 1;                                                        \
         ___header->elem_size = sizeof(___TYPE);                                    \
         ___TYPE* ___start = (___TYPE*)(___header + 1);                             \
         ___start;                                                                  \
     })

#define DYNARRAY_FREE(___DYNARR)                                    \
    {                                                               \
        DynArrHeader* ___header = (DynArrHeader*)___DYNARR - 1;     \
        free(___header);                                            \
    }

#define DYNARRAY_APPEND(___DYNARR, ___DATA)                                                     \
    {                                                                                           \
        typeof(___DYNARR) ___raw_header = (___DYNARR);                                          \
        DynArrHeader* ___header = (DynArrHeader*)(*___raw_header) - 1;                                 \
        if (___header->length >= ___header->cap) {                                              \
            ___header->cap += 1;                                                                \
            size_t ___new_size = ___header->elem_size * ___header->cap;                         \
            DynArrHeader* ___new_header = realloc(___header, sizeof(*___header) + ___new_size); \
            if (!___new_header) {                                                               \
                perror("Memory reallocation failed");                                           \
                exit(EXIT_FAILURE);                                                             \
            }                                                                                   \
            ___header = ___new_header;                                                          \
        }                                                                                       \
        typeof(*___DYNARR) ___start = (typeof(*___DYNARR))(___header + 1);                        \
        *___raw_header = ___start;                                                                   \
        ___start[___header->length++] = ___DATA;                                                \
    }

#define DYNARRAY_LENGTH(___DYNARR)                                      \
    ({                                                                  \
        DynArrHeader* ___header = (DynArrHeader*)___DYNARR - 1;  \
        ___header->length;                                              \
     })
