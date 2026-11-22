// code-gauge-allow-file fileNcss -- One source-fact pass shares directive lexing, physical-position conversion, prefix spans and fact bitsets.
#ifndef TREE_SITTER_C_EMPTY_DIRECTIVES_H_
#define TREE_SITTER_C_EMPTY_DIRECTIVES_H_

#include "directiveBuffer.h"
#include <stdlib.h>

#define DIRECTIVE_FACT_PAGE_SHIFT 15
#define DIRECTIVE_FACT_PAGE_SIZE (1 << DIRECTIVE_FACT_PAGE_SHIFT)

// Each kind of fact is a directory of `pages` pointers, one per `DIRECTIVE_FACT_PAGE_SIZE` source bytes up to and
// possibly past the furthest fact, to bitset pages allocated only where a fact is set, so a missing directory or page
// holds no fact; source without facts, such as an excluded range, costs one pointer per page of each directory.
// `empty_directives_release` frees them.
typedef struct {
    uint8_t **starts, **ends, **comments, **line_ends, **directive_starts, **directive_ends;
    uint32_t pages;
    // `anchored` and `ready` belong to the scanner state: whether the token at the start of the input is out, and
    // whether the facts, which `built` says exist, are in use. `pragmas` is the fact that the input read for them holds
    // `pragma_word`, without which no scan has to look for a `_Pragma` operator.
    bool built, anchored, ready, pragmas;
} EmptyDirectives;

typedef enum { DIRECTIVE_ORDINARY, DIRECTIVE_CONDITION, DIRECTIVE_DIAGNOSTIC, DIRECTIVE_REQUIRED, DIRECTIVE_UNDEF, DIRECTIVE_LINE, DIRECTIVE_PRAGMA, DIRECTIVE_HEADER } DirectiveKind;

// The reverse pass's state; the trailing run's open span covers the bytes from `tail_start` to before `tail_end`.
typedef struct {
    bool prefix, line_end, tail, tail_open;
    uint32_t tail_start, tail_end;
} DirectiveCarry;

typedef struct {
    bool beginning, pending, operand, pragma_pending, pragma_newline, pragma_name;
    unsigned pragma_depth, header;
    DirectiveKind kind;
    // `comment_start` is the first comment after a pending `#`, or 0.
    uint32_t start, comment_start, required_end;
} DirectiveLine;

static bool directive_scan_buffered(DirectiveScan *scan, DirectiveInput *input);
static bool directive_scan_lexer(DirectiveScan *scan, DirectiveBuffer *buffer);
static void directive_scan_resume(DirectiveScan *scan, DirectiveSpans *run, DirectiveSpans *tail);
static void directive_scan_handoff(DirectiveScan *scan, uint32_t hash, bool skipped);
static bool directive_segment(EmptyDirectives *state, DirectiveInput *input);
static bool directive_segment_ends(const DirectiveInput *input);
static bool directive_short_line_comment(const DirectiveInput *input, uint32_t index, uint32_t limit);
static bool directive_line_clean(const DirectiveLine *line);
static void directive_shift(DirectiveBuffer *buffer, uint32_t start);
static void directive_finish(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveSpans *run, DirectiveSpans *tail);
static void directive_prefixes(EmptyDirectives *state, const DirectiveInput *input, const DirectiveSpans *prefix, DirectiveSpans *tail);
static bool directive_step_back(const DirectiveInput *input, uint32_t *index, uint32_t *position, uint32_t *byte, int32_t *c, uint32_t *width);
static void directive_carry(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveCarry *carry, int32_t c, uint32_t byte);
static void directive_carry_tail(DirectiveCarry *carry, DirectiveSpans *tail, uint32_t byte, uint32_t width);
static void directive_spans_reverse(DirectiveSpans *spans);
static void directive_mark_spans(EmptyDirectives *state, DirectiveBuffer *buffer, const DirectiveSpans *spans);
static uint32_t directive_skip_inert(const EmptyDirectives *state, const DirectiveInput *input, uint32_t end, uint32_t position, uint32_t *byte);
static void directive_set(EmptyDirectives *state, DirectiveBuffer *buffer, uint8_t ***facts, uint32_t byte);
static bool directive_bit(const EmptyDirectives *state, uint8_t *const *facts, uint32_t byte);
static bool directive_grow_directories(EmptyDirectives *state, uint32_t pages);
static void directive_barrier(EmptyDirectives *state, DirectiveInput *input);
static void directive_line_end(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static void directive_complete_required(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static bool directive_requires_operand(DirectiveKind kind);
static void directive_line_character(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static void directive_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static bool directive_pragma_part(DirectiveLine *line, int32_t c);
static bool directive_neutral(const DirectiveLine *line);
static void directive_neutral_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static void directive_neutral_units(DirectiveInput *input);
static void directive_advance_plain(DirectiveInput *input, uint32_t length);
static bool directive_header_ready(DirectiveLine *line, int32_t c);
static bool directive_header(DirectiveInput *input, int32_t close);
static void directive_line_comment(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static bool directive_skip_splice(DirectiveInput *input);
static uint32_t directive_splice_end(const DirectiveInput *input);

static bool empty_directives_build(EmptyDirectives *state, TSLexer *lexer);
static void empty_directives_release(EmptyDirectives *state);
static bool empty_directives_line_end_at(const EmptyDirectives *state, uint32_t byte);
static bool empty_directives_start_at(const EmptyDirectives *state, uint32_t byte);
static void empty_directives_consume_started(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix);
static int32_t directive_next(DirectiveInput *input);
static int32_t directive_physical_next(DirectiveInput *input);
static uint32_t directive_expected_width(const DirectiveInput *input, uint32_t index, int32_t *c);
static int32_t directive_peek(const DirectiveInput *input);
static int32_t directive_plain(const DirectiveInput *input);
static void directive_skip_plain(DirectiveInput *input);
typedef enum { DIRECTIVE_RUN_WORD, DIRECTIVE_RUN_SPACE, DIRECTIVE_RUN_LINE_COMMENT, DIRECTIVE_RUN_BLOCK_COMMENT } DirectiveRun;
static inline uint32_t directive_plain_run(DirectiveInput *input, DirectiveRun run);
static uint32_t directive_run_end(const DirectiveBuffer *buffer, DirectiveRun run, uint32_t index, uint32_t end);
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
static uint32_t directive_byte(const DirectiveInput *input);
static void directive_record(EmptyDirectives *state, DirectiveInput *input, const DirectiveLine *line, uint32_t end);

// Scans the zero-width token `symbol` that starts the directive context: once at the start of the input, where it
// anchors the tree, and, when comments lead the input, once more where the facts are built after them. Returns false
// while a comment is still ahead, having consumed characters.
static bool empty_directives_initialize(EmptyDirectives *state, TSLexer *lexer, TSSymbol symbol) {
    TSLexerCheckpoint start;
    lexer->get_checkpoint(lexer, &start);
    lexer->result_symbol = symbol;
    bool anchored = state->anchored;
    state->anchored = true;
    if (!state->built) state->built = empty_directives_build(state, lexer);
    state->ready = state->built;
    lexer->mark_end_at(lexer, &start);
    return state->ready || !anchored;
}

static bool empty_directives_after_keyword(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->directive_ends) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return directive_bit(state, state->directive_ends, position.bytes);
}

// False only when the facts show that no `_Pragma` operator can follow any position.
static bool empty_directives_pragma_possible(const EmptyDirectives *state) {
    return !state || !state->built || state->pragmas;
}

// Whether none of the `count` tokens, at least eight, is valid but those whose byte in `extras` is 0xff.
static inline bool empty_directives_only_valid(const bool *valid_symbols, const uint8_t *extras, unsigned count) {
    _Static_assert(sizeof(bool) == 1, "valid_symbols is read as bytes");
    uint64_t others = 0;
    for (unsigned i = 0; i < count; i += 8) {
        unsigned offset = i + 8 > count ? count - 8 : i;
        uint64_t valid, extra;
        memcpy(&valid, valid_symbols + offset, sizeof valid);
        memcpy(&extra, extras + offset, sizeof extra);
        others |= valid & ~extra;
    }
    return !others;
}

static bool empty_directives_at_start(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->directive_starts) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return directive_bit(state, state->directive_starts, position.bytes);
}

static bool empty_directives_at_line_end(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->line_ends || !(directive_horizontal(lexer->lookahead) || directive_newline(lexer->lookahead))) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return empty_directives_line_end_at(state, position.bytes);
}

static bool empty_directives_line_end_at(const EmptyDirectives *state, uint32_t byte) {
    return directive_bit(state, state->line_ends, byte);
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
    return directive_bit(state, state->starts, byte);
}

// Consumes the rest of a null directive whose start was confirmed at a position the lexer has not left except over
// prefix characters: as a token (`skip_prefix`) up to its first recorded end, which `directive_record` places before
// the comments or after the line, and otherwise the whole logical line, since some scanner lookahead dispatches on
// the character after the skip without reading comments.
static void empty_directives_consume_started(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix) {
    TSLexerPosition position;
    while (lexer->lookahead != '#' && !pragma_eof(lexer)) lexer->advance(lexer, skip_prefix);
    for (bool whole = !skip_prefix;; whole = false) {
        do {
            lexer->advance(lexer, false);
            lexer->get_position(lexer, &position);
        } while (!pragma_eof(lexer) && !directive_bit(state, state->ends, position.bytes));
        if (!whole || pragma_eof(lexer) || !directive_bit(state, state->comments, position.bytes)) return;
    }
}

// Ordinary code goes through `DirectiveScan` without being stored, and only directive segments, each from a directive's
// `#` to the end of the logical line where no directive state is left, are stored for the line-state pass and the
// reverse prefix pass. The facts are stored as `EmptyDirectives` describes.
//
// Returns false without building when a comment follows the lexer's position after spaces only, which leaves the build
// to the scan after the comment's token. That scan starts from the initial state where the lexer ends the comment: the
// state this pass has there too, unless the lexer ends the comment earlier, as at a backslash that spaces separate
// from its newline, and then what follows is read as the lexer's tokens take it and not as comment text.
//
// A build from the start of an input that begins with a block comment would read to the end of the input in a scan
// that consumes nothing, which the runtime takes for a scanner stuck in an unterminated comment: it then reads the
// whole input once more and guards every later scan.
static bool empty_directives_build(EmptyDirectives *state, TSLexer *lexer) {
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    DirectiveBuffer buffer = {.lexer = lexer, .fill_byte = position.bytes};
    DirectiveInput input = {.buffer = &buffer, .byte = position.bytes};
    // `run` holds the run before a segment's `#`, and `tail` the segment's trailing run.
    DirectiveSpans run = {0}, tail = {0};
    DirectiveScan scan = {.beginning = true, .run = &run, .classes = buffer.classes, .pragma = &buffer.pragma};
    if (directive_scan_leading_comment(&scan, lexer)) {
        free(run.items);
        return false;
    }
    directive_unit_classes(buffer.classes);
    for (;;) {
        if (!directive_scan_buffered(&scan, &input)) {
            buffer.length = buffer.position_count = 0;
            if (!directive_scan_lexer(&scan, &buffer)) break;
            input = (DirectiveInput){.buffer = &buffer, .byte = buffer.fill_byte};
        }
        directive_shift(&buffer, input.index);
        buffer.carries = false;
        uint32_t start = input.byte;
        input = (DirectiveInput){.buffer = &buffer, .byte = start, .previous_byte = start, .before_previous_byte = start};
        bool more = directive_segment(state, &input);
        directive_prefixes(state, &input, &run, &tail);
        if (!more || buffer.failed) break;
        directive_scan_resume(&scan, &run, &tail);
    }
    directive_finish(state, &buffer, &run, &tail);
    return true;
}

// Restarts `scan` on ordinary code, continuing the run with a segment's trailing run, whose spans `tail` holds in byte
// order; the lists swap so that `run` holds them.
static void directive_scan_resume(DirectiveScan *scan, DirectiveSpans *run, DirectiveSpans *tail) {
    DirectiveSpans before = *run;
    *run = *tail;
    *tail = before;
    *scan = (DirectiveScan){.beginning = true, .run = run, .classes = scan->classes, .pragma = scan->pragma};
    if (!run->count) return;
    DirectiveSpan last = run->items[--run->count];
    scan->run_spans = run->count;
    scan->in_run = true;
    scan->run_start = last.start;
    scan->run_end = last.end;
}

// Completes the run's spans before the directive's `#` at `hash`, which follows bytes left out when `skipped`.
static void directive_scan_handoff(DirectiveScan *scan, uint32_t hash, bool skipped) {
    if (scan->in_run) directive_scan_close_run(scan, hash, skipped);
    scan->run->count = scan->in_run ? scan->run_spans : 0;
}

// Feeds the characters a directive segment's lookahead stored past its end. Returns true at a directive's `#`, which
// is left at `input`.
static bool directive_scan_buffered(DirectiveScan *scan, DirectiveInput *input) {
    while (input->index < input->buffer->length) {
        DirectiveInput before = *input;
        int32_t c = directive_physical_next(input);
        if (directive_scan_feed(scan, c)) {
            *input = before;
            directive_scan_handoff(scan, before.byte, before.byte != scan->run_end);
            return true;
        }
        directive_scan_run(scan, c, input->previous_byte, input->byte);
    }
    return false;
}

// Feeds the lexer's characters until a directive's `#`, which is left as the lookahead, or the end of the input.
static bool directive_scan_lexer(DirectiveScan *scan, DirectiveBuffer *buffer) {
    TSLexer *lexer = buffer->lexer;
    TSLexerPosition position;
    for (;;) {
        directive_scan_plain_run(scan, lexer);
        if (pragma_eof(lexer)) break;
        int32_t c = lexer->lookahead;
        if (directive_scan_feed(scan, c)) {
            lexer->get_position(lexer, &position);
            buffer->fill_byte = position.bytes;
            directive_scan_handoff(scan, position.bytes, lexer->is_at_included_range_start(lexer));
            return true;
        }
        directive_scan_lexer_run(scan, lexer, pragma_space(c) || c == '\\');
        lexer->advance(lexer, false);
    }
    lexer->get_position(lexer, &position);
    buffer->fill_byte = position.bytes;
    return false;
}

// Runs the line-state pass from a directive's `#` until a logical line ends with no directive state left and
// `directive_segment_ends` lets `DirectiveScan` take over again. Returns false at the end of the input.
static bool directive_segment(EmptyDirectives *state, DirectiveInput *input) {
    DirectiveLine line = {.beginning = true};
    for (;;) {
        // Horizontal spaces never change the line state.
        directive_plain_run(input, DIRECTIVE_RUN_SPACE);
        int32_t c = directive_next(input);
        if (c < 0) break;
        directive_line_character(state, input, &line, c);
        if (directive_line_clean(&line) && directive_segment_ends(input)) return true;
    }
    directive_complete_required(state, input, &line);
    if (line.pending) directive_record(state, input, &line, input->buffer->fill_byte);
    return false;
}

// A segment goes on over a following line that, after its indentation, starts another directive, is blank, or is a
// line comment that ends within `DIRECTIVE_FILL_UNITS` characters, because handing each short line over costs more than
// the line-state pass on it, until the segment holds `DIRECTIVE_SEGMENT_UNITS` characters. Code or another comment,
// which may run on for any length, goes back to `DirectiveScan`.
static bool directive_segment_ends(const DirectiveInput *input) {
    if (input->index >= DIRECTIVE_SEGMENT_UNITS) return true;
    uint32_t index = input->index, limit = input->index + DIRECTIVE_FILL_UNITS;
    while (index < limit && directive_has(input, index) && directive_horizontal(directive_character(input, index))) index++;
    if (index >= limit || !directive_has(input, index)) return true;
    uint16_t c = directive_character(input, index);
    return c != '#' && !directive_newline(c) && !directive_short_line_comment(input, index, limit);
}

// A line splice would continue the comment, so the line holds no `\`.
static bool directive_short_line_comment(const DirectiveInput *input, uint32_t index, uint32_t limit) {
    if (directive_character(input, index) != '/' || !directive_has(input, index + 1) ||
        directive_character(input, index + 1) != '/') return false;
    for (index += 2; index < limit && directive_has(input, index); index++) {
        uint16_t c = directive_character(input, index);
        if (c == '\\') return false;
        if (directive_newline(c)) return true;
    }
    return false;
}

static bool directive_line_clean(const DirectiveLine *line) {
    return line->beginning && !line->pending && !line->operand && !line->pragma_pending && !line->pragma_newline &&
        !line->pragma_name && !line->pragma_depth && !line->header && line->kind == DIRECTIVE_ORDINARY && !line->required_end;
}

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

// Without all the facts, none is kept, and a `_Pragma` operator may be anywhere.
static void directive_finish(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveSpans *run, DirectiveSpans *tail) {
    bool failed = buffer->failed || run->failed || tail->failed;
    free(run->items);
    free(tail->items);
    directive_release(buffer);
    state->pragmas = buffer->pragma || failed;
    if (failed) empty_directives_release(state);
}

static void empty_directives_release(EmptyDirectives *state) {
    uint8_t **directories[] = {state->starts, state->ends, state->comments, state->line_ends, state->directive_starts, state->directive_ends};
    for (unsigned i = 0; i < sizeof directories / sizeof *directories; i++) {
        if (!directories[i]) continue;
        for (uint32_t page = 0; page < state->pages; page++) free(directories[i][page]);
        free(directories[i]);
    }
    state->starts = state->ends = state->comments = state->line_ends = state->directive_starts = state->directive_ends = NULL;
    state->pages = 0;
}

// Marks the prefixes of the segment's null directives and the spaces before its directive line ends, reading the
// segment backwards from `input`, where its forward pass stopped. A prefix reaching the segment's `#` extends over the
// run before the segment, whose spans `prefix` holds. Stores in `tail`, in byte order, the spans of the segment's
// trailing run of spaces and backslashes after its last line end, over which a later null directive's prefix may extend.
static void directive_prefixes(EmptyDirectives *state, const DirectiveInput *input, const DirectiveSpans *prefix, DirectiveSpans *tail) {
    DirectiveCarry carry = {.tail = true};
    uint32_t byte = input->byte, position = input->position_index, width;
    bool complete = true;
    tail->count = 0;
    for (uint32_t i = input->index; i > 0;) {
        if (!carry.prefix && !carry.line_end && !carry.tail) i = directive_skip_inert(state, input, i, position, &byte);
        if (!i) break;
        int32_t c;
        complete = directive_step_back(input, &i, &position, &byte, &c, &width);
        if (!complete) break;
        directive_carry(state, input->buffer, &carry, c, byte);
        if (carry.tail) directive_carry_tail(&carry, tail, byte, width);
        else if (!input->buffer->carries) break;
    }
    if (carry.tail_open) directive_spans_push(tail, carry.tail_start, carry.tail_end);
    directive_spans_reverse(tail);
    if (complete && carry.prefix) directive_mark_spans(state, input->buffer, prefix);
}

static void directive_spans_reverse(DirectiveSpans *spans) {
    for (uint32_t i = 0; i < spans->count / 2; i++) {
        DirectiveSpan span = spans->items[i];
        spans->items[i] = spans->items[spans->count - 1 - i];
        spans->items[spans->count - 1 - i] = span;
    }
}

static void directive_mark_spans(EmptyDirectives *state, DirectiveBuffer *buffer, const DirectiveSpans *spans) {
    for (uint32_t i = 0; i < spans->count; i++) {
        for (uint32_t b = spans->items[i].start; b < spans->items[i].end; b++) directive_set(state, buffer, &state->starts, b);
    }
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

// Carries null-directive prefixes back from their `#` and line ends back from their barriers over the character `c` at
// `byte`, and whether the trailing run continues.
static void directive_carry(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveCarry *carry, int32_t c, uint32_t byte) {
    bool hash = c == '#' && directive_bit(state, state->starts, byte);
    bool condition_end = directive_bit(state, state->line_ends, byte);
    bool space = pragma_space(c) || c == '\\';
    carry->line_end = condition_end || (carry->line_end && directive_horizontal(c));
    if (carry->line_end) directive_set(state, buffer, &state->line_ends, byte);
    carry->prefix = !condition_end && (hash || (carry->prefix && space));
    if (carry->prefix) directive_set(state, buffer, &state->starts, byte);
    carry->tail = carry->tail && !condition_end && space;
}

// Extends the trailing run back over the character at `byte`, starting a span where an included range left bytes out.
static void directive_carry_tail(DirectiveCarry *carry, DirectiveSpans *tail, uint32_t byte, uint32_t width) {
    if (width > DIRECTIVE_CHARACTER_BYTES) width = DIRECTIVE_CHARACTER_BYTES;
    if (carry->tail_open && byte + width == carry->tail_start) {
        carry->tail_start = byte;
        return;
    }
    if (carry->tail_open) directive_spans_push(tail, carry->tail_start, carry->tail_end);
    carry->tail_start = byte;
    carry->tail_end = byte + width;
    carry->tail_open = true;
}

// Steps back from the character before `end` over characters that cannot change the reverse pass's state while no
// prefix or line end is carried: plain-width characters that are neither `#` nor barriers. Returns the new end.
static uint32_t directive_skip_inert(const EmptyDirectives *state, const DirectiveInput *input, uint32_t end, uint32_t position, uint32_t *byte) {
    const DirectiveBuffer *buffer = input->buffer;
    uint32_t stop = position ? buffer->positions[position - 1].index + 1 : 0, ascii_width = buffer->utf16 ? 2 : 1;
    while (end > stop) {
        uint32_t c = directive_character(input, end - 1);
        uint32_t width = c < 0x80 ? ascii_width : buffer->utf16 && (c < 0xd800 || c > 0xdfff) ? 2 : 0;
        if (!width || c == '#' || width > *byte) break;
        uint32_t previous = *byte - width;
        if (directive_bit(state, state->line_ends, previous)) break;
        *byte = previous;
        end--;
    }
    return end;
}

static void directive_set(EmptyDirectives *state, DirectiveBuffer *buffer, uint8_t ***facts, uint32_t byte) {
    if (buffer->failed) return;
    uint32_t page = byte >> DIRECTIVE_FACT_PAGE_SHIFT;
    if (page >= state->pages && !directive_grow_directories(state, state->pages * 2 > page ? state->pages * 2 : page + 1)) {
        buffer->failed = true;
        return;
    }
    if (!*facts && !(*facts = calloc(state->pages, sizeof **facts))) {
        buffer->failed = true;
        return;
    }
    uint8_t **bits = &(*facts)[page];
    if (!*bits && !(*bits = calloc(DIRECTIVE_FACT_PAGE_SIZE / 8, 1))) {
        buffer->failed = true;
        return;
    }
    uint32_t offset = byte & (DIRECTIVE_FACT_PAGE_SIZE - 1);
    (*bits)[offset / 8] |= 1u << (offset % 8);
}

static bool directive_bit(const EmptyDirectives *state, uint8_t *const *facts, uint32_t byte) {
    uint32_t page = byte >> DIRECTIVE_FACT_PAGE_SHIFT, offset = byte & (DIRECTIVE_FACT_PAGE_SIZE - 1);
    return facts && page < state->pages && facts[page] && (facts[page][offset / 8] & (1u << (offset % 8)));
}

static bool directive_grow_directories(EmptyDirectives *state, uint32_t pages) {
    uint8_t ***directories[] = {&state->starts, &state->ends, &state->comments, &state->line_ends, &state->directive_starts, &state->directive_ends};
    if ((size_t)pages > SIZE_MAX / sizeof(uint8_t *)) return false;
    for (unsigned i = 0; i < sizeof directories / sizeof *directories; i++) {
        if (!*directories[i]) continue;
        uint8_t **directory = realloc(*directories[i], (size_t)pages * sizeof *directory);
        if (!directory) return false;
        memset(directory + state->pages, 0, (size_t)(pages - state->pages) * sizeof *directory);
        *directories[i] = directory;
    }
    state->pages = pages;
    return true;
}

static void directive_line_character(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c) {
    if (c == '\n') {
        directive_line_end(state, input, line);
        return;
    }
    if (c == '/' && (directive_peek(input) == '/' || directive_peek(input) == '*')) {
        if (line->pending && !line->comment_start) {
            line->comment_start = input->previous_byte;
            input->spaced_splice = false;
        }
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
        line->comment_start = 0;
        directive_set(state, input->buffer, &state->directive_starts, line->start);
        line->beginning = false;
        line->pending = true;
        return;
    }
    if (directive_neutral(line)) directive_neutral_code(state, input, line, c);
    else directive_code(state, input, line, c);
}

// code-gauge-allow functionCognitiveComplexity -- Header names, quoted tokens and distinct keyword boundaries update one logical directive's lexical state.
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

// Outside a directive name, a line with no `_Pragma` or header operand in progress keeps its state through every token
// when it is of no directive kind, or of a kind that only waits for an operand it already has: its words need no
// classification.
static bool directive_neutral(const DirectiveLine *line) {
    bool settled = line->kind == DIRECTIVE_ORDINARY || (line->operand && directive_requires_operand(line->kind));
    return settled && !line->pending && !line->pragma_depth && !line->pragma_pending && !line->header;
}

// Also consumes the stored characters after the token that leave the line state as it is.
static void directive_neutral_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c) {
    line->operand = true;
    line->beginning = false;
    if (c == '"' || c == '\'') {
        if (directive_quoted(input, c)) {
            directive_line_end(state, input, line);
            return;
        }
    } else if (preproc_word(c, false) || (c >= '0' && c <= '9')) {
        directive_word(input, c, true, false);
    }
    directive_neutral_units(input);
}

// Consumes the tokens stored after `input` up to the line feed or to the first one that needs more than its units, as
// `directive_line_character` would on a neutral line: `directive_scan_units` reads a line with no directive state, and
// a neutral line keeps its state through every token.
static void directive_neutral_units(DirectiveInput *input) {
    const DirectiveBuffer *buffer = input->buffer;
    for (;;) {
        uint32_t index = input->index, end = buffer->length;
        if (input->position_index < buffer->position_count && buffer->positions[input->position_index].index < end) {
            end = buffer->positions[input->position_index].index;
        }
        uint32_t page_end = (index | (DIRECTIVE_PAGE_SIZE - 1)) + 1;
        if (page_end < end) end = page_end;
        if (index >= end) return;
        DirectiveScan scan = {.classes = buffer->classes};
        const uint16_t *units = buffer->characters[index >> DIRECTIVE_PAGE_SHIFT] + (index & (DIRECTIVE_PAGE_SIZE - 1));
        uint32_t length = directive_scan_units(&scan, (const char *)units, end - index, 2, 0, false).consumed;
        if (length) directive_advance_plain(input, length);
        if (length < end - index) return;
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
    while (directive_has(input, input->index)) {
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
    if (line->pending) directive_record(state, input, line, directive_byte(input));
    line->beginning = true;
    line->pending = false;
    bool required = line->kind == DIRECTIVE_CONDITION ||
        (directive_requires_operand(line->kind) && !line->operand && !line->pragma_pending);
    if (required) directive_barrier(state, input);
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
        directive_set(state, input->buffer, &state->directive_ends, line->required_end);
    }
    line->required_end = 0;
}

static bool directive_requires_operand(DirectiveKind kind) {
    return kind == DIRECTIVE_REQUIRED || kind == DIRECTIVE_UNDEF || kind == DIRECTIVE_LINE || kind == DIRECTIVE_HEADER;
}

static void directive_barrier(EmptyDirectives *state, DirectiveInput *input) {
    if (!input->index || !directive_newline(directive_character(input, input->index - 1))) return;
    input->buffer->carries = true;
    directive_set(state, input->buffer, &state->line_ends, input->previous_byte);
    if (input->index < 2) return;
    int32_t previous = directive_character(input, input->index - 2);
    if (directive_newline(previous) && previous != directive_character(input, input->index - 1)) {
        directive_set(state, input->buffer, &state->line_ends, input->before_previous_byte);
    }
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
    while (directive_has(input, input->index)) {
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
    while (directive_has(input, input->index)) {
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
    DirectiveBuffer *buffer = input->buffer;
    // Directives in a group that may be skipped need no operand, and the parser cannot tell which groups are skipped.
    if (directive_requires_operand(kind) && !buffer->conditional_depth) line->required_end = end;
    else directive_set(state, buffer, &state->directive_ends, end);
    if (word->length < sizeof word->directive) {
        if (strcmp(word->directive, "if") == 0 || strcmp(word->directive, "ifdef") == 0 || strcmp(word->directive, "ifndef") == 0) buffer->conditional_depth++;
        else if (strcmp(word->directive, "endif") == 0 && buffer->conditional_depth) buffer->conditional_depth--;
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
    while (directive_has(&candidate, candidate.index)) {
        if (directive_physical_next(&candidate) != ')') continue;
        DirectiveInput closing = candidate;
        unsigned matched = 0;
        while (matched < length && directive_has(&closing, closing.index) &&
            directive_physical_next(&closing) == delimiter[matched]) matched++;
        if (matched == length && directive_has(&closing, closing.index) && directive_physical_next(&closing) == '"') {
            candidate = closing;
            break;
        }
    }
    *input = candidate;
    return true;
}

static uint32_t directive_byte(const DirectiveInput *input) {
    return input->byte;
}

// A null directive with comments has two ends: before its first comment, marked in `comments` too, and after its line.
// It keeps its comments when a spaced splice follows their start, where the lexer would read them differently.
static void directive_record(EmptyDirectives *state, DirectiveInput *input, const DirectiveLine *line, uint32_t end) {
    input->buffer->carries = true;
    directive_set(state, input->buffer, &state->starts, line->start);
    directive_set(state, input->buffer, &state->ends, end);
    if (!line->comment_start || input->spaced_splice) return;
    directive_set(state, input->buffer, &state->ends, line->comment_start);
    directive_set(state, input->buffer, &state->comments, line->comment_start);
}

#endif
