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
    int32_t length;
} Span;

static Span span_make(char const* start, int32_t length)
{
    return (Span) { .start = start, .length = length };
}

static void span_print(Span span)
{
    printf("%.*s\n", span.length, span.start);
}

static bool span_equal(Span lhs, Span rhs)
{
    if (lhs.length != rhs.length)
        return false;

    return strncmp(lhs.start, rhs.start, lhs.length) == 0;
}

static bool span_equal_to_cstr(Span lhs, char const* rhs)
{
    int32_t length = strlen(rhs);

    if (lhs.length != length)
        return false;

    return strncmp(lhs.start, rhs, length) == 0;
}

typedef struct {
    TokenKind kind;
    int32_t line;
    union {
        PyriteInstruction as_instruction;
        Span as_span;
    };
} Token;

static Token token_make(TokenKind kind, int32_t line, Span span)
{
    return (Token) { .kind = kind, .line = line, .as_span = span };
}

static Token token_make_instruction(PyriteInstruction instruction, int32_t line)
{
    return (Token) {
        .kind = TOK_INSTRUCTION, .line = line, .as_instruction = instruction
    };
}

typedef struct {
    Span name;
    int32_t address;
} Label;

static Label symbol_make(Span name, int32_t address)
{
    return (Label) { .name = name, .address = address };
}

typedef struct {
    char const* input_file;
    char* source;
    int32_t source_length;

    int32_t line;
    int32_t cursor;

    Label* labels;
    Token* tokens;

    uint8_t* program;
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
        int32_t line = assembler->line;

        if (isalpha(current(assembler)) || current(assembler) == '_') {
            int32_t length = 0;
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
            int32_t length = 0;
            do {
                length += 1;
                advance(assembler);
            } while (current(assembler) && isdigit(current(assembler)));

            if (current(assembler) == '.') {
                length += 1;
                advance(assembler);

                int32_t mantissa_length = 0;
                while (current(assembler) && isdigit(current(assembler))) {
                    mantissa_length += 1;
                    advance(assembler);
                }

                if (mantissa_length == 0) {
                    fprintf(stderr,
                        "%s:%d: ERROR: invalid floating point number!\n",
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

        int32_t length = 0;
        do {
            length += 1;
            advance(assembler);
        } while (current(assembler) && !isspace(current(assembler)));

        fprintf(stderr, "%s:%d: ERROR: unknown token: %.*s\n",
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

    assembler->labels = DYNARRAY_MAKE(Label);
    assembler->program = DYNARRAY_MAKE(uint8_t);
}

static void assembler_free(Assembler* assembler)
{
    DYNARRAY_FREE(assembler->program);
    DYNARRAY_FREE(assembler->labels);
    DYNARRAY_FREE(assembler->tokens);
    free(assembler->source);
}

static int32_t program_counter(Assembler* assembler)
{
    return DYNARRAY_LENGTH(assembler->program);
}

static void put_label(Assembler* assembler, Label label)
{
    DYNARRAY_APPEND(&assembler->labels, label);
}

static int32_t lookup_label(Assembler* assembler, Span label)
{
    for (int32_t i = 0; i < (int32_t)DYNARRAY_LENGTH(assembler->labels); i++) {
        if (span_equal(label, assembler->labels[i].name)) {
            return i;
        }
    }

    return -1;
}

static void patch_label(Assembler* assembler, int32_t index, int32_t address)
{
    assembler->labels[index].address = address;
}

static bool is_eof(Assembler* assembler)
{
    return assembler->cursor >= (int32_t)DYNARRAY_LENGTH(assembler->tokens);
}

static Token current_token(Assembler* assembler)
{
    return assembler->tokens[assembler->cursor];
}

static void advance_token(Assembler* assembler)
{
    if (is_eof(assembler))
        return;

    assembler->cursor += 1;
}

static void match_token(Assembler* assembler, TokenKind kind)
{
    if (is_eof(assembler)) {
        Token previous
            = assembler->tokens[DYNARRAY_LENGTH(assembler->tokens) - 1];
        fprintf(stderr, "%s:%d: ERROR: unexpected end of file\n",
            assembler->input_file, previous.line);
        exit(1);
    }
    if (current_token(assembler).kind != kind) {
        fprintf(stderr, "%s:%d: ERROR: unexpected token\n",
            assembler->input_file, current_token(assembler).line);
        exit(1);
    }

    advance_token(assembler);
}

static void parse_tokens(Assembler* assembler)
{
    while (assembler->cursor < (int32_t)DYNARRAY_LENGTH(assembler->tokens)) {
        if (current_token(assembler).kind == TOK_LABEL) {
            // the address will be patched in the second and third pass.
            put_label(
                assembler, symbol_make(current_token(assembler).as_span, -1));
        }
        advance_token(assembler);
    }

    // reset cursor.
    assembler->cursor = 0;

    while (!is_eof(assembler)) {
        Token current = current_token(assembler);
        if (current.kind == TOK_LABEL) {
            patch_label(assembler, lookup_label(assembler, current.as_span),
                program_counter(assembler));
            advance_token(assembler);
            continue;
        }

        if (current.kind == TOK_INSTRUCTION) {
            switch (current.as_instruction) {
            case INS_HALT:
                DYNARRAY_APPEND(&assembler->program, INS_HALT);
                advance_token(assembler);
                break;
            case INS_IPUSH: {
                DYNARRAY_APPEND(&assembler->program, INS_IPUSH);
                advance_token(assembler);

                Span integer_literal = current_token(assembler).as_span;
                match_token(assembler, TOK_INT_LITERAL);

                int64_t integer = strtoll(integer_literal.start, nullptr, 10);
                uint8_t bytes[sizeof(int64_t)];
                memcpy(bytes, &integer, sizeof(int64_t));

                for (int32_t i = 0; i < (int32_t)sizeof(int64_t); i++)
                    DYNARRAY_APPEND(&assembler->program, bytes[i]);
                break;
            }
            case INS_DPUSH: {
                DYNARRAY_APPEND(&assembler->program, INS_DPUSH);
                advance_token(assembler);

                Span double_literal = current_token(assembler).as_span;
                match_token(assembler, TOK_DOUBLE_LITERAL);

                double_t dbl = strtod(double_literal.start, nullptr);
                uint8_t bytes[sizeof(double_t)];

                memcpy(bytes, &dbl, sizeof(double_t));

                for (int32_t i = 0; i < (int32_t)sizeof(double_t); i++)
                    DYNARRAY_APPEND(&assembler->program, bytes[i]);
                break;
            }
            case INS_POP:
                DYNARRAY_APPEND(&assembler->program, INS_POP);
                advance_token(assembler);
                break;
            case INS_PRINT:
                DYNARRAY_APPEND(&assembler->program, INS_PRINT);
                advance_token(assembler);
                break;
            case INS_IADD:
                DYNARRAY_APPEND(&assembler->program, INS_IADD);
                advance_token(assembler);
                break;
            case INS_ISUB:
                DYNARRAY_APPEND(&assembler->program, INS_ISUB);
                advance_token(assembler);
                break;
            case INS_IMUL:
                DYNARRAY_APPEND(&assembler->program, INS_IMUL);
                advance_token(assembler);
                break;
            case INS_IDIV:
                DYNARRAY_APPEND(&assembler->program, INS_IDIV);
                advance_token(assembler);
                break;
            case INS_DADD:
                DYNARRAY_APPEND(&assembler->program, INS_DADD);
                advance_token(assembler);
                break;
            case INS_DSUB:
                DYNARRAY_APPEND(&assembler->program, INS_DSUB);
                advance_token(assembler);
                break;
            case INS_DMUL:
                DYNARRAY_APPEND(&assembler->program, INS_DMUL);
                advance_token(assembler);
                break;
            case INS_DDIV:
                DYNARRAY_APPEND(&assembler->program, INS_DDIV);
                advance_token(assembler);
                break;
            }
        }
    }
}

static void assembler_generate(Assembler* assembler, char const* output_file)
{
    parse_tokens(assembler);

    FILE* stream = fopen(output_file, "wb");
    if (!stream) {
        fprintf(stderr, "ERROR: cannot open file '%s': %s\n", output_file,
            strerror(errno));
        exit(1);
    }

    fwrite("PYRITE", 1, 6, stream);

    int32_t program_length = DYNARRAY_LENGTH(assembler->program);
    fwrite(&program_length, 1, sizeof(int32_t), stream);
    fwrite(assembler->program, 1, program_length, stream);

    printf("program length: %d bytes\n", program_length);

    fclose(stream);
}

int main()
{
    char const* input = "input.pyasm";
    char const* output = "output.pyrite";

    Assembler assembler;
    assembler_init(&assembler, input);
    assembler_generate(&assembler, output);
    assembler_free(&assembler);
}
