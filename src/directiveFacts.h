#ifndef TREE_SITTER_C_DIRECTIVE_FACTS_H_
#define TREE_SITTER_C_DIRECTIVE_FACTS_H_

#include "directiveBuffer.h"
#include <stdlib.h>

#define DIRECTIVE_FACT_PAGE_SHIFT 15
#define DIRECTIVE_FACT_PAGE_SIZE (1 << DIRECTIVE_FACT_PAGE_SHIFT)

typedef struct {
    uint32_t *items;
    uint32_t count, capacity;
} DirectiveOffsets;

typedef struct {
    uint32_t index, byte, end_byte;
} DirectiveBarrier;

typedef struct {
    DirectiveBarrier *items;
    uint32_t count, capacity;
} DirectiveBarriers;

// Null-directive positions (`starts`, `ends`, `comments`) are directories of `pages` pointers, one per
// `DIRECTIVE_FACT_PAGE_SIZE` source bytes, to bitset pages allocated only when a null directive exists; directive
// boundaries (`directive_starts`, `directive_ends`) and condition line-end runs (`line_ends`) are sorted byte arrays.
// `empty_directives_release` frees them.
typedef struct {
    uint8_t **starts, **ends, **comments;
    uint32_t pages;
    DirectiveOffsets directive_starts, directive_ends;
    DirectiveBarriers barriers;
    DirectiveSpans line_ends;
    // `anchored` and `ready` belong to the scanner state: whether the token at the start of the input is out, and
    // whether the facts, which `built` says exist, are in use. `pragmas` is the fact that the input read for them holds
    // `pragma_word`, without which no scan has to look for a `_Pragma` operator. `spaced_splices` enables C++23
    // `[lex.phases]/2` horizontal-whitespace line splices inside directive universal character names.
    bool built, anchored, ready, pragmas, failed, spaced_splices;
} EmptyDirectives;

typedef struct {
    TSSymbol include, define, preproc_if, ifdef, ifndef, preproc_else, elif, elifdef, elifndef, endif, pragma, directive;
} PreprocDirectiveSymbols;

static bool empty_directives_skip_prefix(TSLexer *lexer);
static bool empty_directives_scan_word_step(TSLexer *lexer, char *word, unsigned capacity, unsigned *length, bool *trailing_splice, TSLexerPosition *position);
static bool empty_directives_ucn_follows(TSLexer *lexer, bool continuation);
static bool empty_directives_ucn_follows_splices(TSLexer *lexer, bool continuation, bool spaced_splices);
static bool empty_directives_skip_splices(TSLexer *lexer, bool spaced_splices);
static bool empty_directives_skip_line_splice(TSLexer *lexer, bool spaced_splices);
static inline bool empty_directives_name_character(int32_t c, bool continuation);
static bool directive_word_continues(DirectiveInput *input, bool number, bool strict_identifier, bool *in_identifier, int32_t *previous);
static bool directive_consume_ucn(DirectiveInput *input);
static bool directive_ucn(DirectiveInput *input, bool continuation);
static TSSymbol empty_directives_keyword_symbol(const char *word, const PreprocDirectiveSymbols *symbols);
static bool empty_directives_line_end_at(const EmptyDirectives *state, uint32_t byte);
static bool directive_offset_has(const DirectiveOffsets *offsets, uint32_t byte);
static void directive_offset_push(DirectiveOffsets *offsets, DirectiveBuffer *buffer, uint32_t byte);
static void directive_barrier_push(DirectiveBarriers *barriers, DirectiveBuffer *buffer, uint32_t index, uint32_t byte, uint32_t end_byte);
static bool empty_directives_consume(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix);
static bool empty_directives_start_at(const EmptyDirectives *state, uint32_t byte);
static void empty_directives_consume_started(TSLexer *lexer, EmptyDirectives *state, bool skip_prefix);
static void directive_mark_spans(EmptyDirectives *state, DirectiveBuffer *buffer, const DirectiveSpans *spans, uint8_t ***facts);
static void directive_set(EmptyDirectives *state, DirectiveBuffer *buffer, uint8_t ***facts, uint32_t byte);
static void directive_set_range(EmptyDirectives *state, DirectiveBuffer *buffer, uint8_t ***facts, uint64_t start, uint64_t end);
static void directive_set_page_bits(uint8_t *bits, uint32_t b0, uint32_t b1);
static bool directive_bit(const EmptyDirectives *state, uint8_t *const *facts, uint32_t byte);
static bool directive_grow_directories(EmptyDirectives *state, uint32_t pages);

static inline bool empty_directives_has_directives(const EmptyDirectives *state) {
    return !state || state->failed || state->directive_starts.count > 0;
}

static bool empty_directives_has_keyword_end(const EmptyDirectives *state, uint32_t keyword_end) {
    if (!state) return false;
    return state->failed || directive_offset_has(&state->directive_ends, keyword_end);
}

// Scans `#`, any directive prefix spacing or block comments, and the ASCII directive name into `word`, marking the
// token end at the name's last character and storing the post-keyword cursor byte in `*keyword_end`.
static bool empty_directives_scan_word(TSLexer *lexer, char *word, unsigned capacity, uint32_t *keyword_end) {
    lexer->advance(lexer, false);
    if (!empty_directives_skip_prefix(lexer) || !empty_directives_name_character(lexer->lookahead, false)) return false;
    unsigned length = 0;
    bool trailing_splice = false;
    TSLexerPosition position = {0};
    while (empty_directives_scan_word_step(lexer, word, capacity, &length, &trailing_splice, &position)) {}
    if (!trailing_splice) lexer->get_position(lexer, &position);
    word[length < capacity ? length : 0] = '\0';
    *keyword_end = position.bytes;
    return true;
}

static bool empty_directives_scan_word_step(TSLexer *lexer, char *word, unsigned capacity, unsigned *length, bool *trailing_splice, TSLexerPosition *position) {
    int32_t c = lexer->lookahead;
    if (empty_directives_name_character(c, true)) {
        if (*length < capacity) word[(*length)++] = (char)c;
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        *trailing_splice = false;
        return true;
    }
    if (c != '\\') {
        if (preproc_identifier_character(c, true)) *length = capacity;
        return false;
    }
    if (!*trailing_splice) lexer->get_position(lexer, position);
    *trailing_splice = true;
    lexer->advance(lexer, false);
    if (scan_preproc_newline(lexer, false)) return true;
    if (empty_directives_ucn_follows(lexer, true)) *length = capacity;
    return false;
}

static bool empty_directives_ucn_follows(TSLexer *lexer, bool continuation) {
    return empty_directives_ucn_follows_splices(lexer, continuation, false);
}

static bool empty_directives_ucn_follows_splices(TSLexer *lexer, bool continuation, bool spaced_splices) {
    if (!empty_directives_skip_splices(lexer, spaced_splices)) return false;
    int32_t prefix = lexer->lookahead;
    if (prefix != 'u' && prefix != 'U' && prefix != 'N') return false;
    lexer->advance(lexer, false);
    if (!empty_directives_skip_splices(lexer, spaced_splices)) return false;
    bool braced = prefix != 'U' && lexer->lookahead == '{';
    DirectiveUcn ucn;
    if (!directive_ucn_start(&ucn, prefix, braced)) return false;
    if (braced) lexer->advance(lexer, false);
    for (bool done = false;;) {
        if (!empty_directives_skip_splices(lexer, spaced_splices)) return false;
        if (!directive_ucn_feed(&ucn, lexer->lookahead, continuation, &done)) return false;
        lexer->advance(lexer, false);
        if (done) return true;
    }
}

static bool empty_directives_skip_splices(TSLexer *lexer, bool spaced_splices) {
    while (lexer->lookahead == '\\') {
        lexer->advance(lexer, false);
        if (!empty_directives_skip_line_splice(lexer, spaced_splices)) return false;
    }
    return true;
}

static bool empty_directives_skip_line_splice(TSLexer *lexer, bool spaced_splices) {
    if (spaced_splices) {
        while (directive_horizontal(lexer->lookahead)) lexer->advance(lexer, false);
    }
    return scan_preproc_newline(lexer, false);
}

static inline bool empty_directives_name_character(int32_t c, bool continuation) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (continuation && c == '_');
}

static bool empty_directives_skip_prefix(TSLexer *lexer) {
    for (;;) {
        int32_t c = lexer->lookahead;
        if (directive_horizontal(c)) {
            lexer->advance(lexer, false);
        } else if (c == '\\') {
            lexer->advance(lexer, false);
            if (!scan_preproc_newline(lexer, false)) return false;
        } else if (c == '/') {
            lexer->advance(lexer, false);
            if (!scan_preproc_splices(lexer) || lexer->lookahead != '*' || !scan_preproc_block_comment(lexer)) return false;
        } else {
            return true;
        }
    }
}

static bool empty_directives_select_symbol(const char *word, const PreprocDirectiveSymbols *symbols, const bool *valid_symbols, TSSymbol *result) {
    TSSymbol specific = empty_directives_keyword_symbol(word, symbols);
    if (valid_symbols[specific]) {
        *result = specific;
        return true;
    }
    bool can_fallback = specific == symbols->directive || specific == symbols->define ||
        specific == symbols->include || specific == symbols->pragma;
    if (can_fallback && valid_symbols[symbols->directive]) {
        *result = symbols->directive;
        return true;
    }
    return false;
}

// code-gauge-allow functionCognitiveComplexity -- Maps each fixed directive keyword spelling to its external token slot.
static TSSymbol empty_directives_keyword_symbol(const char *word, const PreprocDirectiveSymbols *symbols) {
    switch (word[0]) {
        case 'd':
            return strcmp(word, "define") == 0 ? symbols->define : symbols->directive;
        case 'e':
            if (strcmp(word, "else") == 0) return symbols->preproc_else;
            if (strcmp(word, "elif") == 0) return symbols->elif;
            if (strcmp(word, "elifdef") == 0) return symbols->elifdef;
            if (strcmp(word, "elifndef") == 0) return symbols->elifndef;
            if (strcmp(word, "endif") == 0) return symbols->endif;
            return symbols->directive;
        case 'i':
            if (strcmp(word, "if") == 0) return symbols->preproc_if;
            if (strcmp(word, "ifdef") == 0) return symbols->ifdef;
            if (strcmp(word, "ifndef") == 0) return symbols->ifndef;
            if (strcmp(word, "include") == 0) return symbols->include;
            return symbols->directive;
        case 'p':
            return strcmp(word, "pragma") == 0 ? symbols->pragma : symbols->directive;
        default:
            return symbols->directive;
    }
}

// False only when the facts show that no `_Pragma` operator can follow any position.
static inline bool empty_directives_pragma_possible(const EmptyDirectives *state) {
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
    if (!state) return false;
    if (state->failed) return true;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return directive_offset_has(&state->directive_starts, position.bytes);
}

static inline bool empty_directives_at_line_end(const EmptyDirectives *state, TSLexer *lexer) {
    if (!state || !state->line_ends.count || !(directive_horizontal(lexer->lookahead) || directive_newline(lexer->lookahead))) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return empty_directives_line_end_at(state, position.bytes);
}

static bool empty_directives_line_end_at(const EmptyDirectives *state, uint32_t byte) {
    const DirectiveSpans *spans = &state->line_ends;
    if (!spans->count || byte < spans->items[0].start || byte >= spans->items[spans->count - 1].end) return false;
    uint32_t low = 0, high = spans->count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        DirectiveSpan span = spans->items[mid];
        if (byte < span.start) high = mid;
        else if (byte >= span.end) low = mid + 1;
        else return true;
    }
    return false;
}

static bool directive_offset_has(const DirectiveOffsets *offsets, uint32_t byte) {
    if (!offsets->count || byte < offsets->items[0] || byte > offsets->items[offsets->count - 1]) return false;
    uint32_t low = 0, high = offsets->count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        uint32_t item = offsets->items[mid];
        if (item == byte) return true;
        if (item < byte) low = mid + 1;
        else high = mid;
    }
    return false;
}

static void directive_offset_push(DirectiveOffsets *offsets, DirectiveBuffer *buffer, uint32_t byte) {
    if (buffer->failed || (offsets->count && offsets->items[offsets->count - 1] == byte)) return;
    if (!directive_grow_items((void **)&offsets->items, offsets->count, &offsets->capacity, sizeof(*offsets->items))) {
        buffer->failed = true;
        return;
    }
    offsets->items[offsets->count++] = byte;
}

static void directive_barrier_push(DirectiveBarriers *barriers, DirectiveBuffer *buffer, uint32_t index, uint32_t byte, uint32_t end_byte) {
    if (buffer->failed || (barriers->count && barriers->items[barriers->count - 1].byte == byte)) return;
    if (!directive_grow_items((void **)&barriers->items, barriers->count, &barriers->capacity, sizeof(*barriers->items))) {
        buffer->failed = true;
        return;
    }
    barriers->items[barriers->count++] = (DirectiveBarrier){index, byte, end_byte};
}

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

static void empty_directives_release(EmptyDirectives *state) {
    uint8_t **directories[] = {state->starts, state->ends, state->comments};
    for (unsigned i = 0; i < sizeof directories / sizeof *directories; i++) {
        if (!directories[i]) continue;
        for (uint32_t page = 0; page < state->pages; page++) free(directories[i][page]);
        free(directories[i]);
    }
    state->starts = state->ends = state->comments = NULL;
    state->pages = 0;
    free(state->directive_starts.items);
    free(state->directive_ends.items);
    free(state->barriers.items);
    free(state->line_ends.items);
    state->directive_starts = state->directive_ends = (DirectiveOffsets){0};
    state->barriers = (DirectiveBarriers){0};
    state->line_ends = (DirectiveSpans){0};
}

static void directive_mark_spans(EmptyDirectives *state, DirectiveBuffer *buffer, const DirectiveSpans *spans, uint8_t ***facts) {
    for (uint32_t i = 0; i < spans->count; i++) {
        directive_set_range(state, buffer, facts, spans->items[i].start, spans->items[i].end);
    }
}

static void directive_set(EmptyDirectives *state, DirectiveBuffer *buffer, uint8_t ***facts, uint32_t byte) {
    directive_set_range(state, buffer, facts, byte, (uint64_t)byte + 1);
}

static void directive_set_range(EmptyDirectives *state, DirectiveBuffer *buffer, uint8_t ***facts, uint64_t start, uint64_t end) {
    if (end > (uint64_t)UINT32_MAX + 1) end = (uint64_t)UINT32_MAX + 1;
    if (buffer->failed || start >= end) return;
    uint32_t last_page = (uint32_t)((end - 1) >> DIRECTIVE_FACT_PAGE_SHIFT);
    if (last_page >= state->pages &&
        !directive_grow_directories(state, state->pages * 2 > last_page ? state->pages * 2 : last_page + 1)) {
        buffer->failed = true;
        return;
    }
    if (!*facts && !(*facts = calloc(state->pages, sizeof **facts))) {
        buffer->failed = true;
        return;
    }
    for (uint64_t byte = start; byte < end;) {
        uint32_t page = (uint32_t)(byte >> DIRECTIVE_FACT_PAGE_SHIFT);
        uint8_t **bits = &(*facts)[page];
        if (!*bits && !(*bits = calloc(DIRECTIVE_FACT_PAGE_SIZE / 8, 1))) {
            buffer->failed = true;
            return;
        }
        uint64_t page_end = ((uint64_t)page + 1) << DIRECTIVE_FACT_PAGE_SHIFT;
        if (page_end > end) page_end = end;
        directive_set_page_bits(*bits, (uint32_t)byte & (DIRECTIVE_FACT_PAGE_SIZE - 1),
            (uint32_t)(page_end - ((uint64_t)page << DIRECTIVE_FACT_PAGE_SHIFT)));
        byte = page_end;
    }
}

static void directive_set_page_bits(uint8_t *bits, uint32_t b0, uint32_t b1) {
    uint32_t w0 = b0 >> 3, w1 = (b1 - 1) >> 3;
    uint8_t start_mask = (uint8_t)(0xffu << (b0 & 7)), end_mask = (uint8_t)(0xffu >> (7 - ((b1 - 1) & 7)));
    if (w0 == w1) {
        bits[w0] |= start_mask & end_mask;
        return;
    }
    bits[w0] |= start_mask;
    if (w1 > w0 + 1) memset(bits + w0 + 1, 0xff, w1 - w0 - 1);
    bits[w1] |= end_mask;
}

static bool directive_bit(const EmptyDirectives *state, uint8_t *const *facts, uint32_t byte) {
    uint32_t page = byte >> DIRECTIVE_FACT_PAGE_SHIFT, offset = byte & (DIRECTIVE_FACT_PAGE_SIZE - 1);
    return facts && page < state->pages && facts[page] && (facts[page][offset / 8] & (1u << (offset % 8)));
}

static bool directive_grow_directories(EmptyDirectives *state, uint32_t pages) {
    uint8_t ***directories[] = {&state->starts, &state->ends, &state->comments};
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

static bool directive_word_continues(DirectiveInput *input, bool number, bool strict_identifier, bool *in_identifier, int32_t *previous) {
    if (strict_identifier && *in_identifier && directive_consume_ucn(input)) {
        *previous = 0x80;
        return true;
    }
    int32_t c = directive_peek(input);
    bool exponent_sign = number && (c == '+' || c == '-') &&
        (*previous == 'e' || *previous == 'E' || *previous == 'p' || *previous == 'P');
    if (!preproc_word(c, true) && !(number && (c == '.' || c == '\'' || exponent_sign))) return false;
    if (strict_identifier && !(c != '\\' && preproc_identifier_character(c, true))) *in_identifier = false;
    *previous = directive_next(input);
    return true;
}

static bool directive_consume_ucn(DirectiveInput *input) {
    if (directive_peek(input) != '\\') return false;
    bool had_spaced = input->spaced_splice;
    DirectiveInput copy = *input;
    copy.spaced_splice = false;
    directive_next(&copy);
    if ((!copy.buffer->spaced_splices && copy.spaced_splice) || !directive_ucn(&copy, true)) return false;
    copy.spaced_splice = had_spaced || copy.spaced_splice;
    *input = copy;
    return true;
}

static bool directive_ucn(DirectiveInput *input, bool continuation) {
    bool had_spaced = input->spaced_splice;
    DirectiveInput copy = *input;
    copy.spaced_splice = false;
    int32_t prefix = directive_next(&copy);
    bool braced = prefix != 'U' && (prefix == 'u' || prefix == 'N') && directive_peek(&copy) == '{';
    DirectiveUcn ucn;
    if (!directive_ucn_start(&ucn, prefix, braced)) return false;
    if (braced) directive_next(&copy);
    for (bool done = false;;) {
        if (!directive_ucn_feed(&ucn, directive_next(&copy), continuation, &done)) return false;
        if (done) break;
    }
    if (!copy.buffer->spaced_splices && copy.spaced_splice) return false;
    copy.spaced_splice = had_spaced || copy.spaced_splice;
    *input = copy;
    return true;
}

#endif
