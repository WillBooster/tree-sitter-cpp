// code-gauge-allow-file fileNcss 493 -- Preprocessor tokens and pragma operators share names, spacing, and literal readers.
#ifndef TREE_SITTER_C_PRAGMA_H_
#define TREE_SITTER_C_PRAGMA_H_

#include "tree_sitter/parser.h"
#include "identifier.h"
#include "gnuRawString.h"
#include <string.h>

static const char pragma_word[] = "_Pragma";

typedef struct {
    bool (*skip_empty)(TSLexer *, void *);
    void *empty_context;
} PragmaTrivia;

typedef struct {
    char *word;
    unsigned capacity;
    unsigned length;
    bool ascii;
    bool lookahead_only;
    bool preserve_end;
} MacroName;

typedef enum {
    PREPROC_ARGUMENT_TOKENS,
    PREPROC_ARGUMENT_TEXT,
    PREPROC_ARGUMENT_PRAGMA,
} PreprocArgumentMode;

typedef enum {
    MACRO_NAME_END,
    MACRO_NAME_CHARACTER,
    MACRO_NAME_SPLICE,
    MACRO_NAME_INVALID,
} MacroNameCharacter;

static bool scan_macro_name(TSLexer *lexer, MacroName *name);
static void scan_macro_name_start(TSLexer *lexer, bool lookahead_only);
static MacroNameCharacter scan_macro_name_character(TSLexer *lexer, bool continuation, MacroName *name);
static bool scan_macro_name_ucn(TSLexer *lexer);

static bool scan_pragma_argument(TSLexer *lexer);
static bool scan_pragma_spacing(TSLexer *lexer);
static bool scan_pragma_comment(TSLexer *lexer);
static bool scan_preproc_line_comment(TSLexer *lexer);
static bool scan_preproc_block_comment(TSLexer *lexer);
static bool scan_pragma_spacing_with(TSLexer *lexer, const PragmaTrivia *trivia);
static bool scan_pragma_empty(TSLexer *lexer, const PragmaTrivia *trivia);
static bool scan_pragma_suffix_end_with(TSLexer *lexer, bool mark_end, const PragmaTrivia *trivia);
static bool scan_pragma_suffix(TSLexer *lexer);
static bool scan_pragma_suffix_end(TSLexer *lexer, bool mark_end);
static inline bool pragma_space(int32_t c);
static bool scan_pragma_word(TSLexer *lexer);
static bool scan_preproc_newline(TSLexer *lexer, bool skip);
static bool scan_preproc_splices(TSLexer *lexer);
static bool scan_preproc_ucn(TSLexer *lexer, bool *in_number);
static bool preproc_word(int32_t c, bool continuation);
static bool preproc_identifier_character(int32_t c, bool continuation);
static bool scan_preproc_quote(TSLexer *lexer, int32_t quote, bool escaped, bool directive_text);
static bool scan_preproc_raw_quote(TSLexer *lexer);

static bool scan_pragma_with(TSLexer *lexer, const PragmaTrivia *trivia) {
    while (pragma_space(lexer->lookahead)) {
        lexer->advance(lexer, true);
    }
    if (!scan_pragma_word(lexer)) return false;
    return scan_pragma_suffix_end_with(lexer, true, trivia);
}

static inline bool scan_pragma(TSLexer *lexer) {
    return scan_pragma_with(lexer, NULL);
}

static bool scan_pragma_suffix(TSLexer *lexer) {
    return scan_pragma_suffix_end(lexer, true);
}

static bool scan_pragma_suffix_end(TSLexer *lexer, bool mark_end) {
    return scan_pragma_suffix_end_with(lexer, mark_end, NULL);
}

static bool scan_pragma_suffix_end_with(TSLexer *lexer, bool mark_end, const PragmaTrivia *trivia) {
    if (!scan_pragma_spacing_with(lexer, trivia) || lexer->lookahead != '(') return false;
    lexer->advance(lexer, false);
    if (!scan_pragma_spacing_with(lexer, trivia)) return false;
    if (!scan_pragma_argument(lexer)) return false;
    if (!scan_pragma_spacing_with(lexer, trivia) || lexer->lookahead != ')') return false;
    lexer->advance(lexer, false);
    if (mark_end) lexer->mark_end(lexer);
    return true;
}

static bool scan_pragma_string_prefix(TSLexer *lexer, bool *raw);

static bool scan_pragma_argument(TSLexer *lexer) {
    bool raw = false;
    if (!scan_pragma_string_prefix(lexer, &raw) || lexer->lookahead != '"') return false;
    lexer->advance(lexer, false);
    if (raw) return scan_gnu_raw_string_end(lexer);
    while (lexer->lookahead != '"') {
        if (pragma_eof(lexer) || lexer->lookahead == '\n' || lexer->lookahead == '\r') return false;
        if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (pragma_eof(lexer)) return false;
            if (scan_preproc_newline(lexer, false)) continue;
        }
        lexer->advance(lexer, false);
    }
    lexer->advance(lexer, false);
    return true;
}

static bool scan_pragma_string_prefix(TSLexer *lexer, bool *raw) {
    if (lexer->lookahead == 'L' || lexer->lookahead == 'u' || lexer->lookahead == 'U') {
        bool utf8 = lexer->lookahead == 'u';
        lexer->advance(lexer, false);
        if (!scan_preproc_splices(lexer)) return false;
        if (utf8 && lexer->lookahead == '8') lexer->advance(lexer, false);
        if (!scan_preproc_splices(lexer)) return false;
    }
    *raw = lexer->lookahead == 'R';
    if (*raw) lexer->advance(lexer, false);
    return scan_preproc_splices(lexer);
}

static bool scan_pragma_spacing_with(TSLexer *lexer, const PragmaTrivia *trivia) {
    for (;;) {
        if (!scan_pragma_spacing(lexer)) return false;
        if (!scan_pragma_empty(lexer, trivia)) return true;
    }
}

static bool scan_pragma_empty(TSLexer *lexer, const PragmaTrivia *trivia) {
    return trivia && trivia->skip_empty && trivia->skip_empty(lexer, trivia->empty_context);
}

static bool scan_pragma_spacing(TSLexer *lexer) {
    for (;;) {
        if (pragma_space(lexer->lookahead)) {
            lexer->advance(lexer, false);
        } else if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (!scan_preproc_newline(lexer, false)) return false;
        } else if (lexer->lookahead == '/') {
            if (!scan_pragma_comment(lexer)) return false;
        } else {
            return true;
        }
    }
}

static bool scan_pragma_comment(TSLexer *lexer) {
    lexer->advance(lexer, false);
    if (!scan_preproc_splices(lexer)) return false;
    if (lexer->lookahead == '/') return scan_preproc_line_comment(lexer);
    if (lexer->lookahead == '*') return scan_preproc_block_comment(lexer);
    return false;
}

static bool scan_preproc_line_comment(TSLexer *lexer) {
    lexer->advance(lexer, false);
    while (!pragma_eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '\\') scan_preproc_newline(lexer, false);
    }
    return true;
}

static bool scan_preproc_block_comment(TSLexer *lexer) {
    lexer->advance(lexer, false);
    bool star = false;
    while (!pragma_eof(lexer)) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '\\' && scan_preproc_newline(lexer, false)) continue;
        if (star && c == '/') return true;
        star = c == '*';
    }
    return false;
}

static PreprocArgumentMode preproc_argument_mode(bool tokens, bool directive, bool lparen) {
    if (tokens && directive && !lparen) return PREPROC_ARGUMENT_PRAGMA;
    return !tokens && directive ? PREPROC_ARGUMENT_TEXT : PREPROC_ARGUMENT_TOKENS;
}

static bool scan_preproc_arg(TSLexer *lexer, bool consumed_backslash, PreprocArgumentMode mode) {
    bool directive_text = mode != PREPROC_ARGUMENT_TOKENS;
    bool raw_allowed = mode != PREPROC_ARGUMENT_TEXT;
    bool has_content = consumed_backslash;
    while (!has_content && pragma_space(lexer->lookahead) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
        lexer->advance(lexer, true);
    }
    bool after_comment = false;
    bool in_number = false;
    bool in_identifier = consumed_backslash && scan_preproc_ucn(lexer, &in_number);
    GnuRawStringPrefix raw_prefix = {0};
    if (consumed_backslash) gnu_raw_string_prefix_invalidate(&raw_prefix);
    if (consumed_backslash) lexer->mark_end(lexer);
    int32_t number_last = 0;
    while (!pragma_eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
        int32_t c = lexer->lookahead;
        bool word = preproc_word(c, in_identifier || in_number);
        bool digit = c >= '0' && c <= '9';
        bool exponent_sign = in_number && (c == '+' || c == '-') &&
                             (number_last == 'e' || number_last == 'E' || number_last == 'p' || number_last == 'P');
        if (c == '.') in_identifier = false;
        if (!word && !digit && c != '.' && c != '\'' && c != '\\' && !exponent_sign) {
            in_number = false;
            in_identifier = false;
        }
        if (digit && !in_identifier) in_number = true;
        if (word && !in_number) in_identifier = true;
        if (in_number && c != '\\') number_last = c;
        if (c != '"' && c != '\\') {
            if (in_number) gnu_raw_string_prefix_invalidate(&raw_prefix);
            else if (word || digit || exponent_sign) gnu_raw_string_prefix_character(&raw_prefix, c);
            else raw_prefix = (GnuRawStringPrefix){0};
        }
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            bool split_opener = lexer->lookahead == '\\';
            bool delimiter = scan_preproc_splices(lexer);
            if (delimiter && lexer->lookahead == '/') {
                if (!has_content || after_comment) break;
                while (!pragma_eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
                    if (lexer->lookahead == '\\') {
                        do {
                            lexer->advance(lexer, false);
                        } while (lexer->lookahead == '\\');
                        scan_preproc_newline(lexer, false);
                    } else {
                        lexer->advance(lexer, false);
                    }
                }
                lexer->mark_end(lexer);
                return true;
            }
            if (delimiter && lexer->lookahead == '*') {
                if (!has_content) return false;
                lexer->advance(lexer, false);
                bool star = false;
                bool split_delimiter = split_opener;
                while (!pragma_eof(lexer)) {
                    if (lexer->lookahead == '\\') {
                        if (!scan_preproc_splices(lexer)) star = false;
                        else if (star && lexer->lookahead == '/') split_delimiter = true;
                        continue;
                    }
                    if (star && lexer->lookahead == '/') break;
                    star = lexer->lookahead == '*';
                    lexer->advance(lexer, false);
                }
                if (pragma_eof(lexer)) break;
                lexer->advance(lexer, false);
                after_comment = !split_delimiter;
                if (split_delimiter) lexer->mark_end(lexer);
                continue;
            }
            if (!delimiter) in_identifier = scan_preproc_ucn(lexer, &in_number);
            has_content = true;
            after_comment = false;
        } else if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (scan_preproc_newline(lexer, false)) {
                if (!after_comment && has_content) lexer->mark_end(lexer);
                continue;
            }
            gnu_raw_string_prefix_invalidate(&raw_prefix);
            if (scan_preproc_ucn(lexer, &in_number)) {
                if (!in_number) in_identifier = true;
                number_last = 0;
            } else {
                in_number = false;
                in_identifier = false;
            }
            has_content = true;
            after_comment = false;
        } else if (lexer->lookahead == '"' || (raw_allowed && lexer->lookahead == '\'')) {
            int32_t quote = lexer->lookahead;
            bool raw = raw_allowed && quote == '"' && gnu_raw_string_prefix(&raw_prefix);
            lexer->advance(lexer, false);
            bool escaped = false;
            if (quote == '\'' && in_number) {
                bool spliced = scan_preproc_splices(lexer);
                int32_t next = lexer->lookahead;
                bool continuation = spliced && ((next >= '0' && next <= '9') || (next >= 'a' && next <= 'z') ||
                                                (next >= 'A' && next <= 'Z') || next == '_');
                escaped = !spliced;
                if (continuation) {
                    has_content = true;
                    after_comment = false;
                    number_last = 0;
                    lexer->mark_end(lexer);
                    continue;
                }
            }
            in_number = false;
            in_identifier = false;
            bool quoted = raw ? scan_preproc_raw_quote(lexer) : scan_preproc_quote(lexer, quote, escaped, directive_text);
            if (!quoted) return true;
            raw_prefix = (GnuRawStringPrefix){0};
            has_content = true;
            after_comment = false;
        } else {
            if (!pragma_space(lexer->lookahead)) {
                has_content = true;
                after_comment = false;
            }
            lexer->advance(lexer, false);
        }
        if (!after_comment && has_content) lexer->mark_end(lexer);
    }
    return has_content;
}

static bool scan_preproc_raw_quote(TSLexer *lexer) {
    if (scan_gnu_raw_string_end(lexer)) return true;
    while (!pragma_eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return false;
}

typedef struct {
    bool literal, slash, block, line, star, split_comment, after_comment, directive_text;
} PreprocQuoteState;

static bool scan_preproc_quote_step(TSLexer *lexer, PreprocQuoteState *state, int32_t c);

// code-gauge-allow functionCognitiveComplexity 25 -- Unterminated quote recovery tracks line splices alongside comment fallback boundaries.
static bool scan_preproc_quote(TSLexer *lexer, int32_t quote, bool escaped, bool directive_text) {
    lexer->mark_end(lexer);
    PreprocQuoteState state = {.literal = true, .directive_text = directive_text};
    while (!pragma_eof(lexer) && (state.block || (lexer->lookahead != '\n' && lexer->lookahead != '\r'))) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '\n' || c == '\r') state.literal = false;
        if (state.literal && c == quote && !escaped) {
            lexer->mark_end(lexer);
            return true;
        }
        if (c == '\\' && scan_preproc_newline(lexer, false)) {
            if (state.slash || state.star) state.split_comment = true;
            if (!state.block && !state.slash && !state.after_comment) lexer->mark_end(lexer);
            continue;
        }
        escaped = c == '\\' && !escaped;
        if (!scan_preproc_quote_step(lexer, &state, c)) return false;
    }
    if (state.slash) lexer->mark_end(lexer);
    return false;
}

// code-gauge-allow functionCognitiveComplexity 30 -- Comment transitions in unterminated quotes share split-splice and mark-end state.
// code-gauge-allow functionNcss 36 -- Comment transitions in unterminated quotes share split-splice and mark-end state.
static bool scan_preproc_quote_step(TSLexer *lexer, PreprocQuoteState *state, int32_t c) {
    if (state->line) {
        if (!state->after_comment) lexer->mark_end(lexer);
    } else if (state->block) {
        if (state->star && c == '/') {
            if (!state->literal) return false;
            state->block = false;
            state->after_comment = !state->split_comment;
            if (state->split_comment) lexer->mark_end(lexer);
            state->split_comment = false;
            state->star = false;
        } else {
            state->star = c == '*';
        }
    } else if (state->slash && c == '/') {
        state->line = state->directive_text;
        state->slash = false;
        if (!state->after_comment) lexer->mark_end(lexer);
    } else if (state->slash && c == '*') {
        state->block = true;
        state->slash = false;
    } else {
        if (state->slash || (c != '/' && !pragma_space(c))) state->after_comment = false;
        state->slash = c == '/';
        if (!state->slash) state->split_comment = false;
        if (!state->slash && !state->after_comment) lexer->mark_end(lexer);
    }
    return true;
}

// code-gauge-allow functionCognitiveComplexity 29 -- Standard, delimited, and named universal character names share hex/name validation.
// code-gauge-allow functionNcss 32 -- Standard, delimited, and named universal character names share hex/name validation.
static bool scan_preproc_ucn(TSLexer *lexer, bool *in_number) {
    while (!scan_preproc_splices(lexer)) *in_number = false;
    int32_t prefix = lexer->lookahead;
    if (prefix != 'u' && prefix != 'U' && prefix != 'N') return false;
    lexer->advance(lexer, false);
    if (!scan_preproc_splices(lexer)) return false;
    bool braced = prefix != 'U' && lexer->lookahead == '{';
    if (prefix == 'N' && !braced) return false;
    if (braced) lexer->advance(lexer, false);
    unsigned remaining = prefix == 'U' ? 8 : 4;
    bool content = false;
    for (;;) {
        if (!scan_preproc_splices(lexer)) return false;
        if (braced && lexer->lookahead == '}') {
            lexer->advance(lexer, false);
            return content;
        }
        int32_t c = lexer->lookahead;
        bool accepted = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (prefix == 'N') accepted = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-';
        if (!accepted) return false;
        lexer->advance(lexer, false);
        content = true;
        if (!braced && --remaining == 0) return true;
    }
}

static bool preproc_word(int32_t c, bool continuation) {
    if (c == '\\') return false;
    if (continuation && c >= 0x80 && !pragma_space(c)) return true;
    return preproc_identifier_character(c, continuation);
}

static bool preproc_identifier_character(int32_t c, bool continuation) {
    if (c >= 0 && c < 0x80) {
        const uint64_t *ascii = continuation ? preproc_identifier_continue_ascii : preproc_identifier_start_ascii;
        return (ascii[c / 64] >> (c % 64)) & 1;
    }
    return continuation
        ? set_contains(preproc_identifier_continue, sizeof(preproc_identifier_continue) / sizeof(TSCharacterRange), c)
        : set_contains(preproc_identifier_start, sizeof(preproc_identifier_start) / sizeof(TSCharacterRange), c);
}

static bool scan_pragma_word(TSLexer *lexer) {
    const char *word = pragma_word;
    for (; *word; word++) {
        if (lexer->lookahead != *word) return false;
        lexer->advance(lexer, false);
    }
    return true;
}

static bool scan_preproc_newline(TSLexer *lexer, bool skip) {
    bool carriage_return = lexer->lookahead == '\r';
    if (carriage_return) lexer->advance(lexer, skip);
    if (lexer->lookahead == '\n') {
        lexer->advance(lexer, skip);
        if (!carriage_return && lexer->lookahead == '\r') lexer->advance(lexer, skip);
        return true;
    }
    return carriage_return;
}

static bool scan_preproc_splices(TSLexer *lexer) {
    while (lexer->lookahead == '\\') {
        lexer->advance(lexer, false);
        if (!scan_preproc_newline(lexer, false)) return false;
    }
    return true;
}

static inline bool pragma_space(int32_t c) {
    return c < 0x85
        ? c == ' ' || (uint32_t)(c - '\t') <= '\r' - '\t'
        : c == 0x85 || c == 0xA0 || c == 0x1680 || (uint32_t)(c - 0x2000) <= 0x200A - 0x2000 ||
          c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

static bool scan_function_macro_name(TSLexer *lexer, bool allow_pragma, TSSymbol pragma_symbol) {
    char word[sizeof(pragma_word)] = {0};
    MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = true};
    if (!scan_macro_name(lexer, &name)) return false;
    bool adjacent = lexer->lookahead == '(';
    if (allow_pragma && name.ascii && strcmp(word, pragma_word) == 0 && scan_pragma_suffix(lexer)) {
        lexer->result_symbol = pragma_symbol;
        return true;
    }
    return adjacent;
}

static bool scan_macro_name(TSLexer *lexer, MacroName *name) {
    bool has_name = false;
    for (;;) {
        if (!has_name) scan_macro_name_start(lexer, name->lookahead_only && !name->preserve_end);
        MacroNameCharacter character = scan_macro_name_character(lexer, has_name, name);
        if (character == MACRO_NAME_INVALID) return false;
        if (character == MACRO_NAME_END) return has_name;
        if (character == MACRO_NAME_SPLICE) {
            if (!has_name) continue;
            return scan_preproc_splices(lexer);
        }
        has_name = true;
        if (!name->lookahead_only) lexer->mark_end(lexer);
    }
}

static void scan_macro_name_start(TSLexer *lexer, bool lookahead_only) {
    while (pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
    if (lookahead_only) lexer->mark_end(lexer);
}

static MacroNameCharacter scan_macro_name_character(TSLexer *lexer, bool continuation, MacroName *name) {
    int32_t c = lexer->lookahead;
    if (c == '\\') {
        lexer->advance(lexer, false);
        if (scan_preproc_newline(lexer, !continuation)) return MACRO_NAME_SPLICE;
        name->ascii = false;
        return scan_macro_name_ucn(lexer) ? MACRO_NAME_CHARACTER : MACRO_NAME_INVALID;
    }
    if (!preproc_identifier_character(c, continuation)) return MACRO_NAME_END;
    if (name->ascii) {
        if (c < 128 && name->length < name->capacity) name->word[name->length++] = (char)c;
        else name->ascii = false;
    }
    lexer->advance(lexer, false);
    return MACRO_NAME_CHARACTER;
}

static bool scan_macro_name_ucn(TSLexer *lexer) {
    unsigned digits = lexer->lookahead == 'u' ? 4 : lexer->lookahead == 'U' ? 8 : 0;
    if (!digits) return false;
    lexer->advance(lexer, false);
    for (unsigned i = 0; i < digits; i++) {
        int32_t c = lexer->lookahead;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        lexer->advance(lexer, false);
    }
    return true;
}

#endif
