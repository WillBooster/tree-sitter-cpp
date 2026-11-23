// code-gauge-allow-file fileNcss -- The C scanner's extras and directive dispatch index this grammar's external-token enum, so they live beside the C++ tokens.
#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"
#include "conditionalMacro.h"
#include "emptyDirectives.h"
#include "initializerConditional.h"
#include "cppTrivia.h"

#include <stdint.h>
#include <string.h>

enum TokenType {
    RAW_STRING_DELIMITER,
    RAW_STRING_CONTENT,
    PACK_INDEX_OPERATOR,
    TEMPLATE_CLOSE,
    SPLIT_TEMPLATE_CLOSE,
    PRAGMA_OPERATOR,
    PREPROC_ARG,
    PREPROC_NEWLINE,
    PREPROC_LPAREN,
    PREPROC_DIRECTIVE_ARG,
    PREPROC_FUNCTION_NAME,
    FUNCTION_ATTRIBUTE_START,
    POST_TYPE_FUNCTION_ATTRIBUTE_START,
    POST_TYPE_FUNCTION_ATTRIBUTE_SUFFIX,
    TRAILING_MACRO_ATTRIBUTE_NAME,
    TRAILING_OBJECT_DECLARATION_START,
    CONDITIONAL_MARKER_IFDEF,
    CONDITIONAL_MARKER_IFNDEF,
    LEADING_CONDITIONAL_MARKER_IFDEF,
    LEADING_CONDITIONAL_MARKER_IFNDEF,
    GNU_RAW_STRING_BODY,
    RAW_MACRO_TYPE_TAIL,
    BRACED_FUNCTION_ATTRIBUTE_START,
    BRACED_MACRO_TYPE_START,
    DIRECTIVE_CONTEXT,
    PREPROC_EMPTY,
    PREPROC_INCLUDE,
    PREPROC_DEFINE,
    PREPROC_IF,
    PREPROC_IFDEF,
    PREPROC_IFNDEF,
    PREPROC_ELSE,
    PREPROC_ELIF,
    PREPROC_ELIFDEF,
    PREPROC_ELIFNDEF,
    PREPROC_ENDIF,
    PREPROC_PRAGMA,
    PREPROC_DIRECTIVE,
    MACRO_CALL_END,
    MACRO_IDENTIFIER_END,
    EMPTY_INITIALIZER_START,
    DECLARATOR_MACRO_NAME,
    TOKEN_COUNT
};

static const PreprocDirectiveSymbols directive_symbols = {
    .include = PREPROC_INCLUDE,
    .define = PREPROC_DEFINE,
    .preproc_if = PREPROC_IF,
    .ifdef = PREPROC_IFDEF,
    .ifndef = PREPROC_IFNDEF,
    .preproc_else = PREPROC_ELSE,
    .elif = PREPROC_ELIF,
    .elifdef = PREPROC_ELIFDEF,
    .elifndef = PREPROC_ELIFNDEF,
    .endif = PREPROC_ENDIF,
    .pragma = PREPROC_PRAGMA,
    .directive = PREPROC_DIRECTIVE,
};

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
    /// The end byte of a comment that a line break separates from the token before it; zero when none. Like
    /// `macro_line_miss`, it depends only on the source text.
    uint32_t later_line_comment_end;
    uint8_t delimiter_length;
    int32_t delimiter[MAX_DELIMITER_LENGTH];
} Scanner;

/// The first characters of a scanned word, enough to compare it with the longest word it is compared with, its full
/// length, and whether it holds upper-case and lower-case letters.
typedef struct {
    char start[sizeof("override")];
    size_t length;
    bool upper, lower;
} AsciiWord;

/// Words that cannot continue a declaration that a conditional marker starts.
static const char *const reserved_words[] = {"namespace", "using", "export", "module", "import"};
#define RESERVED_WORD_COUNT (sizeof(reserved_words) / sizeof(reserved_words[0]))

#if defined(__GNUC__) || defined(__clang__)
#define SCANNER_NOINLINE __attribute__((noinline))
#else
#define SCANNER_NOINLINE
#endif

static inline bool has_unspaced_cpp_tokens(const bool *valid_symbols);
static inline bool can_start_spaced_cpp_token(uint32_t c, const Scanner *scanner, const bool *valid_symbols);
static SCANNER_NOINLINE bool scan_token_spaced_fast(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static SCANNER_NOINLINE bool scan_token_slow(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static inline bool any_preproc_body_valid(const bool *valid_symbols);
static inline bool any_attribute_valid(const bool *valid_symbols);
static bool in_error_recovery(const bool *valid_symbols);
static bool scan_error_recovery_directive(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_raw_string_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_raw_string_delimiter(Scanner *scanner, TSLexer *lexer);
static bool scan_raw_string_content(Scanner *scanner, TSLexer *lexer);
static bool scan_macro_statement_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_macro_line_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, uint32_t start, uint32_t *end);
static inline bool is_macro_call_continuation(int32_t c);
static bool scan_macro_identifier_boundary(Scanner *scanner, TSLexer *lexer);
static bool only_extras_valid(const EmptyDirectives *state, const bool *valid_symbols);
static bool scan_extras(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_empty_directive_extra(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols, bool error_recovery);
static bool scan_preprocessor_token(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols);
static bool scan_spaced_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close);
static bool scan_unconsumed_spaced_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close, bool newline);
static bool can_start_peeked_spaced_token(int32_t peeked, const Scanner *scanner, const bool *valid_symbols);
static bool scan_preproc_tokens(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols);
static bool scan_cpp_hash_directive(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool error_recovery);
static InitializerConditionalKind scan_cached_empty_initializer(Scanner *scanner, TSLexer *lexer, const TSLexerCheckpoint *start, uint32_t *start_byte);
static bool scan_cpp_empty_initializer(Scanner *scanner, TSLexer *lexer, const TSLexerCheckpoint *start, const TSLexerCheckpoint *end, uint32_t start_byte, const CppDirectiveWord *directive);
static bool scan_cpp_conditional_marker(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols, const CppDirectiveWord *directive, const TSLexerCheckpoint *end);
static bool scan_pack_index_operator(Scanner *scanner, TSLexer *lexer);
static bool is_cpp_space(int32_t c);
static bool scan_template_close(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close);
static bool skip_template_splices(TSLexer *lexer, bool skip);
static bool after_later_line_comment(const Scanner *scanner, TSLexer *lexer);
static void note_later_line_comment(Scanner *scanner, TSLexer *lexer, bool newline);
static bool scan_declarator_macro_name(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool newline);
static inline bool starts_declarator_macro_scan(int32_t c);
static bool scan_ascii_word(TSLexer *lexer, AsciiWord *word);
static bool ascii_word_is(const AsciiWord *word, const char *text);
static bool scan_declarator_continuation(Scanner *scanner, TSLexer *lexer, bool newline);
static bool scan_function_clause(Scanner *scanner, TSLexer *lexer);
static bool scan_declaration_attribute(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool consumed_backslash);
static bool starts_macro_scan(int32_t c, bool consumed_backslash);
static bool scan_macro_name_rest(TSLexer *lexer, MacroName *name, bool has_name);
static inline void reset(Scanner *scanner);
static inline void advance(TSLexer *lexer);

void *tree_sitter_cpp_external_scanner_create(void) {
    Scanner *scanner = ts_calloc(1, sizeof(Scanner));
    if (scanner) {
        scanner->directives.spaced_splices = true;
        scanner->initializer_conditionals.directives = &scanner->directives;
    }
    return scanner;
}

void tree_sitter_cpp_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    empty_directives_release(&scanner->directives);
    delete_initializer_conditionals(&scanner->initializer_conditionals);
    ts_free(scanner);
}

enum {
    SERIALIZED_READY = 1,
    SERIALIZED_PENDING_TEMPLATE_CLOSE = 2,
    SERIALIZED_ANCHORED = 4,
    SERIALIZED_FLAGS = SERIALIZED_READY | SERIALIZED_PENDING_TEMPLATE_CLOSE | SERIALIZED_ANCHORED,
    SERIALIZED_PREFIX = 1 + 2 * sizeof(uint32_t),
};

unsigned tree_sitter_cpp_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    _Static_assert(SERIALIZED_PREFIX + MAX_DELIMITER_LENGTH * sizeof(int32_t) < TREE_SITTER_SERIALIZATION_BUFFER_SIZE,
                   "Serialized scanner state is too long!");
    buffer[0] = (scanner->directives.ready ? SERIALIZED_READY : 0) |
        (scanner->directives.anchored ? SERIALIZED_ANCHORED : 0) |
        (scanner->pending_template_close ? SERIALIZED_PENDING_TEMPLATE_CLOSE : 0);
    if (!scanner->pending_template_close && !scanner->pending_macro_identifiers && !scanner->null_directive_end &&
        !scanner->delimiter_length) {
        return 1;
    }
    size_t size = scanner->delimiter_length * sizeof(int32_t);
    memcpy(buffer + 1, &scanner->pending_macro_identifiers, sizeof(uint32_t));
    memcpy(buffer + 1 + sizeof(uint32_t), &scanner->null_directive_end, sizeof(uint32_t));
    memcpy(buffer + SERIALIZED_PREFIX, scanner->delimiter, size);
    return (unsigned)(size + SERIALIZED_PREFIX);
}

void tree_sitter_cpp_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    reset(scanner);
    scanner->directives.ready = length && (buffer[0] & SERIALIZED_READY);
    scanner->directives.anchored = length && (buffer[0] & SERIALIZED_ANCHORED);
    size_t prefix = SERIALIZED_PREFIX;
    if (length < prefix || length - prefix > sizeof(scanner->delimiter) ||
        (length - prefix) % sizeof(int32_t) != 0 || (unsigned char)buffer[0] > SERIALIZED_FLAGS) return;
    scanner->pending_template_close = buffer[0] & SERIALIZED_PENDING_TEMPLATE_CLOSE;
    memcpy(&scanner->pending_macro_identifiers, buffer + 1, sizeof(uint32_t));
    memcpy(&scanner->null_directive_end, buffer + 1 + sizeof(uint32_t), sizeof(uint32_t));
    scanner->delimiter_length = (length - prefix) / sizeof(int32_t);
    memcpy(scanner->delimiter, buffer + prefix, length - prefix);
}

bool tree_sitter_cpp_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    const EmptyDirectives *state = &scanner->directives;
    int32_t look = lexer->lookahead;
    if (look > 0 && look < 0x80 && look != '\\' && state->ready && !state->starts && !state->failed &&
        !scanner->pending_template_close && !has_unspaced_cpp_tokens(valid_symbols)) {
        if (look == ' ' || (uint32_t)(look - '\t') < 5) {
            return scan_token_spaced_fast(scanner, lexer, valid_symbols);
        }
        if (!can_start_spaced_cpp_token((uint32_t)look, scanner, valid_symbols)) return false;
    }
    return scan_token_slow(scanner, lexer, valid_symbols);
}

static inline bool has_unspaced_cpp_tokens(const bool *valid_symbols) {
    _Static_assert(RAW_STRING_DELIMITER == 0 && RAW_STRING_CONTENT == 1 &&
        PREPROC_ARG == 6 && PREPROC_NEWLINE == 7 && PREPROC_LPAREN == 8 &&
        PREPROC_DIRECTIVE_ARG == 9 && PREPROC_FUNCTION_NAME == 10 &&
        GNU_RAW_STRING_BODY == 20 && RAW_MACRO_TYPE_TAIL == 21 &&
        MACRO_CALL_END == 38 && MACRO_IDENTIFIER_END == 39,
        "unspaced token layout");
    uint16_t v0 = 0, v6 = 0, v20 = 0, v38 = 0;
    uint32_t v7 = 0;
    memcpy(&v0, valid_symbols, sizeof v0);
    memcpy(&v6, valid_symbols + PREPROC_ARG, sizeof v6);
    memcpy(&v7, valid_symbols + PREPROC_NEWLINE, sizeof v7);
    memcpy(&v20, valid_symbols + GNU_RAW_STRING_BODY, sizeof v20);
    memcpy(&v38, valid_symbols + MACRO_CALL_END, sizeof v38);
    return (v0 | v6 | v7 | v20 | v38) != 0;
}

static inline bool can_start_spaced_cpp_token(uint32_t c, const Scanner *scanner, const bool *valid_symbols) {
    if (c == '#') return scanner->directives.directive_starts.count > 0;
    if (c == '.') return valid_symbols[PACK_INDEX_OPERATOR];
    if (c == '>') return valid_symbols[TEMPLATE_CLOSE] || valid_symbols[SPLIT_TEMPLATE_CLOSE];
    if (c == '/') return valid_symbols[DECLARATOR_MACRO_NAME];
    if (!((preproc_identifier_start_ascii[c / 64] >> (c % 64)) & 1)) return false;
    return any_attribute_valid(valid_symbols) ||
        (valid_symbols[DECLARATOR_MACRO_NAME] && ((c - 'A') < 26u || c == '_')) ||
        (valid_symbols[TRAILING_OBJECT_DECLARATION_START] && peek_object_starter((uint16_t)c)) ||
        (c == '_' && valid_symbols[PRAGMA_OPERATOR] && scanner->directives.pragmas);
}

static SCANNER_NOINLINE bool scan_token_spaced_fast(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    int32_t peeked = peek_after_ascii_spaces(lexer);
    if (peeked > 0 && !can_start_spaced_cpp_token((uint32_t)peeked, scanner, valid_symbols)) return false;
    return scan_token_slow(scanner, lexer, valid_symbols);
}

static SCANNER_NOINLINE bool scan_token_slow(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
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
    if (only_extras_valid(state, valid_symbols)) return scan_extras(scanner, lexer, valid_symbols);
    if (!state->ready && valid_symbols[DIRECTIVE_CONTEXT]) return empty_directives_initialize(state, lexer, DIRECTIVE_CONTEXT);
    bool error_recovery = in_error_recovery(valid_symbols);
    if (scan_empty_directive_extra(state, lexer, valid_symbols, error_recovery)) return true;
    if (error_recovery) return scan_error_recovery_directive(scanner, lexer, valid_symbols);
    if (valid_symbols[RAW_STRING_DELIMITER] || valid_symbols[RAW_STRING_CONTENT]) {
        return scan_raw_string_token(scanner, lexer, valid_symbols);
    }
    if (valid_symbols[MACRO_CALL_END] || valid_symbols[MACRO_IDENTIFIER_END]) {
        return scan_macro_statement_end(scanner, lexer, valid_symbols);
    }
    bool pending_close = scanner->pending_template_close;
    scanner->pending_template_close = false;
    if (empty_directives_at_line_end(state, lexer)) return false;
    if (any_preproc_body_valid(valid_symbols)) return scan_preprocessor_token(state, lexer, valid_symbols);
    return scan_spaced_token(scanner, lexer, valid_symbols, pending_close);
}

static inline bool any_preproc_body_valid(const bool *valid_symbols) {
    _Static_assert(PREPROC_FUNCTION_NAME - PREPROC_ARG == 4 && RAW_MACRO_TYPE_TAIL - GNU_RAW_STRING_BODY == 1,
        "preproc body tokens are contiguous");
    uint64_t head = 0;
    uint16_t raw = 0;
    memcpy(&head, valid_symbols + PREPROC_ARG, 5);
    memcpy(&raw, valid_symbols + GNU_RAW_STRING_BODY, 2);
    return (head | raw) != 0;
}

static inline bool any_attribute_valid(const bool *valid_symbols) {
    _Static_assert(TRAILING_MACRO_ATTRIBUTE_NAME - FUNCTION_ATTRIBUTE_START == 3,
        "attribute tokens are contiguous");
    uint32_t bits = 0;
    memcpy(&bits, valid_symbols + FUNCTION_ATTRIBUTE_START, 4);
    return bits != 0;
}

static bool in_error_recovery(const bool *valid_symbols) {
    return valid_symbols[RAW_STRING_DELIMITER] && valid_symbols[RAW_STRING_CONTENT];
}

static bool scan_error_recovery_directive(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    scanner->pending_template_close = false;
    bool consumed_backslash = false;
    skip_leading_spaces_and_splices(lexer, is_cpp_space, NULL, &consumed_backslash);
    return !consumed_backslash && lexer->lookahead == '#' &&
        scan_cpp_hash_directive(scanner, lexer, valid_symbols, true);
}

static bool scan_preprocessor_token(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[GNU_RAW_STRING_BODY]) {
        lexer->result_symbol = GNU_RAW_STRING_BODY;
        return scan_gnu_raw_string_token(lexer);
    }
    if (valid_symbols[RAW_MACRO_TYPE_TAIL]) {
        lexer->result_symbol = RAW_MACRO_TYPE_TAIL;
        return scan_raw_macro_type_tail(lexer, &(PragmaTrivia){empty_directives_skip, state});
    }
    if (valid_symbols[PREPROC_FUNCTION_NAME]) {
        lexer->result_symbol = PREPROC_FUNCTION_NAME;
        return scan_function_macro_name(lexer, valid_symbols[PRAGMA_OPERATOR], PRAGMA_OPERATOR);
    }
    return scan_preproc_tokens(state, lexer, valid_symbols);
}

static bool scan_spaced_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close) {
    if (pending_close) {
        if (!skip_template_splices(lexer, true)) return false;
        pending_close = lexer->lookahead == '>';
    } else if (!can_start_peeked_spaced_token(peek_after_ascii_spaces(lexer), scanner, valid_symbols)) {
        return false;
    }
    bool newline = valid_symbols[DECLARATOR_MACRO_NAME] && after_later_line_comment(scanner, lexer);
    bool consumed_backslash = false;
    skip_leading_spaces_and_splices(lexer, is_cpp_space, &newline, &consumed_backslash);
    if (!consumed_backslash) return scan_unconsumed_spaced_token(scanner, lexer, valid_symbols, pending_close, newline);
    return scan_declaration_attribute(scanner, lexer, valid_symbols, true);
}

static bool scan_unconsumed_spaced_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close, bool newline) {
    if (lexer->lookahead == '#') return scan_cpp_hash_directive(scanner, lexer, valid_symbols, false);
    if (valid_symbols[PACK_INDEX_OPERATOR] && lexer->lookahead == '.') return scan_pack_index_operator(scanner, lexer);
    if ((valid_symbols[TEMPLATE_CLOSE] || valid_symbols[SPLIT_TEMPLATE_CLOSE]) && lexer->lookahead == '>') {
        return scan_template_close(scanner, lexer, valid_symbols, pending_close);
    }
    if (valid_symbols[DECLARATOR_MACRO_NAME] && starts_declarator_macro_scan(lexer->lookahead)) {
        return scan_declarator_macro_name(scanner, lexer, valid_symbols, newline);
    }
    return scan_declaration_attribute(scanner, lexer, valid_symbols, false);
}

static bool can_start_peeked_spaced_token(int32_t peeked, const Scanner *scanner, const bool *valid_symbols) {
    if (peeked < 0) return true;
    if (peeked == '#') return empty_directives_has_directives(&scanner->directives);
    if (peeked == '.') return valid_symbols[PACK_INDEX_OPERATOR];
    if (peeked == '>') return valid_symbols[TEMPLATE_CLOSE] || valid_symbols[SPLIT_TEMPLATE_CLOSE];
    if (peeked == '/') return valid_symbols[DECLARATOR_MACRO_NAME];
    if (!starts_macro_scan(peeked, false)) return false;
    return any_attribute_valid(valid_symbols) ||
        (valid_symbols[DECLARATOR_MACRO_NAME] && ((peeked >= 'A' && peeked <= 'Z') || peeked == '_')) ||
        (valid_symbols[TRAILING_OBJECT_DECLARATION_START] && peek_object_starter((uint16_t)peeked)) ||
        (peeked == '_' && valid_symbols[PRAGMA_OPERATOR] && empty_directives_pragma_possible(&scanner->directives));
}

static bool after_later_line_comment(const Scanner *scanner, TSLexer *lexer) {
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    return position.bytes == scanner->later_line_comment_end;
}

/// The parser lexes a comment as a token of its own and scans again after it, so a line break before or inside the
/// comment is recorded for that scan, which would otherwise take a name after the comment to be on the declarator's line.
static void note_later_line_comment(Scanner *scanner, TSLexer *lexer, bool newline) {
    if (!skip_cpp_comment(lexer, &newline) || !newline) return;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    scanner->later_line_comment_end = position.bytes;
}

/// Scans an all-caps name after a function declarator's parameter list as an unexpanded macro, as `NOEXCEPT` in
/// `int f() NOEXCEPT {}`. No keyword that continues a declarator is spelled in all caps, and what follows the name must
/// continue or end the declarator, so a name that starts the next declaration after a macro line, as `API` in
/// `DISABLE_COPY(T)` followed by `API int f();`, is left alone; a name on a later line than the parameter list (`newline`)
/// is the likelier start of such a declaration, so only a body or the end of the declaration may follow it.
static bool scan_declarator_macro_name(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool newline) {
    if (lexer->lookahead == '/') {
        note_later_line_comment(scanner, lexer, newline);
        return false;
    }
    AsciiWord name = {0};
    if (!scan_ascii_word(lexer, &name)) return false;
    // This scan consumed the word that the pragma operator starts with, so it finishes that token itself.
    if (name.lower) {
        lexer->result_symbol = PRAGMA_OPERATOR;
        return valid_symbols[PRAGMA_OPERATOR] && ascii_word_is(&name, pragma_word) &&
            scan_pragma_suffix_end_with(lexer, true, &(PragmaTrivia){empty_directives_skip, &scanner->directives});
    }
    if (!name.upper) return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = DECLARATOR_MACRO_NAME;
    bool line_end = false;
    return skip_cpp_trivia(lexer, &line_end, &scanner->directives, NULL) && scan_declarator_continuation(scanner, lexer, newline);
}

/// A macro name's first character, or the start of a comment that may put the name on a later line.
static inline bool starts_declarator_macro_scan(int32_t c) {
    return (c >= 'A' && c <= 'Z') || c == '_' || c == '/';
}

/// Scans a word of ASCII letters, digits, and underscores; false when another identifier character continues it.
static bool scan_ascii_word(TSLexer *lexer, AsciiWord *word) {
    for (;;) {
        int32_t c = lexer->lookahead;
        bool upper = c >= 'A' && c <= 'Z';
        bool lower = c >= 'a' && c <= 'z';
        if (!upper && !lower && c != '_' && !(c >= '0' && c <= '9')) return !preproc_identifier_character(c, true);
        if (word->length < sizeof(word->start) - 1) word->start[word->length] = (char)c;
        word->length++;
        word->upper |= upper;
        word->lower |= lower;
        advance(lexer);
    }
}

static bool ascii_word_is(const AsciiWord *word, const char *text) {
    return word->length == strlen(text) && strcmp(word->start, text) == 0;
}

static bool scan_declarator_continuation(Scanner *scanner, TSLexer *lexer, bool newline) {
    int32_t c = lexer->lookahead;
    if (c == '{' || c == ';') return true;
    if (newline) return false;
    if (c == ':' || c == '-') {
        advance(lexer);
        return c == '-' ? lexer->lookahead == '>' : lexer->lookahead != c;
    }
    if (c == '=') {
        advance(lexer);
        return scan_function_clause(scanner, lexer);
    }
    AsciiWord word = {0};
    return scan_ascii_word(lexer, &word) &&
        (ascii_word_is(&word, "final") || ascii_word_is(&word, "override") || ascii_word_is(&word, "try"));
}

/// Scans what follows the `=` of `= default`, `= delete`, or `= 0;`. Any other value is the default argument of a
/// parameter, whose trailing name annotates the parameter, as in `void f(int (*callback)(int) UNUSED = nullptr)` and
/// `(*callback)(int) UNUSED = 0)`.
static bool scan_function_clause(Scanner *scanner, TSLexer *lexer) {
    bool line_end = false;
    AsciiWord word = {0};
    if (!skip_cpp_trivia(lexer, &line_end, &scanner->directives, NULL) || !scan_ascii_word(lexer, &word)) return false;
    if (ascii_word_is(&word, "default") || ascii_word_is(&word, "delete")) return true;
    return ascii_word_is(&word, "0") && skip_cpp_trivia(lexer, &line_end, &scanner->directives, NULL) &&
        lexer->lookahead == ';';
}

static bool only_extras_valid(const EmptyDirectives *state, const bool *valid_symbols) {
    static const uint8_t extras[24] = {
        [PRAGMA_OPERATOR] = 0xff,
        [TRAILING_OBJECT_DECLARATION_START] = 0xff,
        [CONDITIONAL_MARKER_IFDEF] = 0xff, [CONDITIONAL_MARKER_IFNDEF] = 0xff,
        [LEADING_CONDITIONAL_MARKER_IFDEF] = 0xff, [LEADING_CONDITIONAL_MARKER_IFNDEF] = 0xff,
    };
    _Static_assert(DIRECTIVE_CONTEXT == 24 && PREPROC_DIRECTIVE == 37 &&
        MACRO_CALL_END == 38 && TOKEN_COUNT == 42, "extras layout");
    if (!state->ready) return false;
    uint32_t tail = 0;
    memcpy(&tail, valid_symbols + MACRO_CALL_END, sizeof tail);
    return !tail && empty_directives_only_valid(valid_symbols, extras, sizeof extras);
}

// Fails without consuming a character unless a directive, pragma, or object-declaration guard can start here, since
// the lexer rereads from the start what a failed scan consumed.
static bool scan_extras(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    EmptyDirectives *state = &scanner->directives;
    if (valid_symbols[PREPROC_EMPTY] && empty_directives_consume(lexer, state, true)) {
        lexer->mark_end(lexer);
        lexer->result_symbol = PREPROC_EMPTY;
        return true;
    }
    bool allow_directive = empty_directives_has_directives(state);
    bool allow_object = valid_symbols[TRAILING_OBJECT_DECLARATION_START];
    bool allow_pragma = valid_symbols[PRAGMA_OPERATOR] && state->pragmas;
    PeekExtrasKind kind = peek_extras_kind(lexer, allow_directive, allow_object, allow_pragma);
    if (kind == PEEK_EXTRAS_NONE) return false;
    bool at_line_end = kind != PEEK_EXTRAS_DIRECTIVE && empty_directives_at_line_end(state, lexer);
    if (kind == PEEK_EXTRAS_DECLARATION && at_line_end) return false;
    bool consumed_backslash = false;
    skip_leading_spaces_and_splices(lexer, is_cpp_space, NULL, &consumed_backslash);
    if (!consumed_backslash && lexer->lookahead == '#') {
        return allow_directive && scan_cpp_hash_directive(scanner, lexer, valid_symbols, false);
    }
    if ((!allow_object && !allow_pragma) || at_line_end) return false;
    return scan_declaration_attribute(scanner, lexer, valid_symbols, consumed_backslash);
}

static bool scan_empty_directive_extra(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols, bool error_recovery) {
    bool directive_body = !error_recovery && (valid_symbols[PREPROC_NEWLINE] || valid_symbols[PREPROC_LPAREN] ||
        valid_symbols[PREPROC_ARG] || valid_symbols[PREPROC_DIRECTIVE_ARG] || valid_symbols[GNU_RAW_STRING_BODY]);
    bool macro_name = !error_recovery && valid_symbols[PREPROC_FUNCTION_NAME];
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

// code-gauge-allow functionCognitiveComplexity -- External-token validity selects one preprocessor lexing mode and preserves its token range.
// code-gauge-allow functionNcss -- External-token validity selects one preprocessor lexing mode and preserves its token range.
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
    return valid_symbols[PRAGMA_OPERATOR] && empty_directives_pragma_possible(state) &&
        scan_pragma_with(lexer, &(PragmaTrivia){empty_directives_skip, state});
}

static bool scan_cpp_hash_directive(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool error_recovery) {
    EmptyDirectives *state = &scanner->directives;
    bool at_start = empty_directives_at_start(state, lexer);
    if (!at_start && !error_recovery) return false;
    TSLexerCheckpoint start;
    lexer->get_checkpoint(lexer, &start);
    uint32_t start_byte = 0;
    bool check_initializer = !error_recovery && valid_symbols[EMPTY_INITIALIZER_START];
    InitializerConditionalKind cached = check_initializer
        ? scan_cached_empty_initializer(scanner, lexer, &start, &start_byte)
        : INITIALIZER_UNKNOWN;
    if (cached == INITIALIZER_EMPTY) return true;
    CppDirectiveWord directive = {0};
    if (!scan_cpp_directive_word(lexer, &directive)) return false;
    if (!at_start || (!directive.starts_with_ucn && !empty_directives_has_keyword_end(state, directive.keyword_end))) {
        if (!error_recovery) return false;
        lexer->result_symbol = GNU_RAW_STRING_BODY;
        return true;
    }
    TSLexerCheckpoint end;
    lexer->get_mark_end_checkpoint(lexer, &end);
    if (check_initializer && cached == INITIALIZER_UNKNOWN &&
        scan_cpp_empty_initializer(scanner, lexer, &start, &end, start_byte, &directive)) {
        return true;
    }
    if (!error_recovery && scan_cpp_conditional_marker(state, lexer, valid_symbols, &directive, &end)) return true;
    return empty_directives_select_symbol(directive.keyword, &directive_symbols, valid_symbols, &lexer->result_symbol);
}

static InitializerConditionalKind scan_cached_empty_initializer(Scanner *scanner, TSLexer *lexer, const TSLexerCheckpoint *start, uint32_t *start_byte) {
    TSLexerPosition start_position;
    lexer->get_position(lexer, &start_position);
    *start_byte = start_position.bytes;
    InitializerConditionalKind cached = initializer_conditional_lookup(&scanner->initializer_conditionals, *start_byte);
    if (cached == INITIALIZER_EMPTY) {
        lexer->mark_end_at(lexer, start);
        lexer->result_symbol = EMPTY_INITIALIZER_START;
    }
    return cached;
}

static bool scan_cpp_empty_initializer(Scanner *scanner, TSLexer *lexer, const TSLexerCheckpoint *start, const TSLexerCheckpoint *end, uint32_t start_byte, const CppDirectiveWord *directive) {
    const char *word = directive->keyword;
    if (strcmp(word, "ifdef") != 0 && strcmp(word, "ifndef") != 0 && strcmp(word, "if") != 0) return false;
    bool empty = scan_empty_initializer_after_keyword(&scanner->initializer_conditionals, lexer, start_byte, directive->unmatched_backslash);
    lexer->mark_start(lexer, start);
    lexer->mark_end_at(lexer, empty ? start : end);
    if (empty) lexer->result_symbol = EMPTY_INITIALIZER_START;
    return empty;
}

static bool scan_cpp_conditional_marker(EmptyDirectives *state, TSLexer *lexer, const bool *valid_symbols, const CppDirectiveWord *directive, const TSLexerCheckpoint *end) {
    if (directive->unmatched_backslash) return false;
    bool ifdef = strcmp(directive->keyword, "ifdef") == 0;
    if (!ifdef && strcmp(directive->keyword, "ifndef") != 0) return false;
    if (!valid_symbols[LEADING_CONDITIONAL_MARKER_IFDEF] && !valid_symbols[LEADING_CONDITIONAL_MARKER_IFNDEF] &&
        !valid_symbols[CONDITIONAL_MARKER_IFDEF] && !valid_symbols[CONDITIONAL_MARKER_IFNDEF]) {
        return false;
    }
    const ConditionalMacroSymbols symbols = {
        .ifdef = CONDITIONAL_MARKER_IFDEF, .ifndef = CONDITIONAL_MARKER_IFNDEF,
        .leading_ifdef = LEADING_CONDITIONAL_MARKER_IFDEF, .leading_ifndef = LEADING_CONDITIONAL_MARKER_IFNDEF,
        .reserved_words = reserved_words, .reserved_word_count = RESERVED_WORD_COUNT,
        .skip_empty = empty_directives_skip, .empty_context = state,
    };
    if (scan_conditional_macro_after_keyword(lexer, ifdef, &symbols, valid_symbols)) return true;
    lexer->mark_end_at(lexer, end);
    return false;
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

static bool is_cpp_space(int32_t c) {
    return c == ' ' || (uint32_t)(c - '\t') <= '\r' - '\t';
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

static bool scan_declaration_attribute(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool consumed_backslash) {
    if (!starts_macro_scan(lexer->lookahead, consumed_backslash)) return false;
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
        return scan_function_attribute_guard_from(lexer,
            token == FUNCTION_ATTRIBUTE_START || token == POST_TYPE_FUNCTION_ATTRIBUTE_START,
            token != FUNCTION_ATTRIBUTE_START, token == TRAILING_MACRO_ATTRIBUTE_NAME, consumed_backslash, &symbols);
    }
    if (consumed_backslash) return false;
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && empty_directives_pragma_possible(state) &&
        scan_pragma_with(lexer, &symbols.trivia);
}

static bool starts_macro_scan(int32_t c, bool consumed_backslash) {
    if (consumed_backslash) return c == 'u' || c == 'U';
    if (c < 0) return false;
    if (c < 0x80) return c == '\\' || ((preproc_identifier_start_ascii[c / 64] >> (c % 64)) & 1);
    return preproc_identifier_character(c, false);
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
