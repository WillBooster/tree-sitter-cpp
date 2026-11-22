#ifndef TREE_SITTER_CPP_TRIVIA_H_
#define TREE_SITTER_CPP_TRIVIA_H_

#include "emptyDirectives.h"
#include "pragma.h"

#include <wctype.h>

typedef struct {
    char keyword[10];
    uint32_t keyword_end;
    bool starts_with_ucn;
    bool unmatched_backslash;
} CppDirectiveWord;

typedef struct {
    unsigned length;
    bool plain_ascii;
    bool keyword_prefix;
    bool trailing_splice;
} CppDirectiveScanState;

static bool skip_cpp_comment(TSLexer *lexer, bool *newline);
static bool skip_cpp_space(TSLexer *lexer, bool *newline, uint32_t *failure);
static void skip_cpp_whitespace_run(TSLexer *lexer, bool *newline);
static inline bool is_unicode_whitespace(int32_t c);
static inline bool at_eof(const TSLexer *lexer);
static bool has_plain_ascii_directive_word(const TSLexer *lexer);
static bool scan_cpp_extended_directive_word(TSLexer *lexer, CppDirectiveWord *out);
static bool skip_cpp_directive_prefix(TSLexer *lexer, bool *consumed_backslash);
static void scan_cpp_directive_tail(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, TSLexerPosition *prefix_end);
static void accept_cpp_directive_character(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, int32_t c, bool first, TSLexerPosition *prefix_end);
static bool scan_cpp_directive_ucn(TSLexer *lexer);

/// Skips the trivia a lookahead crosses, null directives included, which end a line like the newline they contain.
/// Returns false at a `\\` or `/` that starts no trivia, setting `*failure` (when given) to its byte.
static bool skip_cpp_trivia(TSLexer *lexer, bool *newline, EmptyDirectives *directives, uint32_t *failure) {
    for (;;) {
        if (!skip_cpp_space(lexer, newline, failure)) return false;
        if (!empty_directives_consume(lexer, directives, false)) return true;
        *newline = true;
    }
}

static bool skip_cpp_space(TSLexer *lexer, bool *newline, uint32_t *failure) {
    if (lexer->lookahead == ' ') lexer->advance(lexer, false);
    if (!is_unicode_whitespace(lexer->lookahead) && lexer->lookahead != '\\' && lexer->lookahead != '/') return true;
    for (;;) {
        skip_cpp_whitespace_run(lexer, newline);
        int32_t c = lexer->lookahead;
        if (c != '\\' && c != '/') return true;
        if (failure) {
            TSLexerPosition position;
            lexer->get_position(lexer, &position);
            *failure = position.bytes;
        }
        if (c == '\\') {
            lexer->advance(lexer, false);
            if (!scan_preproc_newline(lexer, false)) return false;
        } else if (!skip_cpp_comment(lexer, newline)) {
            return false;
        }
    }
}

static void skip_cpp_whitespace_run(TSLexer *lexer, bool *newline) {
    for (;;) {
        if (skip_ascii_space_units(lexer, newline)) continue;
        if (!is_unicode_whitespace(lexer->lookahead)) return;
        *newline |= lexer->lookahead == '\n' || lexer->lookahead == '\r';
        lexer->advance(lexer, false);
    }
}

static bool skip_cpp_comment(TSLexer *lexer, bool *newline) {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '/') {
        int32_t previous = 0;
        while (!at_eof(lexer) && (lexer->lookahead != '\n' || previous == '\\')) {
            if (lexer->lookahead != '\r') previous = lexer->lookahead;
            lexer->advance(lexer, false);
        }
        return true;
    }
    if (lexer->lookahead != '*') return false;
    lexer->advance(lexer, false);
    bool star = false;
    while (!at_eof(lexer)) {
        int32_t c = lexer->lookahead;
        *newline |= c == '\n' || c == '\r';
        lexer->advance(lexer, false);
        if (star && c == '/') return true;
        star = c == '*';
    }
    return false;
}

static inline bool is_unicode_whitespace(int32_t c) {
    if (c < 0) return false;
    if (c < 0x80) return c == ' ' || (uint32_t)(c - '\t') < 5;
    return iswspace((wint_t)c) != 0;
}

static inline bool at_eof(const TSLexer *lexer) {
    return lexer->lookahead == 0 && lexer->eof(lexer);
}

static bool scan_cpp_directive_word(TSLexer *lexer, CppDirectiveWord *out) {
    *out = (CppDirectiveWord){0};
    if (has_plain_ascii_directive_word(lexer)) {
        return empty_directives_scan_word(lexer, out->keyword, sizeof out->keyword, &out->keyword_end);
    }
    return scan_cpp_extended_directive_word(lexer, out);
}

static bool has_plain_ascii_directive_word(const TSLexer *lexer) {
    uint32_t count = 0, size = 0;
    const char *units = lexer->peek_units(lexer, &count, &size);
    uint32_t i = 1;
    while (i < count && (directive_unit(units, size, i) == ' ' || directive_unit(units, size, i) == '\t')) i++;
    if (i >= count || !empty_directives_name_character((int32_t)directive_unit(units, size, i), false)) return false;
    while (i < count && empty_directives_name_character((int32_t)directive_unit(units, size, i), true)) i++;
    if (i >= count) return false;
    uint32_t sep = directive_unit(units, size, i);
    return sep < 0x80 && sep != '\\' && sep != '$';
}

static bool scan_cpp_extended_directive_word(TSLexer *lexer, CppDirectiveWord *out) {
    lexer->advance(lexer, false);
    bool consumed_backslash = false;
    if (!skip_cpp_directive_prefix(lexer, &consumed_backslash)) return false;
    CppDirectiveScanState scan = {.plain_ascii = true, .keyword_prefix = true};
    TSLexerPosition prefix_end = {0};
    if (consumed_backslash) {
        if (!scan_cpp_directive_ucn(lexer)) return false;
        out->starts_with_ucn = true;
        scan.plain_ascii = scan.keyword_prefix = false;
        lexer->mark_end(lexer);
    } else {
        int32_t first = lexer->lookahead;
        if (!((first >= '0' && first <= '9') || (first != '\\' && preproc_identifier_character(first, false)))) {
            return false;
        }
        accept_cpp_directive_character(lexer, out, &scan, first, true, &prefix_end);
    }
    scan_cpp_directive_tail(lexer, out, &scan, &prefix_end);
    if (scan.keyword_prefix && !scan.trailing_splice) lexer->get_position(lexer, &prefix_end);
    out->keyword_end = prefix_end.bytes;
    out->keyword[scan.plain_ascii && scan.length < sizeof out->keyword ? scan.length : 0] = '\0';
    return true;
}

static bool skip_cpp_directive_prefix(TSLexer *lexer, bool *consumed_backslash) {
    *consumed_backslash = false;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (directive_horizontal(c)) {
            lexer->advance(lexer, false);
        } else if (c == '/') {
            lexer->advance(lexer, false);
            if (!scan_preproc_splices(lexer) || lexer->lookahead != '*' || !scan_preproc_block_comment(lexer)) {
                return false;
            }
        } else if (c == '\\') {
            lexer->advance(lexer, false);
            if (!scan_preproc_newline(lexer, false)) {
                *consumed_backslash = true;
                return true;
            }
        } else {
            return true;
        }
    }
}

static void scan_cpp_directive_tail(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, TSLexerPosition *prefix_end) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == '\\') {
            if (scan->keyword_prefix && !scan->trailing_splice) lexer->get_position(lexer, prefix_end);
            scan->trailing_splice = true;
            lexer->advance(lexer, false);
            if (scan_preproc_newline(lexer, false)) continue;
            if (!scan_cpp_directive_ucn(lexer)) {
                out->unmatched_backslash = true;
                return;
            }
            scan->keyword_prefix = scan->plain_ascii = false;
            lexer->mark_end(lexer);
            continue;
        }
        if (!preproc_identifier_character(c, true)) return;
        accept_cpp_directive_character(lexer, out, scan, c, false, prefix_end);
    }
}

static void accept_cpp_directive_character(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, int32_t c, bool first, TSLexerPosition *prefix_end) {
    if (!empty_directives_name_character(c, !first)) scan->plain_ascii = false;
    if (!first && scan->keyword_prefix && !empty_directives_name_character(c, true)) {
        if (!scan->trailing_splice) lexer->get_position(lexer, prefix_end);
        scan->keyword_prefix = false;
    }
    if (scan->plain_ascii && scan->length < sizeof out->keyword) out->keyword[scan->length++] = (char)c;
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    if (scan->keyword_prefix) scan->trailing_splice = false;
}

static bool scan_cpp_directive_ucn(TSLexer *lexer) {
    if (!scan_preproc_splices(lexer)) return false;
    int32_t c = lexer->lookahead;
    if (c != 'u' && c != 'U' && c != 'N') return false;
    bool in_number = false;
    return scan_preproc_ucn(lexer, &in_number);
}

#endif
