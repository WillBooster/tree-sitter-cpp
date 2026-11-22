// The line-state pass of `directiveScan.h` over code units read from memory instead of characters fed one at a time.
// Included by `directiveScan.h`, whose `DirectiveScan` it reads and leaves as the character path would.
#ifndef TREE_SITTER_C_DIRECTIVE_UNITS_H_
#define TREE_SITTER_C_DIRECTIVE_UNITS_H_

// The classes of an ASCII character among the units of the input: the kind of code it is, whether it is `_` or `P`,
// without both of which no text holds `pragma_word`, and whether it is plain text of a line comment, of a block comment
// and of a literal.
enum { UNIT_WORD = 1, UNIT_SPACE = 2, UNIT_PUNCTUATION = 4, UNIT_UNDERSCORE = 8, UNIT_P = 16, UNIT_LINE = 32, UNIT_BLOCK = 64, UNIT_QUOTE = 128 };
#define UNIT_PRAGMA (UNIT_UNDERSCORE | UNIT_P)
#define UNIT_CLASS_COUNT 128

typedef struct {
    uint32_t consumed, line_feeds, tail;
} DirectiveUnits;

// The pass's state over `count` units of which `i` are read. The word, comment or literal being read starts at `start`
// when `fresh`, and before the units otherwise; `seen` holds the classes of its characters.
typedef struct {
    const char *units;
    const uint8_t *classes;
    uint32_t count, i, start, line_feeds, line, quote;
    int32_t previous;
    DirectiveScanMode mode;
    bool lines, beginning, star, number, pragma, fresh;
    uint8_t seen;
} DirectiveUnitPass;

#if defined(__GNUC__) || defined(__clang__)
#define DIRECTIVE_INLINE __attribute__((always_inline)) static inline
#else
#define DIRECTIVE_INLINE static inline
#endif

DIRECTIVE_INLINE bool directive_units_code(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_code_token(DirectiveUnitPass *pass, unsigned size, uint32_t c);
DIRECTIVE_INLINE bool directive_units_word(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_line_comment(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_block_comment(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_quoted(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE void directive_units_open(DirectiveUnitPass *pass, DirectiveScanMode mode, uint32_t length);
DIRECTIVE_INLINE bool directive_units_close(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_line_feed(DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE bool directive_units_escape(const DirectiveUnitPass *pass, unsigned size);
DIRECTIVE_INLINE void directive_units_note_pragma(DirectiveUnitPass *pass, unsigned size);
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
    if (!lines) return (DirectiveUnits){pass.mode == SCAN_CODE ? pass.i : pass.start, 0, 0};
    if (!pass.i) return (DirectiveUnits){0};
    if (pass.mode != SCAN_CODE) directive_units_note_pragma(&pass, size);
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

// Each of these reads the units of one mode and returns true when those of `pass->mode` follow.

DIRECTIVE_INLINE bool directive_units_code(DirectiveUnitPass *pass, unsigned size) {
    uint32_t c = 0;
    uint8_t class = 0, plain = 0;
    while (pass->i < pass->count && (c = directive_unit(pass->units, size, pass->i)) < UNIT_CLASS_COUNT &&
        ((class = pass->classes[c]) & (UNIT_SPACE | UNIT_PUNCTUATION))) {
        plain |= class;
        pass->i++;
    }
    if (plain & UNIT_PUNCTUATION) pass->beginning = false;
    if (pass->i >= pass->count || c >= UNIT_CLASS_COUNT) return false;
    if (!(class & UNIT_WORD)) return directive_units_code_token(pass, size, c);
    pass->beginning = false;
    pass->number = c >= '0' && c <= '9';
    directive_units_open(pass, SCAN_WORD, 1);
    pass->seen = class;
    return true;
}

// Reads `c`, which is neither plain code nor part of a word.
// code-gauge-allow functionCognitiveComplexity -- Each character that opens a line, a literal or a comment changes the mode in its own way.
DIRECTIVE_INLINE bool directive_units_code_token(DirectiveUnitPass *pass, unsigned size, uint32_t c) {
    if (c == '\n') {
        if (!directive_units_line_feed(pass, size)) return false;
        pass->beginning = true;
        return true;
    }
    if (c == '"' || c == '\'') {
        pass->quote = c;
        directive_units_open(pass, SCAN_QUOTE, 1);
    } else if (c == '/') {
        if (pass->i + 1 >= pass->count) return false;
        uint32_t next = directive_unit(pass->units, size, pass->i + 1);
        // A line splice may separate the `/` from what makes it a comment.
        if (next == '\\') return false;
        if (next == '/' || next == '*') {
            pass->star = false;
            directive_units_open(pass, next == '/' ? SCAN_LINE_COMMENT : SCAN_BLOCK_COMMENT, 2);
            return true;
        }
        pass->i++;
    } else if (c == '#' && !pass->beginning) {
        pass->i++;
    } else {
        return false;
    }
    pass->beginning = false;
    return true;
}

// code-gauge-allow functionCognitiveComplexity -- A number continues over signs and separators that end any other word.
DIRECTIVE_INLINE bool directive_units_word(DirectiveUnitPass *pass, unsigned size) {
    uint32_t c = 0;
    uint8_t class;
    for (;;) {
        while (pass->i < pass->count && (c = directive_unit(pass->units, size, pass->i)) < UNIT_CLASS_COUNT &&
            ((class = pass->classes[c]) & UNIT_WORD)) {
            pass->seen |= class;
            pass->i++;
        }
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
    // The word may be a raw string prefix, which no word of more characters is.
    if (c == '"' && (!pass->fresh || pass->i - pass->start < sizeof ((GnuRawStringPrefix *)0)->value)) return false;
    return directive_units_close(pass, size);
}

// code-gauge-allow duplicateLines -- The loops over a comment's and a literal's text share their shape, and each ends its text differently.
DIRECTIVE_INLINE bool directive_units_line_comment(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        if (pass->i >= pass->count) return false;
        uint32_t c = directive_unit(pass->units, size, pass->i);
        if (c >= UNIT_CLASS_COUNT) return false;
        uint8_t class = pass->classes[c];
        if (class & UNIT_LINE) {
            pass->seen |= class;
            pass->i++;
        } else if (c == '\\' && directive_units_escape(pass, size)) {
            pass->i++;
        } else if (c == '\n' && !pass->lines) {
            // The comment is read, and the line feed ends the line for the caller.
            pass->mode = SCAN_CODE;
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
// code-gauge-allow duplicateLines -- The loops over a comment's and a literal's text share their shape, and each ends its text differently.
DIRECTIVE_INLINE bool directive_units_block_comment(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        if (pass->i >= pass->count) return false;
        uint32_t c = directive_unit(pass->units, size, pass->i);
        if (c >= UNIT_CLASS_COUNT) return false;
        uint8_t class = pass->classes[c];
        bool star = pass->star;
        pass->star = c == '*';
        if (class & UNIT_BLOCK) {
            pass->seen |= class;
            pass->i++;
        } else if (c == '*') {
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

// code-gauge-allow duplicateLines -- The loops over a comment's and a literal's text share their shape, and each ends its text differently.
DIRECTIVE_INLINE bool directive_units_quoted(DirectiveUnitPass *pass, unsigned size) {
    for (;;) {
        if (pass->i >= pass->count) return false;
        uint32_t c = directive_unit(pass->units, size, pass->i);
        if (c >= UNIT_CLASS_COUNT) return false;
        uint8_t class = pass->classes[c];
        if (class & UNIT_QUOTE) {
            pass->seen |= class;
            pass->i++;
        } else if (c == pass->quote) {
            pass->i++;
            return directive_units_close(pass, size);
        } else if (c == '"' || c == '\'') {
            pass->i++;
        } else if (c == '\\' && directive_units_escape(pass, size)) {
            pass->seen |= pass->classes[directive_unit(pass->units, size, pass->i + 1)];
            pass->i += 2;
        } else {
            return false;
        }
    }
}

// Starts a word, comment or literal whose first `length` units are read with this.
DIRECTIVE_INLINE void directive_units_open(DirectiveUnitPass *pass, DirectiveScanMode mode, uint32_t length) {
    pass->mode = mode;
    pass->start = pass->i;
    pass->fresh = true;
    pass->seen = 0;
    pass->i += length;
}

// Ends the word, comment or literal being read; code follows it.
DIRECTIVE_INLINE bool directive_units_close(DirectiveUnitPass *pass, unsigned size) {
    directive_units_note_pragma(pass, size);
    pass->mode = SCAN_CODE;
    return true;
}

// Reads the line feed at `pass->i` unless the pass stops at line feeds, or a carriage return may belong to it.
DIRECTIVE_INLINE bool directive_units_line_feed(DirectiveUnitPass *pass, unsigned size) {
    if (!pass->lines || pass->i + 1 >= pass->count || directive_unit(pass->units, size, pass->i + 1) == '\r') return false;
    pass->line_feeds++;
    pass->line = ++pass->i;
    return true;
}

// Whether the backslash at `pass->i` starts no line splice and the unit after it is a plain character.
DIRECTIVE_INLINE bool directive_units_escape(const DirectiveUnitPass *pass, unsigned size) {
    if (pass->i + 1 >= pass->count) return false;
    uint32_t next = directive_unit(pass->units, size, pass->i + 1);
    return next < UNIT_CLASS_COUNT && (pass->classes[next] & (UNIT_LINE | UNIT_SPACE)) == UNIT_LINE;
}

DIRECTIVE_INLINE void directive_units_note_pragma(DirectiveUnitPass *pass, unsigned size) {
    if ((pass->seen & UNIT_PRAGMA) != UNIT_PRAGMA) return;
    if (directive_units_pragma(pass->units, size, pass->fresh ? pass->start : 0, pass->i)) pass->pragma = true;
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
        // A comment's or a literal's opener is no part of `pragma_word`, so what was read before it does not matter.
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
        uint32_t matched = 0;
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
        uint8_t code = directive_scan_ascii_word(c) ? UNIT_WORD : directive_horizontal(c) ? UNIT_SPACE : directive_scan_ascii_plain(c) ? UNIT_PUNCTUATION : 0;
        uint8_t letter = c == '_' ? UNIT_UNDERSCORE : c == 'P' ? UNIT_P : 0;
        uint8_t block = text && c != '*' && c != '/' ? UNIT_BLOCK : 0, quote = text && c != '"' && c != '\'' ? UNIT_QUOTE : 0;
        classes[c] = (uint8_t)(code | letter | (text ? UNIT_LINE : 0) | block | quote);
    }
}

#endif
