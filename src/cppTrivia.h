#ifndef TREE_SITTER_CPP_TRIVIA_H_
#define TREE_SITTER_CPP_TRIVIA_H_

#include "conditionalMacro.h"
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
static void scan_cpp_directive_tail(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, TSLexerPosition *prefix_end);
static bool scan_cpp_directive_backslash(TSLexer *lexer, bool continuation, bool *spliced);
static void accept_cpp_directive_character(TSLexer *lexer, CppDirectiveWord *out, CppDirectiveScanState *scan, int32_t c, bool first, TSLexerPosition *prefix_end);
static inline bool ascii_identifier_unit(uint32_t c, bool continuation);
static int peek_cpp_units_line_end(const char *units, uint32_t size, uint32_t i, uint32_t count);
static int peek_cpp_units_line_comment(const char *units, uint32_t size, uint32_t i, uint32_t count);
static bool scan_cpp_ifdef_word(TSLexer *lexer, char *word, unsigned capacity, bool *ascii, bool escaped);
static bool scan_cpp_ifdef_word_tail(TSLexer *lexer, char *word, unsigned capacity, unsigned *length, bool *ascii);
static void append_cpp_ifdef_character(TSLexer *lexer, char *word, unsigned capacity, unsigned *length, bool *ascii);
static bool scan_cpp_directive_line_end(EmptyDirectives *state, TSLexer *lexer, bool *had_pragma);
static bool skip_cpp_ifdef_trivia(TSLexer *lexer, bool allow_line_comment, bool *escaped);
static bool skip_cpp_inline_spacing(TSLexer *lexer, bool spaced_splices, bool allow_line_comment);
static bool skip_cpp_inline_comment(TSLexer *lexer, bool spaced_splices, bool allow_line_comment);
static bool skip_cpp_block_comment_body(TSLexer *lexer, bool spaced_splices);

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
        if (!skip_cpp_inline_spacing(lexer, true, false)) return false;
        if (lexer->lookahead != '\\') return true;
        bool spliced = false;
        if (!scan_cpp_directive_backslash(lexer, false, &spliced)) return false;
        if (!spliced) {
            *starts_with_ucn = true;
            return true;
        }
    }
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

static int peek_cpp_directive_line_end(const TSLexer *lexer) {
    uint32_t count = 0, size = 0;
    const char *units = lexer->peek_units(lexer, &count, &size);
    return peek_cpp_units_line_end(units, size, 0, count);
}

static int peek_cpp_ifdef_header(const TSLexer *lexer) {
    uint32_t count = 0, size = 0;
    const char *units = lexer->peek_units(lexer, &count, &size);
    uint32_t i = 0;
    while (i < count && (directive_unit(units, size, i) == ' ' || directive_unit(units, size, i) == '\t')) i++;
    if (!i || i >= count) return 0;
    uint32_t first = directive_unit(units, size, i);
    if (first == '\n' || first == '\r') return -1;
    if (!ascii_identifier_unit(first, false)) return 0;
    uint32_t start = i++;
    while (i < count && ascii_identifier_unit(directive_unit(units, size, i), true)) i++;
    if (i >= count) return 0;
    uint32_t stop = directive_unit(units, size, i);
    if (stop >= 0x80 || stop == '\\' || (i - start == 7 && directive_unit(units, size, start) == '_')) return 0;
    return peek_cpp_units_line_end(units, size, i, count);
}

static inline bool ascii_identifier_unit(uint32_t c, bool continuation) {
    if (c >= 0x80 || c == '\\') return false;
    const uint64_t *table = continuation ? preproc_identifier_continue_ascii : preproc_identifier_start_ascii;
    return ((table[c / 64] >> (c % 64)) & 1) != 0;
}

static int peek_cpp_units_line_end(const char *units, uint32_t size, uint32_t i, uint32_t count) {
    while (i < count && (directive_unit(units, size, i) == ' ' || directive_unit(units, size, i) == '\t')) i++;
    if (i >= count) return 0;
    uint32_t c = directive_unit(units, size, i);
    if (c == '\n' || c == '\r') return 1;
    if (c == '/') return peek_cpp_units_line_comment(units, size, i, count);
    if (c >= 0x80 || c == '\v' || c == '\f' || c == '\\' || c == '_') return 0;
    return -1;
}

static int peek_cpp_units_line_comment(const char *units, uint32_t size, uint32_t i, uint32_t count) {
    if (i + 1 >= count || directive_unit(units, size, i + 1) != '/') return 0;
    for (i += 2; i < count; i++) {
        uint32_t c = directive_unit(units, size, i);
        if (c == '\n') return 1;
        if (c >= 0x80 || c == '\r' || c == '\\') return 0;
    }
    return 0;
}

static bool scan_cpp_ifdef_header(EmptyDirectives *state, TSLexer *lexer, bool *had_pragma) {
    if (had_pragma) *had_pragma = false;
    for (;;) {
        bool escaped = false;
        if (!skip_cpp_ifdef_trivia(lexer, false, &escaped)) return false;
        char word[sizeof(pragma_word)] = {0};
        bool ascii = false;
        if (!scan_cpp_ifdef_word(lexer, word, sizeof(word) - 1, &ascii, escaped)) return false;
        if (!ascii || strcmp(word, pragma_word) != 0 || !empty_directives_pragma_possible(state)) {
            return scan_cpp_directive_line_end(state, lexer, had_pragma);
        }
        bool after_escaped = false;
        if (!skip_cpp_ifdef_trivia(lexer, true, &after_escaped) || after_escaped) return false;
        if (lexer->lookahead != '(') return scan_cpp_directive_line_end(state, lexer, had_pragma);
        if (!scan_pragma_suffix_end_with(lexer, false, &(PragmaTrivia){empty_directives_skip, state})) return false;
        if (had_pragma) *had_pragma = true;
    }
}

static bool scan_cpp_ifdef_word(TSLexer *lexer, char *word, unsigned capacity, bool *ascii, bool escaped) {
    unsigned length = 0;
    *ascii = !escaped;
    if (escaped) {
        if (!scan_macro_name_ucn(lexer)) return false;
    } else if (lexer->lookahead == '\\' || !preproc_identifier_character(lexer->lookahead, false)) {
        return false;
    } else {
        append_cpp_ifdef_character(lexer, word, capacity, &length, ascii);
    }
    if (!scan_cpp_ifdef_word_tail(lexer, word, capacity, &length, ascii)) return false;
    word[*ascii ? length : 0] = '\0';
    return true;
}

static bool scan_cpp_ifdef_word_tail(TSLexer *lexer, char *word, unsigned capacity, unsigned *length, bool *ascii) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c != '\\') {
            if (!preproc_identifier_character(c, true)) return true;
            append_cpp_ifdef_character(lexer, word, capacity, length, ascii);
            continue;
        }
        lexer->advance(lexer, false);
        *ascii = false;
        if (scan_preproc_newline(lexer, false)) continue;
        if (!scan_macro_name_ucn(lexer)) return false;
    }
}

static void append_cpp_ifdef_character(TSLexer *lexer, char *word, unsigned capacity, unsigned *length, bool *ascii) {
    int32_t c = lexer->lookahead;
    if (*ascii && c < 0x80 && *length < capacity) word[(*length)++] = (char)c;
    else *ascii = false;
    lexer->advance(lexer, false);
}

static bool scan_cpp_directive_line_end(EmptyDirectives *state, TSLexer *lexer, bool *had_pragma) {
    for (;;) {
        bool escaped = false;
        if (!skip_cpp_ifdef_trivia(lexer, true, &escaped) || escaped) return false;
        int32_t c = lexer->lookahead;
        if (c == '\n' || c == '\r' || at_eof(lexer)) return true;
        if (c != '_' || !empty_directives_pragma_possible(state) || !scan_pragma_word(lexer)) return false;
        if (!skip_cpp_ifdef_trivia(lexer, false, &escaped) || escaped || lexer->lookahead != '(') return false;
        if (!scan_pragma_suffix_end_with(lexer, false, &(PragmaTrivia){empty_directives_skip, state})) return false;
        if (had_pragma) *had_pragma = true;
    }
}

static bool skip_cpp_ifdef_trivia(TSLexer *lexer, bool allow_line_comment, bool *escaped) {
    *escaped = false;
    for (;;) {
        if (!skip_cpp_inline_spacing(lexer, false, allow_line_comment)) return false;
        if (lexer->lookahead != '\\') return true;
        lexer->advance(lexer, false);
        if (!scan_preproc_newline(lexer, false)) {
            *escaped = true;
            return true;
        }
    }
}

static bool skip_cpp_inline_spacing(TSLexer *lexer, bool spaced_splices, bool allow_line_comment) {
    for (;;) {
        if (directive_horizontal(lexer->lookahead)) {
            lexer->advance(lexer, false);
        } else if (lexer->lookahead == '/') {
            if (!skip_cpp_inline_comment(lexer, spaced_splices, allow_line_comment)) return false;
        } else {
            return true;
        }
    }
}

static bool skip_cpp_inline_comment(TSLexer *lexer, bool spaced_splices, bool allow_line_comment) {
    lexer->advance(lexer, false);
    if (!empty_directives_skip_splices(lexer, spaced_splices)) return false;
    if (lexer->lookahead == '*') return skip_cpp_block_comment_body(lexer, spaced_splices);
    return allow_line_comment && lexer->lookahead == '/' && scan_preproc_line_comment(lexer);
}

static bool skip_cpp_block_comment_body(TSLexer *lexer, bool spaced_splices) {
    lexer->advance(lexer, false);
    bool star = false;
    while (!pragma_eof(lexer)) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '\\' && empty_directives_skip_line_splice(lexer, spaced_splices)) continue;
        if (star && c == '/') return true;
        star = c == '*';
    }
    return false;
}

#endif
