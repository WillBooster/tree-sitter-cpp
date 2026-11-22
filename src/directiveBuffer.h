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
    bool utf16, encoding_known, failed, carries, pragma;
    uint8_t classes[UNIT_CLASS_COUNT];
} DirectiveBuffer;

// A position in a `DirectiveBuffer`; copies share the buffer, so a lookahead copy reads what the original will.
typedef struct {
    DirectiveBuffer *buffer;
    uint32_t index, byte, position_index, previous_byte, before_previous_byte;
    // Whether a line splice with spaces before its newline was read, which the lexer's tokens do not take as a splice.
    bool spaced_splice;
} DirectiveInput;

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

#endif
