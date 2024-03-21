#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynarr.h"
#include "pyrite.h"

typedef struct {
    char const* start;
    int32_t length;
} Span;

static Span span_make(char const* start, int32_t length)
{
    return (Span) { .start = start, .length = length };
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

typedef enum {
    PREC_SEGMENT,
    PREC_IMPORT,
} PreprocessorKind;

typedef enum {
    SEGMENT_READONLY,
    SEGMENT_CODE,
    SEGMENT_UNKNOWN,
} Segment;

typedef struct {
    PreprocessorKind kind;
    union {
        Segment as_segment;
        Span as_import;
    };
} Preprocessor;

static Preprocessor preprocessor_make_segment(Segment segment)
{
    return (Preprocessor) { .kind = PREC_SEGMENT, .as_segment = segment };
}

static Preprocessor preprocessor_make_import(Span import)
{
    return (Preprocessor) { .kind = PREC_IMPORT, .as_import = import };
}

typedef enum {
    TOK_INSTRUCTION,
    TOK_LABEL,
    TOK_IDENTIFIER,
    TOK_INT_LITERAL,
    TOK_DOUBLE_LITERAL,
    TOK_STRING_LITERAL,
    TOK_PREPROCESSOR,
} TokenKind;

typedef struct {
    TokenKind kind;
    int32_t line;
    union {
        PyriteInstruction as_instruction;
        Span as_span;
        Preprocessor as_preprocessor;
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

static Token token_make_preprocessor(Preprocessor prec, int32_t line)
{
    return (Token) {
        .kind = TOK_PREPROCESSOR, .line = line, .as_preprocessor = prec
    };
}

typedef struct {
    Span name;
    int32_t address;
} Label;

static Label label_make(Span name, int32_t address)
{
    return (Label) { .name = name, .address = address };
}

typedef struct {
    Span name;
    Token data;
} DataLabel;

static DataLabel data_label_make(Span name, Token data)
{
    return (DataLabel) { .name = name, .data = data };
}

typedef enum {
    SYMBOL_LABEL,
    SYMBOL_DATA_LABEL,
} SymbolKind;

typedef struct {
    SymbolKind kind;
    union {
        Label as_label;
        DataLabel as_data_label;
    };
} Symbol;

static Symbol symbol_make_label(Label label)
{
    return (Symbol) { .kind = SYMBOL_LABEL, .as_label = label };
}

static Symbol symbol_make_data_label(DataLabel data_label)
{
    return (Symbol) { .kind = SYMBOL_DATA_LABEL, .as_data_label = data_label };
}

typedef struct {
    char const* input_file;
    char* source;
    int32_t source_length;

    int32_t line;
    int32_t cursor;

    Symbol* symbols;
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
    while (isspace(current(assembler)))
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

        if (current(assembler) == '@') {
            advance(assembler);

            start = assembler->source + assembler->cursor;

            int32_t length = 0;
            do {
                length += 1;
                advance(assembler);
            } while (current(assembler) && isalpha(current(assembler)));

            Span prec = span_make(start, length);

            PreprocessorKind kind;
            if (span_equal_to_cstr(prec, "segment")) {
                kind = PREC_SEGMENT;
            } else if (span_equal_to_cstr(prec, "import")) {
                kind = PREC_IMPORT;
                fprintf(stderr,
                    "%s:%d: ERROR: import preprocessor is not implemented "
                    "yet\n",
                    assembler->input_file, line);
            } else {
                fprintf(stderr,
                    "%s:%d: ERROR: '%.*s' is not a valid preprocessor\n",
                    assembler->input_file, line, prec.length, prec.start);
                exit(1);
            }

            skip_whitespace(assembler);
            start = assembler->source + assembler->cursor;

            if (kind == PREC_SEGMENT) {
                length = 0;
                while (current(assembler) && isalpha(current(assembler))) {
                    length += 1;
                    advance(assembler);
                }

                Span segment_kind = span_make(start, length);
                if (span_equal_to_cstr(segment_kind, "readonly")) {
                    DYNARRAY_APPEND(&assembler->tokens,
                        token_make_preprocessor(
                            preprocessor_make_segment(SEGMENT_READONLY), line));
                } else if (span_equal_to_cstr(segment_kind, "code")) {
                    DYNARRAY_APPEND(&assembler->tokens,
                        token_make_preprocessor(
                            preprocessor_make_segment(SEGMENT_CODE), line));
                } else {
                    fprintf(stderr, "%s:%d: ERROR: segment '%.*s' is unknown\n",
                        assembler->input_file, line, segment_kind.length,
                        segment_kind.start);
                    exit(1);
                }

                continue;
            }
        }

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

        if (current(assembler) == '"') {
            advance(assembler);
            start = assembler->source + assembler->cursor;

            int32_t length = 0;
            while (current(assembler) && current(assembler) != '"'
                && current(assembler) != '\n') {
                length += 1;
                advance(assembler);
            }

            if (current(assembler) != '"') {
                fprintf(stderr, "%s:%d: ERROR: unterminated string literal\n",
                    assembler->input_file, line);
                exit(1);
            }

            advance(assembler);

            DYNARRAY_APPEND(&assembler->tokens,
                token_make(TOK_STRING_LITERAL, line, span_make(start, length)));
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
        fprintf(stderr, "WARNING: file '%s' is empty\n", input_file);
        fprintf(stderr, "exiting now...\n");
        exit(0);
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

    assembler->symbols = DYNARRAY_MAKE(Symbol);
    assembler->program = DYNARRAY_MAKE(uint8_t);
}

static void assembler_free(Assembler* assembler)
{
    DYNARRAY_FREE(assembler->program);
    DYNARRAY_FREE(assembler->symbols);
    DYNARRAY_FREE(assembler->tokens);
    free(assembler->source);
}

static int32_t program_counter(Assembler* assembler)
{
    return DYNARRAY_LENGTH(assembler->program);
}

static void put_label(Assembler* assembler, Label label)
{
    DYNARRAY_APPEND(&assembler->symbols, symbol_make_label(label));
}

static int32_t lookup_label(Assembler* assembler, Span label)
{
    for (int32_t i = 0; i < (int32_t)DYNARRAY_LENGTH(assembler->symbols); i++) {
        if (assembler->symbols[i].kind == SYMBOL_LABEL) {
            if (span_equal(label, assembler->symbols[i].as_label.name)) {
                return i;
            }
        } else {
            if (span_equal(label, assembler->symbols[i].as_data_label.name)) {
                return i;
            }
        }
    }

    return -1;
}

static void patch_label(Assembler* assembler, int32_t index, int32_t address)
{
    assembler->symbols[index].as_label.address = address;
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

#define GENERATE_SINGLE_INSTRUCTION(INSTRUCTION)           \
    {                                                      \
        DYNARRAY_APPEND(&assembler->program, INSTRUCTION); \
        advance_token(assembler);                          \
    }

static bool is_single_instruction(PyriteInstruction instruction)
{
    switch (instruction) {
    case INS_HALT:
    case INS_POP:
    case INS_PRINT:
    case INS_IADD:
    case INS_ISUB:
    case INS_IMUL:
    case INS_IDIV:
    case INS_DADD:
    case INS_DSUB:
    case INS_DMUL:
    case INS_DDIV:
        return true;
    default:
        return false;
    }
}

static void parse_instruction(Assembler* assembler)
{
    Token current = current_token(assembler);

    if (current.kind == TOK_LABEL) {
        patch_label(assembler, lookup_label(assembler, current.as_span),
            program_counter(assembler));
        advance_token(assembler);
        return;
    }

    if (current.kind != TOK_INSTRUCTION) {
        fprintf(stderr, "%s:%d: ERROR: expected instructions\n",
            assembler->input_file, current.line);
        exit(1);
    }

    if (is_single_instruction(current.as_instruction)) {
        GENERATE_SINGLE_INSTRUCTION(current.as_instruction);
        return;
    }

    switch (current.as_instruction) {
    case INS_IPUSH: {
        DYNARRAY_APPEND(&assembler->program, INS_IPUSH);
        advance_token(assembler);

        Token operand = current_token(assembler);
        if (operand.kind == TOK_IDENTIFIER) {
            advance_token(assembler);

            int32_t index = lookup_label(assembler, operand.as_span);
            if (index == -1) {
                fprintf(stderr, "%s:%d: ERROR: no such symbol '%.*s'\n",
                    assembler->input_file, operand.line, operand.as_span.length,
                    operand.as_span.start);
                exit(1);
            }

            Symbol symbol = assembler->symbols[index];
            if (symbol.kind != SYMBOL_DATA_LABEL) {
                fprintf(stderr,
                    "%s:%d: ERROR: symbol '%.*s' is not a data label\n",
                    assembler->input_file, operand.line, operand.as_span.length,
                    operand.as_span.start);
                exit(1);
            }

            Token data = symbol.as_data_label.data;
            if (data.kind != TOK_INT_LITERAL) {
                fprintf(stderr,
                    "%s:%d: ERROR: symbol '%.*s' is not an integer literal\n",
                    assembler->input_file, operand.line, operand.as_span.length,
                    operand.as_span.start);
                exit(1);
            }

            int64_t integer = strtoll(data.as_span.start, nullptr, 10);
            uint8_t bytes[sizeof(int64_t)];
            memcpy(bytes, &integer, sizeof(int64_t));

            for (int32_t i = 0; i < (int32_t)sizeof(int64_t); i++)
                DYNARRAY_APPEND(&assembler->program, bytes[i]);

            break;
        }

        match_token(assembler, TOK_INT_LITERAL);

        int64_t integer = strtoll(operand.as_span.start, nullptr, 10);
        uint8_t bytes[sizeof(int64_t)];
        memcpy(bytes, &integer, sizeof(int64_t));

        for (int32_t i = 0; i < (int32_t)sizeof(int64_t); i++)
            DYNARRAY_APPEND(&assembler->program, bytes[i]);
    } break;
    case INS_DPUSH: {
        DYNARRAY_APPEND(&assembler->program, INS_DPUSH);
        advance_token(assembler);

        Token operand = current_token(assembler);
        if (operand.kind == TOK_IDENTIFIER) {
            advance_token(assembler);

            int32_t index = lookup_label(assembler, operand.as_span);
            if (index == -1) {
                fprintf(stderr, "%s:%d: ERROR: no such symbol '%.*s'\n",
                    assembler->input_file, operand.line, operand.as_span.length,
                    operand.as_span.start);
                exit(1);
            }

            Symbol symbol = assembler->symbols[index];
            if (symbol.kind != SYMBOL_DATA_LABEL) {
                fprintf(stderr,
                    "%s:%d: ERROR: symbol '%.*s' is not a data label\n",
                    assembler->input_file, operand.line, operand.as_span.length,
                    operand.as_span.start);
                exit(1);
            }

            Token data = symbol.as_data_label.data;
            if (data.kind != TOK_DOUBLE_LITERAL) {
                fprintf(stderr,
                    "%s:%d: ERROR: symbol '%.*s' is not a double literal\n",
                    assembler->input_file, operand.line, operand.as_span.length,
                    operand.as_span.start);
                exit(1);
            }

            double_t integer = strtod(data.as_span.start, nullptr);
            uint8_t bytes[sizeof(double_t)];
            memcpy(bytes, &integer, sizeof(double_t));

            for (int32_t i = 0; i < (int32_t)sizeof(double_t); i++)
                DYNARRAY_APPEND(&assembler->program, bytes[i]);

            break;
        }

        match_token(assembler, TOK_DOUBLE_LITERAL);

        double_t dbl = strtod(operand.as_span.start, nullptr);
        uint8_t bytes[sizeof(double_t)];

        memcpy(bytes, &dbl, sizeof(double_t));

        for (int32_t i = 0; i < (int32_t)sizeof(double_t); i++)
            DYNARRAY_APPEND(&assembler->program, bytes[i]);
    } break;
    default:
        break;
    }
}

static void parse_readonly(Assembler* assembler)
{
    Token name = current_token(assembler);

    if (name.kind == TOK_LABEL) {
        advance_token(assembler);
        Token data = current_token(assembler);

        if (data.kind != TOK_INT_LITERAL && data.kind != TOK_DOUBLE_LITERAL
            && data.kind != TOK_STRING_LITERAL) {
            fprintf(stderr, "%s:%d: ERROR: data labels can only holds value\n",
                assembler->input_file, data.line);
            exit(1);
        }

        int32_t index = lookup_label(assembler, name.as_span);
        assembler->symbols[index]
            = symbol_make_data_label(data_label_make(name.as_span, data));
        advance_token(assembler);
    }
}

static void parse_tokens(Assembler* assembler)
{
    while (assembler->cursor < (int32_t)DYNARRAY_LENGTH(assembler->tokens)) {
        if (current_token(assembler).kind == TOK_LABEL) {
            // this is an unknown label so it will be replaced soon by the
            // corresponding segment.
            put_label(
                assembler, label_make(current_token(assembler).as_span, -1));
        }
        advance_token(assembler);
    }

    // reset cursor.
    assembler->cursor = 0;
    Segment current_segment = SEGMENT_UNKNOWN;

    while (!is_eof(assembler)) {
        Token current = current_token(assembler);
        if (current.kind == TOK_PREPROCESSOR) {
            Preprocessor prec = current.as_preprocessor;
            current_segment = prec.as_segment;
            advance_token(assembler);
            continue;
        }

        switch (current_segment) {
        case SEGMENT_READONLY:
            parse_readonly(assembler);
            continue;
        case SEGMENT_CODE:
            parse_instruction(assembler);
            continue;
        case SEGMENT_UNKNOWN:
            fprintf(stderr, "%s:%d: ERROR: expected segment\n",
                assembler->input_file, current.line);
            exit(1);
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
