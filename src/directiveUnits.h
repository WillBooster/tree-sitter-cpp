// code-gauge-allow-file fileNcss -- Unit-level code, comment, quote, and SIMD scanners share `DirectiveUnitPass`.
// The line-state pass of `directiveScan.h` over code units read from memory instead of characters fed one at a time.
// Included by `directiveScan.h`, whose `DirectiveScan` it reads and leaves as the character path would.
#ifndef TREE_SITTER_C_DIRECTIVE_UNITS_H_
#define TREE_SITTER_C_DIRECTIVE_UNITS_H_

#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#define DIRECTIVE_USE_NEON 1
#endif

// The classes of an ASCII character among the units of the input: whether it is part of a word, horizontal space, or
// plain code (word or plain punctuation), whether it is `_` or `P`, without both of which no text holds `pragma_word`,
// and whether it is plain text of a line comment, of a block comment and of a literal.
enum {
    UNIT_WORD = 1,
    UNIT_SPACE = 2,
    UNIT_CODE = 4,
    UNIT_UNDERSCORE = 8,
    UNIT_P = 16,
    UNIT_LINE = 32,
    UNIT_BLOCK = 64,
    UNIT_QUOTE = 128,
};
#define UNIT_PRAGMA (UNIT_UNDERSCORE | UNIT_P)
#define UNIT_CLASS_COUNT 128

typedef struct {
    uint32_t consumed, line_feeds, tail;
} DirectiveUnits;

// The pass's state over `count` units of which `i` are read. `floor` is where the current stretch of plain code began,
// and `pragma_from` where `seen` started accumulating character classes.
typedef struct {
    const char *units;
    const uint8_t *classes;
    uint32_t count, i, start, floor, pragma_from, line_feeds, line, quote;
    int32_t previous;
    DirectiveScanMode mode;
    bool lines, beginning, star, number, pragma, fresh;
    uint8_t seen;
#ifdef DIRECTIVE_USE_NEON
    uint8x16x4_t table_lo, table_hi;
    uint8x16_t seen_vec;
#endif
} DirectiveUnitPass;

#if defined(__GNUC__) || defined(__clang__)
#define DIRECTIVE_INLINE __attribute__((always_inline)) static inline
#else
#define DIRECTIVE_INLINE static inline
#endif

DIRECTIVE_INLINE bool directive_units_code(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_code_beginning(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_code_token(DirectiveUnitPass *pass, unsigned size, uint32_t c);
DIRECTIVE_INLINE bool directive_units_slash(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_word_quote(DirectiveUnitPass *pass, unsigned size, uint32_t c);
DIRECTIVE_INLINE void directive_units_finish_code(DirectiveUnitPass *pass, unsigned size);
static uint32_t directive_units_word_start(const DirectiveUnitPass *pass, unsigned size, uint32_t end);
DIRECTIVE_INLINE bool directive_units_word(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_line_comment(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_block_comment(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_quoted(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE uint32_t directive_units_advance_mask(DirectiveUnitPass *pass, unsigned size, uint8_t mask);
#ifdef DIRECTIVE_USE_NEON
DIRECTIVE_INLINE void directive_units_neon_advance(DirectiveUnitPass *pass, unsigned size, uint8_t mask);
DIRECTIVE_INLINE bool directive_units_neon_load(const char *units, unsigned size, uint32_t i, uint8x16_t *chars);
#endif
DIRECTIVE_INLINE void directive_units_open(DirectiveUnitPass *pass, unsigned size, DirectiveScanMode mode, uint32_t length);
DIRECTIVE_INLINE bool directive_units_close(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_line_feed(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_escape(const DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE void directive_units_flush_pragma(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE uint32_t directive_unit(const char *units, unsigned size, uint32_t index);
static void directive_scan_units_end(DirectiveScan *scan, const char *units, unsigned size, uint32_t from, uint32_t end, uint32_t byte);
static void directive_scan_units_word(DirectiveScan *scan, const char *units, unsigned size, uint32_t first, uint32_t end);
static bool directive_units_pragma(const char *units, unsigned size, uint32_t start, uint32_t end);

// Runs the line-state pass over `count` units of `size` bytes, the first of which is at `byte` of the input, as
// `directive_scan_feed` does one character at a time. It stops before the first unit that needs the physical layer (a
// backslash that may start a line splice, a carriage return, a character that is not ASCII), that starts a directive or
// may open a raw string, or that the units read do not tell apart. Without `lines`, it also stops before each line
// feed, leaves `scan` as it is, and consumes no part of a word, comment or literal that it does not finish.
// code-gauge-allow functionNcss -- One pass copies the scan state in, dispatches on its mode, and copies it back.
DIRECTIVE_INLINE DirectiveUnits directive_scan_units(DirectiveScan *scan, const char *units, uint32_t count, unsigned size, uint32_t byte, bool lines) {
    DirectiveUnitPass pass = {
        .units = units, .classes = scan->classes, .count = count, .quote = (uint32_t)scan->quote, .previous = scan->previous,
        .mode = scan->mode, .lines = lines, .beginning = scan->beginning, .star = scan->star, .number = scan->number,
#ifdef DIRECTIVE_USE_NEON
        .table_lo = vld1q_u8_x4(scan->classes), .table_hi = vld1q_u8_x4(scan->classes + 64), .seen_vec = vdupq_n_u8(0),
#endif
    };
    for (bool more = true; more;) {
        switch (pass.mode) {
            case SCAN_WORD:
                more = directive_units_word(&pass, size);
                break;
            case SCAN_LINE_COMMENT:
                more = directive_units_line_comment(&pass, size);
                break;
            case SCAN_BLOCK_COMMENT:
                more = directive_units_block_comment(&pass, size);
                break;
            case SCAN_QUOTE:
                more = directive_units_quoted(&pass, size);
                break;
            default:
                more = directive_units_code(&pass, size);
        }
    }
    directive_units_finish_code(&pass, size);
    if (!lines) return (DirectiveUnits){pass.mode == SCAN_CODE ? pass.i : pass.start, 0, 0};
    if (!pass.i) return (DirectiveUnits){0};
    directive_units_flush_pragma(&pass, size);
    if (pass.pragma) *scan->pragma = true;
    scan->mode = pass.mode;
    scan->beginning = pass.beginning;
    scan->star = pass.star;
    scan->number = pass.number;
    scan->quote = (int32_t)pass.quote;
    scan->escape = false;
    directive_scan_units_end(scan, units, size, pass.fresh ? pass.start : UINT32_MAX, pass.i, byte);
    return (DirectiveUnits){pass.i, pass.line_feeds, pass.i - pass.line};
}

// Reads plain code, words and line breaks in one mode, opening another mode only at comments, quotes, raw-string
// candidates and digit separators.
DIRECTIVE_INLINE bool directive_units_code(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        if (pass->beginning && !directive_units_code_beginning(pass, size)) {
            return pass->mode != SCAN_CODE;
        }
        uint32_t c = directive_units_advance_mask(pass, size, UNIT_CODE | UNIT_SPACE);
        if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT) return false;
        if (c == '\n' && directive_units_line_feed(pass, size)) {
            pass->beginning = true;
            continue;
        }
        if (c == '#') {
            pass->i++;
            continue;
        }
        return directive_units_code_token(pass, size, c);
    }
}

// Advances from the beginning of a line to its first non-space unit; returns true once the line holds plain code.
DIRECTIVE_INLINE bool directive_units_code_beginning(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        uint32_t c = 0;
        while (pass->i < pass->count && (c = directive_unit(pass->units, size, pass->i)) < UNIT_CLASS_COUNT &&
            (pass->classes[c] & UNIT_SPACE)) {
            pass->i++;
        }
        if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT) return false;
        uint8_t class = pass->classes[c];
        if (class & UNIT_CODE) {
            pass->beginning = false;
            pass->seen |= class;
            pass->i++;
            return true;
        }
        if (c != '\n' || !directive_units_line_feed(pass, size)) {
            return directive_units_code_token(pass, size, c) && pass->mode == SCAN_CODE;
        }
    }
}

// Reads `c`, which is neither plain code nor a space.
DIRECTIVE_INLINE bool directive_units_code_token(DirectiveUnitPass *pass, unsigned size, uint32_t c) {
    if (c == '"' || c == '\'') {
        if (!directive_units_word_quote(pass, size, c)) {
            pass->quote = c;
            directive_units_open(pass, size, SCAN_QUOTE, 1);
        }
        pass->beginning = false;
        return true;
    }
    if (c == '/') return directive_units_slash(pass, size);
    if (c != '#' || pass->beginning) return false;
    pass->i++;
    pass->floor = pass->i;
    return true;
}

DIRECTIVE_INLINE bool directive_units_slash(DirectiveUnitPass *pass, unsigned size) {
    if (pass->i + 1 >= pass->count) return false;
    uint32_t next = directive_unit(pass->units, size, pass->i + 1);
    if (next == '\\') return false;
    if (next == '/' || next == '*') {
        pass->star = false;
        directive_units_open(pass, size, next == '/' ? SCAN_LINE_COMMENT : SCAN_BLOCK_COMMENT, 2);
        return true;
    }
    pass->beginning = false;
    pass->i++;
    pass->floor = pass->i;
    return true;
}

// Reopens the word before `"` when it may be a raw-string prefix, or the number before `'` so `'` is read as a
// separator instead of a character literal.
DIRECTIVE_INLINE bool directive_units_word_quote(DirectiveUnitPass *pass, unsigned size, uint32_t c) {
    if (pass->i <= pass->floor) return false;
    uint32_t prev = directive_unit(pass->units, size, pass->i - 1);
    if (c == '"' && prev != 'R') return false;
    if (c == '\'' && !(pass->classes[prev] & UNIT_WORD) && prev != '.') return false;
    uint32_t start = directive_units_word_start(pass, size, pass->i);
    if (start >= pass->i) return false;
    uint32_t first = directive_unit(pass->units, size, start);
    bool number = first >= '0' && first <= '9';
    if (c == '\'' && !number) return false;
    if (c == '"' && pass->i - start >= sizeof ((GnuRawStringPrefix *)0)->value) return false;
    pass->i = start;
    pass->number = number;
    directive_units_open(pass, size, SCAN_WORD, 1);
    return true;
}

// Reopens an unfinished word at the stop of `SCAN_CODE` when the next character may continue it.
DIRECTIVE_INLINE void directive_units_finish_code(DirectiveUnitPass *pass, unsigned size) {
    if (pass->mode != SCAN_CODE || pass->i <= pass->floor) return;
    if (pass->i < pass->count) {
        uint32_t next = directive_unit(pass->units, size, pass->i);
        if (next < UNIT_CLASS_COUNT && next != '\\') return;
    }
    uint32_t prev = directive_unit(pass->units, size, pass->i - 1);
    if (!(pass->classes[prev] & UNIT_WORD) && prev != '.' && prev != '+' && prev != '-') return;
    uint32_t start = directive_units_word_start(pass, size, pass->i);
    if (start >= pass->i) return;
    uint32_t first = directive_unit(pass->units, size, start);
    pass->mode = SCAN_WORD;
    pass->start = start;
    pass->fresh = true;
    pass->number = first >= '0' && first <= '9';
}

// Finds the start of the unfinished word or preprocessing number in `[pass->floor .. end]`, or `end` when none is open.
// code-gauge-allow functionCognitiveComplexity -- Backward candidate bound and forward pp-number replay share one helper.
static uint32_t directive_units_word_start(const DirectiveUnitPass *pass, unsigned size, uint32_t end) {
    uint32_t k = end;
    while (k > pass->floor) {
        uint32_t u = directive_unit(pass->units, size, k - 1);
        if ((pass->classes[u] & UNIT_WORD) || u == '.') {
            k--;
            continue;
        }
        if ((u == '+' || u == '-') && k - 1 > pass->floor) {
            uint32_t p = directive_unit(pass->units, size, k - 2);
            if (p == 'e' || p == 'E' || p == 'p' || p == 'P') {
                k -= 2;
                continue;
            }
        }
        break;
    }
    uint32_t word_start = end, prev = 0;
    bool in_word = false, number = false;
    for (uint32_t j = k; j < end; j++) {
        uint32_t u = directive_unit(pass->units, size, j);
        bool word = (pass->classes[u] & UNIT_WORD) != 0;
        bool exponent = number && (u == '+' || u == '-') && (prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P');
        if (!in_word) {
            if (word) {
                in_word = true;
                number = u >= '0' && u <= '9';
                word_start = j;
            }
        } else if (!word && !(number && (u == '.' || exponent))) {
            in_word = false;
        }
        prev = u;
    }
    return in_word ? word_start : end;
}

// code-gauge-allow functionCognitiveComplexity -- A number continues over signs and separators that end any other word.
DIRECTIVE_INLINE bool directive_units_word(DirectiveUnitPass *pass, unsigned size) {
    uint32_t c = 0;
    for (;;) {
        c = directive_units_advance_mask(pass, size, UNIT_WORD);
        if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT || c == '\\') return false;
        if (!pass->number) break;
        int32_t previous = pass->i > (pass->fresh ? pass->start : 0)
            ? (int32_t)directive_unit(pass->units, size, pass->i - 1)
            : pass->previous;
        bool exponent_sign = (c == '+' || c == '-') &&
            (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P');
        if (!(c == '.' || c == '\'' || exponent_sign)) break;
        pass->i++;
    }
    if (c == '"' && (!pass->fresh || pass->i - pass->start < sizeof ((GnuRawStringPrefix *)0)->value)) return false;
    return directive_units_close(pass, size);
}

DIRECTIVE_INLINE bool directive_units_line_comment(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        uint32_t c = directive_units_advance_mask(pass, size, UNIT_LINE);
        if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT) return false;
        if (c == '\\' && directive_units_escape(pass, size)) {
            pass->i++;
        } else if (c == '\n' && !pass->lines) {
            pass->mode = SCAN_CODE;
            pass->floor = pass->i;
            return false;
        } else if (c == '\n' && directive_units_line_feed(pass, size)) {
            pass->beginning = true;
            return directive_units_close(pass, size);
        } else {
            return false;
        }
    }
}

// code-gauge-allow functionCognitiveComplexity -- The comment's end depends on the character before each `/`.
DIRECTIVE_INLINE bool directive_units_block_comment(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        uint32_t before = pass->i;
        uint32_t c = directive_units_advance_mask(pass, size, UNIT_BLOCK);
        if (pass->i > before) pass->star = false;
        if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT) return false;
        bool star = pass->star;
        pass->star = c == '*';
        if (c == '*') {
            pass->i++;
        } else if (c == '/') {
            pass->i++;
            if (star) return directive_units_close(pass, size);
        } else if (c == '\\' && directive_units_escape(pass, size)) {
            pass->i++;
        } else if (c != '\n' || !directive_units_line_feed(pass, size)) {
            pass->star = star;
            return false;
        }
    }
}

DIRECTIVE_INLINE bool directive_units_quoted(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        uint32_t c = directive_units_advance_mask(pass, size, UNIT_QUOTE);
        if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT) return false;
        if (c == pass->quote) {
            pass->i++;
            return directive_units_close(pass, size);
        }
        if (c == '"' || c == '\'') {
            pass->i++;
        } else if (c == '\\' && directive_units_escape(pass, size)) {
            pass->seen |= pass->classes[directive_unit(pass->units, size, pass->i + 1)];
            pass->i += 2;
        } else {
            return false;
        }
    }
}

#ifdef DIRECTIVE_USE_NEON
DIRECTIVE_INLINE void directive_units_neon_advance(DirectiveUnitPass *pass, unsigned size, uint8_t mask) {
    uint8x16_t mask_vec = vdupq_n_u8(mask);
    uint8x16_t hi_xor = vdupq_n_u8(0x40);
    uint8x16_t nl_vec = vdupq_n_u8('\n');
    bool cross_lines = pass->lines && (mask == (UNIT_CODE | UNIT_SPACE) || mask == UNIT_BLOCK);
    uint8_t tail_req = mask == (UNIT_CODE | UNIT_SPACE) ? UNIT_CODE : UNIT_BLOCK;
    uint8x16_t chars;
    while (pass->count - pass->i >= 16 && directive_units_neon_load(pass->units, size, pass->i, &chars)) {
        uint8x16_t cls = vorrq_u8(vqtbl4q_u8(pass->table_lo, chars), vqtbl4q_u8(pass->table_hi, veorq_u8(chars, hi_xor)));
        if (vminvq_u8(vandq_u8(cls, mask_vec)) == 0) {
            if (!cross_lines || !(vgetq_lane_u8(cls, 15) & tail_req)) break;
            uint8x16_t lf = vceqq_u8(chars, nl_vec);
            if (vminvq_u8(vorrq_u8(vandq_u8(cls, mask_vec), lf)) == 0) break;
            pass->line_feeds += vaddvq_u8(vshrq_n_u8(lf, 7));
            uint64_t nibbles = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(lf), 4)), 0);
            pass->line = pass->i + 16u - ((uint32_t)__builtin_clzll(nibbles) >> 2);
            pass->floor = pass->line;
        }
        pass->seen_vec = vorrq_u8(pass->seen_vec, cls);
        pass->i += 16;
    }
}

DIRECTIVE_INLINE bool directive_units_neon_load(const char *units, unsigned size, uint32_t i, uint8x16_t *chars) {
    if (size == 1) {
        *chars = vld1q_u8((const uint8_t *)units + i);
        return vmaxvq_u8(*chars) < 0x80;
    }
    const uint8_t *ptr = (const uint8_t *)units + 2 * (size_t)i;
    uint16x8_t lo = vreinterpretq_u16_u8(vld1q_u8(ptr));
    uint16x8_t hi = vreinterpretq_u16_u8(vld1q_u8(ptr + 16));
    if (vmaxvq_u16(vorrq_u16(lo, hi)) >= 0x80) return false;
    *chars = vcombine_u8(vmovn_u16(lo), vmovn_u16(hi));
    return true;
}
#endif

DIRECTIVE_INLINE uint32_t directive_units_advance_mask(DirectiveUnitPass *pass, unsigned size, uint8_t mask) {
#ifdef DIRECTIVE_USE_NEON
    if (pass->count - pass->i >= 16) directive_units_neon_advance(pass, size, mask);
#endif
    uint32_t c = 0;
    uint8_t cls = 0;
    while (pass->i < pass->count && (c = directive_unit(pass->units, size, pass->i)) < UNIT_CLASS_COUNT &&
        ((cls = pass->classes[c]) & mask)) {
        pass->seen |= cls;
        pass->i++;
    }
    return c;
}

// Starts a word, comment or literal whose first `length` units are read with this.
DIRECTIVE_INLINE void directive_units_open(DirectiveUnitPass *pass, unsigned size, DirectiveScanMode mode, uint32_t length) {
    directive_units_flush_pragma(pass, size);
    pass->mode = mode;
    pass->start = pass->i;
    pass->fresh = true;
    pass->i += length;
}

// Ends the word, comment or literal being read; code follows it.
DIRECTIVE_INLINE bool directive_units_close(DirectiveUnitPass *pass, unsigned size) {
    directive_units_flush_pragma(pass, size);
    pass->mode = SCAN_CODE;
    pass->floor = pass->i;
    return true;
}

// Reads the line feed at `pass->i` unless the pass stops at line feeds, or a carriage return may belong to it.
DIRECTIVE_INLINE bool directive_units_line_feed(DirectiveUnitPass *pass, unsigned size) {
    if (!pass->lines || pass->i + 1 >= pass->count || directive_unit(pass->units, size, pass->i + 1) == '\r') return false;
    directive_units_flush_pragma(pass, size);
    pass->line_feeds++;
    pass->line = ++pass->i;
    pass->floor = pass->i;
    pass->pragma_from = pass->i;
    return true;
}

// Whether the backslash at `pass->i` starts no line splice and the unit after it is a plain character.
DIRECTIVE_INLINE bool directive_units_escape(const DirectiveUnitPass *pass, unsigned size) {
    if (pass->i + 1 >= pass->count) return false;
    uint32_t next = directive_unit(pass->units, size, pass->i + 1);
    return next < UNIT_CLASS_COUNT && (pass->classes[next] & (UNIT_LINE | UNIT_SPACE)) == UNIT_LINE;
}

DIRECTIVE_INLINE void directive_units_flush_pragma(DirectiveUnitPass *pass, unsigned size) {
#ifdef DIRECTIVE_USE_NEON
    if (vmaxvq_u8(vandq_u8(pass->seen_vec, vdupq_n_u8(UNIT_UNDERSCORE)))) pass->seen |= UNIT_UNDERSCORE;
    if (vmaxvq_u8(vandq_u8(pass->seen_vec, vdupq_n_u8(UNIT_P)))) pass->seen |= UNIT_P;
    pass->seen_vec = vdupq_n_u8(0);
#endif
    if ((pass->seen & UNIT_PRAGMA) == UNIT_PRAGMA &&
        directive_units_pragma(pass->units, size, pass->pragma_from, pass->i)) {
        pass->pragma = true;
    }
    pass->seen = 0;
    pass->pragma_from = pass->i;
}

// The unit at `index` of units of `size` bytes in the host's byte order.
DIRECTIVE_INLINE uint32_t directive_unit(const char *units, unsigned size, uint32_t index) {
    if (size == 1) return (uint8_t)units[index];
    uint16_t unit;
    memcpy(&unit, units + 2 * (size_t)index, sizeof unit);
    return unit;
}

// Leaves in `scan` what the character path keeps of the `end` units read: the last characters of the unfinished word,
// comment or literal, which starts at `from` or, when that is `UINT32_MAX`, before the units, and the run of spaces
// and backslashes that the units end with.
// code-gauge-allow functionCognitiveComplexity -- The characters kept and the run of spaces each depend on where their text starts among the units.
static void directive_scan_units_end(DirectiveScan *scan, const char *units, unsigned size, uint32_t from, uint32_t end, uint32_t byte) {
    if (scan->mode == SCAN_CODE) {
        scan->recent = 0;
    } else {
        uint64_t recent = from == UINT32_MAX ? scan->recent : 0;
        uint32_t first = from == UINT32_MAX ? 0 : from;
        for (uint32_t k = end - first > 8 ? end - 8 : first; k < end; k++) recent = recent << 8 | directive_unit(units, size, k);
        scan->recent = recent;
        if (from != UINT32_MAX && scan->mode == SCAN_WORD) scan->raw = (GnuRawStringPrefix){0};
        if (scan->mode == SCAN_WORD && end > first) directive_scan_units_word(scan, units, size, first, end);
    }
    uint32_t run = end;
    while (run > 0) {
        uint32_t c = directive_unit(units, size, run - 1);
        if (!directive_horizontal((int32_t)c) && c != '\n' && c != '\\') break;
        run--;
    }
    bool continued = run == 0 && scan->in_run;
    scan->in_run = run < end;
    if (!scan->in_run) return;
    if (!continued) {
        scan->run_spans = 0;
        scan->run_start = byte + run * size;
    }
    scan->run_end = byte + (end - 1) * size + 1;
}

// Adds the word's characters from `first` to before `end`, as `directive_scan_word_characters` does.
static void directive_scan_units_word(DirectiveScan *scan, const char *units, unsigned size, uint32_t first, uint32_t end) {
    scan->previous = (int32_t)directive_unit(units, size, end - 1);
    if (scan->raw.length >= sizeof scan->raw.value) return;
    if (end - first > sizeof scan->raw.value - 1 - scan->raw.length) {
        gnu_raw_string_prefix_invalidate(&scan->raw);
        return;
    }
    for (uint32_t k = first; k < end; k++) gnu_raw_string_prefix_character(&scan->raw, (int32_t)directive_unit(units, size, k));
}

// Whether the ASCII units from `start` to before `end` hold `pragma_word`.
static bool directive_units_pragma(const char *units, unsigned size, uint32_t start, uint32_t end) {
    uint32_t length = sizeof pragma_word - 1;
    for (uint32_t i = start; i + length <= end; i++) {
        if (directive_unit(units, size, i) != '_' || directive_unit(units, size, i + 1) != 'P') continue;
        uint32_t matched = 2;
        while (matched < length && directive_unit(units, size, i + matched) == (uint8_t)pragma_word[matched]) matched++;
        if (matched == length) return true;
    }
    return false;
}

// Whether the characters in `recent` end with a start of `pragma_word` that the next ones may complete.
static bool directive_pragma_pending(uint64_t recent) {
    uint64_t word = 0;
    for (unsigned length = 1; length < sizeof pragma_word - 1; length++) {
        word = word << 8 | (uint8_t)pragma_word[length - 1];
        if ((recent & ((1ull << 8 * length) - 1)) == word) return true;
    }
    return false;
}

// code-gauge-allow functionCognitiveComplexity -- Each class is one condition on the character.
static void directive_unit_classes(uint8_t *classes) {
    for (int32_t c = 0; c < UNIT_CLASS_COUNT; c++) {
        bool text = c != 0 && !directive_newline(c) && c != '\\';
        bool word = directive_scan_ascii_word(c);
        bool space = directive_horizontal(c);
        uint8_t code = (word ? UNIT_WORD | UNIT_CODE : 0) | (space ? UNIT_SPACE : directive_scan_ascii_plain(c) ? UNIT_CODE : 0);
        uint8_t letter = c == '_' ? UNIT_UNDERSCORE : c == 'P' ? UNIT_P : 0;
        uint8_t block = text && c != '*' && c != '/' ? UNIT_BLOCK : 0, quote = text && c != '"' && c != '\'' ? UNIT_QUOTE : 0;
        classes[c] = (uint8_t)(code | letter | (text ? UNIT_LINE : 0) | block | quote);
    }
}

#endif
