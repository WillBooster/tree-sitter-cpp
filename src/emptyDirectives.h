// code-gauge-allow-file fileNcss 735 -- One source-fact pass shares compact character storage, directive lexing and physical-position conversion.
#ifndef TREE_SITTER_C_EMPTY_DIRECTIVES_H_
#define TREE_SITTER_C_EMPTY_DIRECTIVES_H_

#include "pragma.h"
#include <stdlib.h>

#define DIRECTIVE_PAGE_SHIFT 12
#define DIRECTIVE_PAGE_SIZE (1 << DIRECTIVE_PAGE_SHIFT)

typedef struct {
    uint32_t index, byte, width;
} DirectivePosition;

typedef struct {
    uint16_t **characters;
    DirectivePosition *positions;
    uint32_t position_count, position_capacity, position_index, previous_byte, before_previous_byte;
    uint8_t *prefix_barriers;
    uint32_t length, index, byte, end, page_count;
    uint32_t conditional_depth;
    bool utf16, encoding_known, has_hash, has_directives, has_directive_ends, has_empty, has_barriers;
} DirectiveInput;

typedef struct {
    uint8_t *starts, *ends, *line_ends, *directive_starts, *directive_ends;
    uint32_t bytes;
    bool built, ready;
} EmptyDirectives;

typedef enum { DIRECTIVE_ORDINARY, DIRECTIVE_CONDITION, DIRECTIVE_DIAGNOSTIC, DIRECTIVE_REQUIRED, DIRECTIVE_UNDEF, DIRECTIVE_LINE, DIRECTIVE_PRAGMA, DIRECTIVE_HEADER } DirectiveKind;

typedef struct {
    bool beginning, pending, operand, pragma_pending, pragma_newline, pragma_name;
    unsigned pragma_depth, header;
    DirectiveKind kind;
    uint32_t start, required_end;
} DirectiveLine;

static void directive_prefixes(EmptyDirectives *state, DirectiveInput *input);
static uint32_t directive_skip_inert(const DirectiveInput *input, uint32_t end, uint32_t position, uint32_t *byte);
static bool directive_append(DirectiveInput *input, uint32_t *capacity, uint32_t *byte, TSLexer *lexer);
static bool directive_reserve(DirectiveInput *input, uint32_t *capacity, unsigned width);
static bool directive_position(DirectiveInput *input, uint32_t index, uint32_t byte, uint32_t width);
static void directive_encode(DirectiveInput *input, uint32_t c);
static uint16_t directive_character(const DirectiveInput *input, uint32_t index);
static void directive_store(DirectiveInput *input, uint16_t c);
static void directive_release(DirectiveInput *input);
static unsigned directive_utf8_width(uint32_t c);
static void directive_barrier(DirectiveInput *input);
static void directive_line_end(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static void directive_complete_required(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static bool directive_requires_operand(DirectiveKind kind);
static void directive_line_character(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static void directive_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static bool directive_pragma_part(DirectiveLine *line, int32_t c);
static bool directive_neutral(const DirectiveLine *line);
static void directive_neutral_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static bool directive_header_ready(DirectiveLine *line, int32_t c);
static bool directive_header(DirectiveInput *input, int32_t close);
static void directive_line_comment(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static uint32_t directive_splice_end(const DirectiveInput *input);

static bool empty_directives_build(EmptyDirectives *state, TSLexer *lexer);
static void empty_directives_prune(EmptyDirectives *state, DirectiveInput *input);
static bool empty_directives_line_end_at(const EmptyDirectives *state, uint32_t byte);
static bool empty_directives_start_at(const EmptyDirectives *state, uint32_t byte);
static void empty_directives_consume_started(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix);
static void empty_directives_find(EmptyDirectives *state, DirectiveInput *input);
static int32_t directive_next(DirectiveInput *input);
static int32_t directive_physical_next(DirectiveInput *input);
static uint32_t directive_expected_width(const DirectiveInput *input, uint32_t index, int32_t *c);
static int32_t directive_peek(const DirectiveInput *input);
static int32_t directive_plain(const DirectiveInput *input);
static void directive_skip_plain(DirectiveInput *input);
typedef enum { DIRECTIVE_RUN_WORD, DIRECTIVE_RUN_CODE, DIRECTIVE_RUN_SPACE, DIRECTIVE_RUN_LINE_COMMENT, DIRECTIVE_RUN_BLOCK_COMMENT } DirectiveRun;
static inline uint32_t directive_plain_run(DirectiveInput *input, DirectiveRun run);
static bool directive_run_continues(DirectiveRun run, uint16_t c);
static void directive_pragma_name(DirectiveLine *line, const DirectiveInput *input);
static bool directive_pragma_operator(const DirectiveInput *input);
static int32_t directive_pragma_token(DirectiveInput *input);
static bool directive_comment(DirectiveInput *input, bool *line);
static bool directive_quoted(DirectiveInput *input, int32_t quote);
static bool directive_raw(DirectiveInput *input);
typedef struct {
    GnuRawStringPrefix raw;
    char directive[13];
    unsigned length;
} DirectiveWord;

static uint32_t directive_keyword_end(const DirectiveInput *input);
static DirectiveKind directive_keyword(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, const DirectiveWord *word, uint32_t end);
static DirectiveWord directive_word(DirectiveInput *input, int32_t first, bool raw, bool classify);
static void directive_word_run(DirectiveInput *input, DirectiveWord *word, bool classify);
static DirectiveKind directive_word_kind(const DirectiveWord *word);
static void directive_word_character(DirectiveWord *word, int32_t c);
static void directive_word_role(DirectiveLine *line, DirectiveKind kind, bool name, bool pragma, bool had_operand, bool identifier);
static bool directive_horizontal(int32_t c);
static bool directive_newline(int32_t c);
static uint32_t directive_byte(const DirectiveInput *input);
static void directive_record(EmptyDirectives *state, DirectiveInput *input, uint32_t start, uint32_t end);

static bool empty_directives_initialize(EmptyDirectives *state, TSLexer *lexer) {
    TSLexerCheckpoint start;
    lexer->get_checkpoint(lexer, &start);
    if (!state->built) {
        state->built = true;
        empty_directives_build(state, lexer);
    }
    state->ready = true;
    lexer->mark_end_at(lexer, &start);
    return true;
}

static bool empty_directives_after_keyword(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->directive_ends) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return position.bytes <= state->bytes && (state->directive_ends[position.bytes / 8] & (1u << (position.bytes % 8)));
}

static bool empty_directives_at_start(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->directive_starts) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return position.bytes < state->bytes && (state->directive_starts[position.bytes / 8] & (1u << (position.bytes % 8)));
}

static bool empty_directives_at_line_end(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->line_ends || !(directive_horizontal(lexer->lookahead) || directive_newline(lexer->lookahead))) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return empty_directives_line_end_at(state, position.bytes);
}

static bool empty_directives_line_end_at(const EmptyDirectives *state, uint32_t byte) {
    return state->line_ends && byte < state->bytes && (state->line_ends[byte / 8] & (1u << (byte % 8)));
}

static bool empty_directives_consume(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix);

static bool empty_directives_skip(TSLexer *lexer, void *payload) {
    return empty_directives_consume(lexer, payload, false);
}

// A null directive, including its prefix, starts only at `#`, `\\` or a space, so other characters cannot match `starts`.
static bool empty_directives_consume(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix) {
    int32_t c = lexer->lookahead;
    if (!state || !state->starts || !(c == '#' || c == '\\' || pragma_space(c))) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    if (!empty_directives_start_at(state, position.bytes)) return false;
    empty_directives_consume_started(lexer, state, skip_prefix);
    return true;
}

static bool empty_directives_start_at(const EmptyDirectives *state, uint32_t byte) {
    return state->starts && byte < state->bytes && (state->starts[byte / 8] & (1u << (byte % 8)));
}

// Consumes the rest of a null directive whose start was confirmed at a position the lexer has not left except over
// prefix characters.
static void empty_directives_consume_started(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix) {
    TSLexerPosition position;
    while (lexer->lookahead != '#' && !pragma_eof(lexer)) lexer->advance(lexer, skip_prefix);
    do {
        lexer->advance(lexer, false);
        lexer->get_position(lexer, &position);
    } while (!pragma_eof(lexer) && position.bytes < state->bytes &&
        !(state->ends[position.bytes / 8] & (1u << (position.bytes % 8))));
}

// code-gauge-allow functionNcss 35 -- The source-fact buffers are allocated and released together so initialization fails atomically.
static bool empty_directives_build(EmptyDirectives *state, TSLexer *lexer) {
    TSLexerPosition start;
    lexer->get_position(lexer, &start);
    DirectiveInput input = {.byte = start.bytes};
    uint32_t capacity = 0, byte = start.bytes;
    while (!pragma_eof(lexer)) {
        if (!directive_append(&input, &capacity, &byte, lexer)) goto failed;
    }
    input.end = byte;
    state->bytes = byte;
    if (!input.has_hash) {
        directive_release(&input);
        return true;
    }
    state->starts = calloc((size_t)state->bytes / 8 + 1, 1);
    state->ends = calloc((size_t)state->bytes / 8 + 1, 1);
    state->directive_starts = calloc((size_t)state->bytes / 8 + 1, 1);
    state->directive_ends = calloc((size_t)state->bytes / 8 + 1, 1);
    input.prefix_barriers = calloc((size_t)state->bytes / 8 + 1, 1);
    if (!state->starts || !state->ends || !state->directive_starts || !state->directive_ends || !input.prefix_barriers) goto failed;
    empty_directives_find(state, &input);
    if (input.has_empty || input.has_barriers) directive_prefixes(state, &input);
    empty_directives_prune(state, &input);
    directive_release(&input);
    return true;
failed:
    free(input.prefix_barriers);
    directive_release(&input);
    free(state->starts);
    free(state->ends);
    free(state->directive_starts);
    free(state->directive_ends);
    state->starts = state->ends = state->directive_starts = state->directive_ends = NULL;
    return false;
}

static void empty_directives_prune(EmptyDirectives *state, DirectiveInput *input) {
    if (!input->has_empty) {
        free(state->starts);
        free(state->ends);
        state->starts = state->ends = NULL;
    }
    if (!input->has_directives) {
        free(state->directive_starts);
        state->directive_starts = NULL;
    }
    if (!input->has_directive_ends) {
        free(state->directive_ends);
        state->directive_ends = NULL;
    }
    if (input->has_barriers) state->line_ends = input->prefix_barriers;
    else free(input->prefix_barriers);
}

// code-gauge-allow functionCognitiveComplexity 29 -- The reverse lexical pass applies sparse physical-position corrections while carrying null-prefix and line-end facts.
static void directive_prefixes(EmptyDirectives *state, DirectiveInput *input) {
    bool prefix = false, line_end = false;
    uint32_t byte = input->end, position = input->position_count;
    for (uint32_t i = input->length; i > 0;) {
        if (!prefix && !line_end) i = directive_skip_inert(input, i, position, &byte);
        if (!i) break;
        i--;
        if (i && directive_character(input, i) >= 0xdc00 && directive_character(input, i) <= 0xdfff &&
            directive_character(input, i - 1) >= 0xd800 && directive_character(input, i - 1) <= 0xdbff) continue;
        int32_t c;
        uint32_t width = directive_expected_width(input, i, &c);
        if (position && input->positions[position - 1].index == i) byte = input->positions[--position].byte;
        else {
            if (width > byte) return;
            byte -= width;
        }
        if (byte > state->bytes) return;
        bool hash = c == '#' && (state->starts[byte / 8] & (1u << (byte % 8)));
        bool condition_end = input->prefix_barriers[byte / 8] & (1u << (byte % 8));
        line_end = condition_end || (line_end && directive_horizontal(c));
        if (line_end) input->prefix_barriers[byte / 8] |= 1u << (byte % 8);
        prefix = !condition_end && (hash || (prefix && (pragma_space(c) || c == '\\')));
        if (prefix) state->starts[byte / 8] |= 1u << (byte % 8);
    }
}

// Steps back from the character before `end` over characters that cannot change the reverse pass's state while no
// prefix or line end is carried: plain-width characters that are neither `#` nor barriers. Returns the new end.
static uint32_t directive_skip_inert(const DirectiveInput *input, uint32_t end, uint32_t position, uint32_t *byte) {
    uint32_t stop = position ? input->positions[position - 1].index + 1 : 0, ascii_width = input->utf16 ? 2 : 1;
    while (end > stop) {
        uint32_t c = directive_character(input, end - 1);
        uint32_t width = c < 0x80 ? ascii_width : input->utf16 && (c < 0xd800 || c > 0xdfff) ? 2 : 0;
        if (!width || c == '#' || width > *byte) break;
        uint32_t previous = *byte - width;
        if (input->prefix_barriers[previous / 8] & (1u << (previous % 8))) break;
        *byte = previous;
        end--;
    }
    return end;
}

// `*byte` is the lexer's position before the character and is advanced past it.
static bool directive_append(DirectiveInput *input, uint32_t *capacity, uint32_t *byte, TSLexer *lexer) {
    uint32_t c = lexer->lookahead, index = input->length, start = *byte;
    if (c == '#') input->has_hash = true;
    unsigned width = directive_utf8_width(c), utf16_width = c > 0xffff && c <= 0x10ffff ? 4 : 2;
    if (!directive_reserve(input, capacity, c > 0xffff && c <= 0x10ffff ? 2 : 1)) return false;
    directive_encode(input, c);
    TSLexerPosition end;
    lexer->advance(lexer, false);
    lexer->get_position(lexer, &end);
    *byte = end.bytes;
    uint32_t measured = end.bytes - start;
    if (!input->encoding_known && c <= 0x10ffff && width != utf16_width && !lexer->is_at_included_range_start(lexer)) {
        input->utf16 = measured == utf16_width;
        input->encoding_known = true;
    }
    if (c > 0x10ffff || measured != (input->utf16 ? utf16_width : width)) {
        return directive_position(input, index, start, measured);
    }
    return true;
}

static bool directive_position(DirectiveInput *input, uint32_t index, uint32_t byte, uint32_t width) {
    if (input->position_count == input->position_capacity) {
        uint32_t capacity = input->position_capacity ? input->position_capacity * 2 : 16;
        if (capacity < input->position_capacity || (size_t)capacity > SIZE_MAX / sizeof(*input->positions)) return false;
        void *positions = realloc(input->positions, (size_t)capacity * sizeof(*input->positions));
        if (!positions) return false;
        input->positions = positions;
        input->position_capacity = capacity;
    }
    input->positions[input->position_count++] = (DirectivePosition){index, byte, width};
    return true;
}

static bool directive_reserve(DirectiveInput *input, uint32_t *capacity, unsigned width) {
    if (input->length > UINT32_MAX - width) return false;
    uint32_t last = (input->length + width - 1) >> DIRECTIVE_PAGE_SHIFT;
    if (last >= *capacity) {
        uint32_t next = *capacity ? *capacity * 2 : 16;
        if (next < *capacity || (size_t)next > SIZE_MAX / sizeof(*input->characters)) return false;
        void *characters = realloc(input->characters, (size_t)next * sizeof(*input->characters));
        if (!characters) return false;
        input->characters = characters;
        *capacity = next;
    }
    while (input->page_count <= last) {
        uint16_t *page = malloc((size_t)DIRECTIVE_PAGE_SIZE * sizeof(*page));
        if (!page) return false;
        input->characters[input->page_count++] = page;
    }
    return true;
}

static void directive_encode(DirectiveInput *input, uint32_t c) {
    if (c > 0x10ffff) directive_store(input, 0xdfff);
    else if (c <= 0xffff) directive_store(input, c);
    else {
        c -= 0x10000;
        directive_store(input, 0xd800 | (c >> 10));
        directive_store(input, 0xdc00 | (c & 0x3ff));
    }
}

static uint16_t directive_character(const DirectiveInput *input, uint32_t index) {
    return input->characters[index >> DIRECTIVE_PAGE_SHIFT][index & (DIRECTIVE_PAGE_SIZE - 1)];
}

static void directive_store(DirectiveInput *input, uint16_t c) {
    input->characters[input->length >> DIRECTIVE_PAGE_SHIFT][input->length & (DIRECTIVE_PAGE_SIZE - 1)] = c;
    input->length++;
}

static void directive_release(DirectiveInput *input) {
    for (uint32_t i = 0; i < input->page_count; i++) free(input->characters[i]);
    free(input->characters);
    free(input->positions);
}

static unsigned directive_utf8_width(uint32_t c) {
    return c < 0x80 || c > 0x10ffff ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
}

static void empty_directives_find(EmptyDirectives *state, DirectiveInput *input) {
    DirectiveLine line = {.beginning = true};
    while (input->index < input->length) {
        // Horizontal spaces never change the line state.
        directive_plain_run(input, DIRECTIVE_RUN_SPACE);
        int32_t c = directive_next(input);
        if (c < 0) break;
        directive_line_character(state, input, &line, c);
    }
    directive_complete_required(state, input, &line);
    if (line.pending) directive_record(state, input, line.start, input->end);
}

static void directive_line_character(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c) {
    if (c == '\n') {
        directive_line_end(state, input, line);
        return;
    }
    if (c == '/' && (directive_peek(input) == '/' || directive_peek(input) == '*')) {
        directive_line_comment(state, input, line);
        return;
    }
    if (directive_horizontal(c) || (line->beginning && pragma_space(c))) return;
    if (c == '#' && line->beginning) {
        line->pragma_pending = line->pragma_newline = false;
        line->pragma_depth = 0;
        line->kind = DIRECTIVE_ORDINARY;
        line->operand = false;
        line->start = input->previous_byte;
        state->directive_starts[line->start / 8] |= 1u << (line->start % 8);
        input->has_directives = true;
        line->beginning = false;
        line->pending = true;
        return;
    }
    if (directive_neutral(line)) directive_neutral_code(state, input, line, c);
    else directive_code(state, input, line, c);
}

// code-gauge-allow functionCognitiveComplexity 19 -- Header names, quoted tokens and distinct keyword boundaries update one logical directive's lexical state.
static void directive_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c) {
    bool name = line->pending;
    bool header = directive_header_ready(line, c);
    bool pragma = directive_pragma_part(line, c);
    bool operand = line->operand;
    if (!name && !pragma) line->operand = true;
    line->beginning = line->pending = false;
    if (header) {
        if (directive_header(input, c == '<' ? '>' : '"')) directive_line_end(state, input, line);
        return;
    }
    if (c == '"' || c == '\'') {
        if (directive_quoted(input, c)) directive_line_end(state, input, line);
        return;
    }
    if (!preproc_word(c, false) && !(c >= '0' && c <= '9')) return;
    uint32_t end = name ? directive_keyword_end(input) : 0;
    DirectiveWord word = directive_word(input, c, line->kind != DIRECTIVE_DIAGNOSTIC, true);
    DirectiveKind kind = name ? directive_keyword(state, input, line, &word, end) : directive_word_kind(&word);
    if (!name && !pragma && kind == DIRECTIVE_PRAGMA) directive_pragma_name(line, input);
    directive_word_role(line, kind, name, pragma, operand, preproc_word(c, false));
}

// Outside a directive name, a line of no directive kind with no `_Pragma` or header operand in progress keeps that
// state through every token, so its words need no classification.
static bool directive_neutral(const DirectiveLine *line) {
    return !line->pending && line->kind == DIRECTIVE_ORDINARY && !line->pragma_depth && !line->pragma_pending && !line->header;
}

// Also consumes the following characters that leave the line state as it is, which is every plain character but `/`.
static void directive_neutral_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c) {
    line->operand = true;
    line->beginning = false;
    for (;;) {
        if (c == '"' || c == '\'') {
            if (directive_quoted(input, c)) {
                directive_line_end(state, input, line);
                return;
            }
        } else if (preproc_word(c, false) || (c >= '0' && c <= '9')) {
            directive_word(input, c, true, false);
        }
        directive_plain_run(input, DIRECTIVE_RUN_CODE);
        c = directive_plain(input);
        if (c < 0 || c == '/') return;
        directive_skip_plain(input);
    }
}

static void directive_word_role(DirectiveLine *line, DirectiveKind kind, bool name, bool pragma, bool had_operand, bool identifier) {
    if (name) {
        line->kind = kind;
        return;
    }
    if (pragma) return;
    if (kind == DIRECTIVE_PRAGMA) {
        bool required = directive_requires_operand(line->kind) && line->kind != DIRECTIVE_UNDEF;
        if (required && !had_operand) {
            line->operand = false;
            line->pragma_pending = true;
        }
        return;
    }
    if (identifier && line->kind == DIRECTIVE_CONDITION) line->header = 1;
}

static bool directive_header_ready(DirectiveLine *line, int32_t c) {
    if (line->header == 1) {
        line->header = c == '(' ? 2 : 0;
        return false;
    }
    bool argument = line->header == 2;
    line->header = 0;
    return (argument || (line->kind == DIRECTIVE_HEADER && !line->operand)) && (c == '<' || c == '"');
}

static bool directive_header(DirectiveInput *input, int32_t close) {
    while (input->index < input->length) {
        int32_t c = directive_next(input);
        if (c == close) return false;
        if (c == '\n') return true;
    }
    return false;
}

static bool directive_pragma_part(DirectiveLine *line, int32_t c) {
    if (line->pragma_depth) {
        if (c == '(') line->pragma_depth++;
        if (c == ')') line->pragma_depth--;
        return true;
    }
    if (!line->pragma_pending) return false;
    line->pragma_pending = false;
    if (c == '(') {
        line->pragma_depth = 1;
        line->pragma_newline = false;
        return true;
    }
    if (line->pragma_newline) line->kind = DIRECTIVE_ORDINARY;
    line->pragma_newline = false;
    line->operand = true;
    return false;
}

static void directive_line_comment(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line) {
    bool single = false;
    if (!directive_comment(input, &single)) line->pending = false;
    if (single) directive_line_end(state, input, line);
}

static void directive_line_end(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line) {
    directive_complete_required(state, input, line);
    if (line->pending) directive_record(state, input, line->start, directive_byte(input));
    line->beginning = true;
    line->pending = false;
    bool required = line->kind == DIRECTIVE_CONDITION ||
        (directive_requires_operand(line->kind) && !line->operand && !line->pragma_pending);
    if (required) directive_barrier(input);
    line->header = 0;
    if (line->pragma_pending) line->pragma_newline = true;
    if (!line->pragma_depth && !line->pragma_pending) {
        line->kind = DIRECTIVE_ORDINARY;
        line->operand = false;
    }
}

static void directive_complete_required(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line) {
    bool pragma_name = line->pragma_name;
    line->pragma_name = false;
    if (!line->required_end) return;
    if (line->operand || line->pragma_pending || pragma_name) {
        uint32_t end = line->required_end;
        state->directive_ends[end / 8] |= 1u << (end % 8);
        input->has_directive_ends = true;
    }
    line->required_end = 0;
}

static bool directive_requires_operand(DirectiveKind kind) {
    return kind == DIRECTIVE_REQUIRED || kind == DIRECTIVE_UNDEF || kind == DIRECTIVE_LINE || kind == DIRECTIVE_HEADER;
}

static void directive_barrier(DirectiveInput *input) {
    if (!input->index || !directive_newline(directive_character(input, input->index - 1))) return;
    uint32_t byte = input->previous_byte;
    input->prefix_barriers[byte / 8] |= 1u << (byte % 8);
    input->has_barriers = true;
    if (input->index < 2) return;
    int32_t previous = directive_character(input, input->index - 2);
    if (directive_newline(previous) && previous != directive_character(input, input->index - 1)) {
        byte = input->before_previous_byte;
        input->prefix_barriers[byte / 8] |= 1u << (byte % 8);
    }
}

static int32_t directive_next(DirectiveInput *input) {
    int32_t plain = directive_plain(input);
    if (plain >= 0) {
        directive_skip_plain(input);
        return plain;
    }
    while (input->index < input->length) {
        int32_t c = directive_physical_next(input);
        uint32_t after = c == '\\' ? directive_splice_end(input) : input->index;
        if (after != input->index) {
            while (input->index < after) directive_physical_next(input);
            continue;
        }
        if (directive_newline(c)) {
            if (input->index < input->length && directive_newline(directive_character(input, input->index)) &&
                directive_character(input, input->index) != c) directive_physical_next(input);
            return '\n';
        }
        return c;
    }
    return -1;
}

static int32_t directive_physical_next(DirectiveInput *input) {
    uint32_t index = input->index;
    int32_t c;
    uint32_t width = directive_expected_width(input, index, &c);
    input->index += c > 0xffff ? 2 : 1;
    input->before_previous_byte = input->previous_byte;
    input->previous_byte = input->byte;
    if (input->position_index < input->position_count && input->positions[input->position_index].index == index) {
        DirectivePosition position = input->positions[input->position_index++];
        input->previous_byte = position.byte;
        input->byte = position.byte + position.width;
    } else input->byte += width;
    return c;
}

// Decodes the character at `index` into `*c` and returns its byte width when it has no recorded position.
static uint32_t directive_expected_width(const DirectiveInput *input, uint32_t index, int32_t *c) {
    uint32_t unit = directive_character(input, index);
    if (unit == 0xdfff) {
        *c = 0xfffd;
        return input->utf16 ? 2 : 1;
    }
    if (unit >= 0xd800 && unit <= 0xdbff && index + 1 < input->length) {
        uint32_t low = directive_character(input, index + 1);
        if (low >= 0xdc00 && low <= 0xdfff) {
            *c = (int32_t)(0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
            return 4;
        }
    }
    *c = (int32_t)unit;
    return input->utf16 ? 2 : directive_utf8_width(unit);
}

static uint32_t directive_splice_end(const DirectiveInput *input) {
    uint32_t after = input->index;
    while (after < input->length && directive_horizontal(directive_character(input, after))) after++;
    if (after == input->length || !directive_newline(directive_character(input, after))) return input->index;
    int32_t newline = directive_character(input, after++);
    if (after < input->length && directive_newline(directive_character(input, after)) &&
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
    uint32_t index = input->index, end = input->length;
    if (input->position_index < input->position_count && input->positions[input->position_index].index < end) {
        end = input->positions[input->position_index].index;
    }
    while (index < end) {
        uint16_t c = directive_character(input, index);
        if (c >= 0x80 || c == '\\' || directive_newline(c) || !directive_run_continues(run, c)) break;
        index++;
    }
    uint32_t length = index - input->index, width = input->utf16 ? 2 : 1;
    if (!length) return 0;
    input->before_previous_byte = length > 1 ? input->byte + (length - 2) * width : input->previous_byte;
    input->previous_byte = input->byte + (length - 1) * width;
    input->byte += length * width;
    input->index = index;
    return length;
}

// `c` is ASCII.
static bool directive_run_continues(DirectiveRun run, uint16_t c) {
    bool identifier = (preproc_identifier_continue_ascii[c / 64] >> (c % 64)) & 1;
    switch (run) {
        case DIRECTIVE_RUN_WORD:
            return identifier;
        case DIRECTIVE_RUN_CODE:
            // The rest of a neutral line: characters that start no token.
            return !identifier && c != '"' && c != '\'' && c != '/';
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
    input->byte += input->utf16 ? 2 : 1;
}

// The next character when it is ASCII, cannot start a line splice or a newline, and has its expected width; otherwise -1.
static int32_t directive_plain(const DirectiveInput *input) {
    if (input->index >= input->length) return -1;
    uint16_t c = directive_character(input, input->index);
    if (c >= 0x80 || c == '\\' || directive_newline(c)) return -1;
    bool positioned = input->position_index < input->position_count &&
        input->positions[input->position_index].index == input->index;
    return positioned ? -1 : c;
}

// `_Pragma` is the macro name of `#define _Pragma(x)` or `#ifdef _Pragma`, but an operator before a string literal.
static void directive_pragma_name(DirectiveLine *line, const DirectiveInput *input) {
    if (line->kind == DIRECTIVE_UNDEF || (line->kind == DIRECTIVE_REQUIRED && !directive_pragma_operator(input))) line->pragma_name = true;
}

// Mirrors the argument prefix accepted by `scan_pragma_argument` and the spacing a directive line can hold.
static bool directive_pragma_operator(const DirectiveInput *input) {
    DirectiveInput copy = *input;
    if (directive_pragma_token(&copy) != '(') return false;
    int32_t c = directive_pragma_token(&copy);
    if (c == 'L' || c == 'U') c = directive_next(&copy);
    else if (c == 'u') {
        c = directive_next(&copy);
        if (c == '8') c = directive_next(&copy);
    }
    if (c == 'R') c = directive_next(&copy);
    return c == '"';
}

static int32_t directive_pragma_token(DirectiveInput *input) {
    for (;;) {
        int32_t c = directive_next(input);
        if (c == '/' && directive_peek(input) == '*') {
            bool line = false;
            directive_comment(input, &line);
        } else if (!directive_horizontal(c)) return c;
    }
}

static bool directive_comment(DirectiveInput *input, bool *line) {
    *line = directive_next(input) == '/';
    int32_t previous = 0;
    DirectiveRun run = *line ? DIRECTIVE_RUN_LINE_COMMENT : DIRECTIVE_RUN_BLOCK_COMMENT;
    while (input->index < input->length) {
        if (previous != '*') {
            uint32_t length = directive_plain_run(input, run);
            if (length) previous = directive_character(input, input->index - 1);
        }
        int32_t c = directive_next(input);
        if (*line && c == '\n') return true;
        if (!*line && previous == '*' && c == '/') return true;
        previous = c;
    }
    return *line;
}

static bool directive_quoted(DirectiveInput *input, int32_t quote) {
    while (input->index < input->length) {
        int32_t c = directive_next(input);
        if (c == quote) return false;
        if (c == '\\') c = directive_next(input);
        if (c == '\n') return true;
        if (c < 0) return false;
    }
    return false;
}

static uint32_t directive_keyword_end(const DirectiveInput *input) {
    DirectiveInput name = *input;
    int32_t c;
    while ((c = directive_peek(&name)) == '_' || (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) directive_next(&name);
    return directive_byte(&name);
}

static DirectiveKind directive_keyword(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, const DirectiveWord *word, uint32_t end) {
    DirectiveKind kind = directive_word_kind(word);
    // Directives in a group that may be skipped need no operand, and the parser cannot tell which groups are skipped.
    if (directive_requires_operand(kind) && !input->conditional_depth) line->required_end = end;
    else {
        state->directive_ends[end / 8] |= 1u << (end % 8);
        input->has_directive_ends = true;
    }
    if (word->length < sizeof word->directive) {
        if (strcmp(word->directive, "if") == 0 || strcmp(word->directive, "ifdef") == 0 || strcmp(word->directive, "ifndef") == 0) input->conditional_depth++;
        else if (strcmp(word->directive, "endif") == 0 && input->conditional_depth) input->conditional_depth--;
    }
    return kind;
}

// Without `classify`, only `raw` of the result is complete.
static DirectiveWord directive_word(DirectiveInput *input, int32_t first, bool raw, bool classify) {
    DirectiveWord word = {0};
    directive_word_character(&word, first);
    bool number = first >= '0' && first <= '9';
    int32_t previous = first;
    if (!number) directive_word_run(input, &word, classify);
    for (;;) {
        int32_t c = directive_peek(input);
        bool exponent_sign = number && (c == '+' || c == '-') &&
            (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P');
        if (!preproc_word(c, true) && !(number && (c == '.' || c == '\'' || exponent_sign))) break;
        previous = directive_next(input);
        directive_word_character(&word, previous);
    }
    if (raw && gnu_raw_string_prefix(&word.raw) && directive_peek(input) == '"') directive_raw(input);
    return word;
}

static void directive_word_run(DirectiveInput *input, DirectiveWord *word, bool classify) {
    uint32_t start = input->index, length = directive_plain_run(input, DIRECTIVE_RUN_WORD);
    // Both parts of the word stop changing after these many characters.
    uint32_t significant = classify ? sizeof word->directive : sizeof word->raw.value;
    for (uint32_t i = 0; i < length && i < significant; i++) directive_word_character(word, directive_character(input, start + i));
}

static DirectiveKind directive_word_kind(const DirectiveWord *word) {
    if (word->length >= sizeof word->directive) return DIRECTIVE_ORDINARY;
    if (strcmp(word->directive, "if") == 0 || strcmp(word->directive, "elif") == 0) return DIRECTIVE_CONDITION;
    if (strcmp(word->directive, pragma_word) == 0) return DIRECTIVE_PRAGMA;
    if (strcmp(word->directive, "include") == 0 || strcmp(word->directive, "include_next") == 0 ||
        strcmp(word->directive, "embed") == 0) return DIRECTIVE_HEADER;
    if (strcmp(word->directive, "warning") == 0 || strcmp(word->directive, "error") == 0) return DIRECTIVE_DIAGNOSTIC;
    if (strcmp(word->directive, "undef") == 0) return DIRECTIVE_UNDEF;
    if (strcmp(word->directive, "line") == 0) return DIRECTIVE_LINE;
    static const char *const required[] = {
        "define", "ifdef", "ifndef", "elifdef", "elifndef",
    };
    for (unsigned i = 0; i < sizeof required / sizeof *required; i++) {
        if (strcmp(word->directive, required[i]) == 0) return DIRECTIVE_REQUIRED;
    }
    return DIRECTIVE_ORDINARY;
}

static void directive_word_character(DirectiveWord *word, int32_t c) {
    gnu_raw_string_prefix_character(&word->raw, c);
    if (word->length < sizeof word->directive - 1 && c > 0 && c < 128) {
        word->directive[word->length++] = (char)c;
    } else {
        word->length = sizeof word->directive;
    }
}

static bool directive_raw(DirectiveInput *input) {
    DirectiveInput candidate = *input;
    directive_next(&candidate);
    int32_t delimiter[16];
    unsigned length = 0;
    int32_t c;
    while ((c = directive_next(&candidate)) != '(') {
        if (length == 16 || c < 0x21 || c > 0x7e || c == ')' || c == '\\') return false;
        delimiter[length++] = c;
    }
    while (candidate.index < candidate.length) {
        if (directive_physical_next(&candidate) != ')') continue;
        DirectiveInput closing = candidate;
        unsigned matched = 0;
        while (matched < length && closing.index < closing.length &&
            directive_physical_next(&closing) == delimiter[matched]) matched++;
        if (matched == length && closing.index < closing.length && directive_physical_next(&closing) == '"') {
            candidate = closing;
            break;
        }
    }
    *input = candidate;
    return true;
}

static bool directive_horizontal(int32_t c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

static bool directive_newline(int32_t c) {
    return c == '\n' || c == '\r';
}

static uint32_t directive_byte(const DirectiveInput *input) {
    return input->byte;
}

static void directive_record(EmptyDirectives *state, DirectiveInput *input, uint32_t start, uint32_t end) {
    state->starts[start / 8] |= 1u << (start % 8);
    state->ends[end / 8] |= 1u << (end % 8);
    input->has_empty = true;
}

#endif
