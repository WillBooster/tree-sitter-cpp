// code-gauge-allow-file fileNcss -- Annotation and argument scanners share lexical helpers and keyword tables.
#ifndef TREE_SITTER_C_FUNCTION_MACRO_H_
#define TREE_SITTER_C_FUNCTION_MACRO_H_

#include "pragma.h"
#include "keywords.h"
#include "macroProof.h"

typedef struct {
    bool identifier;
    bool number;
    int32_t last;
    GnuRawStringPrefix raw_prefix;
} MacroArgumentWord;

typedef enum {
    MACRO_ARGUMENT_PLAIN,
    MACRO_ARGUMENT_CONSUMED,
    MACRO_ARGUMENT_INVALID,
} MacroArgumentToken;

typedef struct {
    bool post_type;
    bool declarator_group;
    bool explicit_type;
    bool modifier;
    bool declarator_name;
    bool unterminated_raw;
} MacroPrototypeTail;

typedef struct {
    PragmaTrivia trivia;
    MacroProofMemo *proof_memo;
    TSSymbol pragma;
    TSSymbol object_start;
    bool allow_pragma;
    bool allow_object_start;
    TSSymbol braced_start;
    bool allow_braced_start;
} MacroAttributeSymbols;

typedef enum {
    MACRO_GROUP_INVALID, MACRO_GROUP_BALANCED, MACRO_GROUP_BRACED,
} MacroGroup;

static MacroArgumentToken function_macro_argument_token(TSLexer *lexer, MacroArgumentWord *word, bool *unterminated_raw);

static bool function_macro_balanced(TSLexer *lexer, int32_t open, int32_t close, bool *literal, bool *unterminated_raw, const PragmaTrivia *trivia);
static bool function_macro_modifier_balanced(TSLexer *lexer, int32_t open, int32_t close, const PragmaTrivia *trivia);
static MacroGroup function_macro_group_prefix(TSLexer *lexer, int32_t open, int32_t close, bool *literal, bool *unterminated_raw, bool type_argument, const PragmaTrivia *trivia);
static MacroGroup function_macro_group_contents(TSLexer *lexer, int32_t open, int32_t close, bool *literal, bool *unterminated_raw, bool type_argument, bool modifier, const PragmaTrivia *trivia);
static bool function_macro_group_separator(int32_t c, unsigned depth, unsigned nested, bool type_argument, unsigned braces);
static bool function_macro_type_prefix_keyword(const MacroName *name, const char *const *reserved_words, unsigned reserved_count);
static bool function_macro_group(int32_t c, int32_t open, int32_t close, unsigned *depth, unsigned *nested, unsigned *braces);
static void function_macro_note_literal(bool *literal, unsigned depth, unsigned nested, const MacroArgumentWord *word, int32_t c);
static bool function_macro_tail_word(TSLexer *lexer, bool escaped, MacroPrototypeTail *tail);
static MacroGroup function_macro_tail_group(TSLexer *lexer, int32_t c, MacroPrototypeTail *tail, const PragmaTrivia *trivia);
static bool function_macro_tail_punctuator(TSLexer *lexer, int32_t c, MacroPrototypeTail *tail);
static bool function_macro_declaration_continues(TSLexer *lexer, bool post_type, bool allow_braced, const PragmaTrivia *trivia);
static bool function_macro_argument_quote(TSLexer *lexer, int32_t quote, bool raw, bool *unterminated_raw);
static bool function_macro_argument_escape(TSLexer *lexer, MacroArgumentWord *word);
static bool function_macro_argument_slash(TSLexer *lexer, bool allow_division);
static bool function_macro_tail_spacing(TSLexer *lexer, bool *escaped, const PragmaTrivia *trivia);
static void function_macro_argument_word(MacroArgumentWord *word, int32_t c);

static bool function_macro_trailing_argument_prefix(TSLexer *lexer, const PragmaTrivia *trivia);
static bool function_macro_object_prefix(TSLexer *lexer, const PragmaTrivia *trivia);
static bool function_macro_object_word(TSLexer *lexer, MacroName *name, bool escaped);
static bool function_macro_scalar_type_keyword(const char *word);
static bool function_macro_object_prefix_keyword(const char *word);
static bool function_macro_attribute_keyword(const char *word, bool trailing);
static bool function_macro_keyword(const char *word);
static bool function_macro_word_in(const char *word, const char *const *words, unsigned count);

// code-gauge-allow functionCognitiveComplexity -- Balanced delimiter recovery, trivia, and braced handoff share one consumed group.
// code-gauge-allow functionNcss -- Balanced delimiter recovery, trivia, and braced handoff share one consumed group.
static inline bool scan_raw_macro_type_tail(TSLexer *lexer, const PragmaTrivia *trivia) {
    while (pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
    if (lexer->lookahead == ')' || pragma_eof(lexer)) return false;
    unsigned depth = 1;
    unsigned nested = 0;
    unsigned braces = 0;
    bool balanced = true;
    MacroArgumentWord word = {0};
    while (!pragma_eof(lexer)) {
        if (scan_pragma_empty(lexer, trivia)) {
            word = (MacroArgumentWord){0};
            continue;
        }
        int32_t c = lexer->lookahead;
        if (c == ')' && depth == 1) break;
        if (function_macro_argument_token(lexer, &word, NULL) != MACRO_ARGUMENT_PLAIN) continue;
        if (c == '{' && balanced) return false;
        if (balanced) balanced = function_macro_group(c, '(', ')', &depth, &nested, &braces);
        else if (c == '(') depth++;
        else if (c == ')') depth--;
        function_macro_argument_word(&word, c);
        lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    return true;
}

static bool function_macro_first_word(TSLexer *lexer, const MacroName *name, const MacroAttributeSymbols *symbols, bool *handled) {
    const PragmaTrivia *trivia = &symbols->trivia;
    *handled = true;
    if (name->ascii && strcmp(name->word, pragma_word) == 0) {
        lexer->result_symbol = symbols->pragma;
        return symbols->allow_pragma && scan_pragma_suffix_end_with(lexer, true, trivia);
    }
    if (symbols->allow_object_start && name->ascii && function_macro_scalar_type_keyword(name->word)) {
        lexer->result_symbol = symbols->object_start;
        return function_macro_object_prefix(lexer, trivia);
    }
    *handled = false;
    return false;
}

// code-gauge-allow functionCognitiveComplexity -- Attribute roles share first-word dispatch, token ranges, and group results.
// code-gauge-allow functionNcss -- Attribute roles share first-word dispatch, token ranges, and group results.
static bool scan_function_attribute_guard(TSLexer *lexer, bool prototype, bool require_literal, bool trailing, const MacroAttributeSymbols *symbols) {
    const PragmaTrivia *trivia = &symbols->trivia;
    bool braced_only = symbols->allow_braced_start && lexer->result_symbol == symbols->braced_start;
    char word[FUNCTION_MACRO_KEYWORD_MAX_LENGTH + 1] = {0};
    MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = true, .lookahead_only = !trailing};
    if (!scan_macro_name(lexer, &name)) return false;
    bool handled;
    bool accepted = function_macro_first_word(lexer, &name, symbols, &handled);
    if (handled) return accepted;
    if (lexer->result_symbol == symbols->object_start) return false;
    if (name.ascii && function_macro_attribute_keyword(word, trailing)) return false;
    if (!scan_pragma_spacing_with(lexer, trivia) || lexer->lookahead != '(') return false;
    if (trailing) return function_macro_trailing_argument_prefix(lexer, trivia);
    bool literal = false;
    bool unterminated_raw = false;
    MacroGroup group = function_macro_group_prefix(lexer, '(', ')', &literal, &unterminated_raw, false, trivia);
    if (group == MACRO_GROUP_BRACED && symbols->allow_braced_start) {
        lexer->result_symbol = symbols->braced_start;
        return true;
    }
    if (braced_only) return false;
    if (unterminated_raw) return true;
    if (group != MACRO_GROUP_BALANCED) return false;
    return (!require_literal || literal) && (!prototype || function_macro_declaration_continues(lexer, require_literal, symbols->allow_braced_start, trivia));
}

static bool function_macro_bracket_attribute(TSLexer *lexer, const PragmaTrivia *trivia) {
    lexer->advance(lexer, false);
    if (!scan_pragma_spacing_with(lexer, trivia) || lexer->lookahead != '[') return false;
    if (!function_macro_modifier_balanced(lexer, '[', ']', trivia) ||
        !scan_pragma_spacing_with(lexer, trivia) || lexer->lookahead != ']') return false;
    lexer->advance(lexer, false);
    return scan_pragma_spacing_with(lexer, trivia);
}

static bool function_macro_type_modifier_word(const MacroName *name) {
    return name->ascii && function_macro_word_in(name->word, function_macro_group_modifiers,
        sizeof(function_macro_group_modifiers) / sizeof(function_macro_group_modifiers[0])) &&
        !function_macro_word_in(name->word, function_macro_type_keywords,
            sizeof(function_macro_type_keywords) / sizeof(function_macro_type_keywords[0]));
}

typedef enum {
    MACRO_TYPE_PREFIX_INVALID, MACRO_TYPE_PREFIX_MODIFIER, MACRO_TYPE_PREFIX_MARKER, MACRO_TYPE_PREFIX_TYPE,
    MACRO_TYPE_PREFIX_KNOWN_TYPE, MACRO_TYPE_PREFIX_BALANCED_TYPE, MACRO_TYPE_PREFIX_TAGGED_TYPE,
} MacroTypePrefix;

typedef struct {
    bool marker, known_type, tag, body, started;
} MacroTypeState;

typedef enum { MACRO_TYPE_CONTINUE, MACRO_TYPE_ACCEPT, MACRO_TYPE_REJECT } MacroTypeResult;

static MacroTypePrefix function_macro_type_prefix_item(TSLexer *lexer, const MacroName *name, const char *const *reserved_words, unsigned reserved_count, const MacroTypeState *state, const PragmaTrivia *trivia);
static MacroTypePrefix function_macro_known_type_item(TSLexer *lexer, const MacroName *name, const PragmaTrivia *trivia);
static MacroTypePrefix function_macro_macro_type_item(TSLexer *lexer, const MacroTypeState *state, const PragmaTrivia *trivia);

static MacroTypeResult function_macro_type_transition(MacroTypeState *state, MacroTypePrefix kind);
static MacroTypeResult function_macro_type_group(TSLexer *lexer, MacroTypeState *state, const PragmaTrivia *trivia);
static MacroTypeResult function_macro_type_word(TSLexer *lexer, const MacroAttributeSymbols *symbols,
    const char *const *reserved_words, unsigned reserved_count, MacroTypeState *state);

static bool function_macro_prove_braced_type(TSLexer *lexer, const MacroAttributeSymbols *symbols,
    const char *const *reserved_words, unsigned reserved_count, MacroProofMemo *memo);

static bool scan_braced_macro_type_start(TSLexer *lexer, const MacroAttributeSymbols *symbols, const char *const *reserved_words, unsigned reserved_count) {
    MacroProofMemo *memo = symbols->proof_memo;
    if (!macro_proof_begin(memo, reserved_words, reserved_count, &symbols->trivia)) memo = NULL;
    bool accepted = function_macro_prove_braced_type(lexer, symbols, reserved_words, reserved_count, memo);
    macro_proof_finish(memo, accepted);
    return accepted;
}

static bool function_macro_prove_braced_type(TSLexer *lexer, const MacroAttributeSymbols *symbols,
    const char *const *reserved_words, unsigned reserved_count, MacroProofMemo *memo) {
    const PragmaTrivia *trivia = &symbols->trivia;
    MacroTypeState state = {0};
    for (;;) {
        while (pragma_space(lexer->lookahead)) lexer->advance(lexer, true);
        uint8_t key = state.marker | (state.known_type << 1) | (state.tag << 2) | (state.body << 3);
        unsigned cached = state.started ? macro_proof_step(memo, lexer, key) : 0;
        if (cached) return cached == 2;
        if (scan_pragma_empty(lexer, trivia)) continue;
        MacroTypeResult result = lexer->lookahead == '[' || lexer->lookahead == '{'
            ? function_macro_type_group(lexer, &state, trivia)
            : function_macro_type_word(lexer, symbols, reserved_words, reserved_count, &state);
        if (result != MACRO_TYPE_CONTINUE) return result == MACRO_TYPE_ACCEPT;
    }
}

static MacroTypeResult function_macro_type_group(TSLexer *lexer, MacroTypeState *state, const PragmaTrivia *trivia) {
    if (lexer->lookahead == '[') {
        if (!state->started) lexer->mark_end(lexer);
        state->started = true;
        return function_macro_bracket_attribute(lexer, trivia) ? MACRO_TYPE_CONTINUE : MACRO_TYPE_REJECT;
    }
    if (!state->body || !function_macro_modifier_balanced(lexer, '{', '}', trivia)) return MACRO_TYPE_REJECT;
    state->body = state->tag = false;
    return MACRO_TYPE_CONTINUE;
}

static MacroTypeResult function_macro_type_word(TSLexer *lexer, const MacroAttributeSymbols *symbols,
    const char *const *reserved_words, unsigned reserved_count, MacroTypeState *state) {
    char word[FUNCTION_MACRO_KEYWORD_MAX_LENGTH + 1] = {0};
    MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = true, .lookahead_only = true, .preserve_end = state->started};
    if (!scan_macro_name(lexer, &name)) return MACRO_TYPE_REJECT;
    if (!state->started) {
        bool handled;
        bool accepted = function_macro_first_word(lexer, &name, symbols, &handled);
        if (handled) return accepted ? MACRO_TYPE_ACCEPT : MACRO_TYPE_REJECT;
    }
    state->started = true;
    MacroTypePrefix kind = function_macro_type_prefix_item(lexer, &name, reserved_words, reserved_count, state, &symbols->trivia);
    return function_macro_type_transition(state, kind);
}

static MacroTypeResult function_macro_type_transition(MacroTypeState *state, MacroTypePrefix kind) {
    switch (kind) {
        case MACRO_TYPE_PREFIX_INVALID:
            return MACRO_TYPE_REJECT;
        case MACRO_TYPE_PREFIX_TYPE:
            return state->marker ? MACRO_TYPE_ACCEPT : MACRO_TYPE_REJECT;
        case MACRO_TYPE_PREFIX_MODIFIER:
            return MACRO_TYPE_CONTINUE;
        case MACRO_TYPE_PREFIX_MARKER:
            if (state->known_type && !state->tag) return MACRO_TYPE_REJECT;
            state->marker |= !state->known_type;
            state->body = state->tag;
            state->tag = false;
            return MACRO_TYPE_CONTINUE;
        case MACRO_TYPE_PREFIX_KNOWN_TYPE:
        case MACRO_TYPE_PREFIX_BALANCED_TYPE:
        case MACRO_TYPE_PREFIX_TAGGED_TYPE:
            if (!state->marker || (state->known_type && kind == MACRO_TYPE_PREFIX_BALANCED_TYPE)) return MACRO_TYPE_REJECT;
            state->known_type = true;
            state->tag = state->body = kind == MACRO_TYPE_PREFIX_TAGGED_TYPE;
            return MACRO_TYPE_CONTINUE;
    }
    return MACRO_TYPE_REJECT;
}

// code-gauge-allow functionCognitiveComplexity -- Keyword, modifier, and group alternatives classify one consumed type-prefix word.
static MacroTypePrefix function_macro_type_prefix_item(TSLexer *lexer, const MacroName *name, const char *const *reserved_words, unsigned reserved_count, const MacroTypeState *state, const PragmaTrivia *trivia) {
    if (name->ascii && strcmp(name->word, pragma_word) == 0) {
        return scan_pragma_suffix_end_with(lexer, false, trivia) && scan_pragma_spacing_with(lexer, trivia)
            ? MACRO_TYPE_PREFIX_MODIFIER : MACRO_TYPE_PREFIX_INVALID;
    }
    if (!scan_pragma_spacing_with(lexer, trivia)) return MACRO_TYPE_PREFIX_INVALID;
    if (function_macro_type_modifier_word(name)) {
        return lexer->lookahead == '(' && function_macro_modifier_balanced(lexer, '(', ')', trivia) && scan_pragma_spacing_with(lexer, trivia)
            ? MACRO_TYPE_PREFIX_MODIFIER : MACRO_TYPE_PREFIX_INVALID;
    }
    // Before a marker, function_macro_type_transition rejects every type, so its parentheses need not be scanned.
    if (name->ascii && function_macro_word_in(name->word, function_macro_type_keywords,
        sizeof(function_macro_type_keywords) / sizeof(function_macro_type_keywords[0]))) {
        return state->marker ? function_macro_known_type_item(lexer, name, trivia) : MACRO_TYPE_PREFIX_INVALID;
    }
    if (name->ascii && function_macro_word_in(name->word, function_macro_type_prefix_modifiers,
        sizeof(function_macro_type_prefix_modifiers) / sizeof(function_macro_type_prefix_modifiers[0]))) return MACRO_TYPE_PREFIX_MODIFIER;
    if (function_macro_type_prefix_keyword(name, reserved_words, reserved_count)) return MACRO_TYPE_PREFIX_INVALID;
    if (lexer->lookahead == '(') {
        return state->marker ? function_macro_macro_type_item(lexer, state, trivia) : MACRO_TYPE_PREFIX_INVALID;
    }
    return MACRO_TYPE_PREFIX_MARKER;
}

static MacroTypePrefix function_macro_known_type_item(TSLexer *lexer, const MacroName *name, const PragmaTrivia *trivia) {
    if (lexer->lookahead == '(' && (!function_macro_modifier_balanced(lexer, '(', ')', trivia) || !scan_pragma_spacing_with(lexer, trivia))) return MACRO_TYPE_PREFIX_INVALID;
    if (strcmp(name->word, "struct") == 0 || strcmp(name->word, "union") == 0 || strcmp(name->word, "enum") == 0) return MACRO_TYPE_PREFIX_TAGGED_TYPE;
    return MACRO_TYPE_PREFIX_KNOWN_TYPE;
}

static MacroTypePrefix function_macro_macro_type_item(TSLexer *lexer, const MacroTypeState *state, const PragmaTrivia *trivia) {
    MacroGroup group = function_macro_group_prefix(lexer, '(', ')', NULL, NULL, !state->known_type, trivia);
    if (group == MACRO_GROUP_BRACED) return MACRO_TYPE_PREFIX_TYPE;
    if (group == MACRO_GROUP_BALANCED && scan_pragma_spacing_with(lexer, trivia)) return MACRO_TYPE_PREFIX_BALANCED_TYPE;
    return MACRO_TYPE_PREFIX_INVALID;
}

// code-gauge-allow functionCognitiveComplexity -- Literal prefixes inspect one consumed first argument token without restarting the lexer.
static bool function_macro_trailing_argument_prefix(TSLexer *lexer, const PragmaTrivia *trivia) {
    do {
        lexer->advance(lexer, false);
        if (!scan_pragma_spacing_with(lexer, trivia)) return false;
    } while (lexer->lookahead == '(');
    int32_t first = lexer->lookahead;
    if (first == '.') {
        lexer->advance(lexer, false);
        first = lexer->lookahead;
        return first >= '0' && first <= '9';
    }
    if (first == 'R' || first == 'L' || first == 'u' || first == 'U') {
        GnuRawStringPrefix prefix = {0};
        while (lexer->lookahead != '"' && prefix.length < sizeof(prefix.value)) {
            gnu_raw_string_prefix_character(&prefix, lexer->lookahead);
            lexer->advance(lexer, false);
            if (!scan_preproc_splices(lexer)) return false;
        }
        return lexer->lookahead == '"' && gnu_raw_string_prefix(&prefix);
    }
    return first == ',' || first == '"' || first == '\'' || (first >= '0' && first <= '9');
}

static bool function_macro_object_prefix(TSLexer *lexer, const PragmaTrivia *trivia) {
    bool escaped = false;
    if (!function_macro_tail_spacing(lexer, &escaped, trivia)) return false;
    while (!escaped && lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        if (!function_macro_tail_spacing(lexer, &escaped, trivia)) return false;
    }
    char word[FUNCTION_MACRO_KEYWORD_MAX_LENGTH + 1] = {0};
    MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = true};
    if (!function_macro_object_word(lexer, &name, escaped) ||
        (name.ascii && (function_macro_object_prefix_keyword(word) || strcmp(word, pragma_word) == 0))) return false;
    escaped = false;
    if (!function_macro_tail_spacing(lexer, &escaped, trivia)) return false;
    name = (MacroName){.word = word, .capacity = sizeof(word) - 1, .ascii = true};
    if (!function_macro_object_word(lexer, &name, escaped) || (name.ascii && function_macro_attribute_keyword(word, true))) return false;
    return scan_pragma_spacing_with(lexer, trivia) && lexer->lookahead == '(' && function_macro_trailing_argument_prefix(lexer, trivia);
}

static bool function_macro_object_word(TSLexer *lexer, MacroName *name, bool escaped) {
    memset(name->word, 0, name->capacity + 1);
    if (escaped) {
        name->ascii = false;
        if (!scan_macro_name_ucn(lexer)) return false;
    }
    bool continuation = escaped;
    for (;;) {
        MacroNameCharacter c = scan_macro_name_character(lexer, continuation, name);
        if (c == MACRO_NAME_END) return continuation;
        if (c == MACRO_NAME_SPLICE && continuation) return scan_preproc_splices(lexer);
        if (c != MACRO_NAME_CHARACTER) return false;
        continuation = true;
    }
}

static bool function_macro_scalar_type_keyword(const char *word) {
    return function_macro_word_in(word, function_macro_scalar_type_keywords,
        sizeof(function_macro_scalar_type_keywords) / sizeof(function_macro_scalar_type_keywords[0]));
}

static bool function_macro_object_prefix_keyword(const char *word) {
    return function_macro_word_in(word, function_macro_object_prefix_keywords,
        sizeof(function_macro_object_prefix_keywords) / sizeof(function_macro_object_prefix_keywords[0]));
}

static bool function_macro_attribute_keyword(const char *word, bool trailing) {
    if (!trailing) return function_macro_keyword(word);
    return strcmp(word, pragma_word) == 0 || function_macro_word_in(word, function_macro_group_modifiers,
        sizeof(function_macro_group_modifiers) / sizeof(function_macro_group_modifiers[0]));
}

static bool function_macro_balanced(TSLexer *lexer, int32_t open, int32_t close, bool *literal, bool *unterminated_raw, const PragmaTrivia *trivia) {
    return function_macro_group_prefix(lexer, open, close, literal, unterminated_raw, false, trivia) == MACRO_GROUP_BALANCED;
}

static bool function_macro_modifier_balanced(TSLexer *lexer, int32_t open, int32_t close, const PragmaTrivia *trivia) {
    lexer->advance(lexer, false);
    return function_macro_group_contents(lexer, open, close, NULL, NULL, false, true, trivia) == MACRO_GROUP_BALANCED;
}

// code-gauge-allow functionParameterCount -- One group proof uses its delimiter pair, optional outputs, grammar mode, and trivia context.
static MacroGroup function_macro_group_prefix(TSLexer *lexer, int32_t open, int32_t close, bool *literal, bool *unterminated_raw, bool type_argument, const PragmaTrivia *trivia) {
    lexer->advance(lexer, false);
    if (type_argument && (!scan_pragma_spacing_with(lexer, trivia) || (!preproc_word(lexer->lookahead, false) && lexer->lookahead != '\\'))) {
        return MACRO_GROUP_INVALID;
    }
    return function_macro_group_contents(lexer, open, close, literal, unterminated_raw, type_argument, false, trivia);
}

// code-gauge-allow functionCognitiveComplexity -- Trivia and delimiter alternatives update the same nesting and argument-word state.
// code-gauge-allow functionParameterCount -- The shared group lexer uses its delimiter pair, optional outputs, two grammar modes, and trivia context.
static MacroGroup function_macro_group_contents(TSLexer *lexer, int32_t open, int32_t close, bool *literal, bool *unterminated_raw, bool type_argument, bool modifier, const PragmaTrivia *trivia) {
    unsigned depth = 1;
    unsigned nested = 0;
    unsigned braces = open == '{';
    MacroArgumentWord word = {0};
    while (depth) {
        if (scan_pragma_empty(lexer, trivia)) {
            word = (MacroArgumentWord){0};
            continue;
        }
        if (pragma_eof(lexer)) return MACRO_GROUP_INVALID;
        int32_t c = lexer->lookahead;
        if (c == '{' && !modifier) return MACRO_GROUP_BRACED;
        if (function_macro_group_separator(c, depth, nested, type_argument, braces)) return MACRO_GROUP_INVALID;
        function_macro_note_literal(literal, depth, nested, &word, c);
        MacroArgumentToken token = function_macro_argument_token(lexer, &word, unterminated_raw);
        if (token == MACRO_ARGUMENT_INVALID) return MACRO_GROUP_INVALID;
        if (token == MACRO_ARGUMENT_CONSUMED) continue;
        function_macro_argument_word(&word, c);
        if (!function_macro_group(c, open, close, &depth, &nested, &braces)) return MACRO_GROUP_INVALID;
        lexer->advance(lexer, false);
    }
    return nested == 0 ? MACRO_GROUP_BALANCED : MACRO_GROUP_INVALID;
}

static bool function_macro_group_separator(int32_t c, unsigned depth, unsigned nested, bool type_argument, unsigned braces) {
    return ((c == ';' || c == '#') && !braces) || (type_argument && c == ',' && depth == 1 && !nested);
}

static bool function_macro_type_prefix_keyword(const MacroName *name, const char *const *reserved_words, unsigned reserved_count) {
    if (!name->ascii) return false;
    if (function_macro_keyword(name->word)) return true;
    for (unsigned i = 0; i < reserved_count; i++) {
        if (strcmp(name->word, reserved_words[i]) == 0) return true;
    }
    return false;
}

static bool function_macro_group(int32_t c, int32_t open, int32_t close, unsigned *depth, unsigned *nested, unsigned *braces) {
    if (c == '{') (*braces)++;
    else if (c == '}' && *braces) (*braces)--;
    if (c == open) (*depth)++;
    else if (c == close) (*depth)--;
    else if (c > 0 && c < 128 && strchr("([{", c)) (*nested)++;
    else if (c > 0 && c < 128 && strchr(")]}", c)) {
        if (!*nested) return false;
        (*nested)--;
    }
    return true;
}

static void function_macro_note_literal(bool *literal, unsigned depth, unsigned nested, const MacroArgumentWord *word, int32_t c) {
    if (!literal || depth != 1 || nested) return;
    if (c == '"' || (c == '\'' && !word->number) || (c >= '0' && c <= '9' && !word->identifier)) *literal = true;
}

// code-gauge-allow functionCognitiveComplexity -- Words, groups, and punctuators advance the same declaration-tail state.
static bool function_macro_declaration_continues(TSLexer *lexer, bool post_type, bool allow_braced, const PragmaTrivia *trivia) {
    MacroPrototypeTail tail = {.post_type = post_type, .explicit_type = post_type};
    for (;;) {
        bool escaped = false;
        if (!function_macro_tail_spacing(lexer, &escaped, trivia) || pragma_eof(lexer)) return false;
        int32_t c = lexer->lookahead;
        if (escaped || preproc_word(c, false)) {
            if (!function_macro_tail_word(lexer, escaped, &tail)) return false;
        } else if (c == ';') {
            return tail.declarator_group && tail.explicit_type;
        } else if (c <= 127 && strchr("{}#)]", c)) {
            return false;
        } else if (c == '(' || c == '[') {
            MacroGroup group = function_macro_tail_group(lexer, c, &tail, trivia);
            if (group != MACRO_GROUP_BALANCED) return tail.unterminated_raw || (allow_braced && group == MACRO_GROUP_BRACED);
        } else if (c == '"' || c == '\'') {
            if (!function_macro_argument_quote(lexer, c, false, NULL)) return false;
        } else if (!function_macro_tail_punctuator(lexer, c, &tail)) {
            return false;
        }
    }
}

static bool function_macro_tail_punctuator(TSLexer *lexer, int32_t c, MacroPrototypeTail *tail) {
    if (c == '*' && tail->post_type && tail->declarator_name && !tail->declarator_group) return false;
    lexer->advance(lexer, false);
    tail->modifier = false;
    return true;
}

static MacroGroup function_macro_tail_group(TSLexer *lexer, int32_t c, MacroPrototypeTail *tail, const PragmaTrivia *trivia) {
    bool literal = false;
    MacroGroup group = function_macro_group_prefix(lexer, c, c == '(' ? ')' : ']', &literal, &tail->unterminated_raw, false, trivia);
    if (group != MACRO_GROUP_BALANCED) return group;
    if (c == '(' && !tail->modifier) {
        if (literal && tail->post_type && !tail->declarator_group) tail->declarator_name = false;
        else tail->declarator_group = true;
    }
    tail->modifier = false;
    return MACRO_GROUP_BALANCED;
}

static bool function_macro_tail_spacing(TSLexer *lexer, bool *escaped, const PragmaTrivia *trivia) {
    for (;;) {
        if (scan_pragma_empty(lexer, trivia)) continue;
        int32_t c = lexer->lookahead;
        if (pragma_space(c)) {
            lexer->advance(lexer, false);
        } else if (c == '\\') {
            lexer->advance(lexer, false);
            if (scan_preproc_newline(lexer, false)) continue;
            *escaped = lexer->lookahead == 'u' || lexer->lookahead == 'U';
            return *escaped;
        } else if (c == '/') {
            if (!function_macro_argument_slash(lexer, false)) return false;
        } else {
            return true;
        }
    }
}

static bool function_macro_tail_word(TSLexer *lexer, bool escaped, MacroPrototypeTail *tail) {
    char word[FUNCTION_MACRO_KEYWORD_MAX_LENGTH + 1] = {0};
    MacroName name = {.word = word, .capacity = sizeof(word) - 1, .ascii = !escaped};
    if (escaped && !scan_macro_name_ucn(lexer)) return false;
    bool continuation = escaped;
    while (scan_macro_name_character(lexer, continuation, &name) == MACRO_NAME_CHARACTER) continuation = true;
    bool pragma = name.ascii && strcmp(word, pragma_word) == 0;
    bool keyword = name.ascii && function_macro_keyword(word);
    if (tail->post_type && !tail->declarator_group && !keyword && !pragma) {
        if (tail->declarator_name) return false;
        tail->declarator_name = true;
    }
    tail->explicit_type |= name.ascii && function_macro_word_in(word, function_macro_type_keywords,
        sizeof(function_macro_type_keywords) / sizeof(function_macro_type_keywords[0]));
    tail->modifier = pragma || (name.ascii && function_macro_word_in(word, function_macro_group_modifiers,
        sizeof(function_macro_group_modifiers) / sizeof(function_macro_group_modifiers[0])));
    return !tail->post_type || !keyword ||
        function_macro_word_in(word, function_macro_declaration_modifiers,
            sizeof(function_macro_declaration_modifiers) / sizeof(function_macro_declaration_modifiers[0]));
}

static bool function_macro_argument_escape(TSLexer *lexer, MacroArgumentWord *word) {
    lexer->advance(lexer, false);
    if (scan_preproc_newline(lexer, false)) return true;
    gnu_raw_string_prefix_invalidate(&word->raw_prefix);
    if (!scan_preproc_ucn(lexer, &word->number)) return false;
    word->identifier = !word->number;
    return true;
}

static MacroArgumentToken function_macro_argument_token(TSLexer *lexer, MacroArgumentWord *word, bool *unterminated_raw) {
    int32_t c = lexer->lookahead;
    if (pragma_space(c)) {
        lexer->advance(lexer, false);
        *word = (MacroArgumentWord){0};
    } else if (c == '/') {
        if (!function_macro_argument_slash(lexer, true)) return MACRO_ARGUMENT_INVALID;
        *word = (MacroArgumentWord){0};
    } else if (c == '\\') {
        if (!function_macro_argument_escape(lexer, word)) return MACRO_ARGUMENT_INVALID;
    } else if (c == '"' || (c == '\'' && !word->number)) {
        if (!function_macro_argument_quote(lexer, c,
            c == '"' && gnu_raw_string_prefix(&word->raw_prefix), unterminated_raw)) return MACRO_ARGUMENT_INVALID;
        *word = (MacroArgumentWord){0};
    } else {
        return MACRO_ARGUMENT_PLAIN;
    }
    return MACRO_ARGUMENT_CONSUMED;
}

// code-gauge-allow functionCognitiveComplexity -- Raw and escaped quote scanning share delimiter, newline, and splice termination.
static bool function_macro_argument_quote(TSLexer *lexer, int32_t quote, bool raw, bool *unterminated_raw) {
    lexer->advance(lexer, false);
    if (raw) {
        bool terminated = scan_gnu_raw_string_end(lexer);
        if (!terminated && unterminated_raw) *unterminated_raw = pragma_eof(lexer);
        return terminated;
    }
    while (!pragma_eof(lexer)) {
        int32_t c = lexer->lookahead;
        lexer->advance(lexer, false);
        if (c == quote) return true;
        if (c == '\n' || c == '\r') return false;
        if (c != '\\') continue;
        if (scan_preproc_newline(lexer, false)) continue;
        if (!scan_preproc_splices(lexer)) continue;
        if (pragma_eof(lexer)) return false;
        lexer->advance(lexer, false);
    }
    return false;
}

static bool function_macro_argument_slash(TSLexer *lexer, bool allow_division) {
    lexer->advance(lexer, false);
    if (!scan_preproc_splices(lexer)) return allow_division;
    if (lexer->lookahead == '/') return scan_preproc_line_comment(lexer);
    if (lexer->lookahead == '*') return scan_preproc_block_comment(lexer);
    return allow_division;
}

// code-gauge-allow functionCognitiveComplexity -- Identifier, pp-number, and GNU raw-string prefix transitions share token-character state.
static void function_macro_argument_word(MacroArgumentWord *word, int32_t c) {
    bool member = preproc_word(c, word->identifier || word->number);
    bool digit = c >= '0' && c <= '9';
    bool exponent_sign = word->number && (c == '+' || c == '-') &&
                         (word->last == 'e' || word->last == 'E' || word->last == 'p' || word->last == 'P');
    if (c == '.') word->identifier = false;
    if (!member && !digit && c != '.' && c != '\'' && !exponent_sign) *word = (MacroArgumentWord){0};
    if (digit && !word->identifier) word->number = true;
    if (member && !word->number) word->identifier = true;
    if (word->number) {
        word->last = c;
        gnu_raw_string_prefix_invalidate(&word->raw_prefix);
    }
    else if (member || digit || exponent_sign) gnu_raw_string_prefix_character(&word->raw_prefix, c);
    else word->raw_prefix = (GnuRawStringPrefix){0};
}

static bool function_macro_keyword(const char *word) {
    return function_macro_word_in(word, function_macro_keywords,
        sizeof(function_macro_keywords) / sizeof(function_macro_keywords[0]));
}

static bool function_macro_word_in(const char *word, const char *const *words, unsigned count) {
    unsigned start = 0;
    unsigned end = count;
    while (start < end) {
        unsigned middle = start + (end - start) / 2;
        int order = (unsigned char)word[0] - (unsigned char)words[middle][0];
        if (!order && word[0]) order = strcmp(word + 1, words[middle] + 1);
        if (order == 0) return true;
        if (order < 0) end = middle;
        else start = middle + 1;
    }
    return false;
}

#endif
