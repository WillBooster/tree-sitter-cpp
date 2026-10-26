#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"
#include "conditionalMacro.h"

#include <string.h>
#include <stdint.h>
#include <wctype.h>

enum TokenType { RAW_STRING_DELIMITER, RAW_STRING_CONTENT, PACK_INDEX_OPERATOR, TEMPLATE_CLOSE, SPLIT_TEMPLATE_CLOSE, PRAGMA_OPERATOR, PREPROC_ARG, PREPROC_NEWLINE, PREPROC_LPAREN, PREPROC_DIRECTIVE_ARG, PREPROC_FUNCTION_NAME, FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_START, POST_TYPE_FUNCTION_ATTRIBUTE_SUFFIX, TRAILING_MACRO_ATTRIBUTE_NAME, TRAILING_OBJECT_DECLARATION_START, CONDITIONAL_MARKER_IFDEF, CONDITIONAL_MARKER_IFNDEF, LEADING_CONDITIONAL_MARKER_IFDEF, LEADING_CONDITIONAL_MARKER_IFNDEF };

/// The spec limits delimiters to 16 chars
#define MAX_DELIMITER_LENGTH 16

typedef struct {
    bool pending_template_close;
    uint8_t delimiter_length;
    int32_t delimiter[MAX_DELIMITER_LENGTH];
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void reset(Scanner *scanner) {
    scanner->pending_template_close = false;
    scanner->delimiter_length = 0;
    memset(scanner->delimiter, 0, sizeof scanner->delimiter);
}

static bool scan_raw_string_delimiter(Scanner *scanner, TSLexer *lexer) {
    if (scanner->delimiter_length > 0) {
        // Closing delimiter: must exactly match the opening delimiter.
        // We already checked this when scanning content, but this is how we
        // know when to stop. We can't stop at ", because R"""hello""" is valid.
        for (int i = 0; i < scanner->delimiter_length; ++i) {
            if (lexer->lookahead != scanner->delimiter[i]) {
                return false;
            }
            advance(lexer);
        }
        reset(scanner);
        return true;
    }

    // d-char is any basic character except parens, backslashes, and spaces.
    for (;;) {
        if (lexer->lookahead == '(') {
            // Rather than create a token for an empty delimiter, we fail and
            // let the grammar fall back to a delimiter-less rule.
            return scanner->delimiter_length > 0;
        }
        if (scanner->delimiter_length >= MAX_DELIMITER_LENGTH || lexer->eof(lexer) || lexer->lookahead == '\\' ||
            iswspace(lexer->lookahead)) {
            return false;
        }
        scanner->delimiter[scanner->delimiter_length++] = lexer->lookahead;
        advance(lexer);
    }
}

static bool scan_raw_string_content(Scanner *scanner, TSLexer *lexer) {
    // The progress made through the delimiter since the last ')'.
    // The delimiter may not contain ')' so a single counter suffices.
    for (int delimiter_index = -1;;) {
        // If we hit EOF, consider the content to terminate there.
        // This forms an incomplete raw_string_literal, and models the code
        // well.
        if (lexer->eof(lexer)) {
            lexer->mark_end(lexer);
            return true;
        }

        if (delimiter_index >= 0) {
            if (delimiter_index == scanner->delimiter_length) {
                if (lexer->lookahead == '"') {
                    return true;
                }
                delimiter_index = -1;
            } else {
                if (lexer->lookahead == scanner->delimiter[delimiter_index]) {
                    delimiter_index += 1;
                } else {
                    delimiter_index = -1;
                }
            }
        }

        if (delimiter_index == -1 && lexer->lookahead == ')') {
            // The content doesn't include the )delimiter" part.
            // We must still scan through it, but exclude it from the token.
            lexer->mark_end(lexer);
            delimiter_index = 0;
        }

        advance(lexer);
    }
}

void *tree_sitter_cpp_external_scanner_create() {
    Scanner *scanner = (Scanner *)ts_calloc(1, sizeof(Scanner));
    memset(scanner, 0, sizeof(Scanner));
    return scanner;
}

static bool scan_preprocessor(TSLexer *lexer, const bool *valid_symbols);
static bool scan_declaration_macro(TSLexer *lexer, const bool *valid_symbols);
static bool scan_pack_index_operator(TSLexer *lexer);
static bool scan_template_close(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols, bool pending_close);
static bool scan_raw_string_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols);
static bool skip_template_splices(TSLexer *lexer, bool skip);
static bool skip_pack_index_whitespace(TSLexer *lexer, bool skip);
static bool skip_pack_index_trivia(TSLexer *lexer);

bool tree_sitter_cpp_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;

    if (valid_symbols[RAW_STRING_DELIMITER] || valid_symbols[RAW_STRING_CONTENT]) {
        return scan_raw_string_token(scanner, lexer, valid_symbols);
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
    if (valid_symbols[PACK_INDEX_OPERATOR] && lexer->lookahead == '.') {
        return scan_pack_index_operator(lexer);
    }
    if ((valid_symbols[TEMPLATE_CLOSE] || valid_symbols[SPLIT_TEMPLATE_CLOSE]) && lexer->lookahead == '>') {
        return scan_template_close(scanner, lexer, valid_symbols, pending_close);
    }
    return scan_declaration_macro(lexer, valid_symbols);
}

static bool scan_declaration_macro(TSLexer *lexer, const bool *valid_symbols) {
    bool leading = valid_symbols[LEADING_CONDITIONAL_MARKER_IFDEF] || valid_symbols[LEADING_CONDITIONAL_MARKER_IFNDEF];
    bool conditional = leading || valid_symbols[CONDITIONAL_MARKER_IFDEF] || valid_symbols[CONDITIONAL_MARKER_IFNDEF];
    if (conditional) {
        while (pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
        const ConditionalMacroSymbols symbols = {
            .ifdef = CONDITIONAL_MARKER_IFDEF, .ifndef = CONDITIONAL_MARKER_IFNDEF,
            .leading_ifdef = LEADING_CONDITIONAL_MARKER_IFDEF, .leading_ifndef = LEADING_CONDITIONAL_MARKER_IFNDEF,
        };
        if (lexer->lookahead == '#') return scan_conditional_macro(lexer, &symbols, valid_symbols);
    }
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
        return scan_function_attribute_guard(lexer,
            token == FUNCTION_ATTRIBUTE_START || token == POST_TYPE_FUNCTION_ATTRIBUTE_START,
            token != FUNCTION_ATTRIBUTE_START, token == TRAILING_MACRO_ATTRIBUTE_NAME, &symbols);
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && scan_pragma(lexer);
}

static bool scan_raw_string_token(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[RAW_STRING_DELIMITER] && valid_symbols[RAW_STRING_CONTENT]) return false;
    scanner->pending_template_close = false;
    // Raw-string delimiters and contents are space-sensitive.
    lexer->result_symbol = valid_symbols[RAW_STRING_DELIMITER] ? RAW_STRING_DELIMITER : RAW_STRING_CONTENT;
    return valid_symbols[RAW_STRING_DELIMITER]
        ? scan_raw_string_delimiter(scanner, lexer)
        : scan_raw_string_content(scanner, lexer);
}

static bool skip_template_splices(TSLexer *lexer, bool skip) {
    while (lexer->lookahead == '\\') {
        lexer->advance(lexer, skip);
        if (!scan_preproc_newline(lexer, skip)) return false;
    }
    return true;
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

unsigned tree_sitter_cpp_external_scanner_serialize(void *payload, char *buffer) {
    _Static_assert(1 + MAX_DELIMITER_LENGTH * sizeof(int32_t) < TREE_SITTER_SERIALIZATION_BUFFER_SIZE,
                   "Serialized delimiter is too long!");

    Scanner *scanner = (Scanner *)payload;
    size_t size = scanner->delimiter_length * sizeof(int32_t);
    buffer[0] = scanner->pending_template_close;
    memcpy(buffer + 1, scanner->delimiter, size);
    return (unsigned)size + 1;
}

void tree_sitter_cpp_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    reset(scanner);
    if (length == 0) return;
    if (length - 1 > sizeof(scanner->delimiter) || (length - 1) % sizeof(int32_t) != 0 || (unsigned char)buffer[0] > 1) {
        return;
    }
    scanner->pending_template_close = buffer[0] != 0;
    scanner->delimiter_length = (length - 1) / sizeof(int32_t);
    memcpy(scanner->delimiter, buffer + 1, length - 1);
}

void tree_sitter_cpp_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    ts_free(scanner);
}

static bool scan_pack_index_operator(TSLexer *lexer) {
    if (!skip_pack_index_whitespace(lexer, true)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (lexer->lookahead != '.') {
            return false;
        }
        advance(lexer);
    }
    lexer->mark_end(lexer);
    if (!skip_pack_index_trivia(lexer) || lexer->lookahead != '[') {
        return false;
    }
    advance(lexer);
    if (!skip_pack_index_trivia(lexer) || lexer->lookahead == ']' || lexer->eof(lexer)) {
        return false;
    }
    lexer->result_symbol = PACK_INDEX_OPERATOR;
    return true;
}

static bool skip_pack_index_trivia(TSLexer *lexer) {
    for (;;) {
        if (!skip_pack_index_whitespace(lexer, false)) {
            return false;
        }
        if (lexer->lookahead != '/') {
            break;
        }
        advance(lexer);
        if (lexer->lookahead == '/') {
            int32_t previous = 0;
            while (!lexer->eof(lexer) && (lexer->lookahead != '\n' || previous == '\\')) {
                if (lexer->lookahead != '\r') {
                    previous = lexer->lookahead;
                }
                advance(lexer);
            }
        } else if (lexer->lookahead == '*') {
            advance(lexer);
            bool star = false;
            for (;;) {
                if (lexer->eof(lexer)) {
                    return false;
                }
                int32_t c = lexer->lookahead;
                advance(lexer);
                if (star && c == '/') {
                    break;
                }
                star = c == '*';
            }
        } else {
            return false;
        }
    }
    return true;
}

static bool skip_pack_index_whitespace(TSLexer *lexer, bool skip) {
    for (;;) {
        if (iswspace(lexer->lookahead)) {
            lexer->advance(lexer, skip);
        } else if (lexer->lookahead == '\\') {
            lexer->advance(lexer, skip);
            if (!scan_preproc_newline(lexer, skip)) return false;
        } else {
            return true;
        }
    }
}

static bool scan_preprocessor(TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[PREPROC_FUNCTION_NAME] && !valid_symbols[PREPROC_LPAREN]) {
        lexer->result_symbol = PREPROC_FUNCTION_NAME;
        return scan_function_macro_name(lexer, valid_symbols[PRAGMA_OPERATOR], PRAGMA_OPERATOR);
    }
    bool directive_text = !valid_symbols[PREPROC_ARG] && valid_symbols[PREPROC_DIRECTIVE_ARG];
    TSSymbol argument_symbol = directive_text ? PREPROC_DIRECTIVE_ARG : PREPROC_ARG;
    if (valid_symbols[PREPROC_LPAREN]) {
        while (lexer->lookahead == '\\') {
            lexer->advance(lexer, true);
            if (!scan_preproc_newline(lexer, true)) return false;
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROC_LPAREN;
            return true;
        }
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
                return (valid_symbols[PREPROC_ARG] || directive_text) && scan_preproc_arg(lexer, true, directive_text);
            }
        }
        if (lexer->eof(lexer) || scan_preproc_newline(lexer, false)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = PREPROC_NEWLINE;
            return true;
        }
    }
    if (valid_symbols[PREPROC_ARG] || directive_text) {
        lexer->result_symbol = argument_symbol;
        return scan_preproc_arg(lexer, false, directive_text);
    }
    lexer->result_symbol = PRAGMA_OPERATOR;
    return valid_symbols[PRAGMA_OPERATOR] && scan_pragma(lexer);
}
