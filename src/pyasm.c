#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynarr.h"
#include "pyrite.h"

typedef enum {
    TOK_INSTRUCTION,
    TOK_LABEL,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_DOUBLE_LITERAL,
} TokenKind;

typedef struct {
    char const* start;
    int length;
} Span;

static Span span_make(char const* start, int length)
{
    return (Span) { .start = start, .length = length };
}

static void span_print(Span span)
{
    printf("%.*s\n", span.length, span.start);
}

static bool span_equal_to_cstr(Span lhs, char const* rhs)
{
    int length = strlen(rhs);

    if (lhs.length != length)
        return false;

    return strncmp(lhs.start, rhs, length) == 0;
}

typedef struct {
    TokenKind kind;
    int line;
    union {
        PyriteInstruction as_instruction;
        Span as_span;
    };
} Token;

Token token_make(TokenKind kind, int line, Span span)
{
    return (Token) { .kind = kind, .line = line, .as_span = span };
}

Token token_make_instruction(PyriteInstruction instruction, int line)
{
    return (Token) {
        .kind = TOK_INSTRUCTION, .line = line, .as_instruction = instruction
    };
}

typedef struct {
    char const* input_file;
    char* source;
    int source_length;

    int line;
    int cursor;

    Token* tokens;
} Assembler;

static char current(Assembler* assembler)
{
    return assembler->source[assembler->cursor] != '\0'
        ? assembler->source[assembler->cursor]
        : '\0';
}

static void advance(Assembler* assembler)
{
    if (!current(assembler))
        return;

    if (current(assembler) == '\n') {
        assembler->line += 1;
    }

    assembler->cursor += 1;
}

static void skip_whitespace(Assembler* assembler)
{
    while (current(assembler) && isspace(current(assembler)))
        advance(assembler);
}

static void get_tokens(Assembler* assembler)
{
    assembler->tokens = DYNARRAY_MAKE(Token);

    while (current(assembler)) {
        skip_whitespace(assembler);

        if (!current(assembler))
            return;

        char const* start = assembler->source + assembler->cursor;
        int line = assembler->line;

        if (isalpha(current(assembler)) || current(assembler) == '_') {
            int length = 0;
            do {
                length += 1;
                advance(assembler);
            } while (current(assembler)
                && (isalnum(current(assembler)) || current(assembler) == '_'));

            if (current(assembler) == ':') {
                advance(assembler);
                DYNARRAY_APPEND(&assembler->tokens,
                    token_make(TOK_LABEL, line, span_make(start, length)));
                continue;
            }

            Span span = span_make(start, length);

            if (span_equal_to_cstr(span, "halt")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_HALT, line));
            } else if (span_equal_to_cstr(span, "ipush")) {
                DYNARRAY_APPEND(&assembler->tokens,
                    token_make_instruction(INS_IPUSH, line));
            } else if (span_equal_to_cstr(span, "dpush")) {
                DYNARRAY_APPEND(&assembler->tokens,
                    token_make_instruction(INS_DPUSH, line));
            } else if (span_equal_to_cstr(span, "pop")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_POP, line));
            } else if (span_equal_to_cstr(span, "print")) {
                DYNARRAY_APPEND(&assembler->tokens,
                    token_make_instruction(INS_PRINT, line));
            } else if (span_equal_to_cstr(span, "iadd")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_IADD, line));
            } else if (span_equal_to_cstr(span, "isub")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_ISUB, line));
            } else if (span_equal_to_cstr(span, "imul")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_IMUL, line));
            } else if (span_equal_to_cstr(span, "idiv")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_IDIV, line));
            } else if (span_equal_to_cstr(span, "dadd")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_DADD, line));
            } else if (span_equal_to_cstr(span, "dsub")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_DSUB, line));
            } else if (span_equal_to_cstr(span, "dmul")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_DMUL, line));
            } else if (span_equal_to_cstr(span, "ddiv")) {
                DYNARRAY_APPEND(
                    &assembler->tokens, token_make_instruction(INS_DDIV, line));
            } else {
                DYNARRAY_APPEND(&assembler->tokens,
                    token_make(TOK_IDENTIFIER, line, span_make(start, length)));
            }

            continue;
        }

        if (isdigit(current(assembler))) {
            int length = 0;
            do {
                length += 1;
                advance(assembler);
            } while (current(assembler) && isdigit(current(assembler)));

            if (current(assembler) == '.') {
                length += 1;
                advance(assembler);

                int mantissa_length = 0;
                while (current(assembler) && isdigit(current(assembler))) {
                    mantissa_length += 1;
                    advance(assembler);
                }

                if (mantissa_length == 0) {
                    fprintf(stderr,
                        "%s:%d:ERROR: invalid floating point number!\n",
                        assembler->input_file, line);
                    exit(1);
                }

                DYNARRAY_APPEND(&assembler->tokens,
                    token_make(TOK_DOUBLE_LITERAL, line,
                        span_make(start, length + mantissa_length)));
                continue;
            }

            DYNARRAY_APPEND(&assembler->tokens,
                token_make(TOK_INT_LITERAL, line, span_make(start, length)));
            continue;
        }

        int length = 0;
        do {
            length += 1;
            advance(assembler);
        } while (current(assembler) && !isspace(current(assembler)));

        fprintf(stderr, "%s:%d:ERROR: unknown token: %.*s\n",
            assembler->input_file, line, length, start);
        exit(1);
    }
}

static void assembler_init(Assembler* assembler, char const* input_file)
{
    assembler->input_file = input_file;
    assembler->source = NULL;
    assembler->line = 1;
    assembler->cursor = 0;

    FILE* stream = fopen(input_file, "r");
    if (!stream) {
        fprintf(stderr, "ERROR: cannot open file '%s': %s\n", input_file,
            strerror(errno));
        exit(1);
    }

    fseek(stream, 0, SEEK_END);
    long size = ftell(stream);
    fseek(stream, 0, SEEK_SET);

    if (size == 0) {
        fprintf(stderr, "WARNING: input file is empty '%s'\n", input_file);
        return;
    }

    assembler->source = malloc(sizeof(char) * size + 1);
    if (!fread(assembler->source, sizeof(char), size, stream)) {
        fprintf(stderr, "ERROR: cannot read file '%s'\n", input_file);
        exit(1);
    }

    fclose(stream);

    assembler->source[size] = '\0'; // null terminated.
    assembler->source_length = size;

    get_tokens(assembler);
    assembler->cursor = 0; // now the cursor is used by the parser.
}

static void assembler_free(Assembler* assembler)
{
    DYNARRAY_FREE(assembler->tokens);
    free(assembler->source);
}

int main()
{
    char const* input = "input.pyasm";
    /* char const* output = "output.pyasm"; */

    Assembler assembler;
    assembler_init(&assembler, input);

    for (size_t i = 0; i < DYNARRAY_LENGTH(assembler.tokens); i++) {
        if (assembler.tokens[i].kind == TOK_INSTRUCTION) {
            printf("0x%X\n", assembler.tokens[i].as_instruction);
        } else {
            span_print(assembler.tokens[i].as_span);
        }
    }

    assembler_free(&assembler);
}
