// code-gauge-allow-file fileNcss -- One source-fact pass shares directive lexing, line-state tracking, and reverse prefix carry.
#ifndef TREE_SITTER_C_EMPTY_DIRECTIVES_H_
#define TREE_SITTER_C_EMPTY_DIRECTIVES_H_

#include "directiveFacts.h"

typedef struct {
    uint32_t start, end;
    bool open;
} DirectiveOpenSpan;

typedef enum { DIRECTIVE_ORDINARY, DIRECTIVE_CONDITION, DIRECTIVE_DIAGNOSTIC, DIRECTIVE_REQUIRED, DIRECTIVE_UNDEF, DIRECTIVE_LINE, DIRECTIVE_PRAGMA, DIRECTIVE_HEADER } DirectiveKind;

// The reverse pass's state over a segment's condition line ends and trailing trivia run.
typedef struct {
    bool prefix, line_end, tail;
    DirectiveOpenSpan line_end_span, tail_span;
} DirectiveCarry;

typedef struct {
    bool beginning, pending, operand, pragma_pending, pragma_newline, pragma_name;
    unsigned pragma_depth, header;
    DirectiveKind kind;
    // `comment_start` is the first comment after a pending `#`, or 0.
    uint32_t start, comment_start, required_end;
} DirectiveLine;

typedef struct {
    GnuRawStringPrefix raw;
    char directive[13];
    unsigned length;
} DirectiveWord;

typedef struct {
    const char *units;
    uint32_t count, size, base_byte;
} DirectiveUnitsSlice;

typedef struct {
    uint32_t hash, kw_end, nl;
    bool barrier;
} DirectivePlainOutcome;

static bool directive_scan_buffered(DirectiveScan *scan, DirectiveInput *input);
static bool directive_scan_lexer(EmptyDirectives *state, DirectiveScan *scan, DirectiveBuffer *buffer);
static bool directive_scan_lexer_plain_lines(EmptyDirectives *state, DirectiveScan *scan, DirectiveBuffer *buffer);
static uint32_t directive_units_skip_blank_lines(DirectiveUnitsSlice slice, uint32_t *index_io);
static void directive_scan_finish_plain_lines(DirectiveScan *scan, TSLexer *lexer, DirectiveUnitsSlice slice, uint32_t consumed, uint32_t line_feeds, DirectivePlainOutcome last);
static bool directive_units_plain_line(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveUnitsSlice slice, uint32_t *index_io, DirectivePlainOutcome *out);
static bool directive_units_plain_keyword(DirectiveUnitsSlice slice, uint32_t *index_io, DirectiveWord *word);
static bool directive_units_record_plain(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveUnitsSlice slice, const DirectiveWord *word, DirectiveKind kind, uint32_t hash, uint32_t kw_end, uint32_t nl, bool has_operand);
static bool directive_units_plain_header(const char *units, uint32_t size, uint32_t *index_io, uint32_t count, bool *has_operand);
static bool directive_units_plain_tail(const char *units, uint32_t size, uint32_t i, uint32_t count, DirectiveKind kind, uint32_t *nl_out, bool *has_operand);
static bool directive_units_plain_comment(const char *units, uint32_t size, uint32_t i, uint32_t count, uint32_t *nl_out);
static void directive_scan_resume(DirectiveScan *scan, DirectiveSpans *run, DirectiveSpans *tail);
static void directive_scan_handoff(DirectiveScan *scan, uint32_t hash, bool skipped);
static bool directive_segment(EmptyDirectives *state, DirectiveInput *input);
static bool directive_segment_ends(const DirectiveInput *input);
static bool directive_lexer_segment_ends(const TSLexer *lexer);
static bool directive_units_short_comment(const char *units, uint32_t size, uint32_t i, uint32_t limit);
static bool directive_short_line_comment(const DirectiveInput *input, uint32_t index, uint32_t limit);
static bool directive_line_clean(const DirectiveLine *line);
static void directive_finish(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveSpans *run, DirectiveSpans *tail);
static void directive_prefixes(EmptyDirectives *state, const DirectiveInput *input, const DirectiveSpans *prefix, DirectiveSpans *tail);
static bool directive_prefixes_done(const EmptyDirectives *state, const DirectiveBuffer *buffer, const DirectiveCarry *carry);
static void directive_carry(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveCarry *carry, int32_t c, uint32_t byte, uint32_t span_width);
static void directive_open_span_extend(DirectiveOpenSpan *span, DirectiveSpans *spans, uint32_t byte, uint32_t width);
static void directive_open_span_flush(DirectiveOpenSpan *span, DirectiveSpans *spans);
static void directive_spans_reverse_from(DirectiveSpans *spans, uint32_t from);
static uint32_t directive_skip_inert(const EmptyDirectives *state, const DirectiveInput *input, uint32_t end, uint32_t *position, uint32_t *byte);
static uint32_t directive_inert_width(uint32_t c, bool utf16);
static void directive_barrier(EmptyDirectives *state, DirectiveInput *input);
static void directive_line_end(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static void directive_complete_required(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static bool directive_requires_operand(DirectiveKind kind);
static void directive_line_character(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static void directive_start_hash(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static bool directive_try_plain_directive(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);
static void directive_record_plain_directive(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, const DirectiveWord *word, DirectiveKind kind, uint32_t kw_end_byte, uint32_t nl, bool has_operand);
static bool directive_scan_plain_header(const uint16_t *page, uint32_t *index_io, uint32_t end, bool *has_operand);
static bool directive_scan_plain_tail(const uint16_t *page, uint32_t i, uint32_t end, DirectiveKind kind, uint32_t *nl_out, bool *has_operand);
static bool directive_scan_plain_comment(const uint16_t *page, uint32_t i, uint32_t end, uint32_t *nl_out);
static void directive_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static bool directive_pragma_part(DirectiveLine *line, int32_t c);
static bool directive_neutral(const DirectiveLine *line);
static bool directive_skip_plain_line(DirectiveInput *input, int32_t first, bool condition);
static uint32_t directive_plain_line_end(const DirectiveInput *input);
static bool directive_plain_condition_step(const uint16_t *page, uint32_t i, uint32_t end, bool *after_lparen, uint16_t *prev);
static void directive_neutral_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c);
static void directive_neutral_units(DirectiveInput *input);
static bool directive_header_ready(DirectiveLine *line, int32_t c);
static bool directive_header(DirectiveInput *input, int32_t close);
static void directive_line_comment(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line);

static bool empty_directives_build(EmptyDirectives *state, TSLexer *lexer);
static void directive_pragma_name(DirectiveLine *line, const DirectiveInput *input);
static bool directive_pragma_operator(const DirectiveInput *input);
static int32_t directive_pragma_token(DirectiveInput *input);
static bool directive_comment(DirectiveInput *input, bool *line);
static bool directive_quoted(DirectiveInput *input, int32_t quote);
static bool directive_raw(DirectiveInput *input);

static uint32_t directive_keyword_end(const DirectiveInput *input);
static DirectiveKind directive_keyword(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, const DirectiveWord *word, uint32_t end);
static DirectiveWord directive_word(DirectiveInput *input, int32_t first, bool raw, bool classify);
static void directive_word_run(DirectiveInput *input, DirectiveWord *word, bool classify);
static DirectiveKind directive_word_kind(const DirectiveWord *word);
static void directive_word_character(DirectiveWord *word, int32_t c);
static void directive_word_role(DirectiveLine *line, DirectiveKind kind, bool name, bool pragma, bool had_operand, bool identifier);
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
            if (!directive_scan_lexer(state, &scan, &buffer)) break;
            input = (DirectiveInput){.buffer = &buffer, .byte = buffer.fill_byte};
        }
        directive_shift(&buffer, input.index);
        buffer.carries = false;
        state->barriers.count = 0;
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
static bool directive_scan_lexer(EmptyDirectives *state, DirectiveScan *scan, DirectiveBuffer *buffer) {
    TSLexer *lexer = buffer->lexer;
    TSLexerPosition position;
    for (;;) {
        directive_scan_plain_run(scan, lexer);
        if (pragma_eof(lexer)) break;
        int32_t c = lexer->lookahead;
        if (c == '#' && scan->beginning && !scan->splice && !scan->pair &&
            directive_scan_lexer_plain_lines(state, scan, buffer)) {
            continue;
        }
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

// Scans consecutive plain ASCII directive lines directly from `peek_units` without buffering or running the reverse pass.
static bool directive_scan_lexer_plain_lines(EmptyDirectives *state, DirectiveScan *scan, DirectiveBuffer *buffer) {
    TSLexer *lexer = buffer->lexer;
    if (lexer->is_at_included_range_start(lexer) || directive_pragma_pending(scan->recent)) return false;
    DirectiveUnitsSlice slice = {0};
    slice.units = lexer->peek_units(lexer, &slice.count, &slice.size);
    if (slice.count < 4) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    slice.base_byte = position.bytes;
    uint32_t i = 0, consumed = 0, line_feeds = 0;
    DirectivePlainOutcome last = {0};
    for (;;) {
        uint32_t line_start = i;
        uint32_t extra_lf = consumed ? directive_units_skip_blank_lines(slice, &i) : 0;
        if (i >= slice.count || directive_unit(slice.units, slice.size, i) != '#' ||
            !directive_units_plain_line(state, buffer, slice, &i, &last)) {
            i = line_start;
            break;
        }
        line_feeds += extra_lf + 1;
        consumed = i;
    }
    if (!consumed) return false;
    directive_scan_finish_plain_lines(scan, lexer, slice, consumed, line_feeds, last);
    return true;
}

static uint32_t directive_units_skip_blank_lines(DirectiveUnitsSlice slice, uint32_t *index_io) {
    uint32_t i = *index_io, extra_lf = 0;
    for (; i < slice.count; i++) {
        uint32_t ch = directive_unit(slice.units, slice.size, i);
        if (directive_horizontal((int32_t)ch)) continue;
        if (ch == '\n' && i + 1 < slice.count && directive_unit(slice.units, slice.size, i + 1) != '\r') {
            extra_lf++;
            continue;
        }
        break;
    }
    *index_io = i;
    return extra_lf;
}

static void directive_scan_finish_plain_lines(DirectiveScan *scan, TSLexer *lexer, DirectiveUnitsSlice slice, uint32_t consumed, uint32_t line_feeds, DirectivePlainOutcome last) {
    scan->mode = SCAN_CODE;
    scan->beginning = true;
    scan->recent = 0;
    scan->run_spans = 0;
    if (last.barrier) {
        scan->in_run = false;
    } else {
        uint32_t run = last.nl;
        while (run > last.kw_end && directive_horizontal((int32_t)directive_unit(slice.units, slice.size, run - 1))) run--;
        scan->in_run = true;
        scan->run_start = slice.base_byte + run * slice.size;
        scan->run_end = slice.base_byte + (consumed - 1) * slice.size + 1;
    }
    lexer->advance_units(lexer, consumed, line_feeds, 0);
}

static bool directive_units_plain_line(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveUnitsSlice slice, uint32_t *index_io, DirectivePlainOutcome *out) {
    uint32_t hash = *index_io, i = hash + 1;
    DirectiveWord word = {0};
    if (!directive_units_plain_keyword(slice, &i, &word)) return false;
    uint32_t kw_end = i, nl = 0;
    DirectiveKind kind = directive_word_kind(&word);
    bool has_operand = false;
    if (kind == DIRECTIVE_HEADER && !directive_units_plain_header(slice.units, slice.size, &i, slice.count, &has_operand)) return false;
    if (!directive_units_plain_tail(slice.units, slice.size, i, slice.count, kind, &nl, &has_operand)) return false;
    bool barrier = directive_units_record_plain(state, buffer, slice, &word, kind, hash, kw_end, nl, has_operand);
    *index_io = nl + 1;
    *out = (DirectivePlainOutcome){.hash = hash, .kw_end = kw_end, .nl = nl, .barrier = barrier};
    return true;
}

static bool directive_units_plain_keyword(DirectiveUnitsSlice slice, uint32_t *index_io, DirectiveWord *word) {
    uint32_t i = *index_io;
    while (i < slice.count && directive_horizontal((int32_t)directive_unit(slice.units, slice.size, i))) i++;
    if (i >= slice.count || !empty_directives_name_character((int32_t)directive_unit(slice.units, slice.size, i), false)) return false;
    while (i < slice.count && empty_directives_name_character((int32_t)directive_unit(slice.units, slice.size, i), true)) {
        directive_word_character(word, (int32_t)directive_unit(slice.units, slice.size, i++));
    }
    if (i >= slice.count || word->length >= sizeof word->directive) return false;
    uint32_t sep = directive_unit(slice.units, slice.size, i);
    if (sep >= 0x80 || sep == '\\' || preproc_word((int32_t)sep, true)) return false;
    *index_io = i;
    return true;
}

// code-gauge-allow functionParameterCount -- Commits starts, conditional depth, ends, and line-end spans for one unbuffered directive.
static bool directive_units_record_plain(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveUnitsSlice slice, const DirectiveWord *word, DirectiveKind kind, uint32_t hash, uint32_t kw_end, uint32_t nl, bool has_operand) {
    directive_offset_push(&state->directive_starts, buffer, slice.base_byte + hash * slice.size);
    bool ignored = buffer->conditional_depth > 0;
    if (strcmp(word->directive, "if") == 0 || strcmp(word->directive, "ifdef") == 0 || strcmp(word->directive, "ifndef") == 0) buffer->conditional_depth++;
    else if (strcmp(word->directive, "endif") == 0 && buffer->conditional_depth) buffer->conditional_depth--;
    if (!directive_requires_operand(kind) || ignored || has_operand) {
        directive_offset_push(&state->directive_ends, buffer, slice.base_byte + kw_end * slice.size);
    }
    bool barrier = kind == DIRECTIVE_CONDITION || (directive_requires_operand(kind) && !has_operand);
    if (barrier) {
        uint32_t sp = nl;
        while (sp > kw_end && directive_horizontal((int32_t)directive_unit(slice.units, slice.size, sp - 1))) sp--;
        directive_spans_push(&state->line_ends, slice.base_byte + sp * slice.size, slice.base_byte + (nl + 1) * slice.size);
    }
    return barrier;
}

static bool directive_units_plain_header(const char *units, uint32_t size, uint32_t *index_io, uint32_t count, bool *has_operand) {
    uint32_t i = *index_io;
    while (i < count && directive_horizontal((int32_t)directive_unit(units, size, i))) i++;
    uint32_t open = i < count ? directive_unit(units, size, i) : 0;
    if (open != '<' && open != '"') return false;
    uint32_t close = open == '<' ? '>' : '"';
    for (i++; i < count && directive_unit(units, size, i) != close; i++) {
        uint32_t c = directive_unit(units, size, i);
        if (c >= 0x80 || c == '\\' || directive_newline((int32_t)c)) return false;
    }
    if (i >= count) return false;
    *index_io = i + 1;
    *has_operand = true;
    return true;
}

// code-gauge-allow functionCognitiveComplexity -- Validates plain-tail exclusions up to an unpaired `\n` in one unit pass.
// code-gauge-allow functionParameterCount -- Scans a unit slice from `i` to `count` and outputs newline index and operand presence.
static bool directive_units_plain_tail(const char *units, uint32_t size, uint32_t i, uint32_t count, DirectiveKind kind, uint32_t *nl_out, bool *has_operand) {
    bool after_lparen = false;
    uint32_t prev = 0;
    for (; i < count; i++) {
        uint32_t c = directive_unit(units, size, i);
        if (c == '\n') {
            if (i + 1 >= count || directive_unit(units, size, i + 1) == '\r') return false;
            *nl_out = i;
            return true;
        }
        if (c == '/') return directive_units_plain_comment(units, size, i, count, nl_out);
        if (c >= 0x80 || c == '\r' || c == '"' || c == '\'' || c == '\\') return false;
        if (prev == '_' && c == 'P' && (i + 1 >= count || directive_unit(units, size, i + 1) == 'r')) return false;
        if (kind == DIRECTIVE_CONDITION && c == '<' && after_lparen) return false;
        if (!directive_horizontal((int32_t)c)) {
            *has_operand = true;
            after_lparen = c == '(';
        }
        prev = c;
    }
    return false;
}

static bool directive_units_plain_comment(const char *units, uint32_t size, uint32_t i, uint32_t count, uint32_t *nl_out) {
    if (i + 1 >= count || directive_unit(units, size, i + 1) != '/') return false;
    for (i += 2; i < count; i++) {
        uint32_t c = directive_unit(units, size, i);
        if (c == '\n') {
            if (i + 1 >= count || directive_unit(units, size, i + 1) == '\r') return false;
            *nl_out = i;
            return true;
        }
        if (c >= 0x80 || c == '\r' || c == '\\') return false;
    }
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
    if (input->index == input->buffer->length) return directive_lexer_segment_ends(input->buffer->lexer);
    uint32_t index = input->index, limit = input->index + DIRECTIVE_FILL_UNITS;
    while (index < limit && directive_has(input, index) && directive_horizontal(directive_character(input, index))) index++;
    if (index >= limit || !directive_has(input, index)) return true;
    uint16_t c = directive_character(input, index);
    return c != '#' && !directive_newline(c) && !directive_short_line_comment(input, index, limit);
}

static bool directive_lexer_segment_ends(const TSLexer *lexer) {
    uint32_t count, size;
    const char *units = lexer->peek_units(lexer, &count, &size);
    if (!count) {
        int32_t c = lexer->lookahead;
        return c != '#' && !directive_newline(c);
    }
    uint32_t limit = count < DIRECTIVE_FILL_UNITS ? count : DIRECTIVE_FILL_UNITS;
    uint32_t i = 0;
    while (i < limit && directive_horizontal((int32_t)directive_unit(units, size, i))) i++;
    if (i >= limit) return true;
    uint32_t c = directive_unit(units, size, i);
    return c != '#' && !directive_newline((int32_t)c) && !directive_units_short_comment(units, size, i, limit);
}

static bool directive_units_short_comment(const char *units, uint32_t size, uint32_t i, uint32_t limit) {
    if (directive_unit(units, size, i) != '/' || i + 1 >= limit || directive_unit(units, size, i + 1) != '/') return false;
    for (i += 2; i < limit; i++) {
        uint32_t c = directive_unit(units, size, i);
        if (c >= 0x80 || c == '\\') return false;
        if (directive_newline((int32_t)c)) return true;
    }
    return false;
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

// Without all the facts, none is kept, and a `_Pragma` operator or `#` directive may be anywhere.
static void directive_finish(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveSpans *run, DirectiveSpans *tail) {
    bool failed = buffer->failed || run->failed || tail->failed || state->line_ends.failed;
    free(run->items);
    free(tail->items);
    free(state->barriers.items);
    state->barriers = (DirectiveBarriers){0};
    directive_release(buffer);
    state->pragmas = buffer->pragma || failed;
    state->failed = failed;
    if (failed) empty_directives_release(state);
}

// Marks the prefixes of the segment's null directives and the spaces before its directive line ends, reading the
// segment backwards from `input`, where its forward pass stopped. A prefix reaching the segment's `#` extends over the
// run before the segment, whose spans `prefix` holds. Stores in `tail`, in byte order, the spans of the segment's
// trailing run of spaces and backslashes after its last line end, over which a later directive's prefix may extend.
// code-gauge-allow functionCognitiveComplexity -- Carries null-directive prefixes, condition line ends, and trailing trivia runs in one backward pass.
static void directive_prefixes(EmptyDirectives *state, const DirectiveInput *input, const DirectiveSpans *prefix, DirectiveSpans *tail) {
    DirectiveCarry carry = {.tail = true};
    uint32_t byte = input->byte, position = input->position_index, width;
    uint32_t line_ends_base = state->line_ends.count;
    uint32_t unit_bytes = input->buffer->encoding_known && input->buffer->utf16 ? 2u : 1u;
    bool complete = true;
    tail->count = 0;
    for (uint32_t i = input->index; i > 0;) {
        if (!carry.prefix && !carry.line_end && !carry.tail) {
            i = directive_skip_inert(state, input, i, &position, &byte);
        }
        if (!i) break;
        int32_t c;
        complete = directive_step_back(input, &i, &position, &byte, &c, &width);
        if (!complete) break;
        uint32_t span_width = width < unit_bytes ? width : unit_bytes;
        directive_carry(state, input->buffer, &carry, c, byte, span_width ? span_width : 1u);
        if (carry.tail) directive_open_span_extend(&carry.tail_span, tail, byte, width);
        else if (directive_prefixes_done(state, input->buffer, &carry)) break;
    }
    directive_open_span_flush(&carry.line_end_span, &state->line_ends);
    directive_spans_reverse_from(&state->line_ends, line_ends_base);
    directive_open_span_flush(&carry.tail_span, tail);
    directive_spans_reverse_from(tail, 0);
    state->barriers.count = 0;
    if (complete && carry.prefix) directive_mark_spans(state, input->buffer, prefix, &state->starts);
}

static bool directive_prefixes_done(const EmptyDirectives *state, const DirectiveBuffer *buffer, const DirectiveCarry *carry) {
    return !buffer->carries || (!carry->prefix && !carry->line_end && !state->barriers.count && !state->starts);
}

static void directive_spans_reverse_from(DirectiveSpans *spans, uint32_t from) {
    uint32_t length = spans->count - from;
    for (uint32_t i = 0; i < length / 2; i++) {
        DirectiveSpan span = spans->items[from + i];
        spans->items[from + i] = spans->items[spans->count - 1 - i];
        spans->items[spans->count - 1 - i] = span;
    }
}

// Carries null-directive prefixes back from their `#` and line ends back from their barriers over the character `c` at
// `byte`, and whether the trailing run continues.
static void directive_carry(EmptyDirectives *state, DirectiveBuffer *buffer, DirectiveCarry *carry, int32_t c, uint32_t byte, uint32_t span_width) {
    while (state->barriers.count && state->barriers.items[state->barriers.count - 1].byte > byte) state->barriers.count--;
    bool condition_end = state->barriers.count && state->barriers.items[state->barriers.count - 1].byte == byte;
    if (condition_end) state->barriers.count--;
    bool hash = c == '#' && directive_bit(state, state->starts, byte);
    bool space = pragma_space(c) || c == '\\';
    carry->line_end = condition_end || (carry->line_end && directive_horizontal(c));
    if (carry->line_end) directive_open_span_extend(&carry->line_end_span, &state->line_ends, byte, span_width);
    else directive_open_span_flush(&carry->line_end_span, &state->line_ends);
    carry->prefix = !condition_end && (hash || (carry->prefix && space));
    if (carry->prefix) directive_set(state, buffer, &state->starts, byte);
    carry->tail = carry->tail && !condition_end && space;
}

// Extends `span` back over the character at `byte`, starting a new span in `spans` where bytes were left out.
static void directive_open_span_extend(DirectiveOpenSpan *span, DirectiveSpans *spans, uint32_t byte, uint32_t width) {
    if (width > DIRECTIVE_CHARACTER_BYTES) width = DIRECTIVE_CHARACTER_BYTES;
    if (span->open && byte + width == span->start) {
        span->start = byte;
        return;
    }
    directive_open_span_flush(span, spans);
    span->start = byte;
    span->end = byte + width;
    span->open = true;
}

static void directive_open_span_flush(DirectiveOpenSpan *span, DirectiveSpans *spans) {
    if (!span->open) return;
    directive_spans_push(spans, span->start, span->end);
    span->open = false;
}

// Steps back from the character before `end` over characters that cannot change the reverse pass's state while no
// prefix or line end is carried: plain-width characters that are neither `#` nor barriers. Returns the new end.
// code-gauge-allow functionCognitiveComplexity -- Jumps directly to the next barrier when no null directives exist and steps over inert units otherwise.
static uint32_t directive_skip_inert(const EmptyDirectives *state, const DirectiveInput *input, uint32_t end, uint32_t *position, uint32_t *byte) {
    const DirectiveBuffer *buffer = input->buffer;
    if (!state->starts) {
        if (!state->barriers.count) return 0;
        DirectiveBarrier target = state->barriers.items[state->barriers.count - 1];
        if (target.index + 1 < end) {
            *byte = target.end_byte;
            while (*position && buffer->positions[*position - 1].index > target.index) (*position)--;
            return target.index + 1;
        }
        return end;
    }
    uint32_t stop = *position ? buffer->positions[*position - 1].index + 1 : 0;
    bool has_barrier = state->barriers.count > 0;
    uint32_t barrier_byte = has_barrier ? state->barriers.items[state->barriers.count - 1].byte : 0;
    while (end > stop) {
        uint32_t width = directive_inert_width(directive_character(input, end - 1), buffer->utf16);
        if (!width || width > *byte) break;
        uint32_t previous = *byte - width;
        if (has_barrier && previous <= barrier_byte) break;
        *byte = previous;
        end--;
    }
    return end;
}

static uint32_t directive_inert_width(uint32_t c, bool utf16) {
    if (c == '#') return 0;
    if (c < 0x80) return utf16 ? 2 : 1;
    return utf16 && (c < 0xd800 || c > 0xdfff) ? 2 : 0;
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
        directive_start_hash(state, input, line);
        return;
    }
    if (directive_neutral(line)) {
        directive_neutral_code(state, input, line, c);
        return;
    }
    if (!line->pending && !line->pragma_depth && !line->pragma_pending && !line->header &&
        line->kind == DIRECTIVE_CONDITION && directive_skip_plain_line(input, c, true)) {
        line->operand = true;
        line->beginning = false;
        return;
    }
    directive_code(state, input, line, c);
}

static void directive_start_hash(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line) {
    line->pragma_pending = line->pragma_newline = false;
    line->pragma_depth = 0;
    line->kind = DIRECTIVE_ORDINARY;
    line->operand = false;
    line->start = input->previous_byte;
    line->comment_start = 0;
    directive_offset_push(&state->directive_starts, input->buffer, line->start);
    if (directive_try_plain_directive(state, input, line)) return;
    line->beginning = false;
    line->pending = true;
}

// Fast-paths an entire single-line ASCII directive (`#define`, `#include`, `#if`, `#ifdef`, `#endif`, etc.) that
// contains no block comments, non-header quotes, splices, or `_Pragma` tokens.
// code-gauge-allow functionCognitiveComplexity -- Validates keyword, optional header path, and plain tail in one pass.
// code-gauge-allow functionNcss -- Keeps the single-page fast path self-contained.
static bool directive_try_plain_directive(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line) {
    DirectiveBuffer *buffer = input->buffer;
    if (!buffer->encoding_known) return false;
    uint32_t index = input->index, end = directive_plain_line_end(input);
    if (index >= end) return false;
    const uint16_t *page = buffer->characters[index >> DIRECTIVE_PAGE_SHIFT];
    uint32_t mask = DIRECTIVE_PAGE_SIZE - 1, i = index;
    while (i < end && directive_horizontal(page[i & mask])) i++;
    if (i >= end || !empty_directives_name_character(page[i & mask], false)) return false;
    DirectiveWord word = {0};
    while (i < end && empty_directives_name_character(page[i & mask], true)) directive_word_character(&word, page[i++ & mask]);
    if (i >= end || word.length >= sizeof word.directive) return false;
    uint16_t sep = page[i & mask];
    if (sep >= 0x80 || sep == '\\' || preproc_word(sep, true)) return false;
    DirectiveKind kind = directive_word_kind(&word);
    uint32_t kw_end_byte = input->byte + (i - index) * (buffer->utf16 ? 2u : 1u);
    bool has_operand = false;
    if (kind == DIRECTIVE_HEADER && !directive_scan_plain_header(page, &i, end, &has_operand)) return false;
    uint32_t nl = 0;
    if (!directive_scan_plain_tail(page, i, end, kind, &nl, &has_operand)) return false;
    directive_record_plain_directive(state, input, line, &word, kind, kw_end_byte, nl, has_operand);
    return true;
}

// code-gauge-allow functionParameterCount -- Commits the parsed keyword, depth, advance, and barrier of one plain directive line.
static void directive_record_plain_directive(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, const DirectiveWord *word, DirectiveKind kind, uint32_t kw_end_byte, uint32_t nl, bool has_operand) {
    DirectiveBuffer *buffer = input->buffer;
    bool ignored = buffer->conditional_depth > 0;
    if (strcmp(word->directive, "if") == 0 || strcmp(word->directive, "ifdef") == 0 || strcmp(word->directive, "ifndef") == 0) buffer->conditional_depth++;
    else if (strcmp(word->directive, "endif") == 0 && buffer->conditional_depth) buffer->conditional_depth--;
    if (!directive_requires_operand(kind) || ignored || has_operand) directive_offset_push(&state->directive_ends, buffer, kw_end_byte);
    directive_advance_plain(input, nl - input->index);
    directive_next(input);
    line->beginning = true;
    line->pending = false;
    if (kind == DIRECTIVE_CONDITION || (directive_requires_operand(kind) && !has_operand)) directive_barrier(state, input);
}

static bool directive_scan_plain_header(const uint16_t *page, uint32_t *index_io, uint32_t end, bool *has_operand) {
    uint32_t mask = DIRECTIVE_PAGE_SIZE - 1, i = *index_io;
    while (i < end && directive_horizontal(page[i & mask])) i++;
    if (i >= end || (page[i & mask] != '<' && page[i & mask] != '"')) return false;
    uint16_t close = page[i & mask] == '<' ? '>' : '"';
    for (i++; i < end && page[i & mask] != close; i++) {
        uint16_t c = page[i & mask];
        if (c >= 0x80 || c == '\\' || directive_newline(c)) return false;
    }
    if (i >= end) return false;
    *index_io = i + 1;
    *has_operand = true;
    return true;
}

// code-gauge-allow functionCognitiveComplexity -- Validates plain-tail exclusions and trailing `//` comment in one pass.
static bool directive_scan_plain_tail(const uint16_t *page, uint32_t i, uint32_t end, DirectiveKind kind, uint32_t *nl_out, bool *has_operand) {
    uint32_t mask = DIRECTIVE_PAGE_SIZE - 1;
    bool after_lparen = false;
    uint16_t prev = 0;
    for (; i < end; i++) {
        uint16_t c = page[i & mask];
        if (directive_newline(c)) {
            *nl_out = i;
            return true;
        }
        if (c == '/') return directive_scan_plain_comment(page, i, end, nl_out);
        if (c >= 0x80 || c == '"' || c == '\'' || c == '\\') return false;
        if (prev == '_' && c == 'P' && (i + 1 >= end || page[(i + 1) & mask] == 'r')) return false;
        if (kind == DIRECTIVE_CONDITION && c == '<' && after_lparen) return false;
        if (!directive_horizontal(c)) {
            *has_operand = true;
            after_lparen = c == '(';
        }
        prev = c;
    }
    return false;
}

static bool directive_scan_plain_comment(const uint16_t *page, uint32_t i, uint32_t end, uint32_t *nl_out) {
    uint32_t mask = DIRECTIVE_PAGE_SIZE - 1;
    if (i + 1 >= end || page[(i + 1) & mask] != '/') return false;
    for (i += 2; i < end; i++) {
        uint16_t c = page[i & mask];
        if (directive_newline(c)) {
            *nl_out = i;
            return true;
        }
        if (c >= 0x80 || c == '\\') return false;
    }
    return false;
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
// when it is of no directive kind, or of a kind that only waits for an operand it already has.
static bool directive_neutral(const DirectiveLine *line) {
    if (line->pending || line->pragma_depth || line->pragma_pending || line->header) return false;
    return line->kind == DIRECTIVE_ORDINARY || (line->operand && directive_requires_operand(line->kind));
}

// Advances to the buffered newline when the rest of the line is plain ASCII with no quote, splice, block comment, or
// condition-changing `<` or `_Pragma`.
// code-gauge-allow functionCognitiveComplexity -- Checks all plain-line exclusions in one pass to the buffered newline.
static bool directive_skip_plain_line(DirectiveInput *input, int32_t first, bool condition) {
    if ((uint32_t)first >= 0x80u || first == '"' || first == '\'' || first == '\\' || (condition && first == '<')) return false;
    uint32_t index = input->index, end = directive_plain_line_end(input);
    if (index >= end) return false;
    const DirectiveBuffer *buffer = input->buffer;
    const uint16_t *page = buffer->characters[index >> DIRECTIVE_PAGE_SHIFT];
    if (first == '/' && page[index & (DIRECTIVE_PAGE_SIZE - 1)] == '*') return false;
    bool after_lparen = first == '(';
    uint16_t prev = (uint16_t)first;
    for (uint32_t i = index; i < end; i++) {
        uint16_t c = page[i & (DIRECTIVE_PAGE_SIZE - 1)];
        if (directive_newline(c)) {
            if (i > index) directive_advance_plain(input, i - index);
            return true;
        }
        if (c >= 0x80 || c == '"' || c == '\'' || c == '\\') return false;
        if (c == '/' && (i + 1 >= end || page[(i + 1) & (DIRECTIVE_PAGE_SIZE - 1)] == '*')) return false;
        if (condition && !directive_plain_condition_step(page, i, end, &after_lparen, &prev)) return false;
    }
    return false;
}

static uint32_t directive_plain_line_end(const DirectiveInput *input) {
    if (!directive_has(input, input->index)) return input->index;
    const DirectiveBuffer *buffer = input->buffer;
    uint32_t index = input->index, end = buffer->length;
    if (input->position_index < buffer->position_count && buffer->positions[input->position_index].index < end) {
        end = buffer->positions[input->position_index].index;
    }
    uint32_t page_end = (index | (DIRECTIVE_PAGE_SIZE - 1)) + 1;
    return page_end < end ? page_end : end;
}

static bool directive_plain_condition_step(const uint16_t *page, uint32_t i, uint32_t end, bool *after_lparen, uint16_t *prev) {
    uint32_t mask = DIRECTIVE_PAGE_SIZE - 1;
    uint16_t c = page[i & mask];
    if (c == '<' && *after_lparen) return false;
    if (*prev == '_' && c == 'P' && (i + 1 >= end || page[(i + 1) & mask] == 'r')) return false;
    if (!directive_horizontal(c)) *after_lparen = c == '(';
    *prev = c;
    return true;
}

// Also consumes the stored characters after the token that leave the line state as it is.
static void directive_neutral_code(EmptyDirectives *state, DirectiveInput *input, DirectiveLine *line, int32_t c) {
    line->operand = true;
    line->beginning = false;
    if (directive_skip_plain_line(input, c, false)) return;
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
        directive_offset_push(&state->directive_ends, input->buffer, line->required_end);
    }
    line->required_end = 0;
}

static bool directive_requires_operand(DirectiveKind kind) {
    return kind == DIRECTIVE_REQUIRED || kind == DIRECTIVE_UNDEF || kind == DIRECTIVE_LINE || kind == DIRECTIVE_HEADER;
}

static void directive_barrier(EmptyDirectives *state, DirectiveInput *input) {
    if (!input->index || !directive_newline(directive_character(input, input->index - 1))) return;
    input->buffer->carries = true;
    if (input->index >= 2) {
        int32_t previous = directive_character(input, input->index - 2);
        if (directive_newline(previous) && previous != directive_character(input, input->index - 1)) {
            directive_barrier_push(&state->barriers, input->buffer, input->index - 2, input->before_previous_byte, input->previous_byte);
        }
    }
    directive_barrier_push(&state->barriers, input->buffer, input->index - 1, input->previous_byte, input->byte);
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
    else directive_offset_push(&state->directive_ends, buffer, end);
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

// code-gauge-allow functionCognitiveComplexity -- Dispatches fixed directive keywords by length instead of scanning every table entry.
// code-gauge-allow functionNcss -- Length-bucketed dispatch keeps all fixed directive spellings in one switch.
static DirectiveKind directive_word_kind(const DirectiveWord *word) {
    switch (word->length) {
        case 2:
            return memcmp(word->directive, "if", 2) == 0 ? DIRECTIVE_CONDITION : DIRECTIVE_ORDINARY;
        case 4:
            if (memcmp(word->directive, "elif", 4) == 0) return DIRECTIVE_CONDITION;
            return memcmp(word->directive, "line", 4) == 0 ? DIRECTIVE_LINE : DIRECTIVE_ORDINARY;
        case 5:
            if (memcmp(word->directive, "ifdef", 5) == 0) return DIRECTIVE_REQUIRED;
            if (memcmp(word->directive, "undef", 5) == 0) return DIRECTIVE_UNDEF;
            if (memcmp(word->directive, "embed", 5) == 0) return DIRECTIVE_HEADER;
            return memcmp(word->directive, "error", 5) == 0 ? DIRECTIVE_DIAGNOSTIC : DIRECTIVE_ORDINARY;
        case 6:
            return memcmp(word->directive, "define", 6) == 0 || memcmp(word->directive, "ifndef", 6) == 0
                ? DIRECTIVE_REQUIRED : DIRECTIVE_ORDINARY;
        case 7:
            if (memcmp(word->directive, pragma_word, 7) == 0) return DIRECTIVE_PRAGMA;
            if (memcmp(word->directive, "include", 7) == 0) return DIRECTIVE_HEADER;
            if (memcmp(word->directive, "elifdef", 7) == 0) return DIRECTIVE_REQUIRED;
            return memcmp(word->directive, "warning", 7) == 0 ? DIRECTIVE_DIAGNOSTIC : DIRECTIVE_ORDINARY;
        case 8:
            return memcmp(word->directive, "elifndef", 8) == 0 ? DIRECTIVE_REQUIRED : DIRECTIVE_ORDINARY;
        case 12:
            return memcmp(word->directive, "include_next", 12) == 0 ? DIRECTIVE_HEADER : DIRECTIVE_ORDINARY;
        default:
            return DIRECTIVE_ORDINARY;
    }
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
