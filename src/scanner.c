#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"
#include "conditionalMacro.h"
#include "initializerConditional.h"
#include "cppTrivia.h"

#include <stdint.h>
#include <string.h>

enum TokenType { RAW_STRING_DELIMITER, RAW_STRING_CONTENT, PACK_INDEX_OPERATOR, TEMPLATE_CLOSE, SPLIT_TEMPLATE_CLOSE, PRAGMA_OPERATOR, PREPROC_ARG, PREPROC_NEWLINE, PREPROC_LPAREN, PREPROC_DIRECTIVE_ARG, PREPROC_FUNCTION_NAME, FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_SUFFIX, TRAILING_MACRO_ATTRIBUTE_NAME, TRAILING_OBJECT_DECLARATION_START, CONDITIONAL_MARKER_IFDEF, CONDITIONAL_MARKER_IFNDEF, LEADING_CONDITIONAL_MARKER_IFDEF, LEADING_CONDITIONAL_MARKER_IFNDEF, MACRO_CALL_END, MACRO_IDENTIFIER_END, EMPTY_INITIALIZER_START, INITIALIZER_DIRECTIVE_NAME };

/// C++ [lex.string] limits raw-string d-char-sequences to 16 characters.
#define MAX_DELIMITER_LENGTH 16

typedef struct {
    InitializerConditionals initializer_conditionals;
    bool pending_template_close;
    uint32_t pending_macro_identifiers;
    uint8_t delimiter_length;
    int32_t delimiter[MAX_DELIMITER_LENGTH];
} Scanner;

typedef enum { PREPROC_CONTINUE, PREPROC_REJECT, PREPROC_ACCEPT } PreprocScanResult;

static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_raw_string_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_raw_string_delimiter(Scanner *scanner, TSLexer *lexer);
static bool scan_raw_string_content(Scanner *scanner, TSLexer *lexer);
static bool scan_macro_statement_end(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static inline bool is_macro_call_continuation(int32_t c);
static bool scan_macro_identifier_boundary(Scanner *scanner, TSLexer *lexer);
static bool scan_preprocessor(TSLexer *lexer, const bool *valid_symbols);
static PreprocScanResult scan_preproc_lparen(TSLexer *lexer);
static PreprocScanResult scan_preproc_newline_or_escaped_arg(
    TSLexer *lexer, bool allow_arg, bool directive_text, TSSymbol argument_symbol
);
static bool scan_initializer_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool scan_pack_index_operator(TSLexer *lexer);
static bool skip_pack_index_trivia(TSLexer *lexer);
static bool skip_pack_index_whitespace(TSLexer *lexer, bool skip);
static bool scan_template_close(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close);
static bool skip_template_splices(TSLexer *lexer, bool skip);
static bool scan_declaration_macro(TSLexer *lexer, const bool *valid_symbols);
static inline bool is_declaration_macro_start(int32_t c);
static bool scan_declaration_attribute(TSLexer *lexer, const bool *valid_symbols);
static bool scan_cpp_attribute_guard(
    TSLexer *lexer, bool prototype, bool require_literal, bool trailing, const MacroAttributeSymbols *symbols
);
static bool scan_ascii_macro_name(TSLexer *lexer, MacroName *name);
static bool scan_macro_name_rest(TSLexer *lexer, MacroName *name, bool has_name);
static inline bool is_ascii_macro_continue(int32_t c);
static bool skip_to_attribute_lparen(TSLexer *lexer);
static inline void reset(Scanner *scanner);
static inline void advance(TSLexer *lexer);

void *tree_sitter_cpp_external_scanner_create(void) {
    return ts_calloc(1, sizeof(Scanner));
}

void tree_sitter_cpp_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    delete_initializer_conditionals(&scanner->initializer_conditionals);
    ts_free(scanner);
}

unsigned tree_sitter_cpp_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    _Static_assert(1 + sizeof(scanner->pending_macro_identifiers) + MAX_DELIMITER_LENGTH * sizeof(int32_t) < TREE_SITTER_SERIALIZATION_BUFFER_SIZE,
                   "Serialized scanner state is too long!");
    if (!scanner->pending_template_close && !scanner->pending_macro_identifiers && !scanner->delimiter_length) {
        return 0;
    }
    size_t size = scanner->delimiter_length * sizeof(int32_t);
    buffer[0] = scanner->pending_template_close;
    memcpy(buffer + 1, &scanner->pending_macro_identifiers, sizeof(scanner->pending_macro_identifiers));
    memcpy(buffer + 1 + sizeof(scanner->pending_macro_identifiers), scanner->delimiter, size);
    return (unsigned)(size + 1 + sizeof(scanner->pending_macro_identifiers));
}

void tree_sitter_cpp_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    reset(scanner);
    if (length == 0) return;
    size_t prefix = 1 + sizeof(scanner->pending_macro_identifiers);
    if (length < prefix || length - prefix > sizeof(scanner->delimiter) ||
        (length - prefix) % sizeof(int32_t) != 0 || (unsigned char)buffer[0] > 1) return;
    scanner->pending_template_close = buffer[0] != 0;
    memcpy(&scanner->pending_macro_identifiers, buffer + 1, sizeof(scanner->pending_macro_identifiers));
    scanner->delimiter_length = (length - prefix) / sizeof(int32_t);
    memcpy(scanner->delimiter, buffer + prefix, length - prefix);
}

bool tree_sitter_cpp_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;
    bool found = scan_token(scanner, lexer, valid_symbols);
    if (found) {
        if (lexer->result_symbol != MACRO_IDENTIFIER_END) scanner->pending_macro_identifiers = 0;
        else scanner->pending_template_close = false;
    }
    return found;
}

// code-gauge-allow functionCognitiveComplexity 18 -- Ordered token dispatch keeps mutually exclusive scanner contexts together.
static bool scan_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[RAW_STRING_DELIMITER] || valid_symbols[RAW_STRING_CONTENT]) {
        return scan_raw_string_token(scanner, lexer, valid_symbols);
    }

    if (valid_symbols[MACRO_CALL_END] || valid_symbols[MACRO_IDENTIFIER_END]) {
        return scan_macro_statement_end(scanner, lexer, valid_symbols);
    }

    bool pending_close = scanner->pending_template_close;
    scanner->pending_template_close = false;

    if (valid_symbols[PREPROC_ARG] || valid_symbols[PREPROC_NEWLINE] || valid_symbols[PREPROC_LPAREN] || valid_symbols[PREPROC_DIRECTIVE_ARG] || valid_symbols[PREPROC_FUNCTION_NAME]) {
        return scan_preprocessor(lexer, valid_symbols);
    }
    if (pending_close) {
        if (!skip_template_splices(lexer, true)) return false;
        pending_close = lexer->lookahead == '>';
    }
    if (!skip_pack_index_whitespace(lexer, true)) return false;
    if ((valid_symbols[EMPTY_INITIALIZER_START] || valid_symbols[INITIALIZER_DIRECTIVE_NAME]) && lexer->lookahead == '#') {
        return scan_initializer_token(scanner, lexer, valid_symbols);
    }
    if (valid_symbols[PACK_INDEX_OPERATOR] && lexer->lookahead == '.') {
        return scan_pack_index_operator(lexer);
    }
    if ((valid_symbols[TEMPLATE_CLOSE] || valid_symbols[SPLIT_TEMPLATE_CLOSE]) && lexer->lookahead == '>') {
        return scan_template_close(scanner, lexer, valid_symbols, pending_close);
    }
    return scan_declaration_macro(lexer, valid_symbols);
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
    lexer->mark_end(lexer);
    bool newline = false;
    if (!skip_cpp_trivia(lexer, &newline)) return false;
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
        if (!skip_cpp_trivia(lexer, &newline) || (!newline && !at_eof(lexer))) return false;
        remaining++;
    }
    scanner->pending_macro_identifiers = remaining;
    return true;
}

static bool scan_preprocessor(TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[PREPROC_FUNCTION_NAME] && !valid_symbols[PREPROC_LPAREN]) {
        lexer->result_symbol = PREPROC_FUNCTION_NAME;
        return scan_function_macro_name(lexer, valid_symbols[PRAGMA_OPERATOR], PRAGMA_OPERATOR);
    }
    if (valid_symbols[PREPROC_LPAREN]) {
        PreprocScanResult lparen = scan_preproc_lparen(lexer);
        if (lparen != PREPROC_CONTINUE) return lparen == PREPROC_ACCEPT;
    }
    bool directive_text = !valid_symbols[PREPROC_ARG] && valid_symbols[PREPROC_DIRECTIVE_ARG];
    bool allow_arg = valid_symbols[PREPROC_ARG] || directive_text;
    TSSymbol argument_symbol = directive_text ? PREPROC_DIRECTIVE_ARG : PREPROC_ARG;
    if (valid_symbols[PREPROC_NEWLINE]) {
        PreprocScanResult newline = scan_preproc_newline_or_escaped_arg(
            lexer, allow_arg, directive_text, argument_symbol
        );
        if (newline != PREPROC_CONTINUE) return newline == PREPROC_ACCEPT;
    }
    if (allow_arg) {
        lexer->result_symbol = argument_symbol;
        return scan_preproc_arg(lexer, false, directive_text);
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && scan_pragma(lexer);
}

static PreprocScanResult scan_preproc_lparen(TSLexer *lexer) {
    while (lexer->lookahead == '\\') {
        lexer->advance(lexer, true);
        if (!scan_preproc_newline(lexer, true)) return PREPROC_REJECT;
    }
    if (lexer->lookahead != '(') return PREPROC_CONTINUE;
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    lexer->result_symbol = PREPROC_LPAREN;
    return PREPROC_ACCEPT;
}

static PreprocScanResult scan_preproc_newline_or_escaped_arg(
    TSLexer *lexer, bool allow_arg, bool directive_text, TSSymbol argument_symbol
) {
    for (;;) {
        while (pragma_space(lexer->lookahead) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
            lexer->advance(lexer, true);
        }
        if (lexer->lookahead != '\\') break;
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        if (!scan_preproc_newline(lexer, true)) {
            lexer->result_symbol = argument_symbol;
            return allow_arg && scan_preproc_arg(lexer, true, directive_text) ? PREPROC_ACCEPT : PREPROC_REJECT;
        }
    }
    if (!at_eof(lexer) && !scan_preproc_newline(lexer, false)) return PREPROC_CONTINUE;
    lexer->mark_end(lexer);
    lexer->result_symbol = PREPROC_NEWLINE;
    return PREPROC_ACCEPT;
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

static bool scan_pack_index_operator(TSLexer *lexer) {
    for (int i = 0; i < 3; ++i) {
        if (lexer->lookahead != '.') return false;
        advance(lexer);
    }
    lexer->mark_end(lexer);
    if (!skip_pack_index_trivia(lexer) || lexer->lookahead != '[') return false;
    advance(lexer);
    if (!skip_pack_index_trivia(lexer) || lexer->lookahead == ']' || at_eof(lexer)) return false;
    lexer->result_symbol = PACK_INDEX_OPERATOR;
    return true;
}

static bool skip_pack_index_trivia(TSLexer *lexer) {
    bool newline = false;
    for (;;) {
        if (!skip_pack_index_whitespace(lexer, false)) return false;
        if (lexer->lookahead != '/') return true;
        if (!skip_cpp_comment(lexer, &newline)) return false;
    }
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

static bool scan_declaration_macro(TSLexer *lexer, const bool *valid_symbols) {
    static const char *const reserved[] = {"namespace", "using", "export", "module", "import"};
    bool conditional = valid_symbols[LEADING_CONDITIONAL_MARKER_IFDEF] ||
        valid_symbols[LEADING_CONDITIONAL_MARKER_IFNDEF] ||
        valid_symbols[CONDITIONAL_MARKER_IFDEF] ||
        valid_symbols[CONDITIONAL_MARKER_IFNDEF];
    if (conditional && (lexer->lookahead == '#' || lexer->lookahead >= 0x80)) {
        while (pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
        const ConditionalMacroSymbols symbols = {
            .ifdef = CONDITIONAL_MARKER_IFDEF, .ifndef = CONDITIONAL_MARKER_IFNDEF,
            .leading_ifdef = LEADING_CONDITIONAL_MARKER_IFDEF, .leading_ifndef = LEADING_CONDITIONAL_MARKER_IFNDEF,
            .reserved_words = reserved, .reserved_word_count = sizeof(reserved) / sizeof(reserved[0]),
        };
        if (lexer->lookahead == '#') return scan_conditional_macro(lexer, &symbols, valid_symbols);
    }
    if (!is_declaration_macro_start(lexer->lookahead)) return false;
    return scan_declaration_attribute(lexer, valid_symbols);
}

static inline bool is_declaration_macro_start(int32_t c) {
    if ((uint32_t)c >= 0x80u) return true;
    return ((uint32_t)(c | 0x20) - 'a' < 26u) || c == '_' || c == '$' || c == '\\';
}

static bool scan_declaration_attribute(TSLexer *lexer, const bool *valid_symbols) {
    static const TSSymbol attribute_tokens[] = {
        FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_SUFFIX,
        TRAILING_MACRO_ATTRIBUTE_NAME, TRAILING_OBJECT_DECLARATION_START,
    };
    const MacroAttributeSymbols symbols = {
        .pragma = PRAGMA_OPERATOR,
        .object_start = TRAILING_OBJECT_DECLARATION_START,
        .allow_pragma = valid_symbols[PRAGMA_OPERATOR],
        .allow_object_start = valid_symbols[TRAILING_OBJECT_DECLARATION_START],
    };
    for (unsigned i = 0; i < sizeof(attribute_tokens) / sizeof(attribute_tokens[0]); i++) {
        TSSymbol token = attribute_tokens[i];
        if (!valid_symbols[token]) continue;
        lexer->result_symbol = token;
        return scan_cpp_attribute_guard(lexer,
            token == FUNCTION_ATTRIBUTE_START || token == POST_TYPE_FUNCTION_ATTRIBUTE_START,
            token != FUNCTION_ATTRIBUTE_START, token == TRAILING_MACRO_ATTRIBUTE_NAME, &symbols);
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && (lexer->lookahead == '_' || lexer->lookahead >= 0x80) && scan_pragma(lexer);
}

// code-gauge-allow functionCognitiveComplexity 18 -- Keeps the ASCII macro-attribute fast path in one linear guard sequence.
static bool scan_cpp_attribute_guard(
    TSLexer *lexer, bool prototype, bool require_literal, bool trailing, const MacroAttributeSymbols *symbols
) {
    if ((uint32_t)lexer->lookahead >= 0x80u || lexer->lookahead == '\\') {
        return scan_function_attribute_guard(lexer, prototype, require_literal, trailing, symbols);
    }
    char word[FUNCTION_MACRO_KEYWORD_MAX_LENGTH + 1];
    MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = true, .lookahead_only = !trailing};
    if (!scan_ascii_macro_name(lexer, &name)) return false;
    if (name.ascii && name.length == sizeof(pragma_word) - 1 &&
        memcmp(word, pragma_word, sizeof(pragma_word) - 1) == 0) {
        lexer->result_symbol = symbols->pragma;
        return symbols->allow_pragma && scan_pragma_suffix(lexer);
    }
    if (symbols->allow_object_start && name.ascii && (uint32_t)(word[0] - 'b') <= ('v' - 'b') &&
        function_macro_scalar_type_keyword(word)) {
        lexer->result_symbol = symbols->object_start;
        return function_macro_object_prefix(lexer);
    }
    if (lexer->result_symbol == symbols->object_start || !skip_to_attribute_lparen(lexer)) return false;
    if (name.ascii && function_macro_attribute_keyword(word, trailing)) return false;
    if (trailing) return function_macro_trailing_argument_prefix(lexer);
    bool literal = false;
    return function_macro_balanced(lexer, '(', ')', &literal) && (!require_literal || literal) &&
        (!prototype || function_macro_declaration_continues(lexer, require_literal));
}

static bool scan_ascii_macro_name(TSLexer *lexer, MacroName *name) {
    if (name->lookahead_only) lexer->mark_end(lexer);
    do {
        if (name->length < name->capacity) name->word[name->length++] = (char)lexer->lookahead;
        else name->ascii = false;
        lexer->advance(lexer, false);
    } while (is_ascii_macro_continue(lexer->lookahead));
    name->word[name->length] = '\0';
    if (!name->lookahead_only) lexer->mark_end(lexer);
    if (lexer->lookahead != '\\' && lexer->lookahead < 0x80) return true;
    memset(name->word + name->length, 0, name->capacity + 1 - name->length);
    return scan_macro_name_rest(lexer, name, true);
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

static inline bool is_ascii_macro_continue(int32_t c) {
    return ((uint32_t)(c | 0x20) - 'a' < 26u) || ((uint32_t)(c - '0') < 10u) || c == '_' || c == '$';
}

static bool skip_to_attribute_lparen(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || (uint32_t)(lexer->lookahead - '\t') < 5u) {
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead == '(') return true;
    if (lexer->lookahead != '\\' && lexer->lookahead != '/' && lexer->lookahead < 0x80) return false;
    return scan_pragma_spacing(lexer) && lexer->lookahead == '(';
}

static inline void reset(Scanner *scanner) {
    scanner->pending_template_close = false;
    scanner->pending_macro_identifiers = 0;
    scanner->delimiter_length = 0;
}

static inline void advance(TSLexer *lexer) {
    lexer->advance(lexer, false);
}
