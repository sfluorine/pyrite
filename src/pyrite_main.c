#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dynarr.h"

typedef struct {
    int* arr;
} Node;

void do_something(Node* node)
{
    DYNARRAY_APPEND(&node->arr, 69);
    DYNARRAY_APPEND(&node->arr, 420);
    DYNARRAY_APPEND(&node->arr, 69420);
    DYNARRAY_APPEND(&node->arr, 42069);
}

int main()
{
    Node node;
    node.arr = DYNARRAY_MAKE(int);

    do_something(&node);

    for (size_t i = 0; i < DYNARRAY_LENGTH(node.arr); i++) {
        printf("%d\n", node.arr[i]);
    }

    DYNARRAY_FREE(node.arr);
}
