#ifndef TREE_SITTER_CPP_TRIVIA_H_
#define TREE_SITTER_CPP_TRIVIA_H_

#include "pragma.h"

#include <wctype.h>

static bool skip_cpp_comment(TSLexer *lexer, bool *newline);

static bool skip_cpp_trivia(TSLexer *lexer, bool *newline) {
    for (;;) {
        while (iswspace(lexer->lookahead)) {
            *newline |= lexer->lookahead == '\n' || lexer->lookahead == '\r';
            lexer->advance(lexer, false);
        }
        if (lexer->lookahead == '\\') {
            lexer->advance(lexer, false);
            if (!scan_preproc_newline(lexer, false)) return false;
        } else if (lexer->lookahead == '/') {
            if (!skip_cpp_comment(lexer, newline)) return false;
        } else {
            return true;
        }
    }
}

static bool skip_cpp_comment(TSLexer *lexer, bool *newline) {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '/') {
        int32_t previous = 0;
        while (!lexer->eof(lexer) && (lexer->lookahead != '\n' || previous == '\\')) {
            if (lexer->lookahead != '\r') previous = lexer->lookahead;
            lexer->advance(lexer, false);
        }
        return true;
    }
    if (lexer->lookahead != '*') return false;
    lexer->advance(lexer, false);
    bool star = false;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        *newline |= c == '\n' || c == '\r';
        lexer->advance(lexer, false);
        if (star && c == '/') return true;
        star = c == '*';
    }
    return false;
}

#endif
