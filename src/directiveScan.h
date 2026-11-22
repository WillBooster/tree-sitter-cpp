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
    // Set when the characters read hold `pragma_word`, also in a comment or a literal: a scan starts inside a literal
    // after an escape sequence, and the lexer ends some comments earlier than this pass does.
    bool *pragma;
    // The characters read last, the latest in the low byte and zero for one that is not ASCII. Code outside words is
    // left out, so a word's first character restarts them.
    uint64_t recent;
    int32_t quote, previous, pair;
    uint32_t spaces, run_start, run_end, run_spans;
    unsigned delimiter_length, closing_length, closing_end;
    char delimiter[16], closing[18];
    bool beginning, number, escape, star, splice, in_run;
} DirectiveScan;

static bool directive_scan_leading_comment(DirectiveScan *scan, TSLexer *lexer);
static bool directive_scan_feed(DirectiveScan *scan, int32_t c);
static inline bool directive_scan_plain_mode(DirectiveScan *scan, TSLexer *lexer);
static inline bool directive_scan_plain_word(DirectiveScan *scan, TSLexer *lexer);
static inline void directive_scan_word_characters(DirectiveScan *scan, uint64_t recent, uint32_t count);
static inline bool directive_pragma_word(uint64_t recent);
static inline uint64_t directive_scan_note(DirectiveScan *scan, uint64_t recent, int32_t c);
static inline bool directive_scan_plain_code(DirectiveScan *scan, TSLexer *lexer);
static inline bool directive_scan_plain_block_comment(DirectiveScan *scan, TSLexer *lexer);
static inline void directive_scan_plain_text(DirectiveScan *scan, TSLexer *lexer, int32_t end, bool raw);
static inline void directive_scan_lexer_run(DirectiveScan *scan, TSLexer *lexer, bool member);
static inline bool directive_scan_ascii_word(int32_t c);
static inline bool directive_scan_ascii_plain(int32_t c);
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

// Feeds the spaces at the lexer's position, and a `/` after them with the character that follows it. Returns true when
// they open a comment. It reads no further than that one `/`, so that the scans after a postponed build, which start
// where this one's tokens end, do not read the same characters again and again.
static bool directive_scan_leading_comment(DirectiveScan *scan, TSLexer *lexer) {
    for (bool slash = false;;) {
        if (scan->mode == SCAN_LINE_COMMENT || scan->mode == SCAN_BLOCK_COMMENT) return true;
        int32_t c = lexer->lookahead;
        bool space = !slash && scan->mode == SCAN_CODE && !scan->splice && (pragma_space(c) || c == '/');
        if ((!space && scan->mode != SCAN_SLASH) || pragma_eof(lexer)) return false;
        slash = slash || c == '/';
        directive_scan_feed(scan, c);
        directive_scan_lexer_run(scan, lexer, pragma_space(c) || c == '\\');
        lexer->advance(lexer, false);
    }
}

// Consumes the lexer's ASCII characters that need no physical-layer work and change no state but the line beginning,
// the current word, the end of a comment and the run of spaces, which is most of the input, and stops at a character
// that `directive_scan_feed` has to handle. Each mode has a loop of its own, which keeps what it reads in registers
// across the lexer's calls.
static void directive_scan_plain_run(DirectiveScan *scan, TSLexer *lexer) {
    if (scan->splice) return;
    // Only another newline can belong to the newline before it.
    if (scan->pair && directive_newline(lexer->lookahead)) return;
    scan->pair = 0;
    while (directive_scan_plain_mode(scan, lexer)) {}
}

// Consumes the plain characters of the current mode. Returns true when those of another mode may follow.
static inline bool directive_scan_plain_mode(DirectiveScan *scan, TSLexer *lexer) {
    switch (scan->mode) {
        case SCAN_WORD:
            return directive_scan_plain_word(scan, lexer);
        case SCAN_CODE:
            return directive_scan_plain_code(scan, lexer);
        case SCAN_BLOCK_COMMENT:
            return directive_scan_plain_block_comment(scan, lexer);
        case SCAN_LINE_COMMENT:
            directive_scan_plain_text(scan, lexer, 0, false);
            return false;
        case SCAN_QUOTE:
            if (!scan->escape) directive_scan_plain_text(scan, lexer, scan->quote, false);
            return false;
        case SCAN_RAW_BODY:
            directive_scan_plain_text(scan, lexer, '"', true);
            return false;
        default:
            return false;
    }
}

// Consumes the rest of the word. Returns true when code follows it.
static inline bool directive_scan_plain_word(DirectiveScan *scan, TSLexer *lexer) {
    int32_t c = lexer->lookahead;
    uint64_t recent = scan->recent;
    uint32_t count = 0;
    for (; directive_scan_ascii_word(c); c = lexer->lookahead) {
        recent = recent << 8 | (uint64_t)c;
        count++;
        lexer->advance(lexer, false);
    }
    if (count) directive_scan_word_characters(scan, recent, count);
    // A line splice may continue the word, and the others may continue a number or open a raw string.
    if (!directive_scan_ascii_plain(c) || (scan->number && (c == '.' || c == '+' || c == '-'))) return false;
    scan->mode = SCAN_CODE;
    return true;
}

// Adds the word's next `count` characters, the last ones of `recent`, as `directive_scan_word` does one at a time: a
// prefix that would exceed three characters is no raw string prefix whatever they are.
static inline void directive_scan_word_characters(DirectiveScan *scan, uint64_t recent, uint32_t count) {
    scan->in_run = false;
    scan->previous = (int32_t)(recent & 0xff);
    scan->recent = recent;
    if (directive_pragma_word(recent)) *scan->pragma = true;
    if (scan->raw.length >= sizeof scan->raw.value) return;
    if (count > sizeof scan->raw.value - 1 - scan->raw.length) {
        gnu_raw_string_prefix_invalidate(&scan->raw);
        return;
    }
    while (count--) gnu_raw_string_prefix_character(&scan->raw, (int32_t)(recent >> (8 * count) & 0xff));
}

// Consumes code up to a word. Returns true when a word follows, whose first character is consumed.
static inline bool directive_scan_plain_code(DirectiveScan *scan, TSLexer *lexer) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (!directive_scan_ascii_plain(c)) return false;
        bool space = directive_horizontal(c);
        directive_scan_lexer_run(scan, lexer, space);
        lexer->advance(lexer, false);
        if (space) continue;
        scan->beginning = false;
        if (!directive_scan_ascii_word(c)) continue;
        directive_scan_word_start(scan, c);
        if (scan->mode == SCAN_WORD) return true;
    }
}

// Consumes the comment's body. Returns true when the comment ends.
static inline bool directive_scan_plain_block_comment(DirectiveScan *scan, TSLexer *lexer) {
    bool star = scan->star, closed = false;
    uint64_t recent = scan->recent;
    while (!closed) {
        int32_t c = lexer->lookahead;
        if (c <= 0 || c >= 0x80 || c == '\n' || c == '\r' || c == '\\') break;
        directive_scan_lexer_run(scan, lexer, directive_horizontal(c));
        recent = directive_scan_note(scan, recent, c);
        lexer->advance(lexer, false);
        closed = star && c == '/';
        star = c == '*';
    }
    scan->recent = recent;
    scan->star = star;
    if (closed) scan->mode = SCAN_CODE;
    return closed;
}

// Consumes the body of a line comment or a literal up to `end`, the closing search of a `raw` string seeing each
// character.
static inline void directive_scan_plain_text(DirectiveScan *scan, TSLexer *lexer, int32_t end, bool raw) {
    uint64_t recent = scan->recent;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c <= 0 || c >= 0x80 || c == '\n' || c == '\r' || c == '\\' || c == end) break;
        directive_scan_lexer_run(scan, lexer, directive_horizontal(c));
        if (raw) directive_scan_raw_body(scan, c);
        recent = directive_scan_note(scan, recent, c);
        lexer->advance(lexer, false);
    }
    scan->recent = recent;
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
    return (uint32_t)c < 0x80 && c != '\\' && ((preproc_identifier_continue_ascii[c / 64] >> (c % 64)) & 1);
}

// ASCII but NUL, newlines, `\`, `/`, quotes and `#`, which code leaves to `directive_scan_feed`.
static inline bool directive_scan_ascii_plain(int32_t c) {
    static const uint64_t special[] = {
        1ull | (1ull << '\n') | (1ull << '\r') | (1ull << '"') | (1ull << '#') | (1ull << '\'') | (1ull << '/'),
        1ull << ('\\' - 64),
    };
    return (uint32_t)c < 0x80 && !((special[c / 64] >> (c % 64)) & 1);
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
    scan->recent = directive_scan_note(scan, scan->recent, c);
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
    scan->recent = c < 0x80 ? (uint64_t)c : 0;
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

// Adds `c` to the characters read last and returns them.
static inline uint64_t directive_scan_note(DirectiveScan *scan, uint64_t recent, int32_t c) {
    recent = recent << 8 | ((uint32_t)c < 0x80 ? (uint64_t)c : 0);
    if (directive_pragma_word(recent)) *scan->pragma = true;
    return recent;
}

// Whether the characters in `recent`, the last one in the low byte, end in `pragma_word`.
static inline bool directive_pragma_word(uint64_t recent) {
    uint64_t word = 0;
    for (unsigned i = 0; i < sizeof pragma_word - 1; i++) word = word << 8 | (uint8_t)pragma_word[i];
    return (recent & ((1ull << 8 * (sizeof pragma_word - 1)) - 1)) == word;
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
