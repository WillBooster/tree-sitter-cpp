// The characters of a directive segment, read from the lexer as the line-state pass of `emptyDirectives.h` needs them.
#ifndef TREE_SITTER_C_DIRECTIVE_BUFFER_H_
#define TREE_SITTER_C_DIRECTIVE_BUFFER_H_

#include "directiveScan.h"
#include <stdlib.h>

#define DIRECTIVE_PAGE_SHIFT 12
#define DIRECTIVE_PAGE_SIZE (1 << DIRECTIVE_PAGE_SHIFT)
#define DIRECTIVE_SEGMENT_UNITS (DIRECTIVE_PAGE_SIZE * 16)
#define DIRECTIVE_FILL_UNITS 256

typedef struct {
    uint32_t index, byte, width;
} DirectivePosition;

// The characters of one directive segment, read from the lexer as the line-state pass needs them.
typedef struct {
    uint16_t **characters;
    DirectivePosition *positions;
    TSLexer *lexer;
    uint32_t length, page_count, page_capacity, position_count, position_capacity;
    // The lexer's byte position, which follows the last stored character.
    uint32_t fill_byte;
    uint32_t conditional_depth;
    // The characters stored last, as `DirectiveScan` keeps those it reads.
    uint64_t recent;
    // Whether the current segment recorded a null directive or a line-end barrier, which the reverse pass carries, and
    // whether the characters stored or scanned hold `pragma_word`.
    bool utf16, encoding_known, failed, carries, pragma, spaced_splices;
    uint8_t classes[UNIT_CLASS_COUNT];
} DirectiveBuffer;

// A position in a `DirectiveBuffer`; copies share the buffer, so a lookahead copy reads what the original will.
typedef struct {
    DirectiveBuffer *buffer;
    uint32_t index, byte, position_index, previous_byte, before_previous_byte;
    // Whether a line splice with spaces before its newline was read, which the lexer's tokens do not take as a splice.
    bool spaced_splice;
} DirectiveInput;

typedef enum { DIRECTIVE_RUN_WORD, DIRECTIVE_RUN_SPACE, DIRECTIVE_RUN_LINE_COMMENT, DIRECTIVE_RUN_BLOCK_COMMENT } DirectiveRun;

typedef struct {
    uint32_t value;
    unsigned digits, limit;
    bool named, braced;
} DirectiveUcn;

static void directive_shift(DirectiveBuffer *buffer, uint32_t start);
static bool directive_step_back(const DirectiveInput *input, uint32_t *index, uint32_t *position, uint32_t *byte, int32_t *c, uint32_t *width);
static int32_t directive_next(DirectiveInput *input);
static int32_t directive_physical_next(DirectiveInput *input);
static uint32_t directive_expected_width(const DirectiveInput *input, uint32_t index, int32_t *c);
static bool directive_skip_splice(DirectiveInput *input);
static uint32_t directive_splice_end(const DirectiveInput *input);
static int32_t directive_peek(const DirectiveInput *input);
static inline uint32_t directive_plain_run(DirectiveInput *input, DirectiveRun run);
static void directive_advance_plain(DirectiveInput *input, uint32_t length);
static uint32_t directive_run_end(const DirectiveBuffer *buffer, DirectiveRun run, uint32_t index, uint32_t end);
static bool directive_run_continues(DirectiveRun run, uint16_t c);
static void directive_skip_plain(DirectiveInput *input);
static int32_t directive_plain(const DirectiveInput *input);
static uint32_t directive_byte(const DirectiveInput *input);
static inline bool directive_has(const DirectiveInput *input, uint32_t index);
static bool directive_fill(DirectiveBuffer *buffer, uint32_t index);
static uint32_t directive_fill_units(DirectiveBuffer *buffer, uint32_t limit);
static bool directive_append(DirectiveBuffer *buffer);
static bool directive_position(DirectiveBuffer *buffer, uint32_t index, uint32_t byte, uint32_t width);
static bool directive_append_ascii(DirectiveBuffer *buffer, uint16_t c);
static inline void directive_note(DirectiveBuffer *buffer, uint32_t c);
static bool directive_reserve(DirectiveBuffer *buffer, unsigned width);
static void directive_encode(DirectiveBuffer *buffer, uint32_t c);
static uint16_t directive_character(const DirectiveInput *input, uint32_t index);
static void directive_store(DirectiveBuffer *buffer, uint16_t c);
static void directive_release(DirectiveBuffer *buffer);
static unsigned directive_utf8_width(uint32_t c);
static bool directive_ucn_start(DirectiveUcn *ucn, int32_t prefix, bool braced);
static bool directive_ucn_feed(DirectiveUcn *ucn, int32_t c, bool continuation, bool *done);
static int directive_hex_digit(int32_t c);
static bool directive_valid_ucn_scalar(uint32_t value, bool continuation);

// Drops the characters before `start`, which `DirectiveScan` has consumed.
static void directive_shift(DirectiveBuffer *buffer, uint32_t start) {
    if (!start) return;
    uint32_t length = buffer->length - start;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t from = start + i;
        buffer->characters[i >> DIRECTIVE_PAGE_SHIFT][i & (DIRECTIVE_PAGE_SIZE - 1)] =
            buffer->characters[from >> DIRECTIVE_PAGE_SHIFT][from & (DIRECTIVE_PAGE_SIZE - 1)];
    }
    buffer->length = length;
    uint32_t count = 0;
    for (uint32_t i = 0; i < buffer->position_count; i++) {
        DirectivePosition position = buffer->positions[i];
        if (position.index < start) continue;
        position.index -= start;
        buffer->positions[count++] = position;
    }
    buffer->position_count = count;
}

// Moves back to the character before `*index`, reading a surrogate pair as one character, and decodes it into `*c` and
// its byte width into `*width`; false when its width exceeds the bytes left.
static bool directive_step_back(const DirectiveInput *input, uint32_t *index, uint32_t *position, uint32_t *byte, int32_t *c, uint32_t *width) {
    const DirectiveBuffer *buffer = input->buffer;
    uint32_t i = --*index;
    if (i && directive_character(input, i) >= 0xdc00 && directive_character(input, i) <= 0xdfff &&
        directive_character(input, i - 1) >= 0xd800 && directive_character(input, i - 1) <= 0xdbff) i = --*index;
    *width = directive_expected_width(input, i, c);
    if (*position && buffer->positions[*position - 1].index == i) {
        DirectivePosition recorded = buffer->positions[--*position];
        *byte = recorded.byte;
        *width = recorded.width;
        return true;
    }
    if (*width > *byte) return false;
    *byte -= *width;
    return true;
}

static int32_t directive_next(DirectiveInput *input) {
    int32_t plain = directive_plain(input);
    if (plain >= 0) {
        directive_skip_plain(input);
        return plain;
    }
    while (directive_has(input, input->index)) {
        int32_t c = directive_physical_next(input);
        if (c == '\\' && directive_skip_splice(input)) continue;
        if (directive_newline(c)) {
            if (directive_has(input, input->index) && directive_newline(directive_character(input, input->index)) &&
                directive_character(input, input->index) != c) directive_physical_next(input);
            return '\n';
        }
        return c;
    }
    return -1;
}

static int32_t directive_physical_next(DirectiveInput *input) {
    const DirectiveBuffer *buffer = input->buffer;
    uint32_t index = input->index;
    int32_t c;
    uint32_t width = directive_expected_width(input, index, &c);
    input->index += c > 0xffff ? 2 : 1;
    input->before_previous_byte = input->previous_byte;
    input->previous_byte = input->byte;
    if (input->position_index < buffer->position_count && buffer->positions[input->position_index].index == index) {
        DirectivePosition position = buffer->positions[input->position_index++];
        input->previous_byte = position.byte;
        input->byte = position.byte + position.width;
    } else input->byte += width;
    return c;
}

// Decodes the character at `index` into `*c` and returns its byte width when it has no recorded position.
static uint32_t directive_expected_width(const DirectiveInput *input, uint32_t index, int32_t *c) {
    uint32_t unit = directive_character(input, index);
    if (unit >= 0xd800 && unit <= 0xdbff && directive_has(input, index + 1)) {
        uint32_t low = directive_character(input, index + 1);
        if (low >= 0xdc00 && low <= 0xdfff) {
            *c = (int32_t)(0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
            return 4;
        }
    }
    *c = (int32_t)unit;
    return input->buffer->utf16 ? 2 : directive_utf8_width(unit);
}

// Consumes the rest of a line splice after its `\\`; false when none follows.
static bool directive_skip_splice(DirectiveInput *input) {
    uint32_t after = directive_splice_end(input);
    if (after == input->index) return false;
    if (directive_horizontal(directive_character(input, input->index))) input->spaced_splice = true;
    while (input->index < after) directive_physical_next(input);
    return true;
}

static uint32_t directive_splice_end(const DirectiveInput *input) {
    uint32_t after = input->index;
    while (directive_has(input, after) && directive_horizontal(directive_character(input, after))) after++;
    if (!directive_has(input, after) || !directive_newline(directive_character(input, after))) return input->index;
    int32_t newline = directive_character(input, after++);
    if (directive_has(input, after) && directive_newline(directive_character(input, after)) &&
        directive_character(input, after) != newline) after++;
    return after;
}

static int32_t directive_peek(const DirectiveInput *input) {
    int32_t plain = directive_plain(input);
    if (plain >= 0) return plain;
    DirectiveInput copy = *input;
    return directive_next(&copy);
}

// Consumes the longest run of plain characters that `run` accepts and returns its length.
static inline uint32_t directive_plain_run(DirectiveInput *input, DirectiveRun run) {
    const DirectiveBuffer *buffer = input->buffer;
    uint32_t index = input->index;
    // Scans the stored characters before the next recorded position and reads more only at their end, because reading
    // may store a character with a recorded position, which ends the run.
    while (directive_has(input, index)) {
        uint32_t end = buffer->length;
        if (input->position_index < buffer->position_count && buffer->positions[input->position_index].index < end) {
            end = buffer->positions[input->position_index].index;
        }
        uint32_t start = index;
        index = directive_run_end(buffer, run, index, end);
        if (index < end || index == start || end < buffer->length) break;
    }
    uint32_t length = index - input->index;
    if (length) directive_advance_plain(input, length);
    return length;
}

// Moves `input` over `length` plain characters, at least one.
static void directive_advance_plain(DirectiveInput *input, uint32_t length) {
    uint32_t width = input->buffer->utf16 ? 2 : 1;
    input->before_previous_byte = length > 1 ? input->byte + (length - 2) * width : input->previous_byte;
    input->previous_byte = input->byte + (length - 1) * width;
    input->byte += length * width;
    input->index += length;
}

// The index of the first stored character from `index` to `end` that ends the run, or `end`.
static uint32_t directive_run_end(const DirectiveBuffer *buffer, DirectiveRun run, uint32_t index, uint32_t end) {
    while (index < end) {
        const uint16_t *page = buffer->characters[index >> DIRECTIVE_PAGE_SHIFT];
        uint32_t page_end = (index | (DIRECTIVE_PAGE_SIZE - 1)) + 1;
        if (page_end > end) page_end = end;
        for (; index < page_end; index++) {
            uint16_t c = page[index & (DIRECTIVE_PAGE_SIZE - 1)];
            if (c >= 0x80 || c == '\\' || directive_newline(c) || !directive_run_continues(run, c)) return index;
        }
    }
    return end;
}

// `c` is ASCII.
static bool directive_run_continues(DirectiveRun run, uint16_t c) {
    bool identifier = (preproc_identifier_continue_ascii[c / 64] >> (c % 64)) & 1;
    switch (run) {
        case DIRECTIVE_RUN_WORD:
            return identifier;
        case DIRECTIVE_RUN_SPACE:
            return directive_horizontal(c);
        case DIRECTIVE_RUN_LINE_COMMENT:
            return true;
        case DIRECTIVE_RUN_BLOCK_COMMENT:
            return c != '*';
    }
    return false;
}

static void directive_skip_plain(DirectiveInput *input) {
    input->index++;
    input->before_previous_byte = input->previous_byte;
    input->previous_byte = input->byte;
    input->byte += input->buffer->utf16 ? 2 : 1;
}

// The next character when it is ASCII, cannot start a line splice or a newline, and has its expected width; otherwise -1.
static int32_t directive_plain(const DirectiveInput *input) {
    if (!directive_has(input, input->index)) return -1;
    uint16_t c = directive_character(input, input->index);
    if (c >= 0x80 || c == '\\' || directive_newline(c)) return -1;
    const DirectiveBuffer *buffer = input->buffer;
    bool positioned = input->position_index < buffer->position_count &&
        buffer->positions[input->position_index].index == input->index;
    return positioned ? -1 : c;
}

static uint32_t directive_byte(const DirectiveInput *input) {
    return input->byte;
}

// Reads characters from the lexer until `index` is stored; false at the end of the input.
static inline bool directive_has(const DirectiveInput *input, uint32_t index) {
    return index < input->buffer->length || directive_fill(input->buffer, index);
}

// Reads ahead to the end of the line or `DIRECTIVE_FILL_UNITS` characters past `index`, since the pass reads each
// stored character several times and the characters it leaves behind go back to `DirectiveScan`.
static bool directive_fill(DirectiveBuffer *buffer, uint32_t index) {
    uint32_t limit = index + DIRECTIVE_FILL_UNITS;
    while (buffer->length < limit && !buffer->failed && !pragma_eof(buffer->lexer)) {
        int32_t c = buffer->lexer->lookahead;
        if (directive_fill_units(buffer, limit)) c = directive_character(&(DirectiveInput){.buffer = buffer}, buffer->length - 1);
        else if (!directive_append(buffer)) buffer->failed = true;
        if (!buffer->failed && directive_newline(c) && buffer->length > index) break;
    }
    return index < buffer->length;
}

// Stores the ASCII characters that the lexer hands out as units, up to the first newline, the end of the page or
// `limit` characters, as `directive_append` stores them one at a time, and returns how many. It stores none where a
// character may take another width than its encoding's (before the encoding is known, and at the start of an included
// range, whose first character follows bytes that are left out) or may complete a `pragma_word` stored before.
// code-gauge-allow functionCognitiveComplexity -- Each bound on the characters stored at once is a condition of its own.
// code-gauge-allow functionNcss -- Each bound on the characters stored at once is a condition of its own.
static uint32_t directive_fill_units(DirectiveBuffer *buffer, uint32_t limit) {
    TSLexer *lexer = buffer->lexer;
    uint32_t index = buffer->length, count, size;
    if (!buffer->encoding_known || (index >> DIRECTIVE_PAGE_SHIFT) >= buffer->page_count || directive_pragma_pending(buffer->recent)) return 0;
    const char *units = lexer->peek_units(lexer, &count, &size);
    if (!count || size != (buffer->utf16 ? 2u : 1u) || lexer->is_at_included_range_start(lexer)) return 0;
    uint32_t room = DIRECTIVE_PAGE_SIZE - (index & (DIRECTIVE_PAGE_SIZE - 1));
    if (count > room) count = room;
    if (count > limit - index) count = limit - index;
    uint16_t *page = buffer->characters[index >> DIRECTIVE_PAGE_SHIFT] + (index & (DIRECTIVE_PAGE_SIZE - 1));
    uint32_t length = 0, c = 0;
    uint8_t seen = 0;
    while (length < count && (c = directive_unit(units, size, length)) < UNIT_CLASS_COUNT) {
        page[length++] = (uint16_t)c;
        seen |= buffer->classes[c];
        if (directive_newline((int32_t)c)) break;
    }
    if (!length) return 0;
    if ((seen & UNIT_PRAGMA) == UNIT_PRAGMA && directive_units_pragma(units, size, 0, length)) buffer->pragma = true;
    for (uint32_t i = length > 8 ? length - 8 : 0; i < length; i++) buffer->recent = buffer->recent << 8 | page[i];
    buffer->length = index + length;
    buffer->fill_byte += length * size;
    bool line_feed = page[length - 1] == '\n';
    lexer->advance_units(lexer, length, line_feed, line_feed ? 0 : length);
    return length;
}

static bool directive_append(DirectiveBuffer *buffer) {
    TSLexer *lexer = buffer->lexer;
    uint32_t c = lexer->lookahead, index = buffer->length, start = buffer->fill_byte;
    if (c < 0x80 && buffer->encoding_known && (index >> DIRECTIVE_PAGE_SHIFT) < buffer->page_count) {
        return directive_append_ascii(buffer, (uint16_t)c);
    }
    unsigned width = directive_utf8_width(c), utf16_width = c > 0xffff && c <= 0x10ffff ? 4 : 2;
    if (!directive_reserve(buffer, c > 0xffff && c <= 0x10ffff ? 2 : 1)) return false;
    directive_encode(buffer, c);
    directive_note(buffer, c);
    TSLexerPosition end;
    lexer->advance(lexer, false);
    lexer->get_position(lexer, &end);
    buffer->fill_byte = end.bytes;
    uint32_t measured = end.bytes - start;
    // A character stored before the encoding is known keeps its measured width, which a later choice cannot change.
    bool known = buffer->encoding_known;
    if (!known && c <= 0x10ffff && width != utf16_width && !lexer->is_at_included_range_start(lexer)) {
        buffer->utf16 = measured == utf16_width;
        buffer->encoding_known = true;
    }
    if (!known || c > 0x10ffff || measured != (buffer->utf16 ? utf16_width : width)) {
        return directive_position(buffer, index, start, measured);
    }
    return true;
}

static bool directive_position(DirectiveBuffer *buffer, uint32_t index, uint32_t byte, uint32_t width) {
    if (buffer->position_count == buffer->position_capacity) {
        uint32_t capacity = buffer->position_capacity ? buffer->position_capacity * 2 : 16;
        if (capacity < buffer->position_capacity || (size_t)capacity > SIZE_MAX / sizeof(*buffer->positions)) return false;
        void *positions = realloc(buffer->positions, (size_t)capacity * sizeof(*buffer->positions));
        if (!positions) return false;
        buffer->positions = positions;
        buffer->position_capacity = capacity;
    }
    buffer->positions[buffer->position_count++] = (DirectivePosition){index, byte, width};
    return true;
}

// An ASCII character on a stored page, once the encoding is known, takes the expected width unless the lexer skipped to
// another included range after it.
static bool directive_append_ascii(DirectiveBuffer *buffer, uint16_t c) {
    TSLexer *lexer = buffer->lexer;
    uint32_t index = buffer->length, start = buffer->fill_byte;
    buffer->characters[index >> DIRECTIVE_PAGE_SHIFT][index & (DIRECTIVE_PAGE_SIZE - 1)] = c;
    buffer->length = index + 1;
    directive_note(buffer, c);
    TSLexerPosition end;
    lexer->advance(lexer, false);
    lexer->get_position(lexer, &end);
    buffer->fill_byte = end.bytes;
    return end.bytes - start == (buffer->utf16 ? 2u : 1u) || directive_position(buffer, index, start, end.bytes - start);
}

static inline void directive_note(DirectiveBuffer *buffer, uint32_t c) {
    buffer->recent = buffer->recent << 8 | (c < 0x80 ? c : 0);
    if (directive_pragma_word(buffer->recent)) buffer->pragma = true;
}

static bool directive_reserve(DirectiveBuffer *buffer, unsigned width) {
    if (buffer->length > UINT32_MAX - width) return false;
    uint32_t last = (buffer->length + width - 1) >> DIRECTIVE_PAGE_SHIFT;
    if (last >= buffer->page_capacity) {
        uint32_t next = buffer->page_capacity ? buffer->page_capacity * 2 : 16;
        if (next < buffer->page_capacity || (size_t)next > SIZE_MAX / sizeof(*buffer->characters)) return false;
        void *characters = realloc(buffer->characters, (size_t)next * sizeof(*buffer->characters));
        if (!characters) return false;
        buffer->characters = characters;
        buffer->page_capacity = next;
    }
    while (buffer->page_count <= last) {
        uint16_t *page = malloc((size_t)DIRECTIVE_PAGE_SIZE * sizeof(*page));
        if (!page) return false;
        buffer->characters[buffer->page_count++] = page;
    }
    return true;
}

// Only the two units of a character above U+FFFF form a surrogate pair. A decoding error is stored as U+FFFD with the
// recorded position `directive_append` gives it, and a lone surrogate as U+E000, which `preproc_word` and `pragma_space`
// classify alike, so that neither joins a neighbouring lone surrogate into a character the lexer did not read.
static void directive_encode(DirectiveBuffer *buffer, uint32_t c) {
    if (c > 0x10ffff) directive_store(buffer, 0xfffd);
    else if (c >= 0xd800 && c <= 0xdfff) directive_store(buffer, 0xe000);
    else if (c <= 0xffff) directive_store(buffer, c);
    else {
        c -= 0x10000;
        directive_store(buffer, 0xd800 | (c >> 10));
        directive_store(buffer, 0xdc00 | (c & 0x3ff));
    }
}

static uint16_t directive_character(const DirectiveInput *input, uint32_t index) {
    return input->buffer->characters[index >> DIRECTIVE_PAGE_SHIFT][index & (DIRECTIVE_PAGE_SIZE - 1)];
}

static void directive_store(DirectiveBuffer *buffer, uint16_t c) {
    buffer->characters[buffer->length >> DIRECTIVE_PAGE_SHIFT][buffer->length & (DIRECTIVE_PAGE_SIZE - 1)] = c;
    buffer->length++;
}

static void directive_release(DirectiveBuffer *buffer) {
    for (uint32_t i = 0; i < buffer->page_count; i++) free(buffer->characters[i]);
    free(buffer->characters);
    free(buffer->positions);
}

static unsigned directive_utf8_width(uint32_t c) {
    return c < 0x80 || c > 0x10ffff ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
}

static bool directive_ucn_start(DirectiveUcn *ucn, int32_t prefix, bool braced) {
    if (prefix == 'N') {
        if (!braced) return false;
        *ucn = (DirectiveUcn){.named = true, .braced = true};
        return true;
    }
    if ((prefix != 'u' && prefix != 'U') || (prefix == 'U' && braced)) return false;
    *ucn = (DirectiveUcn){.limit = prefix == 'U' ? 8u : 4u, .braced = braced};
    return true;
}

static bool directive_ucn_feed(DirectiveUcn *ucn, int32_t c, bool continuation, bool *done) {
    *done = false;
    if (ucn->braced && c == '}') {
        *done = true;
        return ucn->digits > 0 && (ucn->named || directive_valid_ucn_scalar(ucn->value, continuation));
    }
    if (ucn->named) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-')) return false;
        ucn->digits++;
        return true;
    }
    int digit = directive_hex_digit(c);
    if (digit < 0) return false;
    ucn->value = ucn->value <= 0x10ffff ? (ucn->value << 4) | (uint32_t)digit : 0x110000;
    if (++ucn->digits == ucn->limit && !ucn->braced) {
        *done = true;
        return directive_valid_ucn_scalar(ucn->value, continuation);
    }
    return true;
}

static int directive_hex_digit(int32_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool directive_valid_ucn_scalar(uint32_t value, bool continuation) {
    return value >= 0xa0 && value <= 0x10ffff && preproc_identifier_character((int32_t)value, continuation);
}

#endif
