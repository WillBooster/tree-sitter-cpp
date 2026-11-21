#ifndef TREE_SITTER_C_MACRO_PROOF_H_
#define TREE_SITTER_C_MACRO_PROOF_H_

#include "pragma.h"
#include <stdlib.h>

typedef struct { void *children[256]; } MacroProofNode;
typedef struct { uint32_t values[256]; } MacroProofPage;
typedef struct { uint32_t byte; uint8_t state; } MacroProofKey;
typedef struct {
    MacroProofNode *root;
    MacroProofKey *trace;
    uint32_t length, capacity;
    const char *const *reserved_words;
    unsigned reserved_count;
    bool (*skip_empty)(TSLexer *, void *);
    void *empty_context;
    bool bound;
} MacroProofMemo;

static MacroProofPage *macro_proof_page(MacroProofMemo *memo, uint32_t byte, bool create);
static void macro_proof_append(MacroProofMemo *memo, MacroProofKey key);
static void macro_proof_free_node(MacroProofNode *node, unsigned level);

static bool macro_proof_begin(MacroProofMemo *memo, const char *const *reserved_words, unsigned reserved_count, const PragmaTrivia *trivia) {
    if (!memo) return false;
    if (memo->bound && (memo->reserved_words != reserved_words || memo->reserved_count != reserved_count ||
        memo->skip_empty != trivia->skip_empty || memo->empty_context != trivia->empty_context)) return false;
    memo->bound = true;
    memo->reserved_words = reserved_words;
    memo->reserved_count = reserved_count;
    memo->skip_empty = trivia->skip_empty;
    memo->empty_context = trivia->empty_context;
    memo->length = 0;
    return true;
}

static unsigned macro_proof_step(MacroProofMemo *memo, TSLexer *lexer, uint8_t state) {
    if (!memo) return 0;
    TSLexerPosition position;
    lexer->get_position(lexer, &position);
    MacroProofPage *page = macro_proof_page(memo, position.bytes, false);
    unsigned result = page ? (page->values[position.bytes & 255] >> (2 * state)) & 3 : 0;
    if (!result) macro_proof_append(memo, (MacroProofKey){position.bytes, state});
    return result;
}

static void macro_proof_finish(MacroProofMemo *memo, bool accepted) {
    if (!memo) return;
    for (uint32_t i = 0; i < memo->length; i++) {
        MacroProofKey key = memo->trace[i];
        MacroProofPage *page = macro_proof_page(memo, key.byte, true);
        if (!page) continue;
        unsigned shift = 2 * key.state;
        uint32_t *value = &page->values[key.byte & 255];
        *value = (*value & ~(3u << shift)) | ((accepted ? 2u : 1u) << shift);
    }
}

static void macro_proof_destroy(MacroProofMemo *memo) {
    if (!memo) return;
    if (memo->root) {
        macro_proof_free_node(memo->root, 3);
        free(memo->root);
    }
    free(memo->trace);
}

static MacroProofPage *macro_proof_page(MacroProofMemo *memo, uint32_t byte, bool create) {
    if (!memo->root && create) memo->root = calloc(1, sizeof(*memo->root));
    MacroProofNode *node = memo->root;
    if (!node) return NULL;
    for (unsigned level = 3; level > 0; level--) {
        unsigned index = (byte >> (8 * level)) & 255;
        if (!node->children[index] && create) {
            node->children[index] = calloc(1, level == 1 ? sizeof(MacroProofPage) : sizeof(MacroProofNode));
        }
        if (!node->children[index]) return NULL;
        if (level == 1) return (MacroProofPage *)node->children[index];
        node = (MacroProofNode *)node->children[index];
    }
    return NULL;
}

static void macro_proof_append(MacroProofMemo *memo, MacroProofKey key) {
    if (memo->length == memo->capacity) {
        uint32_t next = memo->capacity ? memo->capacity * 2 : 64;
        if (next < memo->capacity || (size_t)next > SIZE_MAX / sizeof(*memo->trace)) return;
        void *trace = realloc(memo->trace, (size_t)next * sizeof(*memo->trace));
        if (!trace) return;
        memo->trace = (MacroProofKey *)trace;
        memo->capacity = next;
    }
    memo->trace[memo->length++] = key;
}

static void macro_proof_free_node(MacroProofNode *node, unsigned level) {
    for (unsigned i = 0; i < 256; i++) {
        if (!node->children[i]) continue;
        if (level > 1) macro_proof_free_node((MacroProofNode *)node->children[i], level - 1);
        free(node->children[i]);
    }
}

#endif
