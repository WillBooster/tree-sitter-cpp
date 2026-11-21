// code-gauge-allow-file fileNcss 437 -- The C scanner's extras and directive dispatch index this grammar's external-token enum, so they live beside the C++ tokens.
#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"
#include "conditionalMacro.h"
#include "emptyDirectives.h"
#include "initializerConditional.h"
#include "cppTrivia.h"

#include <stdint.h>
#include <string.h>

enum TokenType { RAW_STRING_DELIMITER, RAW_STRING_CONTENT, PACK_INDEX_OPERATOR, TEMPLATE_CLOSE, SPLIT_TEMPLATE_CLOSE, PRAGMA_OPERATOR, PREPROC_ARG, PREPROC_NEWLINE, PREPROC_LPAREN, PREPROC_DIRECTIVE_ARG, PREPROC_FUNCTION_NAME, FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_SUFFIX, TRAILING_MACRO_ATTRIBUTE_NAME, TRAILING_OBJECT_DECLARATION_START, CONDITIONAL_MARKER_IFDEF, CONDITIONAL_MARKER_IFNDEF, LEADING_CONDITIONAL_MARKER_IFDEF, LEADING_CONDITIONAL_MARKER_IFNDEF, GNU_RAW_STRING_BODY, RAW_MACRO_TYPE_TAIL, BRACED_FUNCTION_ATTRIBUTE_START, BRACED_MACRO_TYPE_START, DIRECTIVE_CONTEXT, PREPROC_EMPTY, PREPROC_DIRECTIVE_START, MACRO_CALL_END, MACRO_IDENTIFIER_END, EMPTY_INITIALIZER_START, INITIALIZER_DIRECTIVE_NAME };

/// C++ [lex.string] limits raw-string d-char-sequences to 16 characters.
#define MAX_DELIMITER_LENGTH 16

/// A trivia run from which no macro line ends, for the macro-line tokens that `valid` marks (bit 0 for the call end, bit 1
/// for the identifier end); every scan starting inside it reaches the same token with no more newlines, so it fails too.
/// It depends only on the source text, so it is kept across scanner states.
typedef struct {
    uint32_t start, end;
    uint8_t valid;
} MacroLineMiss;

typedef struct {
    EmptyDirectives directives;
    InitializerConditionals initializer_conditionals;
    bool pending_template_close;
    uint32_t pending_macro_identifiers;
    /// The byte after the last null directive scanned, which ends a line like a newline; zero when none.
    uint32_t null_directive_end;
    MacroLineMiss macro_line_miss;
    uint8_t delimiter_length;
    int32_t delimiter[MAX_DELIMITER_LENGTH];
} Scanner;

/// Words that cannot continue a declaration that a conditional marker starts.
static const char *const reserved_words[] = {"namespace", "using", "export", "module", "import"};
#define RESERVED_WORD_COUNT (sizeof(reserved_words) / sizeof(reserved_words[0]))

static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_raw_string_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_raw_string_delimiter(Scanner *scanner, TSLexer *lexer);
static bool scan_raw_string_content(Scanner *scanner, TSLexer *lexer);
static bool scan_macro_statement_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_macro_line_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, uint32_t start, uint32_t *end);
static inline bool is_macro_call_continuation(int32_t c);
static bool scan_macro_identifier_boundary(Scanner *scanner, TSLexer *lexer);
static bool only_extras_valid(const EmptyDirectives *state, const bool *valid_symbols);
static bool scan_extras(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols);
static bool scan_extras_pragma(EmptyDirectives *state, TSLexer *lexer, const TSLexerCheckpoint *start, bool skipped);
static uint32_t extras_start_byte(TSLexer *lexer, const TSLexerCheckpoint *start, bool skipped);
static bool scan_directive_context(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols);
static bool scan_preprocessor_token(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols);
static bool scan_spaced_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close);
static bool scan_preproc_tokens(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols);
static bool scan_initializer_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_pack_index_operator(Scanner *scanner, TSLexer *lexer);
static bool skip_pack_index_whitespace(TSLexer *lexer, bool skip);
static bool scan_template_close(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close);
static bool skip_template_splices(TSLexer *lexer, bool skip);
static bool scan_declaration_macro(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_declaration_attribute(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_macro_name_rest(TSLexer *lexer, MacroName *name, bool has_name);
static inline void reset(Scanner *scanner);
static inline void advance(TSLexer *lexer);

void *tree_sitter_cpp_external_scanner_create(void) {
    Scanner *scanner = ts_calloc(1, sizeof(Scanner));
    if (scanner) scanner->initializer_conditionals.directives = &scanner->directives;
    return scanner;
}

void tree_sitter_cpp_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    EmptyDirectives *state = &scanner->directives;
    free(state->starts);
    free(state->ends);
    free(state->line_ends);
    free(state->directive_starts);
    free(state->directive_ends);
    delete_initializer_conditionals(&scanner->initializer_conditionals);
    ts_free(scanner);
}

enum { SERIALIZED_READY = 1, SERIALIZED_PENDING_TEMPLATE_CLOSE = 2, SERIALIZED_PREFIX = 1 + 2 * sizeof(uint32_t) };

unsigned tree_sitter_cpp_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    _Static_assert(SERIALIZED_PREFIX + MAX_DELIMITER_LENGTH * sizeof(int32_t) < TREE_SITTER_SERIALIZATION_BUFFER_SIZE,
                   "Serialized scanner state is too long!");
    if (!scanner->pending_template_close && !scanner->pending_macro_identifiers && !scanner->null_directive_end &&
        !scanner->delimiter_length) {
        buffer[0] = scanner->directives.ready ? SERIALIZED_READY : 0;
        return 1;
    }
    size_t size = scanner->delimiter_length * sizeof(int32_t);
    buffer[0] = (scanner->directives.ready ? SERIALIZED_READY : 0) |
        (scanner->pending_template_close ? SERIALIZED_PENDING_TEMPLATE_CLOSE : 0);
    memcpy(buffer + 1, &scanner->pending_macro_identifiers, sizeof(uint32_t));
    memcpy(buffer + 1 + sizeof(uint32_t), &scanner->null_directive_end, sizeof(uint32_t));
    memcpy(buffer + SERIALIZED_PREFIX, scanner->delimiter, size);
    return (unsigned)(size + SERIALIZED_PREFIX);
}

void tree_sitter_cpp_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    reset(scanner);
    scanner->directives.ready = length && (buffer[0] & SERIALIZED_READY);
    size_t prefix = SERIALIZED_PREFIX;
    if (length < prefix || length - prefix > sizeof(scanner->delimiter) ||
        (length - prefix) % sizeof(int32_t) != 0 ||
        (unsigned char)buffer[0] > (SERIALIZED_READY | SERIALIZED_PENDING_TEMPLATE_CLOSE)) return;
    scanner->pending_template_close = buffer[0] & SERIALIZED_PENDING_TEMPLATE_CLOSE;
    memcpy(&scanner->pending_macro_identifiers, buffer + 1, sizeof(uint32_t));
    memcpy(&scanner->null_directive_end, buffer + 1 + sizeof(uint32_t), sizeof(uint32_t));
    scanner->delimiter_length = (length - prefix) / sizeof(int32_t);
    memcpy(scanner->delimiter, buffer + prefix, length - prefix);
}

bool tree_sitter_cpp_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    bool found = scan_token(scanner, lexer, valid_symbols);
    if (!found) return false;
    if (lexer->result_symbol == PREPROC_EMPTY) {
        // A macro-identifier count already looked past this directive.
        TSLexerPosition end;
        lexer->get_mark_end(lexer, &end);
        scanner->null_directive_end = end.bytes;
        return true;
    }
    if (lexer->result_symbol != MACRO_IDENTIFIER_END) scanner->pending_macro_identifiers = 0;
    else scanner->pending_template_close = false;
    return true;
}

static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    EmptyDirectives *state = &scanner->directives;
    if (only_extras_valid(state, valid_symbols)) return scan_extras(state, lexer, valid_symbols);
    if (scan_directive_context(state, lexer, valid_symbols)) return true;
    if (valid_symbols[RAW_STRING_DELIMITER] || valid_symbols[RAW_STRING_CONTENT]) {
        return scan_raw_string_token(scanner, lexer, valid_symbols);
    }
    if (valid_symbols[MACRO_CALL_END] || valid_symbols[MACRO_IDENTIFIER_END]) {
        return scan_macro_statement_end(scanner, lexer, valid_symbols);
    }
    bool pending_close = scanner->pending_template_close;
    scanner->pending_template_close = false;
    if (empty_directives_at_line_end(state, lexer)) return false;
    bool raw_macro_type_tail = valid_symbols[RAW_MACRO_TYPE_TAIL] && !valid_symbols[PREPROC_LPAREN];
    if (raw_macro_type_tail || valid_symbols[PREPROC_ARG] || valid_symbols[PREPROC_NEWLINE] ||
        valid_symbols[PREPROC_LPAREN] || valid_symbols[PREPROC_DIRECTIVE_ARG] || valid_symbols[PREPROC_FUNCTION_NAME]) {
        return scan_preprocessor_token(state, lexer, valid_symbols);
    }
    return scan_spaced_token(scanner, lexer, valid_symbols, pending_close);
}

static bool scan_preprocessor_token(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[RAW_MACRO_TYPE_TAIL] && !valid_symbols[PREPROC_LPAREN]) {
        lexer->result_symbol = RAW_MACRO_TYPE_TAIL;
        return scan_raw_macro_type_tail(lexer, &(PragmaTrivia){empty_directives_skip, state});
    }
    if (valid_symbols[PREPROC_FUNCTION_NAME] && !valid_symbols[PREPROC_LPAREN]) {
        lexer->result_symbol = PREPROC_FUNCTION_NAME;
        return scan_function_macro_name(lexer, valid_symbols[PRAGMA_OPERATOR], PRAGMA_OPERATOR);
    }
    return scan_preproc_tokens(state, lexer, valid_symbols);
}

static bool scan_spaced_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close) {
    if (pending_close) {
        if (!skip_template_splices(lexer, true)) return false;
        pending_close = lexer->lookahead == '>';
    }
    if (!skip_pack_index_whitespace(lexer, true)) return false;
    if ((valid_symbols[EMPTY_INITIALIZER_START] || valid_symbols[INITIALIZER_DIRECTIVE_NAME]) && lexer->lookahead == '#') {
        return scan_initializer_token(scanner, lexer, valid_symbols);
    }
    if (valid_symbols[PACK_INDEX_OPERATOR] && lexer->lookahead == '.') {
        return scan_pack_index_operator(scanner, lexer);
    }
    if ((valid_symbols[TEMPLATE_CLOSE] || valid_symbols[SPLIT_TEMPLATE_CLOSE]) && lexer->lookahead == '>') {
        return scan_template_close(scanner, lexer, valid_symbols, pending_close);
    }
    return scan_declaration_macro(scanner, lexer, valid_symbols);
}

static bool only_extras_valid(const EmptyDirectives *state, const bool *valid_symbols) {
    return state->ready && !valid_symbols[PREPROC_DIRECTIVE_START] &&
        !memchr(valid_symbols, true, PRAGMA_OPERATOR * sizeof(*valid_symbols)) &&
        !memchr(valid_symbols + PREPROC_ARG, true, (DIRECTIVE_CONTEXT - PREPROC_ARG) * sizeof(*valid_symbols)) &&
        !memchr(valid_symbols + MACRO_CALL_END, true,
            (INITIALIZER_DIRECTIVE_NAME + 1 - MACRO_CALL_END) * sizeof(*valid_symbols));
}

// `starts` marks only `#`, `\`, and spaces/splices before a null directive's `#`, and `line_ends` marks only spaces and
// newlines, so `extras_start_byte` restores the pre-space checkpoint via `mark_end_at` only after `#`/`\` (when `starts`
// exists) or `_Pragma` (when skipped spaces may start at `line_ends`), avoiding checkpoint/column work on other tokens.
static bool scan_extras(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols) {
    bool allow_empty = valid_symbols[PREPROC_EMPTY] && state->starts;
    bool allow_pragma = valid_symbols[PRAGMA_OPERATOR];
    if (!allow_empty && !allow_pragma) return false;
    bool skipped = pragma_space(lexer->lookahead);
    TSLexerCheckpoint start = {0};
    if (skipped && (allow_empty || state->line_ends)) lexer->get_checkpoint(lexer, &start);
    while (pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
    int32_t c = lexer->lookahead;
    if (c == '_') return allow_pragma && scan_extras_pragma(state, lexer, &start, skipped);
    if (!allow_empty || (c != '#' && c != '\\')) return false;
    if (!empty_directives_start_at(state, extras_start_byte(lexer, &start, skipped))) return false;
    empty_directives_consume_started(lexer, state, true);
    lexer->mark_end(lexer);
    lexer->result_symbol = PREPROC_EMPTY;
    return true;
}

static bool scan_extras_pragma(EmptyDirectives *state, TSLexer *lexer, const TSLexerCheckpoint *start, bool skipped) {
    if (!scan_pragma_word(lexer)) return false;
    if (skipped && state->line_ends && empty_directives_line_end_at(state, extras_start_byte(lexer, start, true))) {
        return false;
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return scan_pragma_suffix_end_with(lexer, true, &(PragmaTrivia){empty_directives_skip, state});
}

static uint32_t extras_start_byte(TSLexer *lexer, const TSLexerCheckpoint *start, bool skipped) {
    TSLexerPosition position;
    if (skipped) {
        lexer->mark_end_at(lexer, start);
        lexer->get_mark_end(lexer, &position);
    } else {
        lexer->get_position(lexer, &position);
    }
    return position.bytes;
}

static bool scan_directive_context(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols) {
    if (!state->ready && valid_symbols[DIRECTIVE_CONTEXT]) {
        lexer->result_symbol = DIRECTIVE_CONTEXT;
        return empty_directives_initialize(state, lexer);
    }
    if (valid_symbols[PREPROC_DIRECTIVE_START] && empty_directives_after_keyword(state, lexer)) {
        lexer->result_symbol = PREPROC_DIRECTIVE_START;
        lexer->mark_end(lexer);
        return true;
    }
    bool directive_body = valid_symbols[PREPROC_NEWLINE] || valid_symbols[PREPROC_LPAREN] ||
        valid_symbols[PREPROC_ARG] || valid_symbols[PREPROC_DIRECTIVE_ARG];
    bool macro_name = valid_symbols[PREPROC_FUNCTION_NAME] && !valid_symbols[PREPROC_LPAREN];
    if (valid_symbols[PREPROC_EMPTY] && !macro_name && (!directive_body || lexer->lookahead == '#') &&
        empty_directives_consume(lexer, state, true)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = PREPROC_EMPTY;
        return true;
    }
    return false;
}

static bool scan_raw_string_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[RAW_STRING_DELIMITER] && valid_symbols[RAW_STRING_CONTENT]) return false;
    scanner->pending_template_close = false;
    lexer->result_symbol = valid_symbols[RAW_STRING_DELIMITER] ? RAW_STRING_DELIMITER : RAW_STRING_CONTENT;
    return valid_symbols[RAW_STRING_DELIMITER]
        ? scan_raw_string_delimiter(scanner, lexer)
        : scan_raw_string_content(scanner, lexer);
}

static bool scan_raw_string_delimiter(Scanner *scanner, TSLexer *lexer) {
    if (scanner->delimiter_length > 0) {
        for (int i = 0; i < scanner->delimiter_length; ++i) {
            if (lexer->lookahead != scanner->delimiter[i]) return false;
            advance(lexer);
        }
        reset(scanner);
        return true;
    }

    for (;;) {
        if (lexer->lookahead == '(') return scanner->delimiter_length > 0;
        if (scanner->delimiter_length >= MAX_DELIMITER_LENGTH || at_eof(lexer) || lexer->lookahead == '\\' ||
            is_unicode_whitespace(lexer->lookahead)) {
            return false;
        }
        scanner->delimiter[scanner->delimiter_length++] = lexer->lookahead;
        advance(lexer);
    }
}

static bool scan_raw_string_content(Scanner *scanner, TSLexer *lexer) {
    for (int delimiter_index = -1; !at_eof(lexer); advance(lexer)) {
        if (delimiter_index == scanner->delimiter_length && lexer->lookahead == '"') return true;
        if (delimiter_index >= 0) {
            bool matches = delimiter_index < scanner->delimiter_length &&
                lexer->lookahead == scanner->delimiter[delimiter_index];
            delimiter_index = matches ? delimiter_index + 1 : -1;
        }
        if (delimiter_index == -1 && lexer->lookahead == ')') {
            lexer->mark_end(lexer);
            delimiter_index = 0;
        }
    }
    lexer->mark_end(lexer);
    return true;
}

static bool scan_macro_statement_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    uint8_t valid = (valid_symbols[MACRO_CALL_END] ? 1 : 0) | (valid_symbols[MACRO_IDENTIFIER_END] ? 2 : 0);
    MacroLineMiss *miss = &scanner->macro_line_miss;
    bool counted = scanner->pending_macro_identifiers != 0;
    if (!counted && miss->valid == valid && miss->start <= position.bytes && position.bytes <= miss->end) return false;
    uint32_t end = 0;
    if (scan_macro_line_end(scanner, lexer, valid_symbols, position.bytes, &end)) return true;
    if (!counted && end) *miss = (MacroLineMiss){.start = position.bytes, .end = end, .valid = valid};
    return false;
}

// Sets `*end` to the byte of the first character after the trivia.
static bool scan_macro_line_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, uint32_t start, uint32_t *end) {
    bool newline = scanner->null_directive_end && start == scanner->null_directive_end;
    lexer->mark_end(lexer);
    if (!skip_cpp_trivia(lexer, &newline, &scanner->directives, end)) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    *end = position.bytes;
    if (at_eof(lexer)) {
        scanner->pending_macro_identifiers = 0;
        lexer->result_symbol = valid_symbols[MACRO_CALL_END] ? MACRO_CALL_END : MACRO_IDENTIFIER_END;
        return true;
    }
    if (!newline) return false;
    if (valid_symbols[MACRO_CALL_END] && !is_macro_call_continuation(lexer->lookahead)) {
        lexer->result_symbol = MACRO_CALL_END;
        return true;
    }
    if (!valid_symbols[MACRO_IDENTIFIER_END] || !scan_macro_identifier_boundary(scanner, lexer)) return false;
    lexer->result_symbol = MACRO_IDENTIFIER_END;
    return true;
}

static inline bool is_macro_call_continuation(int32_t c) {
    if ((uint32_t)c >= 0x80u) return false;
    static const uint64_t mask_lo =
        (1ULL << 0) | (1ULL << '!') | (1ULL << '%') | (1ULL << '&') | (1ULL << '(') |
        (1ULL << '*') | (1ULL << '+') | (1ULL << ',') | (1ULL << '-') | (1ULL << '.') |
        (1ULL << '/') | (1ULL << ':') | (1ULL << ';') | (1ULL << '<') | (1ULL << '=') |
        (1ULL << '>') | (1ULL << '?');
    static const uint64_t mask_hi =
        (1ULL << ('[' - 64)) | (1ULL << ('^' - 64)) | (1ULL << ('{' - 64)) |
        (1ULL << ('|' - 64)) | (1ULL << ('~' - 64));
    uint64_t mask = (uint32_t)c < 64u ? mask_lo : mask_hi;
    return ((mask >> ((uint32_t)c & 63u)) & 1ULL) != 0;
}

static bool scan_macro_identifier_boundary(Scanner *scanner, TSLexer *lexer) {
    if (scanner->pending_macro_identifiers) {
        scanner->pending_macro_identifiers--;
        return true;
    }
    uint32_t remaining = 0;
    for (;;) {
        if (lexer->lookahead == '#' || lexer->lookahead == '}' || at_eof(lexer)) break;
        char word[FUNCTION_MACRO_KEYWORD_MAX_LENGTH + 1] = {0};
        MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = true, .lookahead_only = true};
        if (!scan_macro_name_rest(lexer, &name, false)) return false;
        static const char *const starters[] = {"class", "concept", "decltype", "enum", "export", "namespace", "struct", "template", "union", "using"};
        if (name.ascii && function_macro_word_in(word, starters, sizeof(starters) / sizeof(starters[0]))) break;
        bool newline = false;
        if (!skip_cpp_trivia(lexer, &newline, &scanner->directives, NULL) || (!newline && !at_eof(lexer))) return false;
        remaining++;
    }
    scanner->pending_macro_identifiers = remaining;
    return true;
}

// code-gauge-allow functionCognitiveComplexity 28 -- External-token validity selects one preprocessor lexing mode and preserves its token range.
// code-gauge-allow functionNcss 34 -- External-token validity selects one preprocessor lexing mode and preserves its token range.
static bool scan_preproc_tokens(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols) {
    bool directive_text = !valid_symbols[PREPROC_ARG] && valid_symbols[PREPROC_DIRECTIVE_ARG];
    PreprocArgumentMode argument_mode = preproc_argument_mode(
        valid_symbols[PREPROC_ARG], valid_symbols[PREPROC_DIRECTIVE_ARG], valid_symbols[PREPROC_LPAREN]);
    TSSymbol argument_symbol = directive_text ? PREPROC_DIRECTIVE_ARG : PREPROC_ARG;
    bool argument_valid = valid_symbols[PREPROC_ARG] || directive_text;
    while (valid_symbols[PREPROC_LPAREN] && lexer->lookahead == '\\') {
        lexer->advance(lexer, true);
        if (!scan_preproc_newline(lexer, true)) return false;
    }
    if (valid_symbols[PREPROC_LPAREN] && lexer->lookahead == '(') {
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        lexer->result_symbol = PREPROC_LPAREN;
        return true;
    }
    if (valid_symbols[PREPROC_NEWLINE]) {
        for (;;) {
            while (pragma_space(lexer->lookahead) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                lexer->advance(lexer, true);
            }
            if (lexer->lookahead != '\\') break;
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            if (!scan_preproc_newline(lexer, true)) {
                lexer->result_symbol = argument_symbol;
                return argument_valid && scan_preproc_arg(lexer, true, argument_mode);
            }
        }
        if (pragma_eof(lexer) || scan_preproc_newline(lexer, false)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROC_NEWLINE;
            return true;
        }
    }
    if (argument_valid) {
        lexer->result_symbol = argument_symbol;
        return scan_preproc_arg(lexer, false, argument_mode);
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && scan_pragma_with(lexer, &(PragmaTrivia){empty_directives_skip, state});
}

static bool scan_initializer_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    TSLexerCheckpoint start;
    lexer->get_checkpoint(lexer, &start);
    InitializerStartKind kind = scan_initializer_start(&scanner->initializer_conditionals, lexer,
        valid_symbols[EMPTY_INITIALIZER_START], valid_symbols[INITIALIZER_DIRECTIVE_NAME]);
    lexer->mark_start(lexer, &start);
    if (kind == INITIALIZER_START_DIRECTIVE) {
        lexer->result_symbol = INITIALIZER_DIRECTIVE_NAME;
    } else {
        lexer->result_symbol = EMPTY_INITIALIZER_START;
        lexer->mark_end_at(lexer, &start);
    }
    return kind != INITIALIZER_START_NONE;
}

static bool scan_pack_index_operator(Scanner *scanner, TSLexer *lexer) {
    bool newline = false;
    for (int i = 0; i < 3; ++i) {
        if (lexer->lookahead != '.') return false;
        advance(lexer);
    }
    lexer->mark_end(lexer);
    if (!skip_cpp_trivia(lexer, &newline, &scanner->directives, NULL) || lexer->lookahead != '[') return false;
    advance(lexer);
    if (!skip_cpp_trivia(lexer, &newline, &scanner->directives, NULL) || lexer->lookahead == ']' || at_eof(lexer)) return false;
    lexer->result_symbol = PACK_INDEX_OPERATOR;
    return true;
}

static bool skip_pack_index_whitespace(TSLexer *lexer, bool skip) {
    for (;;) {
        if (is_unicode_whitespace(lexer->lookahead)) {
            lexer->advance(lexer, skip);
        } else if (lexer->lookahead == '\\') {
            lexer->advance(lexer, skip);
            if (!scan_preproc_newline(lexer, skip)) return false;
        } else {
            return true;
        }
    }
}

static bool scan_template_close(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close) {
    if (lexer->lookahead != '>') return false;
    if (pending_close) {
        advance(lexer);
        lexer->mark_end(lexer);
        lexer->result_symbol = valid_symbols[TEMPLATE_CLOSE] ? TEMPLATE_CLOSE : SPLIT_TEMPLATE_CLOSE;
        return true;
    }
    if (!valid_symbols[TEMPLATE_CLOSE]) return false;
    advance(lexer);
    lexer->mark_end(lexer);
    bool splice_lookahead = skip_template_splices(lexer, false);
    // [temp.names] splits >>, but [lex.pptoken] keeps >= and >>= intact.
    // The second half of >> must survive even when the next preprocessing token is >=.
    bool split = splice_lookahead && lexer->lookahead == '>';
    if (split) {
        advance(lexer);
        splice_lookahead = skip_template_splices(lexer, false);
    }
    if (splice_lookahead && lexer->lookahead == '=') return false;
    scanner->pending_template_close = split;
    lexer->result_symbol = TEMPLATE_CLOSE;
    return true;
}

static bool skip_template_splices(TSLexer *lexer, bool skip) {
    while (lexer->lookahead == '\\') {
        lexer->advance(lexer, skip);
        if (!scan_preproc_newline(lexer, skip)) return false;
    }
    return true;
}

static bool scan_declaration_macro(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    EmptyDirectives *state = &scanner->directives;
    bool conditional = valid_symbols[LEADING_CONDITIONAL_MARKER_IFDEF] ||
        valid_symbols[LEADING_CONDITIONAL_MARKER_IFNDEF] ||
        valid_symbols[CONDITIONAL_MARKER_IFDEF] ||
        valid_symbols[CONDITIONAL_MARKER_IFNDEF];
    if (conditional && lexer->lookahead == '#') {
        if (!empty_directives_at_start(state, lexer)) return false;
        const ConditionalMacroSymbols symbols = {
            .ifdef = CONDITIONAL_MARKER_IFDEF, .ifndef = CONDITIONAL_MARKER_IFNDEF,
            .leading_ifdef = LEADING_CONDITIONAL_MARKER_IFDEF, .leading_ifndef = LEADING_CONDITIONAL_MARKER_IFNDEF,
            .reserved_words = reserved_words, .reserved_word_count = RESERVED_WORD_COUNT,
            .skip_empty = empty_directives_skip, .empty_context = state,
        };
        return scan_conditional_macro(lexer, &symbols, valid_symbols);
    }
    return scan_declaration_attribute(scanner, lexer, valid_symbols);
}

static bool scan_declaration_attribute(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    EmptyDirectives *state = &scanner->directives;
    static const TSSymbol attribute_tokens[] = {
        FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_SUFFIX, POST_TYPE_FUNCTION_ATTRIBUTE_START,
        TRAILING_MACRO_ATTRIBUTE_NAME, TRAILING_OBJECT_DECLARATION_START,
    };
    const MacroAttributeSymbols symbols = {
        .trivia = {empty_directives_skip, state},
        .pragma = PRAGMA_OPERATOR,
        .object_start = TRAILING_OBJECT_DECLARATION_START,
        .allow_pragma = valid_symbols[PRAGMA_OPERATOR],
        .allow_object_start = valid_symbols[TRAILING_OBJECT_DECLARATION_START],
    };
    for (unsigned i = 0; i < sizeof(attribute_tokens) / sizeof(attribute_tokens[0]); i++) {
        TSSymbol token = attribute_tokens[i];
        if (!valid_symbols[token]) continue;
        lexer->result_symbol = token;
        return scan_function_attribute_guard(lexer,
            token == FUNCTION_ATTRIBUTE_START || token == POST_TYPE_FUNCTION_ATTRIBUTE_START,
            token != FUNCTION_ATTRIBUTE_START, token == TRAILING_MACRO_ATTRIBUTE_NAME, &symbols);
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && scan_pragma_with(lexer, &symbols.trivia);
}

static bool scan_macro_name_rest(TSLexer *lexer, MacroName *name, bool has_name) {
    for (;;) {
        MacroNameCharacter character = scan_macro_name_character(lexer, has_name, name);
        if (character == MACRO_NAME_INVALID) return false;
        if (character == MACRO_NAME_END) return has_name;
        if (character == MACRO_NAME_SPLICE) return has_name && scan_preproc_splices(lexer);
        has_name = true;
        if (!name->lookahead_only) lexer->mark_end(lexer);
    }
}

static inline void reset(Scanner *scanner) {
    scanner->pending_template_close = false;
    scanner->pending_macro_identifiers = 0;
    scanner->null_directive_end = 0;
    scanner->delimiter_length = 0;
}

static inline void advance(TSLexer *lexer) {
    lexer->advance(lexer, false);
}
