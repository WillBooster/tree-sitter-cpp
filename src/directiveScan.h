#ifndef TREE_SITTER_C_DIRECTIVE_SCAN_H_
#define TREE_SITTER_C_DIRECTIVE_SCAN_H_

#include "pragma.h"
#include <stdlib.h>

// Bytes from `start` to before `end` that hold contiguous characters, and possibly bytes that an included range leaves
// out after them, which the lexer never reads.
typedef struct {
    uint32_t start, end;
} DirectiveSpan;

// A run of characters in byte order, split where included ranges leave bytes out.
typedef struct {
    DirectiveSpan *items;
    uint32_t count, capacity;
    bool failed;
} DirectiveSpans;

// The bytes a stored character contributes to a span at most: a span needs only its characters' starts, and the position
// after a character also includes the bytes an included range leaves out after it.
#define DIRECTIVE_CHARACTER_BYTES 4

typedef enum { SCAN_CODE, SCAN_SLASH, SCAN_LINE_COMMENT, SCAN_BLOCK_COMMENT, SCAN_QUOTE, SCAN_WORD, SCAN_RAW_DELIMITER, SCAN_RAW_BODY } DirectiveScanMode;

// The line-state pass of `emptyDirectives.h` in the state it has on ordinary code, before the first directive and after
// each directive line that ends with no directive state left, fed one physical character at a time so that the code is
// never stored. It also tracks the run of spaces and backslashes before the next character, which a null directive's
// prefix covers: the first `run_spans` spans in `run` and the current one, from `run_start` to before `run_end`.
typedef struct {
    DirectiveScanMode mode;
    GnuRawStringPrefix raw;
    DirectiveSpans *run;
    int32_t quote, previous, pair;
    uint32_t spaces, run_start, run_end, run_spans;
    unsigned delimiter_length, closing_length, closing_end;
    char delimiter[16], closing[18];
    bool beginning, number, escape, star, splice, in_run;
} DirectiveScan;

static inline bool directive_scan_plain(DirectiveScan *scan, TSLexer *lexer);
static inline bool directive_scan_plain_word(DirectiveScan *scan, TSLexer *lexer, int32_t c);
static inline bool directive_scan_plain_code(DirectiveScan *scan, TSLexer *lexer, int32_t c);
static inline bool directive_scan_plain_text(DirectiveScan *scan, TSLexer *lexer, int32_t c);
static inline void directive_scan_lexer_run(DirectiveScan *scan, TSLexer *lexer, bool member);
static inline bool directive_scan_ascii_word(int32_t c);
static inline bool directive_scan_ascii_special(int32_t c);
static bool directive_scan_pending(DirectiveScan *scan, int32_t c);
static bool directive_scan_splice(DirectiveScan *scan, int32_t c);
static bool directive_scan_logical(DirectiveScan *scan, int32_t c);
static bool directive_scan_slash(DirectiveScan *scan, int32_t c);
static void directive_scan_quote(DirectiveScan *scan, int32_t c);
static bool directive_scan_code(DirectiveScan *scan, int32_t c);
static void directive_scan_word_start(DirectiveScan *scan, int32_t c);
static bool directive_scan_word(DirectiveScan *scan, int32_t c);
static bool directive_scan_raw_delimiter(DirectiveScan *scan, int32_t c);
static void directive_scan_raw_body(DirectiveScan *scan, int32_t c);
static bool directive_horizontal(int32_t c);
static bool directive_newline(int32_t c);
static void directive_spans_push(DirectiveSpans *spans, uint32_t start, uint32_t end);
static void directive_scan_close_run(DirectiveScan *scan, uint32_t limit, bool skipped);
static void directive_scan_push(DirectiveScan *scan, uint32_t start, uint32_t end);
static inline void directive_scan_extend(DirectiveScan *scan, uint32_t start, bool skipped);

// Consumes the lexer's ASCII characters that need no physical-layer work and change no state but the line beginning,
// the current word, the end of a comment and the run of spaces, which is most of the input, and stops at a character
// that `directive_scan_feed` has to handle.
static void directive_scan_plain_run(DirectiveScan *target, TSLexer *lexer) {
    if (target->pair || target->splice) return;
    // The lexer's calls cannot reach this copy, so it can stay in registers.
    DirectiveScan copy = *target;
    while (directive_scan_plain(&copy, lexer)) lexer->advance(lexer, false);
    *target = copy;
}

static inline bool directive_scan_plain(DirectiveScan *scan, TSLexer *lexer) {
    int32_t c = lexer->lookahead;
    if (c <= 0 || c >= 0x80) return false;
    switch (scan->mode) {
        case SCAN_WORD:
            return directive_scan_plain_word(scan, lexer, c);
        case SCAN_CODE:
            return directive_scan_plain_code(scan, lexer, c);
        case SCAN_LINE_COMMENT:
            return directive_scan_plain_text(scan, lexer, c);
        case SCAN_BLOCK_COMMENT:
            if (!directive_scan_plain_text(scan, lexer, c)) return false;
            if (scan->star && c == '/') scan->mode = SCAN_CODE;
            scan->star = c == '*';
            return true;
        case SCAN_QUOTE:
            return !scan->escape && c != scan->quote && directive_scan_plain_text(scan, lexer, c);
        case SCAN_RAW_BODY:
            if (c == '"' || !directive_scan_plain_text(scan, lexer, c)) return false;
            directive_scan_raw_body(scan, c);
            return true;
        default:
            return false;
    }
}

static inline bool directive_scan_plain_word(DirectiveScan *scan, TSLexer *lexer, int32_t c) {
    if (!directive_scan_ascii_word(c)) {
        // A line splice may continue the word, and the others may continue a number or open a raw string.
        if (directive_scan_ascii_special(c) || (scan->number && (c == '.' || c == '+' || c == '-'))) return false;
        scan->mode = SCAN_CODE;
        return directive_scan_plain_code(scan, lexer, c);
    }
    scan->in_run = false;
    scan->previous = c;
    if (scan->raw.length < sizeof scan->raw.value) gnu_raw_string_prefix_character(&scan->raw, c);
    return true;
}

static inline bool directive_scan_plain_code(DirectiveScan *scan, TSLexer *lexer, int32_t c) {
    if (directive_scan_ascii_special(c)) return false;
    bool space = directive_horizontal(c);
    directive_scan_lexer_run(scan, lexer, space);
    if (space) return true;
    scan->beginning = false;
    if (directive_scan_ascii_word(c)) directive_scan_word_start(scan, c);
    return true;
}

// The body of a comment or a literal.
static inline bool directive_scan_plain_text(DirectiveScan *scan, TSLexer *lexer, int32_t c) {
    if (c == '\n' || c == '\r' || c == '\\') return false;
    directive_scan_lexer_run(scan, lexer, directive_horizontal(c));
    return true;
}

// Extends or ends the run of spaces and backslashes with the lookahead, as `directive_scan_run` does. A span read from
// the lexer ends just after its last character's start, since the lexer may skip to the next included range after it.
static inline void directive_scan_lexer_run(DirectiveScan *scan, TSLexer *lexer, bool member) {
    if (!member) {
        scan->in_run = false;
        return;
    }
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    directive_scan_extend(scan, position.bytes, scan->in_run && lexer->is_at_included_range_start(lexer));
    scan->run_end = position.bytes + 1;
}

// Starts the run at a character from `start`, or a new span of it when bytes were left out before the character.
static inline void directive_scan_extend(DirectiveScan *scan, uint32_t start, bool skipped) {
    if (!scan->in_run) {
        scan->run_spans = 0;
        scan->run_start = start;
        scan->in_run = true;
    } else if (skipped) {
        directive_scan_push(scan, scan->run_start, scan->run_end);
        scan->run_start = start;
    }
}

// Stores the current span, which reaches `limit` unless bytes were left out before it.
static void directive_scan_close_run(DirectiveScan *scan, uint32_t limit, bool skipped) {
    directive_scan_push(scan, scan->run_start, skipped ? scan->run_end : limit);
}

// The list is written only here, so that the run's start leaves memory alone.
static void directive_scan_push(DirectiveScan *scan, uint32_t start, uint32_t end) {
    scan->run->count = scan->run_spans;
    directive_spans_push(scan->run, start, end);
    scan->run_spans = scan->run->count;
}

static inline bool directive_scan_ascii_word(int32_t c) {
    return c != '\\' && ((preproc_identifier_continue_ascii[c / 64] >> (c % 64)) & 1);
}

// NUL, newlines, `\`, `/`, quotes and `#`, which code leaves to `directive_scan_feed`.
static inline bool directive_scan_ascii_special(int32_t c) {
    static const uint64_t special[] = {
        1ull | (1ull << '\n') | (1ull << '\r') | (1ull << '"') | (1ull << '#') | (1ull << '\'') | (1ull << '/'),
        1ull << ('\\' - 64),
    };
    return (special[c / 64] >> (c % 64)) & 1;
}

// Extends or ends the run with the stored character `c` from `start` to `end`.
static void directive_scan_run(DirectiveScan *scan, int32_t c, uint32_t start, uint32_t end) {
    if (!pragma_space(c) && c != '\\') {
        scan->in_run = false;
        return;
    }
    directive_scan_extend(scan, start, scan->in_run && start != scan->run_end);
    scan->run_end = end - start > DIRECTIVE_CHARACTER_BYTES ? start + DIRECTIVE_CHARACTER_BYTES : end;
}

// Applies the physical layer of `directive_next` (line splices, paired newlines, and the character mapping of
// `directive_encode`) and returns true when `c` starts a directive, which the caller leaves unconsumed.
static bool directive_scan_feed(DirectiveScan *scan, int32_t c) {
    if ((uint32_t)c > 0x10ffff) c = 0xfffd;
    if (scan->mode == SCAN_RAW_BODY) {
        directive_scan_raw_body(scan, c);
        return false;
    }
    if (directive_scan_pending(scan, c)) return false;
    if (c == '\\') {
        scan->splice = true;
        return false;
    }
    if (directive_newline(c)) {
        scan->pair = c;
        c = '\n';
    }
    return directive_scan_logical(scan, c);
}

// Resolves the newline and the line splice that wait for `c`; true when `c` belongs to them.
static bool directive_scan_pending(DirectiveScan *scan, int32_t c) {
    if (scan->pair) {
        int32_t pair = scan->pair;
        scan->pair = 0;
        if (directive_newline(c) && c != pair) return true;
    }
    return scan->splice && directive_scan_splice(scan, c);
}

static bool directive_scan_splice(DirectiveScan *scan, int32_t c) {
    if (directive_horizontal(c)) {
        scan->spaces++;
        return true;
    }
    scan->splice = false;
    if (directive_newline(c)) {
        scan->spaces = 0;
        scan->pair = c;
        return true;
    }
    directive_scan_logical(scan, '\\');
    for (; scan->spaces; scan->spaces--) directive_scan_logical(scan, ' ');
    return false;
}

// Mirrors `directive_line_character` on a line with no directive state, character by character.
static bool directive_scan_logical(DirectiveScan *scan, int32_t c) {
    switch (scan->mode) {
        case SCAN_CODE:
            return directive_scan_code(scan, c);
        case SCAN_SLASH:
            return directive_scan_slash(scan, c);
        case SCAN_LINE_COMMENT:
            if (c == '\n') {
                scan->mode = SCAN_CODE;
                scan->beginning = true;
            }
            return false;
        case SCAN_BLOCK_COMMENT:
            if (scan->star && c == '/') scan->mode = SCAN_CODE;
            scan->star = c == '*';
            return false;
        case SCAN_QUOTE:
            directive_scan_quote(scan, c);
            return false;
        case SCAN_WORD:
            return directive_scan_word(scan, c);
        case SCAN_RAW_DELIMITER:
            return directive_scan_raw_delimiter(scan, c);
        case SCAN_RAW_BODY:
            break;
    }
    return false;
}

static bool directive_scan_slash(DirectiveScan *scan, int32_t c) {
    scan->mode = c == '/' ? SCAN_LINE_COMMENT : c == '*' ? SCAN_BLOCK_COMMENT : SCAN_CODE;
    scan->star = false;
    if (scan->mode != SCAN_CODE) return false;
    scan->beginning = false;
    return directive_scan_code(scan, c);
}

// Mirrors `directive_quoted`.
static void directive_scan_quote(DirectiveScan *scan, int32_t c) {
    if (scan->escape) scan->escape = false;
    else if (c == scan->quote) scan->mode = SCAN_CODE;
    else if (c == '\\') scan->escape = true;
    if (c == '\n' && scan->mode == SCAN_QUOTE) {
        scan->mode = SCAN_CODE;
        scan->beginning = true;
    }
}

static bool directive_scan_code(DirectiveScan *scan, int32_t c) {
    if (c == '\n') {
        scan->beginning = true;
        return false;
    }
    if (c == '/') {
        scan->mode = SCAN_SLASH;
        return false;
    }
    if (directive_horizontal(c) || (scan->beginning && pragma_space(c))) return false;
    if (c == '#' && scan->beginning) return true;
    scan->beginning = false;
    if (c == '"' || c == '\'') {
        scan->mode = SCAN_QUOTE;
        scan->quote = c;
        scan->escape = false;
    } else {
        directive_scan_word_start(scan, c);
    }
    return false;
}

static void directive_scan_word_start(DirectiveScan *scan, int32_t c) {
    if (!preproc_word(c, false) && !(c >= '0' && c <= '9')) return;
    scan->mode = SCAN_WORD;
    scan->number = c >= '0' && c <= '9';
    scan->previous = c;
    scan->raw = (GnuRawStringPrefix){0};
    gnu_raw_string_prefix_character(&scan->raw, c);
}

// Mirrors `directive_word` with `raw` and without `classify`.
static bool directive_scan_word(DirectiveScan *scan, int32_t c) {
    int32_t previous = scan->previous;
    bool exponent_sign = scan->number && (c == '+' || c == '-') &&
        (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P');
    if (preproc_word(c, true) || (scan->number && (c == '.' || c == '\'' || exponent_sign))) {
        scan->previous = c;
        gnu_raw_string_prefix_character(&scan->raw, c);
        return false;
    }
    scan->mode = SCAN_CODE;
    if (c == '"' && gnu_raw_string_prefix(&scan->raw)) {
        scan->mode = SCAN_RAW_DELIMITER;
        scan->delimiter_length = 0;
        return false;
    }
    return directive_scan_code(scan, c);
}

// Mirrors the delimiter check of `directive_raw`. On an invalid delimiter, the `"` opens an ordinary string literal
// instead, so the characters read since are fed again. They hold no `(`, so they cannot start a raw string body.
static bool directive_scan_raw_delimiter(DirectiveScan *scan, int32_t c) {
    if (c == '(') {
        scan->mode = SCAN_RAW_BODY;
        scan->closing_length = scan->closing_end = 0;
        return false;
    }
    if (scan->delimiter_length < sizeof scan->delimiter && c >= 0x21 && c <= 0x7e && c != ')' && c != '\\') {
        scan->delimiter[scan->delimiter_length++] = (char)c;
        return false;
    }
    char replay[sizeof scan->delimiter];
    unsigned length = scan->delimiter_length;
    memcpy(replay, scan->delimiter, length);
    scan->mode = SCAN_QUOTE;
    scan->quote = '"';
    scan->escape = false;
    for (unsigned i = 0; i < length; i++) directive_scan_logical(scan, replay[i]);
    return directive_scan_logical(scan, c);
}

// Mirrors the physical closing search of `directive_raw` with the last characters kept in a ring.
static void directive_scan_raw_body(DirectiveScan *scan, int32_t c) {
    unsigned size = scan->delimiter_length + 2;
    unsigned oldest = scan->closing_end;
    scan->closing[oldest] = c > 0 && c < 0x80 ? (char)c : 0;
    scan->closing_end = oldest + 1 == size ? 0 : oldest + 1;
    if (scan->closing_length < size) scan->closing_length++;
    if (c != '"' || scan->closing_length < size || scan->closing[scan->closing_end] != ')') return;
    for (unsigned i = 0; i < scan->delimiter_length; i++) {
        if (scan->closing[(scan->closing_end + 1 + i) % size] != scan->delimiter[i]) return;
    }
    scan->mode = SCAN_CODE;
}

static bool directive_horizontal(int32_t c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

static bool directive_newline(int32_t c) {
    return c == '\n' || c == '\r';
}

static void directive_spans_push(DirectiveSpans *spans, uint32_t start, uint32_t end) {
    if (spans->count == spans->capacity) {
        uint32_t capacity = spans->capacity ? spans->capacity * 2 : 8;
        DirectiveSpan *items = capacity > spans->capacity && (size_t)capacity <= SIZE_MAX / sizeof(*items)
            ? realloc(spans->items, (size_t)capacity * sizeof(*items))
            : NULL;
        if (!items) {
            spans->failed = true;
            return;
        }
        spans->items = items;
        spans->capacity = capacity;
    }
    spans->items[spans->count++] = (DirectiveSpan){start, end};
}

#endif
