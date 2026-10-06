#ifndef TREE_SITTER_C_PRAGMA_H_
#define TREE_SITTER_C_PRAGMA_H_

#include "tree_sitter/parser.h"
#include "identifier.h"

static const char pragma_word[] = "_Pragma";

static bool scan_pragma_spacing(TSLexer *lexer);
static bool scan_pragma_suffix(TSLexer *lexer);
static bool pragma_space(int32_t c);
static bool scan_pragma_word(TSLexer *lexer);
static bool scan_preproc_newline(TSLexer *lexer, bool skip);
static bool scan_preproc_splices(TSLexer *lexer);
static bool scan_preproc_ucn(TSLexer *lexer, bool *in_number);
static bool preproc_word(int32_t c, bool continuation);
static bool scan_preproc_quote(TSLexer *lexer, int32_t quote, bool escaped, bool directive_text);

static bool scan_pragma(TSLexer *lexer) {
    while (pragma_space(lexer->lookahead)) {
        lexer->advance(lexer, true);
    }
    if (!scan_pragma_word(lexer)) return false;
    return scan_pragma_suffix(lexer);
}

static bool scan_pragma_suffix(TSLexer *lexer) {
    if (!scan_pragma_spacing(lexer) || lexer->lookahead != '(') return false;
    lexer->advance(lexer, false);
    if (!scan_pragma_spacing(lexer)) return false;
    if (lexer->lookahead == 'L' || lexer->lookahead == 'u' || lexer->lookahead == 'U') {
        bool utf8 = lexer->lookahead == 'u';
        lexer->advance(lexer, false);
        if (utf8 && lexer->lookahead == '8') lexer->advance(lexer, false);
    }
    if (lexer->lookahead != '"') return false;
    lexer->advance(lexer, false);
    while (lexer->lookahead != '"') {
        if (lexer->eof(lexer) || lexer->lookahead == '\n' || lexer->lookahead == '\r') return false;
        if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (lexer->eof(lexer)) return false;
            if (scan_preproc_newline(lexer, false)) continue;
        }
        lexer->advance(lexer, false);
    }
    lexer->advance(lexer, false);
    if (!scan_pragma_spacing(lexer) || lexer->lookahead != ')') return false;
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    return true;
}

static bool scan_pragma_spacing(TSLexer *lexer) {
    for (;;) {
        if (pragma_space(lexer->lookahead)) {
            lexer->advance(lexer, false);
        } else if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (!scan_preproc_newline(lexer, false)) return false;
        } else if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '/') {
                lexer->advance(lexer, false);
                while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                    if (lexer->lookahead == '\\') {
                        do {
                            lexer->advance(lexer, false);
                        } while (lexer->lookahead == '\\');
                        if (lexer->lookahead == '\r') lexer->advance(lexer, false);
                        if (lexer->eof(lexer)) return false;
                    }
                    lexer->advance(lexer, false);
                }
            } else if (lexer->lookahead == '*') {
                lexer->advance(lexer, false);
                bool star = false;
                for (;;) {
                    if (lexer->eof(lexer)) return false;
                    if (star && lexer->lookahead == '/') {
                        lexer->advance(lexer, false);
                        break;
                    }
                    star = lexer->lookahead == '*';
                    lexer->advance(lexer, false);
                }
            } else {
                return false;
            }
        } else {
            return true;
        }
    }
}

static bool scan_preproc_arg(TSLexer *lexer, bool consumed_backslash, bool directive_text) {
    bool has_content = consumed_backslash;
    while (!has_content && pragma_space(lexer->lookahead) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
        lexer->advance(lexer, true);
    }
    bool after_comment = false;
    bool in_number = false;
    bool in_identifier = consumed_backslash && scan_preproc_ucn(lexer, &in_number);
    if (consumed_backslash) lexer->mark_end(lexer);
    int32_t number_last = 0;
    while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
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
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            bool split_opener = lexer->lookahead == '\\';
            bool delimiter = scan_preproc_splices(lexer);
            if (delimiter && lexer->lookahead == '/') {
                if (!has_content || after_comment) break;
                while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
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
                while (!lexer->eof(lexer)) {
                    if (lexer->lookahead == '\\') {
                        if (!scan_preproc_splices(lexer)) star = false;
                        else if (star && lexer->lookahead == '/') split_delimiter = true;
                        continue;
                    }
                    if (star && lexer->lookahead == '/') break;
                    star = lexer->lookahead == '*';
                    lexer->advance(lexer, false);
                }
                if (lexer->eof(lexer)) break;
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
            if (scan_preproc_ucn(lexer, &in_number)) {
                if (!in_number) in_identifier = true;
                number_last = 0;
            } else {
                in_number = false;
                in_identifier = false;
            }
            has_content = true;
            after_comment = false;
        } else if (lexer->lookahead == '"' || (!directive_text && lexer->lookahead == '\'')) {
            int32_t quote = lexer->lookahead;
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
            if (!scan_preproc_quote(lexer, quote, escaped, directive_text)) return true;
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

static bool scan_preproc_quote(TSLexer *lexer, int32_t quote, bool escaped, bool directive_text) {
    lexer->mark_end(lexer);
    bool literal = true;
    bool slash = false;
    bool block = false;
    bool line = false;
    bool star = false;
    bool split_comment = false;
    bool after_comment = false;
    while (!lexer->eof(lexer) && (block || (lexer->lookahead != '\n' && lexer->lookahead != '\r'))) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == '\n' || c == '\r') literal = false;
        if (literal && c == quote && !escaped) {
            lexer->mark_end(lexer);
            return true;
        }
        if (c == '\\' && scan_preproc_newline(lexer, false)) {
            if (slash || star) split_comment = true;
            if (!block && !slash && !after_comment) lexer->mark_end(lexer);
            continue;
        }
        escaped = c == '\\' && !escaped;
        if (line) {
            if (!after_comment) lexer->mark_end(lexer);
        } else if (block) {
            if (star && c == '/') {
                if (!literal) return false;
                block = false;
                after_comment = !split_comment;
                if (split_comment) lexer->mark_end(lexer);
                split_comment = false;
                star = false;
            } else {
                star = c == '*';
            }
        } else if (slash && c == '/') {
            line = directive_text;
            slash = false;
            if (!after_comment) lexer->mark_end(lexer);
        } else if (slash && c == '*') {
            block = true;
            slash = false;
        } else {
            if (slash || (c != '/' && !pragma_space(c))) after_comment = false;
            slash = c == '/';
            if (!slash) split_comment = false;
            if (!slash && !after_comment) lexer->mark_end(lexer);
        }
    }
    if (slash) lexer->mark_end(lexer);
    return false;
}

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

static bool pragma_space(int32_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v' ||
           c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

static bool scan_function_macro_name(TSLexer *lexer, bool allow_pragma, TSSymbol pragma_symbol) {
    bool has_name = false;
    bool is_pragma = true;
    unsigned pragma_length = 0;
    for (;;) {
        while (!has_name && pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
        int32_t c = lexer->lookahead;
        if (c == '\\') {
            lexer->advance(lexer, false);
            if (scan_preproc_newline(lexer, !has_name)) {
                if (!has_name) continue;
                if (!scan_preproc_splices(lexer)) return false;
                break;
            }
            is_pragma = false;
            unsigned digits = lexer->lookahead == 'u' ? 4 : lexer->lookahead == 'U' ? 8 : 0;
            if (!digits) return false;
            lexer->advance(lexer, false);
            for (unsigned i = 0; i < digits; i++) {
                c = lexer->lookahead;
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
                lexer->advance(lexer, false);
            }
        } else {
            bool identifier = has_name
                ? set_contains(preproc_identifier_continue, sizeof(preproc_identifier_continue) / sizeof(TSCharacterRange), c)
                : set_contains(preproc_identifier_start, sizeof(preproc_identifier_start) / sizeof(TSCharacterRange), c);
            if (!identifier) break;
            if (is_pragma) {
                if (pragma_length < sizeof(pragma_word) - 1 && c == pragma_word[pragma_length]) pragma_length++;
                else is_pragma = false;
            }
            lexer->advance(lexer, false);
        }
        has_name = true;
        lexer->mark_end(lexer);
    }
    bool adjacent = has_name && lexer->lookahead == '(';
    if (allow_pragma && is_pragma && pragma_length == sizeof(pragma_word) - 1 && scan_pragma_suffix(lexer)) {
        lexer->result_symbol = pragma_symbol;
        return true;
    }
    return adjacent;
}

#endif
