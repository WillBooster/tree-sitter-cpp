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
static bool skip_cpp_directive_prefix(TSLexer *lexer, bool *starts_with_ucn);
static bool skip_cpp_directive_comment(TSLexer *lexer);
static void scan_cpp_directive_tail(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, TSLexerPosition *prefix_end);
static bool scan_cpp_directive_backslash(TSLexer *lexer, bool continuation, bool *spliced);
static void accept_cpp_directive_character(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, int32_t c, bool first, TSLexerPosition *prefix_end);

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
    if (!skip_cpp_directive_prefix(lexer, &out->starts_with_ucn)) return false;
    CppDirectiveScanState scan = {.plain_ascii = !out->starts_with_ucn, .keyword_prefix = !out->starts_with_ucn};
    TSLexerPosition prefix_end = {0};
    if (out->starts_with_ucn) {
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

static bool skip_cpp_directive_prefix(TSLexer *lexer, bool *starts_with_ucn) {
    *starts_with_ucn = false;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (directive_horizontal(c)) {
            lexer->advance(lexer, false);
            continue;
        }
        if (c == '/') {
            if (!skip_cpp_directive_comment(lexer)) return false;
            continue;
        }
        if (c != '\\') return true;
        bool spliced = false;
        if (!scan_cpp_directive_backslash(lexer, false, &spliced)) return false;
        if (!spliced) {
            *starts_with_ucn = true;
            return true;
        }
    }
}

static bool skip_cpp_directive_comment(TSLexer *lexer) {
    lexer->advance(lexer, false);
    if (!empty_directives_skip_splices(lexer, true) || lexer->lookahead != '*') return false;
    lexer->advance(lexer, false);
    bool star = false;
    while (!pragma_eof(lexer)) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '\\' && empty_directives_skip_line_splice(lexer, true)) continue;
        if (star && c == '/') return true;
        star = c == '*';
    }
    return false;
}

static void scan_cpp_directive_tail(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, TSLexerPosition *prefix_end) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c != '\\') {
            if (!preproc_identifier_character(c, true)) return;
            accept_cpp_directive_character(lexer, out, scan, c, false, prefix_end);
            continue;
        }
        if (scan->keyword_prefix && !scan->trailing_splice) lexer->get_position(lexer, prefix_end);
        scan->trailing_splice = true;
        bool spliced = false;
        if (!scan_cpp_directive_backslash(lexer, true, &spliced)) {
            out->unmatched_backslash = true;
            return;
        }
        if (spliced) continue;
        scan->keyword_prefix = scan->plain_ascii = false;
        lexer->mark_end(lexer);
    }
}

static bool scan_cpp_directive_backslash(TSLexer *lexer, bool continuation, bool *spliced) {
    lexer->advance(lexer, false);
    bool had_space = directive_horizontal(lexer->lookahead);
    *spliced = empty_directives_skip_line_splice(lexer, true);
    if (*spliced) return true;
    return !had_space && empty_directives_ucn_follows_splices(lexer, continuation, true);
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

#endif
