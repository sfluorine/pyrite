#include <ctype.h>
#include <stdint.h>

typedef struct {
    char const* source;
    int line;
} Parser;

static void parser_init(Parser* parser, char const* source)
{
    parser->source = source;
    parser->line = 1;
}

static void advance(Parser* parser)
{
    if (!*parser->source)
        return;

    if (*parser->source == '\n') {
        parser->line += 1;
    }

    parser->source++;
}

static void skip_whitespace(Parser* parser)
{
    while (*parser->source && isspace(*parser->source))
        advance(parser);
}

int main()
{
}
