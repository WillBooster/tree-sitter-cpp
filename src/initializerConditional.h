#ifndef TREE_SITTER_CPP_INITIALIZER_CONDITIONAL_H_
#define TREE_SITTER_CPP_INITIALIZER_CONDITIONAL_H_

#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stdint.h>
#include <string.h>
#include <wctype.h>

typedef enum { INITIALIZER_UNKNOWN, INITIALIZER_EMPTY, INITIALIZER_NONEMPTY } InitializerConditionalKind;

typedef struct {
    uint32_t children[2];
    InitializerConditionalKind kind;
} InitializerConditionalNode;

typedef struct {
    uint32_t node;
    bool has_else;
} InitializerConditionalFrame;

typedef struct {
    InitializerConditionalNode *nodes;
    uint32_t node_count;
    uint32_t node_capacity;
    InitializerConditionalFrame *frames;
    uint32_t frame_count;
    uint32_t frame_capacity;
    bool invalid_directive_order;
} InitializerConditionals;

typedef enum { INITIALIZER_IF, INITIALIZER_ELSE, INITIALIZER_ELIF, INITIALIZER_ENDIF, INITIALIZER_OTHER } InitializerDirective;

static bool initializer_conditional_accept_header(InitializerConditionals *state, TSLexer *lexer, InitializerDirective directive);
static bool initializer_conditional_splice(TSLexer *lexer);
static uint32_t initializer_conditional_node(InitializerConditionals *state, uint32_t position);
static void initializer_conditional_push(InitializerConditionals *state, uint32_t node);
static bool initializer_conditional_nonempty(InitializerConditionals *state);
static InitializerDirective initializer_conditional_directive(TSLexer *lexer);
static bool initializer_conditional_header(TSLexer *lexer);
static bool initializer_conditional_header_predicate(TSLexer *lexer);
static void initializer_conditional_literal(TSLexer *lexer, int32_t closing);
static bool initializer_conditional_trivia(TSLexer *lexer);
static bool initializer_conditional_comment(TSLexer *lexer);

// code-gauge-allow functionCognitiveComplexity 19 -- The proof loop keeps directive order and memo updates together; header validation is already separate.
static bool scan_empty_initializer_conditional(InitializerConditionals *state, TSLexer *lexer) {
    if (state->invalid_directive_order) return false;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    uint32_t first = initializer_conditional_node(state, position.bytes);
    if (state->nodes[first].kind != INITIALIZER_UNKNOWN) return state->nodes[first].kind == INITIALIZER_EMPTY;
    state->frame_count = 0;
    for (;;) {
        if (!initializer_conditional_trivia(lexer) || lexer->lookahead != '#') {
            return initializer_conditional_nonempty(state);
        }
        lexer->get_position(lexer, &position);
        InitializerDirective directive = initializer_conditional_directive(lexer);
        if (directive == INITIALIZER_OTHER || (!state->frame_count && directive != INITIALIZER_IF)) {
            return initializer_conditional_nonempty(state);
        }
        if (directive == INITIALIZER_IF) {
            uint32_t node = initializer_conditional_node(state, position.bytes);
            if (state->nodes[node].kind == INITIALIZER_NONEMPTY) return initializer_conditional_nonempty(state);
            initializer_conditional_push(state, node);
        }
        if (!initializer_conditional_accept_header(state, lexer, directive)) {
            return initializer_conditional_nonempty(state);
        }
        if (!state->frame_count) return true;
    }
}

static bool initializer_conditional_accept_header(InitializerConditionals *state, TSLexer *lexer, InitializerDirective directive) {
    if (directive == INITIALIZER_IF) return initializer_conditional_header(lexer);
    if (directive == INITIALIZER_ENDIF) {
        if (initializer_conditional_header(lexer)) return false;
        state->nodes[state->frames[--state->frame_count].node].kind = INITIALIZER_EMPTY;
        return true;
    }
    InitializerConditionalFrame *frame = &state->frames[state->frame_count - 1];
    if (frame->has_else) {
        state->invalid_directive_order = true;
        return false;
    }
    frame->has_else = directive == INITIALIZER_ELSE;
    return initializer_conditional_header(lexer) != frame->has_else;
}

static bool initializer_conditional_splice(TSLexer *lexer) {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '\n' && lexer->lookahead != '\r') return false;
    int32_t newline = lexer->lookahead;
    lexer->advance(lexer, false);
    if ((newline == '\r' && lexer->lookahead == '\n') ||
        (newline == '\n' && lexer->lookahead == '\r')) lexer->advance(lexer, false);
    return true;
}

static uint32_t initializer_conditional_node(InitializerConditionals *state, uint32_t position) {
    if (!state->node_count) {
        state->node_capacity = 64;
        state->nodes = ts_calloc(state->node_capacity, sizeof(*state->nodes));
        state->node_count = 1;
    }
    uint32_t node = 0;
    for (int bit = 31; bit >= 0; bit--) {
        uint32_t branch = (position >> bit) & 1;
        uint32_t child = state->nodes[node].children[branch];
        if (!child) {
            if (state->node_count == state->node_capacity) {
                uint32_t previous_capacity = state->node_capacity;
                state->node_capacity *= 2;
                state->nodes = ts_realloc(state->nodes, state->node_capacity * sizeof(*state->nodes));
                memset(state->nodes + previous_capacity, 0,
                       (state->node_capacity - previous_capacity) * sizeof(*state->nodes));
            }
            child = state->node_count++;
            state->nodes[node].children[branch] = child;
        }
        node = child;
    }
    return node;
}

static void initializer_conditional_push(InitializerConditionals *state, uint32_t node) {
    if (state->frame_count == state->frame_capacity) {
        state->frame_capacity = state->frame_capacity ? state->frame_capacity * 2 : 16;
        state->frames = ts_realloc(state->frames, state->frame_capacity * sizeof(*state->frames));
    }
    state->frames[state->frame_count++] = (InitializerConditionalFrame){.node = node};
}

static bool initializer_conditional_nonempty(InitializerConditionals *state) {
    for (uint32_t i = 0; i < state->frame_count; i++) {
        state->nodes[state->frames[i].node].kind = INITIALIZER_NONEMPTY;
    }
    return false;
}

static InitializerDirective initializer_conditional_directive(TSLexer *lexer) {
    lexer->advance(lexer, false);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, false);
    char name[9] = {0};
    unsigned length = 0;
    while (lexer->lookahead >= 'a' && lexer->lookahead <= 'z') {
        if (length == sizeof(name) - 1) return INITIALIZER_OTHER;
        name[length++] = (char)lexer->lookahead;
        lexer->advance(lexer, false);
    }
    if (!strcmp(name, "if") || !strcmp(name, "ifdef") || !strcmp(name, "ifndef")) return INITIALIZER_IF;
    if (!strcmp(name, "else")) return INITIALIZER_ELSE;
    if (!strcmp(name, "elif") || !strcmp(name, "elifdef") || !strcmp(name, "elifndef")) return INITIALIZER_ELIF;
    if (!strcmp(name, "endif")) return INITIALIZER_ENDIF;
    return INITIALIZER_OTHER;
}

// code-gauge-allow functionCognitiveComplexity 20 -- Token categories keep comments, literals and header names in one line scan.
static bool initializer_conditional_header(TSLexer *lexer) {
    bool content = false;
    bool header_predicate = false;
    while (!lexer->eof(lexer)) {
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            lexer->advance(lexer, false);
            return content;
        }
        if (lexer->lookahead == '\\') {
            content |= !initializer_conditional_splice(lexer);
        } else if (lexer->lookahead == '/') {
            content |= !initializer_conditional_comment(lexer);
        } else if (lexer->lookahead == '\'' || lexer->lookahead == '"' || (header_predicate && lexer->lookahead == '<')) {
            initializer_conditional_literal(lexer, lexer->lookahead == '<' ? '>' : lexer->lookahead);
            content = true;
            header_predicate = false;
        } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
            header_predicate = initializer_conditional_header_predicate(lexer);
            content = true;
        } else {
            bool token = !iswspace(lexer->lookahead);
            content |= token;
            if (token && lexer->lookahead != '(') header_predicate = false;
            lexer->advance(lexer, false);
        }
    }
    return content;
}

static bool initializer_conditional_header_predicate(TSLexer *lexer) {
    char word[32] = {0};
    unsigned length = 0;
    bool ascii = true;
    while (iswalnum(lexer->lookahead) || lexer->lookahead == '_') {
        if (length < sizeof(word) - 1 && lexer->lookahead < 0x80) word[length++] = (char)lexer->lookahead;
        else ascii = false;
        lexer->advance(lexer, false);
    }
    return ascii && (!strcmp(word, "__has_include") || !strcmp(word, "__has_include_next") || !strcmp(word, "__has_embed"));
}

static void initializer_conditional_literal(TSLexer *lexer, int32_t closing) {
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
        if (lexer->lookahead == '\\') {
            bool splice = initializer_conditional_splice(lexer);
            if (!splice && closing != '>' && !lexer->eof(lexer)) lexer->advance(lexer, false);
        } else {
            int32_t c = lexer->lookahead;
            lexer->advance(lexer, false);
            if (c == closing) return;
        }
    }
}

static bool initializer_conditional_trivia(TSLexer *lexer) {
    while (!lexer->eof(lexer)) {
        if (iswspace(lexer->lookahead)) {
            lexer->advance(lexer, false);
        } else if (lexer->lookahead == '/') {
            if (!initializer_conditional_comment(lexer)) return false;
        } else if (lexer->lookahead == '\\') {
            if (!initializer_conditional_splice(lexer)) return false;
        } else {
            return true;
        }
    }
    return false;
}

static bool initializer_conditional_comment(TSLexer *lexer) {
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
        lexer->advance(lexer, false);
        if (star && c == '/') return true;
        star = c == '*';
    }
    return false;
}

static void delete_initializer_conditionals(InitializerConditionals *state) {
    ts_free(state->nodes);
    ts_free(state->frames);
}

#endif
