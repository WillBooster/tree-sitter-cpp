#ifndef TREE_SITTER_C_GNU_RAW_STRING_H_
#define TREE_SITTER_C_GNU_RAW_STRING_H_

#include "tree_sitter/parser.h"
#include <string.h>

typedef struct {
    char value[4];
    unsigned length;
} GnuRawStringPrefix;

static inline bool pragma_eof(const TSLexer *lexer) {
    return lexer->lookahead == 0 && lexer->eof(lexer);
}

static bool scan_gnu_raw_string_body(TSLexer *lexer);
static void gnu_raw_string_prefix_invalidate(GnuRawStringPrefix *prefix);

static inline bool scan_gnu_raw_string_token(TSLexer *lexer) {
    if (!scan_gnu_raw_string_body(lexer)) return false;
    lexer->mark_end(lexer);
    return true;
}

static bool scan_gnu_raw_string_end(TSLexer *lexer) {
    if (!scan_gnu_raw_string_body(lexer) || lexer->lookahead != '"') return false;
    lexer->advance(lexer, false);
    return true;
}

static void gnu_raw_string_prefix_character(GnuRawStringPrefix *prefix, int32_t c) {
    if (prefix->length < sizeof(prefix->value) - 1 && c > 0 && c < 128) {
        prefix->value[prefix->length++] = (char)c;
        prefix->value[prefix->length] = 0;
    } else {
        gnu_raw_string_prefix_invalidate(prefix);
    }
}

static void gnu_raw_string_prefix_invalidate(GnuRawStringPrefix *prefix) {
    prefix->length = sizeof(prefix->value);
}

static bool gnu_raw_string_prefix(const GnuRawStringPrefix *prefix) {
    return prefix->length < sizeof(prefix->value) &&
        (strcmp(prefix->value, "R") == 0 || strcmp(prefix->value, "LR") == 0 ||
         strcmp(prefix->value, "uR") == 0 || strcmp(prefix->value, "UR") == 0 ||
         strcmp(prefix->value, "u8R") == 0);
}

static bool scan_gnu_raw_string_body(TSLexer *lexer) {
    char delimiter[16];
    unsigned length = 0;
    while (lexer->lookahead != '(') {
        int32_t c = lexer->lookahead;
        if (pragma_eof(lexer) || length == sizeof delimiter || c < 0x21 || c > 0x7e || c == ')' || c == '\\') return false;
        delimiter[length++] = (char)c;
        lexer->advance(lexer, false);
    }
    lexer->advance(lexer, false);
    for (int delimiter_index = -1; !pragma_eof(lexer); lexer->advance(lexer, false)) {
        if (delimiter_index >= 0 && (unsigned)delimiter_index == length && lexer->lookahead == '"') break;
        if (delimiter_index >= 0 && (unsigned)delimiter_index < length &&
            lexer->lookahead == delimiter[delimiter_index]) {
            delimiter_index++;
        } else {
            delimiter_index = -1;
        }
        if (lexer->lookahead == ')') delimiter_index = 0;
    }
    return true;
}

#endif
