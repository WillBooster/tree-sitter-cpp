/**
 * @file C++ grammar for tree-sitter
 * @author Max Brunsfeld <maxbrunsfeld@gmail.com>
 * @author Amaan Qureshi <amaanq12@gmail.com>
 * @author John Drouhard <john@drouhard.dev>
 * @author Pablo Hugen <pabloashugen@protonmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// @ts-expect-error -- @willbooster/tree-sitter-c ships no type declarations.
// oxlint-disable-next-line unicorn/prefer-module -- This package is CommonJS, so tree-sitter loads grammar.js as CommonJS.
const C = require('@willbooster/tree-sitter-c/grammar');

const PREC = Object.assign(C.PREC, {
  LAMBDA: 18,
  NEW: C.PREC.CALL + 1,
  // Binds looser than casts and tighter than the multiplicative operators; casts are left-associative so that
  // `(T)a.*b` groups as `((T)a).*b`.
  POINTER_TO_MEMBER: C.PREC.CAST,
  // Where a declaration or type-id reading is certain, outranks the calls (+1 each) that the expression reading of the
  // same tokens adds, however many appear in practice: one in `int(x);` and `^^int()`, two in `int(x)[f()];`.
  CERTAIN_DECLARATION: 10,
  // A function declarator with parentheses outranks a direct initialization of the same tokens, as in
  // `int (*p)(f(y()));`, however many calls (+1 each) its argument holds in practice, unless a parameter starts with an
  // expression keyword (see `expression_keyword_parameter`).
  FUNCTION_OVER_DIRECT_INITIALIZATION: 10,
  // Outranks the function reading of `Foo* p(nullptr);` and its expression reading `Foo * p(nullptr)`.
  KEYWORD_ARGUMENT_INITIALIZATION: 10,
  // Loses to every function reading, even of `T* begin(T (&a)[N]);` whose parameter is parenthesized, and to the
  // expression reading of `Foo* p(1);`.
  POINTER_ARGUMENT_INITIALIZATION: -100,
  // Loses to any reading without such a parameter, the last-resort pointer initialization included.
  EXPRESSION_KEYWORD_PARAMETER: -1000,
  STRUCTURED_BINDING: -1,
  THREE_WAY: C.PREC.RELATIONAL + 1,
});

const FOLD_OPERATORS = [
  '+',
  '-',
  '*',
  '/',
  '%',
  '^',
  '&',
  '|',
  '=',
  '<',
  '>',
  '<<',
  '>>',
  '+=',
  '-=',
  '*=',
  '/=',
  '%=',
  '^=',
  '&=',
  '|=',
  '>>=',
  '<<=',
  '==',
  '!=',
  '<=',
  '>=',
  '&&',
  '||',
  ',',
  '.*',
  '->*',
  'or',
  'and',
  'bitor',
  'xor',
  'bitand',
  'not_eq',
];

const ASSIGNMENT_OPERATORS = [
  '=',
  '*=',
  '/=',
  '%=',
  '+=',
  '-=',
  '<<=',
  '>>=',
  '&=',
  '^=',
  '|=',
  'and_eq',
  'or_eq',
  'xor_eq',
];

const CONTEXTUAL_IDENTIFIER_NAMES = ['pre', 'post', 'typeof', 'typeof_unqual', '__typeof_unqual', '__typeof_unqual__'];
const VA_ARG_CONTEXTUAL_IDENTIFIER_NAMES = [
  ...CONTEXTUAL_IDENTIFIER_NAMES.slice(0, 2),
  'va_arg',
  '__builtin_va_arg',
  ...CONTEXTUAL_IDENTIFIER_NAMES.slice(2),
];

const commaSep = C.commaSep;
const commaSep1 = C.commaSep1;
const preprocIf = C.preprocIf;

// oxlint-disable-next-line unicorn/prefer-module -- This package is CommonJS, so tree-sitter loads grammar.js as CommonJS.
module.exports = grammar(C, {
  name: 'cpp',

  externals: ($, original) => [$.raw_string_delimiter, $.raw_string_content, $._pack_index_operator, ...original],

  conflicts: ($, original) => [
    ...original,
    [$._field_initializer_sequence],
    [$._field_initializer_conditionals],
    [$.preproc_elifdef_in_initializer_list, $.preproc_elifdef_in_block],
    [$.preproc_if_in_initializer_list, $.preproc_if_in_block],
    [$.preproc_elif_in_initializer_list, $.preproc_elif_in_block],
    [$.preproc_else_in_initializer_list, $.preproc_else_in_block],
    [$.preproc_ifdef_in_initializer_list, $.preproc_ifdef_in_block],
    [$.comma_expression, $._initializer_element],
    [$._block_item, $._initializer_directive],
    [$.type_specifier, $.expression, $.template_type],
    [$.type_specifier, $.expression, $.va_arg_expression],
    [$._declarator, $.type_specifier, $.typeof_specifier, $.expression],
    [$.typeof_specifier, $.expression],
    [$.typeof_specifier, $.type_specifier],
    [$.type_specifier, $.template_type],
    [$.type_specifier, $.optional_type_parameter_declaration],
    [$.type_specifier, $.typeof_specifier, $.expression],
    [$._type_declarator, $.typeof_specifier],
    [$._declarator, $.type_specifier, $.typeof_specifier],
    [$._declarator, $.expression, $.va_arg_expression],
    [$.type_definition, $._declaration_specifiers, $._built_in_declaration_specifiers, $._constructor_specifiers],
    [$.type_definition, $.type_specifier, $._built_in_declaration_specifiers],
    [$.storage_class_specifier, $.expression],
    [$.sized_type_specifier, $._sized_bit_int_specifier, $._top_level_expression_statement],
    [$.sized_type_specifier, $._sized_bit_int_specifier, $.expression],
    [$.sized_type_specifier, $._sized_bit_int_specifier, $.expression, $.call_expression],
    [$.expression, $.template_type],
    [$._declarator, $.type_specifier, $.expression, $.va_arg_expression],
    [$.pack_index_expression, $.pack_index_type],
    [$._declaration_modifiers, $.friend_declaration],
    [$.type_qualifier, $.friend_declaration],
    [$._declaration_modifiers, $.friend_declaration, $.using_declaration],
    [$._declaration_modifiers, $.attributed_statement, $.using_declaration],

    [$.template_function, $.template_type],
    [$.template_function, $.template_type, $.expression],
    [$.template_function, $.template_type, $.qualified_identifier],
    [$.template_function, $.template_type, $.qualified_identifier, $.qualified_type_identifier],
    [$.template_type, $.qualified_type_identifier],
    [$.qualified_type_identifier, $.qualified_identifier],
    [$.comma_expression, $.initializer_list],
    [$.expression, $._declarator],
    [$.expression, $.structured_binding_declarator],
    [$.expression, $._declarator, $.type_specifier],
    [$.expression, $.identifier_parameter_pack_expansion],
    [$.expression, $._lambda_capture_identifier],
    [$.expression, $._lambda_capture],
    [$.expression, $.structured_binding_declarator, $._lambda_capture_identifier],
    [$.structured_binding_declarator, $._lambda_capture_identifier],
    [$.parameter_list, $.argument_list],
    [$.type_specifier, $.call_expression],
    [$.sized_type_specifier, $.expression, $.call_expression],
    [$.sized_type_specifier, $.expression],
    [$.sized_type_specifier, $._top_level_expression_statement],
    [$.expression, $.call_expression],
    [$._binary_fold_operator, $._fold_operator],
    [$._function_declarator_core],
    [$._trailing_return_type_descriptor],
    [$._trailing_abstract_pointer_declarator],
    [$._trailing_abstract_reference_declarator],
    [$._declarator, $.parenthesized_argument_initialized_reference_declarator],
    [$._declarator, $.argument_initialized_rvalue_reference_declarator],
    [$._declarator, $.parenthesized_argument_initialized_pointer_declarator],
    [$.null, $.expression_keyword_parameter],
    [$.expression, $.expression_keyword_parameter],
    [$._declarator, $._argument_initialized_name],
    [$.init_declarator, $._argument_initialized_declarator],
    [$._argument_initialized_declarator, $.later_keyword_argument_init_declarator],
    [$.expression, $.parenthesized_qualified_identifier],
    [$.initializer_pair, $.comma_expression],
    [$.expression_statement, $._for_statement_body],
    [$.init_statement, $._for_statement_body],
    [$.field_expression, $.template_method, $.template_type],
    [$.field_expression, $.template_method],
    [$.sizeof_operand_field_expression, $.template_method, $.template_type],
    [$.sizeof_operand_field_expression, $.template_method],
    [$.qualified_field_identifier, $.template_method, $.template_type],
    [$.type_specifier, $.template_type, $.template_function, $.expression],
    [$.splice_type_specifier, $.splice_expression],
    [$.destructor_name, $._template_destructor_name],
    [$._declarator, $.parenthesized_pointer_declarator],
    [$._declarator, $.built_in_parenthesized_declarator],
    [$.required_parentheses_function_declarator, $._direct_initialized_parenthesized_declarator],
    [$.built_in_parenthesized_function_declarator, $._direct_initialized_declarator],
    [$._declaration_specifiers, $._built_in_declaration_specifiers, $._constructor_specifiers],
    [$.type_specifier, $._built_in_declaration_specifiers],
    [$.type_specifier, $.call_expression, $._built_in_declaration_specifiers],
    [$.type_specifier, $.call_expression, $.built_in_function_type_descriptor],
    [$._declarator, $._function_definition_declarator],
  ],

  inline: ($, original) => [...original, $._namespace_identifier, $._contextual_identifier],

  precedences: ($) => [
    [$.argument_list, $.type_qualifier],
    [$._expression_not_binary, $._class_name],
  ],

  rules: {
    _top_level_item: ($, /** @type {ChoiceRule} */ original) =>
      choice(
        ...original.members.filter((member) => !isOldStyleFunctionDefinition(member)),
        $.namespace_definition,
        $.concept_definition,
        $.namespace_alias_definition,
        $.using_declaration,
        $.alias_declaration,
        $.static_assert_declaration,
        $.consteval_block_declaration,
        $.template_declaration,
        $.template_instantiation,
        $.module_declaration,
        $.export_declaration,
        $.import_declaration,
        $.global_module_fragment_declaration,
        $.private_module_fragment_declaration,
        alias($.constructor_or_destructor_definition, $.function_definition),
        alias($.operator_cast_definition, $.function_definition),
        alias($.operator_cast_declaration, $.declaration)
      ),

    _block_item: ($, /** @type {ChoiceRule} */ original) =>
      choice(
        ...original.members.filter(
          (member) =>
            !isOldStyleFunctionDefinition(member) && !(member.type === 'SYMBOL' && member.name.startsWith('preproc_if'))
        ),
        $.namespace_definition,
        $.concept_definition,
        $.namespace_alias_definition,
        $.using_declaration,
        $.alias_declaration,
        $.static_assert_declaration,
        $.consteval_block_declaration,
        $.template_declaration,
        $.template_instantiation,
        $.export_declaration,
        $.import_declaration,
        alias($.preproc_if_in_block, $.preproc_if),
        alias($.preproc_ifdef_in_block, $.preproc_ifdef),
        alias($.constructor_or_destructor_definition, $.function_definition),
        alias($.operator_cast_definition, $.function_definition),
        alias($.operator_cast_declaration, $.declaration)
      ),

    ...preprocIf('', (/** @type {GrammarSymbols<string>} */ $) => $._top_level_item),
    ...preprocIf('_in_block', (/** @type {GrammarSymbols<string>} */ $) => $._block_item),

    placeholder_type_specifier: ($) =>
      prec(
        1,
        seq(
          field(
            'constraint',
            optional(
              choice(alias($.qualified_type_identifier, $.qualified_identifier), $.template_type, $._type_identifier)
            )
          ),
          choice($.auto, alias($.decltype_auto, $.decltype))
        )
      ),

    auto: () => 'auto',
    decltype_auto: ($) => seq('decltype', '(', $.auto, ')'),
    decltype: ($) => seq('decltype', '(', choice($.expression, $.comma_expression), ')'),

    type_specifier: ($) =>
      choice(
        $.struct_specifier,
        $.union_specifier,
        $.enum_specifier,
        $.class_specifier,
        $.sized_type_specifier,
        $.pack_index_type,
        $.primitive_type,
        $.template_type,
        $.dependent_type,
        $.splice_type_specifier,
        $.placeholder_type_specifier,
        $.decltype,
        $.typeof_specifier,
        prec.right(
          choice(
            alias($.qualified_type_identifier, $.qualified_identifier),
            C.grammar.rules._type_identifier,
            alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES.slice(0, 2)), $.type_identifier)
          )
        ),
        alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES.slice(2)), $.type_identifier)
      ),

    typeof_specifier: (_, /** @type {Rule} */ original) => prec.dynamic(-1, original),

    type_qualifier: (_, /** @type {Rule} */ original) => choice(original, 'mutable', 'constinit', 'consteval'),

    type_descriptor: (_, /** @type {Rule} */ original) => prec.right(original),

    attribute: ($, /** @type {SeqRule} */ original) =>
      seq(optional(seq('using', field('namespace', $._contextual_identifier), ':')), ...original.members),

    annotation: ($) => seq('=', $.expression),

    attribute_declaration: ($, /** @type {Rule} */ original) =>
      choice(original, seq('[[', commaSep1($.annotation), ']]')),

    // When used in a trailing return type, these specifiers can now occur immediately before
    // a compound statement. This introduces a shift/reduce conflict that needs to be resolved
    // with an associativity.
    _class_declaration: ($) =>
      seq(
        repeat(choice($.attribute_specifier, $.alignas_qualifier)),
        optional($.ms_declspec_modifier),
        repeat($.attribute_declaration),
        $._class_declaration_item
      ),
    _class_declaration_item: ($) =>
      prec.right(
        seq(
          choice(
            field('name', $._class_name),
            seq(
              optional(field('name', $._class_name)),
              optional($.virtual_specifier),
              optional($.base_class_clause),
              field('body', $.field_declaration_list)
            )
          ),
          optional($.attribute_specifier)
        )
      ),

    class_specifier: ($) => seq('class', $._class_declaration),

    union_specifier: ($) => seq('union', $._class_declaration),

    struct_specifier: ($) => seq('struct', $._class_declaration),

    _class_name: ($) =>
      prec.right(
        choice(
          $._type_identifier,
          $.template_type,
          $.splice_type_specifier,
          alias($.qualified_type_identifier, $.qualified_identifier)
        )
      ),

    function_definition: ($, /** @type {SeqRule} */ original) =>
      choice(
        {
          ...original,
          members: original.members.map((e) =>
            e.type === 'FIELD' && e.name === 'body' ? field('body', choice(e.content, $.try_statement)) : e
          ),
        },
        // Only declarators that end in a parameter list take `= default;` or `= delete;`; offering the clause after any
        // declarator would make `default` a keyword in every initializer, e.g. in `int c = default + 1;` when `default`
        // is a macro.
        {
          ...original,
          members: original.members.map((e) => {
            if (e.type !== 'FIELD') return e;
            if (e.name === 'declarator') return field('declarator', $._function_definition_declarator);
            return e.name === 'body' ? choice($.default_method_clause, $.delete_method_clause) : e;
          }),
        }
      ),

    _function_definition_declarator: ($) =>
      choice(
        $.function_declarator,
        alias($.function_definition_pointer_declarator, $.pointer_declarator),
        alias($.function_definition_reference_declarator, $.reference_declarator)
      ),
    function_definition_pointer_declarator: ($) =>
      withDeclarator(C.grammar.rules.pointer_declarator, $._function_definition_declarator),
    function_definition_reference_declarator: ($) =>
      prec.dynamic(1, prec.right(seq(choice('&', '&&'), $._function_definition_declarator))),

    declaration: ($) =>
      choice(
        seq(
          $._declaration_specifiers,
          field('declarator', declarationItem($)),
          repeat(
            seq(
              ',',
              field(
                'declarator',
                choice(declarationItem($), alias($.later_keyword_argument_init_declarator, $.init_declarator))
              )
            )
          ),
          ';'
        ),
        // A statement that can be a declaration is one ([stmt.ambig]), but only with a built-in type is the declaration
        // reading certain: the expression reading of `void (*fp)();` calls a function-style cast, which is never
        // valid, and that of `int(x);` discards one. With any other type, `foo(*p)();` and `get(*p)[0] = 5;` are
        // commonly calls, so the declaration reading with parentheses (PREC.PAREN_DECLARATOR) keeps losing to them.
        prec.dynamic(
          PREC.CERTAIN_DECLARATION,
          seq(
            $._built_in_declaration_specifiers,
            field('declarator', $._built_in_declarator),
            // Each further declarator outweighs the call, as `y(3)` in `int(x), y(3);`, that its expression reading may
            // contain.
            repeat(seq(',', prec.dynamic(1, field('declarator', choice(declarationItem($), $._built_in_declarator))))),
            ';'
          )
        )
      ),

    _built_in_declarator: ($) =>
      choice($._built_in_parenthesized_declarator, alias($.required_parentheses_init_declarator, $.init_declarator)),

    // The parenthesized declarators that, with a built-in type, carry no PREC.PAREN_DECLARATOR penalty in declarations
    // and conditions; parameters take them except the parenthesized name (see `parameter_declaration`).
    _built_in_parenthesized_declarator: ($) =>
      choice(
        $._required_parentheses_declarator,
        alias($.parenthesized_pointer_declarator, $.parenthesized_declarator),
        $._built_in_parenthesized_name_declarator
      ),

    // Parentheses around the name, as in `int(x);`, `int((x)) = 5;`, `int(x)[3];`, and `int(f)();`, without C's
    // PREC.PAREN_DECLARATOR penalty.
    _built_in_parenthesized_name_declarator: ($) =>
      choice(
        alias($.built_in_parenthesized_declarator, $.parenthesized_declarator),
        alias($.built_in_parenthesized_array_declarator, $.array_declarator),
        alias($.built_in_parenthesized_function_declarator, $.function_declarator)
      ),

    built_in_parenthesized_declarator: ($) =>
      seq(
        '(',
        choice($._contextual_identifier, alias($.built_in_parenthesized_declarator, $.parenthesized_declarator)),
        ')'
      ),
    built_in_parenthesized_array_declarator: ($) =>
      withDeclarator(
        C.grammar.rules.array_declarator,
        choice(
          alias($.built_in_parenthesized_declarator, $.parenthesized_declarator),
          alias($.built_in_parenthesized_array_declarator, $.array_declarator)
        )
      ),
    built_in_parenthesized_function_declarator: ($) =>
      prec.dynamic(
        PREC.FUNCTION_OVER_DIRECT_INITIALIZATION,
        seq(
          field('declarator', alias($.built_in_parenthesized_declarator, $.parenthesized_declarator)),
          $._function_declarator_seq
        )
      ),

    _built_in_declaration_specifiers: ($) =>
      prec.right(
        seq(
          repeat($._declaration_modifiers),
          field('type', choice($.primitive_type, $.sized_type_specifier)),
          repeat($._declaration_modifiers)
        )
      ),

    virtual_specifier: () => choice('final', 'override'),

    _declaration_modifiers: ($, /** @type {Rule} */ original) => choice(original, 'virtual'),

    explicit_function_specifier: ($) => choice('explicit', prec(PREC.CALL, seq('explicit', '(', $.expression, ')'))),

    base_class_clause: ($) =>
      seq(
        ':',
        commaSep1(
          seq(
            repeat($.attribute_declaration),
            optional(
              choice(
                $.access_specifier,
                seq($.access_specifier, optional('virtual')),
                seq('virtual', optional($.access_specifier))
              )
            ),
            choice($._class_name, $.pack_index_type),
            optional('...')
          )
        )
      ),

    enum_specifier: ($) =>
      prec.right(
        seq(
          'enum',
          optional(choice('class', 'struct')),
          repeat($.attribute_declaration),
          choice(
            seq(
              field('name', $._class_name),
              optional($._enum_base_clause),
              optional(field('body', $.enumerator_list))
            ),
            field('body', $.enumerator_list)
          ),
          optional($.attribute_specifier)
        )
      ),

    _enum_base_clause: ($) =>
      prec.left(
        seq(
          ':',
          field(
            'base',
            choice(
              alias($.qualified_type_identifier, $.qualified_identifier),
              $._type_identifier,
              $.pack_index_type,
              $.primitive_type,
              $.sized_type_specifier
            )
          )
        )
      ),

    // The `auto` storage class is removed in C++0x in order to allow for the `auto` type.
    storage_class_specifier: (_, /** @type {ChoiceRule} */ original) =>
      choice(
        ...original.members.filter((member) => !(member.type === 'STRING' && member.value === 'auto')),
        'thread_local'
      ),

    dependent_type: ($) => prec.dynamic(-1, prec.right(seq('typename', $.type_specifier))),

    module_name: ($) => seq($._contextual_identifier, repeat(seq('.', $._contextual_identifier))),

    module_partition: ($) => seq(':', $.module_name),

    module_declaration: ($) =>
      seq(
        optional('export'),
        'module',
        field('name', $.module_name),
        field('partition', optional($.module_partition)),
        optional($.attribute_declaration),
        ';'
      ),

    export_declaration: ($) => prec(1, seq('export', choice($._block_item, $.declaration_list))),

    import_declaration: ($) =>
      seq(
        'import',
        choice(
          field('name', $.module_name),
          field('partition', $.module_partition),
          field('header', choice($.string_literal, $.system_lib_string))
        ),
        optional($.attribute_declaration),
        ';'
      ),

    global_module_fragment_declaration: () => seq('module', ';'),
    private_module_fragment_declaration: () => seq('module', ':', 'private', ';'),

    template_declaration: ($) =>
      seq(
        'template',
        field('parameters', $.template_parameter_list),
        optional($.requires_clause),
        choice(
          $._empty_declaration,
          $.alias_declaration,
          $.declaration,
          $.template_declaration,
          $.function_definition,
          $.concept_definition,
          $.friend_declaration,
          alias($.constructor_or_destructor_declaration, $.declaration),
          alias($.constructor_or_destructor_definition, $.function_definition),
          alias($.operator_cast_declaration, $.declaration),
          alias($.operator_cast_definition, $.function_definition)
        )
      ),

    template_instantiation: ($) =>
      prec(
        1,
        seq(
          optional('extern'),
          'template',
          optional($._declaration_specifiers),
          field('declarator', $._declarator),
          ';'
        )
      ),

    template_parameter_list: ($) =>
      seq(
        '<',
        commaSep(
          choice(
            $.parameter_declaration,
            $.optional_parameter_declaration,
            $.type_parameter_declaration,
            $.variadic_parameter_declaration,
            $.variadic_type_parameter_declaration,
            $.optional_type_parameter_declaration,
            $.template_template_parameter_declaration
          )
        ),
        alias(token(prec(1, '>')), '>')
      ),

    type_parameter_declaration: ($) => prec(1, seq(choice('typename', 'class'), optional($._type_identifier))),

    variadic_type_parameter_declaration: ($) =>
      prec(1, seq(choice('typename', 'class'), '...', optional($._type_identifier))),

    optional_type_parameter_declaration: ($) =>
      seq(
        choice('typename', 'class'),
        optional(field('name', $._type_identifier)),
        '=',
        field('default_type', $.type_specifier)
      ),

    template_template_parameter_declaration: ($) =>
      seq(
        'template',
        field('parameters', $.template_parameter_list),
        choice(
          $.type_parameter_declaration,
          $.variadic_type_parameter_declaration,
          $.optional_type_parameter_declaration
        )
      ),

    parameter_list: ($) =>
      seq(
        '(',
        commaSep(
          choice(
            $.parameter_declaration,
            $.explicit_object_parameter_declaration,
            $.optional_parameter_declaration,
            $.variadic_parameter_declaration,
            '...',
            alias($.expression_keyword_parameter, $.parameter_declaration)
          )
        ),
        ')'
      ),

    explicit_object_parameter_declaration: ($) => seq($.this, $.parameter_declaration),

    // A parameter list competes with the argument list of a direct initialization. With a built-in type, the argument
    // reading of a parameter such as `int (*pf)()` calls a function-style cast, which is never valid, and that of
    // `int (*p)` is a cast that [dcl.ambig.res] reads as a parameter; with any other type, `name(*obj)()` in
    // `std::string s(name(*obj)());` is commonly a call, so the parameter reading with parentheses
    // (PREC.PAREN_DECLARATOR) keeps losing to it there.
    parameter_declaration: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        prec.dynamic(
          PREC.CERTAIN_DECLARATION,
          seq(
            $._built_in_declaration_specifiers,
            field('declarator', $._parameter_parenthesized_declarator),
            repeat($.attribute_specifier)
          )
        )
      ),

    // Unlike in declarations, a parenthesized name such as `int (x)` competes here with the function type `int(x)`,
    // which [dcl.ambig.res] prefers when `x` names a type.
    _parameter_parenthesized_declarator: ($) =>
      choice($._required_parentheses_declarator, alias($.parenthesized_pointer_declarator, $.parenthesized_declarator)),

    optional_parameter_declaration: ($) =>
      choice(
        seq(
          $._declaration_specifiers,
          field('declarator', optional(choice($._declarator, $.abstract_reference_declarator))),
          '=',
          field('default_value', $.expression)
        ),
        prec.dynamic(
          PREC.CERTAIN_DECLARATION,
          seq(
            $._built_in_declaration_specifiers,
            field('declarator', $._parameter_parenthesized_declarator),
            '=',
            field('default_value', $.expression)
          )
        )
      ),

    variadic_parameter_declaration: ($) => seq($._declaration_specifiers, field('declarator', $._variadic_declarator)),

    variadic_declarator: ($) => seq('...', optional($._contextual_identifier)),

    _variadic_declarator: ($) =>
      choice(
        $.variadic_declarator,
        alias($.variadic_pointer_declarator, $.pointer_declarator),
        alias($.variadic_reference_declarator, $.reference_declarator)
      ),

    variadic_pointer_declarator: ($) => withDeclarator(C.grammar.rules.pointer_declarator, $._variadic_declarator),

    variadic_reference_declarator: ($) => seq(choice('&&', '&'), $._variadic_declarator),

    init_declarator: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        seq(field('declarator', $._declarator), field('value', choice($.argument_list, $.initializer_list))),
        // A pointer or reference declarator takes the parameter list that follows its name, so `Foo* p(nullptr);`
        // would otherwise read only as a function taking a parameter of type `nullptr`. These alternatives reduce the
        // declarator before the argument list instead: outranking the function and expression readings when no
        // parameter list holds the argument, and as a last resort otherwise, as in `const T& r(*p);`.
        // `&&` takes only the last-resort alternative, since `ok && check(this->x);` is a common expression statement.
        prec.dynamic(
          PREC.KEYWORD_ARGUMENT_INITIALIZATION,
          seq(
            field(
              'declarator',
              choice(
                alias($.argument_initialized_pointer_declarator, $.pointer_declarator),
                alias($.argument_initialized_reference_declarator, $.reference_declarator)
              )
            ),
            field('value', alias($.keyword_argument_list, $.argument_list))
          )
        ),
        prec.dynamic(
          PREC.POINTER_ARGUMENT_INITIALIZATION,
          seq(field('declarator', $._argument_initialized_declarator), field('value', $.argument_list))
        )
      ),

    // After a comma, a declaration cannot be the call through a function pointer `x * (*fp)(this);`, so the forms that
    // only take the fallback above take the keyword reading too, as in `Foo* k, *(*l)(nullptr);`.
    later_keyword_argument_init_declarator: ($) =>
      prec.dynamic(
        PREC.KEYWORD_ARGUMENT_INITIALIZATION,
        seq(
          field(
            'declarator',
            choice(
              alias($.parenthesized_argument_initialized_pointer_declarator, $.pointer_declarator),
              alias($.parenthesized_argument_initialized_reference_declarator, $.reference_declarator),
              alias($.argument_initialized_rvalue_reference_declarator, $.reference_declarator)
            )
          ),
          field('value', alias($.keyword_argument_list, $.argument_list))
        )
      ),

    // The fallback also takes `&&` and declarators around a parenthesized one, which the keyword reading leaves out
    // because `ok && check(this->x);` and `x * (*fp)(this);` are common expression statements.
    _argument_initialized_declarator: ($) =>
      choice(
        alias($.argument_initialized_pointer_declarator, $.pointer_declarator),
        alias($.argument_initialized_reference_declarator, $.reference_declarator),
        alias($.parenthesized_argument_initialized_pointer_declarator, $.pointer_declarator),
        alias($.parenthesized_argument_initialized_reference_declarator, $.reference_declarator),
        alias($.argument_initialized_rvalue_reference_declarator, $.reference_declarator)
      ),

    argument_initialized_pointer_declarator: ($) =>
      withDeclarator(
        C.grammar.rules.pointer_declarator,
        choice(
          $._argument_initialized_name,
          alias($.argument_initialized_pointer_declarator, $.pointer_declarator),
          alias($.argument_initialized_reference_declarator, $.reference_declarator)
        )
      ),
    argument_initialized_reference_declarator: ($) =>
      prec.dynamic(1, prec.right(seq('&', $._argument_initialized_name))),
    _argument_initialized_name: ($) => choice($._contextual_identifier, $.qualified_identifier),
    parenthesized_argument_initialized_pointer_declarator: ($) =>
      withDeclarator(
        C.grammar.rules.pointer_declarator,
        choice(
          $.parenthesized_declarator,
          alias($.parenthesized_argument_initialized_pointer_declarator, $.pointer_declarator)
        )
      ),
    parenthesized_argument_initialized_reference_declarator: ($) =>
      prec.dynamic(1, prec.right(seq('&', $.parenthesized_declarator))),
    argument_initialized_rvalue_reference_declarator: ($) =>
      prec.dynamic(1, prec.right(seq('&&', choice($._argument_initialized_name, $.parenthesized_declarator)))),

    // A single argument that starts with a keyword that no parameter starts with (see
    // test/unit/expressionKeywordParameter.test.ts): an expression that starts with one, possibly followed by calls,
    // subscripts, and member accesses, as in `this->next`.
    keyword_argument_list: ($) => seq('(', $._keyword_led_expression, ')'),

    _keyword_led_expression: ($) =>
      choice(
        $._keyword_led_postfix_expression,
        alias($.keyword_delete_expression, $.delete_expression),
        alias($.keyword_co_await_expression, $.co_await_expression),
        alias($.keyword_unary_expression, $.unary_expression),
        alias($.keyword_sizeof_expression, $.sizeof_expression)
      ),

    // Calls, subscripts, and member accesses attach only to these, not to a prefix keyword's expression, so that
    // `not check()` reads as `not (check())`.
    _keyword_led_postfix_expression: ($) =>
      choice(
        $._keyword_primary_expression,
        alias($.keyword_led_call_expression, $.call_expression),
        alias($.keyword_led_subscript_expression, $.subscript_expression),
        alias($.keyword_led_field_expression, $.field_expression)
      ),

    _keyword_primary_expression: ($) =>
      choice(
        $.null,
        $.true,
        $.false,
        $.this,
        $.alignof_expression,
        $.offsetof_expression,
        $.generic_expression,
        $.new_expression,
        $.requires_expression,
        $.builtin_available_expression,
        alias($.named_cast_expression, $.call_expression),
        alias($.typeid_expression, $.call_expression)
      ),

    keyword_led_call_expression: ($) =>
      prec.dynamic(
        1,
        prec(PREC.CALL, seq(field('function', $._keyword_led_postfix_expression), field('arguments', $.argument_list)))
      ),
    keyword_led_subscript_expression: ($) =>
      prec(
        PREC.SUBSCRIPT,
        seq(field('argument', $._keyword_led_postfix_expression), field('indices', $.subscript_argument_list))
      ),
    keyword_led_field_expression: ($) =>
      seq(
        prec(
          PREC.FIELD,
          seq(field('argument', $._keyword_led_postfix_expression), field('operator', choice('.', '->')))
        ),
        field('field', fieldExpressionMember($))
      ),
    // A prefix keyword takes only a postfix or primary operand here, so that `not x + 1` ends the keyword reading and
    // keeps the precedence it has elsewhere instead of reading as `not (x + 1)`.
    keyword_unary_expression: ($) =>
      prec.left(PREC.UNARY, seq(field('operator', choice('not', 'compl')), field('argument', $._keyword_operand))),
    keyword_co_await_expression: ($) =>
      prec.left(PREC.UNARY, seq(field('operator', 'co_await'), field('argument', $._keyword_operand))),
    keyword_sizeof_expression: ($) =>
      prec.left(PREC.SIZEOF, seq('sizeof', choice(field('value', $._keyword_operand), sizeofTypeOperand($)))),
    keyword_delete_expression: ($) =>
      seq(choice(seq(optional('::'), 'delete'), $._array_delete_operator), $._keyword_operand),
    // Every primary and postfix expression form, plus `++`/`--` and `new`, so that which operand follows the keyword
    // does not decide between the variable and expression readings.
    _keyword_operand: ($) =>
      choice(
        $._contextual_identifier,
        $.qualified_identifier,
        $.template_function,
        $.number_literal,
        $.user_defined_literal,
        $.string_literal,
        $.raw_string_literal,
        $.concatenated_string,
        $.char_literal,
        $._keyword_primary_expression,
        $.parenthesized_expression,
        $.fold_expression,
        $.compound_literal_expression,
        $.va_arg_expression,
        $.lambda_expression,
        $.splice_expression,
        $.call_expression,
        $.field_expression,
        $.subscript_expression,
        $.update_expression
      ),

    operator_cast: ($) =>
      prec.dynamic(
        3,
        prec.right(1, seq('operator', $._declaration_specifiers, field('declarator', $._abstract_declarator)))
      ),

    // Avoid ambiguity between compound statement and initializer list in a construct like:
    //   A b {};
    compound_statement: (_, /** @type {Rule} */ original) => prec(-1, original),

    enumerator: ($) =>
      seq(
        field(
          'name',
          choice($.identifier, alias(choice('va_arg', '__builtin_va_arg', 'typeof', 'typeof_unqual'), $.identifier))
        ),
        repeat($.attribute_specifier),
        repeat($.attribute_declaration),
        optional(seq('=', field('value', $.expression)))
      ),
    initializer_pair: ($, /** @type {Rule} */ original) =>
      choice(original, seq(field('designator', $.field_designator), field('value', $.initializer_list))),

    field_initializer_list: ($) => seq(':', $._field_initializer_sequence),

    _field_initializer_sequence: ($) =>
      choice(
        seq(
          $.field_initializer,
          optional(choice(seq(',', $._field_initializer_sequence), $._field_initializer_conditionals))
        ),
        $._field_initializer_conditionals
      ),

    _field_initializer_conditionals: ($) =>
      seq(
        $._field_initializer_conditional,
        optional(choice($._field_initializer_sequence, seq(',', $._field_initializer_sequence)))
      ),

    _field_initializer_conditional: ($) =>
      choice(
        alias($.preproc_if_in_field_initializer_list, $.preproc_if),
        alias($.preproc_ifdef_in_field_initializer_list, $.preproc_ifdef)
      ),

    _field_initializer_branch: ($) =>
      choice(
        seq($._field_initializer_sequence, optional(',')),
        seq(',', optional(seq($._field_initializer_sequence, optional(','))))
      ),

    ...preprocIf(
      '_in_field_initializer_list',
      (/** @type {GrammarSymbols<string>} */ $) => $._field_initializer_branch,
      0,
      false
    ),

    field_initializer: ($) =>
      prec(
        1,
        seq(
          choice(
            $._field_identifier,
            $.pack_index_type,
            $.template_method,
            alias($.qualified_field_identifier, $.qualified_identifier)
          ),
          choice($.initializer_list, $.argument_list),
          optional('...')
        )
      ),

    _field_declaration_list_item: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        $.template_declaration,
        alias($.inline_method_definition, $.function_definition),
        alias($.constructor_or_destructor_definition, $.function_definition),
        alias($.constructor_or_destructor_declaration, $.declaration),
        alias($.operator_cast_definition, $.function_definition),
        alias($.operator_cast_declaration, $.declaration),
        $.friend_declaration,
        seq($.access_specifier, ':'),
        $.alias_declaration,
        $.using_declaration,
        $.type_definition,
        $.static_assert_declaration,
        $.consteval_block_declaration,
        ';'
      ),

    field_declaration: ($) =>
      seq(
        $._declaration_specifiers,
        commaSep(
          seq(
            field('declarator', $._field_declarator),
            optional(
              choice(
                $.bitfield_clause,
                field('default_value', $.initializer_list),
                seq('=', field('default_value', choice($.expression, $.initializer_list)))
              )
            )
          )
        ),
        optional($.attribute_specifier),
        ';'
      ),

    inline_method_definition: ($) =>
      seq(
        $._declaration_specifiers,
        field('declarator', $._field_declarator),
        choice(
          field('body', choice($.compound_statement, $.try_statement)),
          $.default_method_clause,
          $.delete_method_clause,
          $.pure_virtual_clause
        )
      ),

    _constructor_specifiers: ($) => choice($._declaration_modifiers, $.explicit_function_specifier),

    operator_cast_definition: ($) =>
      seq(
        repeat($._constructor_specifiers),
        field(
          'declarator',
          choice($.operator_cast, alias($.qualified_operator_cast_identifier, $.qualified_identifier))
        ),
        choice(field('body', choice($.compound_statement, $.try_statement)), $.delete_method_clause)
      ),

    operator_cast_declaration: ($) =>
      prec(
        1,
        seq(
          repeat($._constructor_specifiers),
          field(
            'declarator',
            choice($.operator_cast, alias($.qualified_operator_cast_identifier, $.qualified_identifier))
          ),
          optional(seq('=', field('default_value', $.expression))),
          ';'
        )
      ),

    constructor_try_statement: ($) =>
      seq('try', optional($.field_initializer_list), field('body', $.compound_statement), repeat1($.catch_clause)),

    constructor_or_destructor_definition: ($) =>
      seq(
        repeat($._constructor_specifiers),
        field('declarator', $.function_declarator),
        choice(
          seq(optional($.field_initializer_list), field('body', $.compound_statement)),
          alias($.constructor_try_statement, $.try_statement),
          $.default_method_clause,
          $.delete_method_clause,
          $.pure_virtual_clause
        )
      ),

    constructor_or_destructor_declaration: ($) =>
      seq(repeat($._constructor_specifiers), field('declarator', $.function_declarator), ';'),

    // Outranks the call expression in `X::~X() = default;`, whose assignment reading lexes `default` as an identifier.
    default_method_clause: () => prec.dynamic(1, seq('=', 'default', ';')),
    // The precedences read `= delete("reason")` as the clause rather than as a delete-expression of a string, both within
    // a parse state and against the assignment reading of `A::A() = delete("reason");`.
    delete_method_clause: ($) =>
      prec.dynamic(1, prec(1, seq('=', 'delete', optional(seq('(', field('message', $._string), ')')), ';'))),
    pure_virtual_clause: () => seq('=', /0/, ';'),

    friend_declaration: ($) =>
      seq(
        repeat(choice($.attribute_declaration, $.attribute_specifier)),
        optional(seq('constexpr', repeat($.attribute_specifier))),
        'friend',
        choice(
          $.declaration,
          $.function_definition,
          alias($.operator_cast_declaration, $.declaration),
          seq(choice(seq(optional(choice('class', 'struct', 'union')), $._class_name), $.pack_index_type), ';')
        )
      ),

    access_specifier: () => choice('public', 'private', 'protected'),

    _declarator: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES), $.identifier),
        $.reference_declarator,
        alias($.qualified_pointer_declarator, $.qualified_identifier),
        $.qualified_identifier,
        $.template_function,
        $.operator_name,
        $.destructor_name,
        $.structured_binding_declarator
      ),

    _field_declarator: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        alias($.reference_field_declarator, $.reference_declarator),
        alias($.qualified_pointer_field_declarator, $.qualified_identifier),
        $.template_method,
        $.operator_name
      ),

    _type_declarator: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        alias($.reference_type_declarator, $.reference_declarator),
        alias($.qualified_pointer_type_declarator, $.qualified_identifier)
      ),

    _abstract_declarator: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        $.abstract_reference_declarator,
        alias($.abstract_qualified_pointer_declarator, $.qualified_identifier)
      ),

    reference_declarator: ($) => prec.dynamic(1, prec.right(seq(choice('&', '&&'), $._declarator))),
    qualified_pointer_declarator: ($) => pointerToMember($, $.qualified_pointer_declarator, $.pointer_declarator),
    qualified_pointer_field_declarator: ($) =>
      pointerToMember($, $.qualified_pointer_field_declarator, alias($.pointer_field_declarator, $.pointer_declarator)),
    qualified_pointer_type_declarator: ($) =>
      pointerToMember($, $.qualified_pointer_type_declarator, alias($.pointer_type_declarator, $.pointer_declarator)),
    abstract_qualified_pointer_declarator: ($) =>
      pointerToMember($, $.abstract_qualified_pointer_declarator, $.abstract_pointer_declarator),
    reference_field_declarator: ($) => prec.dynamic(1, prec.right(seq(choice('&', '&&'), $._field_declarator))),
    reference_type_declarator: ($) => prec.dynamic(1, prec.right(seq(choice('&', '&&'), $._type_declarator))),

    abstract_reference_declarator: ($) => prec.right(seq(choice('&', '&&'), optional($._abstract_declarator))),

    structured_binding_declarator: ($) =>
      prec.dynamic(
        PREC.STRUCTURED_BINDING,
        seq('[', commaSep1(seq(optional('...'), $._contextual_identifier, repeat($.attribute_declaration))), ']')
      ),

    ref_qualifier: () => choice('&', '&&'),

    _function_declarator_seq: ($) =>
      seq($._function_declarator_core, optional($._function_postfix), repeat($.contract_specifier)),

    _function_declarator_core: ($) =>
      seq(
        field('parameters', $.parameter_list),
        optional($._function_attributes_start),
        optional($.ref_qualifier),
        optional($._function_exception_specification),
        optional($._function_attributes_end),
        optional($.trailing_return_type)
      ),

    // A parameter that starts with an expression keyword, which lexes as a type name where no keyword is expected, so
    // `int (*p)(nullptr);` and `long(n)(sizeof(b));` would otherwise read as functions taking parameters of those types;
    // as a parameter the keyword is expected, and the parameter it starts weighs so little that the function reading
    // loses, or dies at the tokens that follow it.
    expression_keyword_parameter: ($) =>
      prec.dynamic(
        PREC.EXPRESSION_KEYWORD_PARAMETER,
        seq(
          optional('::'),
          field(
            'type',
            alias(
              choice(
                'nullptr',
                'NULL',
                $.true,
                $.false,
                'sizeof',
                'alignof',
                '_Alignof',
                '__alignof__',
                '__alignof',
                '_alignof',
                'offsetof',
                '_Generic',
                '__builtin_available',
                'asm',
                '__asm__',
                '__asm',
                'new',
                'delete',
                'co_await',
                'requires',
                'static_cast',
                'dynamic_cast',
                'const_cast',
                'reinterpret_cast',
                'typeid',
                'noexcept',
                'not',
                'compl'
              ),
              $.type_identifier
            )
          )
        )
      ),

    _function_attributes_start: ($) =>
      prec(
        1,
        choice(
          seq(repeat1($.attribute_specifier), repeat($.type_qualifier)),
          seq(repeat($.attribute_specifier), repeat1($.type_qualifier))
        )
      ),

    _function_exception_specification: ($) => choice($.noexcept, $.throw_specifier),

    _function_attributes_end: ($) =>
      prec.right(
        seq(
          optional($.gnu_asm_expression),
          choice(
            seq(repeat1($.attribute_specifier), repeat($.attribute_declaration)),
            seq(repeat($.attribute_specifier), repeat1($.attribute_declaration))
          )
        )
      ),

    _function_postfix: ($) => prec.right(choice(repeat1($.virtual_specifier), $.requires_clause)),

    contract_specifier: ($) =>
      choice(
        seq('pre', repeat($.attribute_declaration), '(', field('condition', $.expression), ')'),
        seq(
          'post',
          repeat($.attribute_declaration),
          '(',
          optional(seq(field('result', $._contextual_identifier), repeat($.attribute_declaration), ':')),
          field('condition', $.expression),
          ')'
        )
      ),

    function_declarator: ($) => prec.dynamic(1, seq(field('declarator', $._declarator), $._function_declarator_seq)),

    // Parentheses around a pointer before a parameter list or an array bound, as in `int (*pf)()` and `int (*a)[3]`,
    // are required, so unlike other parenthesized declarators this one carries no PREC.PAREN_DECLARATOR penalty. It is
    // used only where the declaration reading is certain (see `parameter_declaration` and `declaration`).
    parenthesized_pointer_declarator: ($) =>
      seq(
        '(',
        optional($.ms_call_modifier),
        choice(
          $.pointer_declarator,
          alias($.required_parentheses_pointer_declarator, $.pointer_declarator),
          $.reference_declarator,
          alias($.required_parentheses_reference_declarator, $.reference_declarator),
          alias($.qualified_pointer_declarator, $.qualified_identifier),
          alias($.parenthesized_pointer_declarator, $.parenthesized_declarator)
        ),
        ')'
      ),

    // A declarator that needs parentheses, possibly behind pointers and references, as in `Foo *(*f)(Bar)`,
    // `int (*(*fpa)[3])();`, and `int (&(*rg)())[3];`.
    _required_parentheses_declarator: ($) =>
      choice(
        alias($.required_parentheses_function_declarator, $.function_declarator),
        alias($.required_parentheses_array_declarator, $.array_declarator),
        alias($.required_parentheses_pointer_declarator, $.pointer_declarator),
        alias($.required_parentheses_reference_declarator, $.reference_declarator)
      ),

    required_parentheses_pointer_declarator: ($) =>
      withDeclarator(C.grammar.rules.pointer_declarator, $._required_parentheses_declarator),
    required_parentheses_reference_declarator: ($) =>
      prec.dynamic(1, prec.right(seq(choice('&', '&&'), $._required_parentheses_declarator))),

    required_parentheses_function_declarator: ($) =>
      prec.dynamic(
        PREC.FUNCTION_OVER_DIRECT_INITIALIZATION,
        seq(
          field('declarator', alias($.parenthesized_pointer_declarator, $.parenthesized_declarator)),
          $._function_declarator_seq
        )
      ),

    // Nested for further dimensions, as in `int (*a)[3][5]`.
    required_parentheses_array_declarator: ($) =>
      withDeclarator(
        C.grammar.rules.array_declarator,
        choice(
          alias($.parenthesized_pointer_declarator, $.parenthesized_declarator),
          alias($.required_parentheses_array_declarator, $.array_declarator)
        )
      ),

    required_parentheses_init_declarator: ($) =>
      choice(
        seq(
          field('declarator', $._built_in_parenthesized_declarator),
          choice(seq('=', field('value', choice($.initializer_list, $.expression))), field('value', $.initializer_list))
        ),
        seq(field('declarator', $._direct_initialized_declarator), field('value', $.argument_list))
      ),

    // Each shape here has a function form of the same tokens: a parenthesized name only bare
    // (`built_in_parenthesized_function_declarator`), the others also behind pointers and references.
    _direct_initialized_declarator: ($) =>
      choice(
        $._direct_initialized_parenthesized_declarator,
        alias($.built_in_parenthesized_declarator, $.parenthesized_declarator)
      ),

    // The direct initialization competing with a required-parentheses function declarator of the same tokens, behind
    // the same pointers and references, so that `int *(*p)(nullptr);` stays a direct initialization like
    // `int (*p)(nullptr);`.
    _direct_initialized_parenthesized_declarator: ($) =>
      choice(
        alias($.parenthesized_pointer_declarator, $.parenthesized_declarator),
        alias($.direct_initialized_pointer_declarator, $.pointer_declarator),
        alias($.direct_initialized_reference_declarator, $.reference_declarator)
      ),

    direct_initialized_pointer_declarator: ($) =>
      withDeclarator(C.grammar.rules.pointer_declarator, $._direct_initialized_parenthesized_declarator),
    direct_initialized_reference_declarator: ($) =>
      prec.dynamic(1, prec.right(seq(choice('&', '&&'), $._direct_initialized_parenthesized_declarator))),

    function_field_declarator: ($) =>
      prec.dynamic(1, seq(field('declarator', $._field_declarator), $._function_declarator_seq)),

    function_type_declarator: ($) => prec(1, seq(field('declarator', $._type_declarator), $._function_declarator_seq)),

    abstract_function_declarator: ($) =>
      prec(1, seq(field('declarator', optional($._abstract_declarator)), $._function_declarator_seq)),

    trailing_return_type: ($) => seq('->', alias($._trailing_return_type_descriptor, $.type_descriptor)),
    _trailing_return_type_descriptor: ($) =>
      prec(
        1,
        withDeclarator(C.grammar.rules.type_descriptor, optional(prec.right(1, $._trailing_abstract_declarator)))
      ),

    _trailing_abstract_declarator: ($) =>
      choice(
        alias($._trailing_abstract_pointer_declarator, $.abstract_pointer_declarator),
        alias($._trailing_abstract_function_declarator, $.abstract_function_declarator),
        alias($._trailing_abstract_array_declarator, $.abstract_array_declarator),
        alias($._trailing_abstract_parenthesized_declarator, $.abstract_parenthesized_declarator),
        alias($._trailing_abstract_reference_declarator, $.abstract_reference_declarator),
        alias($._trailing_abstract_qualified_pointer_declarator, $.qualified_identifier)
      ),
    _trailing_abstract_pointer_declarator: ($) => {
      const prefix = seq('*', repeat($.attribute_declaration), repeat($.ms_pointer_modifier), repeat($.type_qualifier));
      return choice(
        prec.dynamic(2, seq(prefix, field('declarator', prec.right(1, $._trailing_abstract_declarator)))),
        prec.dynamic(1, seq(prefix, prec(1, blank())))
      );
    },
    _trailing_abstract_function_declarator: ($) =>
      prec(1, seq(field('declarator', optional($._trailing_abstract_declarator)), $._function_declarator_core)),
    _trailing_abstract_array_declarator: ($) =>
      prec.dynamic(
        1,
        withDeclarator(
          C.grammar.rules.abstract_array_declarator,
          optional(prec.right(1, $._trailing_abstract_declarator))
        )
      ),
    _trailing_abstract_parenthesized_declarator: ($) =>
      prec(1, seq('(', optional($.ms_call_modifier), $._trailing_abstract_declarator, ')')),
    _trailing_abstract_reference_declarator: ($) =>
      seq(choice('&', '&&'), choice(prec.right(1, $._trailing_abstract_declarator), prec(1, blank()))),
    _trailing_abstract_qualified_pointer_declarator: ($) =>
      pointerToMember(
        $,
        $._trailing_abstract_qualified_pointer_declarator,
        alias($._trailing_abstract_pointer_declarator, $.abstract_pointer_declarator)
      ),

    noexcept: ($) => prec.right(seq('noexcept', optional(seq('(', optional($.expression), ')')))),

    throw_specifier: ($) => seq('throw', seq('(', commaSep($.type_descriptor), ')')),

    template_type: ($) => seq(field('name', $._type_identifier), field('arguments', $.template_argument_list)),

    template_method: ($) =>
      seq(field('name', choice($._field_identifier, $.operator_name)), field('arguments', $.template_argument_list)),

    template_function: ($) =>
      seq(field('name', $._contextual_identifier), field('arguments', $.template_argument_list)),

    template_argument_list: ($) =>
      seq(
        '<',
        commaSep(
          choice(
            prec.dynamic(3, alias($._template_argument_type_identifier, $.type_descriptor)),
            prec.dynamic(3, $.type_descriptor),
            prec.dynamic(2, alias($.type_parameter_pack_expansion, $.parameter_pack_expansion)),
            prec.dynamic(1, $.expression)
          )
        ),
        alias(token(prec(1, '>')), '>')
      ),

    // A lone identifier as a template argument is resolved to a type statically instead of by GLR with
    // `expression`. Nested template argument lists split into enough versions that, next to the extra versions of
    // an earlier error recovery, tree-sitter's version limit dropped the type interpretation, and incremental
    // parsing then reused that subtree after the error was gone.
    _template_argument_type_identifier: ($) => prec(1, field('type', $._type_identifier)),

    namespace_definition: ($) =>
      seq(
        optional('inline'),
        'namespace',
        optional($.attribute_declaration),
        field('name', optional(choice($._namespace_identifier, $.nested_namespace_specifier))),
        field('body', $.declaration_list)
      ),

    namespace_alias_definition: ($) =>
      seq(
        'namespace',
        field('name', $._namespace_identifier),
        '=',
        choice($._namespace_identifier, $.nested_namespace_specifier, $.splice_specifier),
        ';'
      ),

    _namespace_specifier: ($) => seq(optional('inline'), $._namespace_identifier),

    nested_namespace_specifier: ($) =>
      prec(
        1,
        seq(optional($._namespace_specifier), '::', choice($.nested_namespace_specifier, $._namespace_specifier))
      ),

    using_declaration: ($) =>
      seq(
        repeat($.attribute_declaration),
        'using',
        choice(
          seq(
            choice('namespace', 'enum'),
            choice($._contextual_identifier, $.qualified_identifier, $.splice_type_specifier)
          ),
          commaSep1(
            choice(
              $._contextual_identifier,
              $.splice_type_specifier,
              seq(optional('typename'), $.qualified_identifier, optional('...'))
            )
          )
        ),
        ';'
      ),

    alias_declaration: ($) =>
      seq(
        'using',
        field('name', $._type_identifier),
        repeat($.attribute_declaration),
        '=',
        field('type', $.type_descriptor),
        ';'
      ),

    static_assert_declaration: ($) =>
      seq(
        'static_assert',
        '(',
        field('condition', $.expression),
        optional(seq(',', field('message', $._string))),
        ')',
        ';'
      ),

    consteval_block_declaration: ($) => seq('consteval', field('body', $.compound_statement)),

    concept_definition: ($) => seq('concept', field('name', $._contextual_identifier), '=', $.expression, ';'),

    _top_level_statement: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        $.co_return_statement,
        $.co_yield_statement,
        $.for_range_loop,
        $.expansion_statement,
        $.try_statement,
        $.throw_statement
      ),

    _non_case_statement: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        $.co_return_statement,
        $.co_yield_statement,
        $.for_range_loop,
        $.expansion_statement,
        $.try_statement,
        $.throw_statement
      ),

    switch_statement: ($) => seq('switch', field('condition', $.condition_clause), field('body', $.compound_statement)),

    while_statement: ($) => seq('while', field('condition', $.condition_clause), field('body', $.statement)),

    if_statement: ($) =>
      prec.right(
        seq(
          'if',
          choice(
            seq(
              optional('constexpr'),
              field('condition', $.condition_clause),
              field('consequence', $.statement),
              optional(field('alternative', $.else_clause))
            ),
            seq(
              optional(choice('!', 'not')),
              'consteval',
              field('consequence', $.compound_statement),
              optional(field('alternative', alias($._consteval_else_clause, $.else_clause)))
            )
          )
        )
      ),

    _consteval_else_clause: ($) => seq('else', $.compound_statement),

    // Using prec(1) instead of prec.dynamic(1) causes issues with the
    // range loop's declaration specifiers if `int` is passed in, it'll
    // always prefer the standard for loop and give us a parse error.
    _for_statement_body: ($, /** @type {Rule} */ original) => prec.dynamic(1, original),
    for_range_loop: ($) => seq('for', '(', $._for_range_loop_body, ')', field('body', $.statement)),
    _for_range_loop_body: ($) =>
      seq(
        field('initializer', optional($.init_statement)),
        $._declaration_specifiers,
        field('declarator', $._declarator),
        ':',
        field('right', choice($.expression, $.initializer_list))
      ),

    init_statement: ($) => choice($.alias_declaration, $.type_definition, $.declaration, $.expression_statement),

    condition_clause: ($) =>
      seq(
        '(',
        field('initializer', optional($.init_statement)),
        field('value', choice($.expression, $.comma_expression, alias($.condition_declaration, $.declaration))),
        ')'
      ),

    condition_declaration: ($) =>
      choice(
        seq(
          $._declaration_specifiers,
          field('declarator', $._declarator),
          choice(seq('=', field('value', $.expression)), field('value', $.initializer_list))
        ),
        // See the built-in alternative of `declaration`: `if (int (*p)() = f())` declares `p`.
        prec.dynamic(
          PREC.CERTAIN_DECLARATION,
          seq(
            $._built_in_declaration_specifiers,
            field('declarator', $._built_in_parenthesized_declarator),
            choice(seq('=', field('value', $.expression)), field('value', $.initializer_list))
          )
        )
      ),

    return_statement: ($, /** @type {Rule} */ original) =>
      seq(choice(original, seq('return', $.initializer_list, ';'))),

    co_return_statement: ($) => seq('co_return', optional($.expression), ';'),

    co_yield_statement: ($) => seq('co_yield', $.expression, ';'),

    throw_statement: ($) => seq('throw', optional($.expression), ';'),

    try_statement: ($) => seq('try', field('body', $.compound_statement), repeat1($.catch_clause)),

    catch_clause: ($) => seq('catch', field('parameters', $.parameter_list), field('body', $.compound_statement)),

    _expression_not_binary: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        prec.dynamic(-2, alias(choice('unsigned', 'signed', 'long', 'short'), $.identifier)),
        alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES), $.identifier),
        $.co_await_expression,
        $.requires_expression,
        $.requires_clause,
        $.template_function,
        $.qualified_identifier,
        $.new_expression,
        $.delete_expression,
        $.lambda_expression,
        $.parameter_pack_expansion,
        $.pack_index_expression,
        $.this,
        $.user_defined_literal,
        $.fold_expression,
        $.reflect_expression,
        $.splice_expression
      ),

    _string: ($) => choice($.string_literal, $.raw_string_literal, $.concatenated_string),

    raw_string_literal: ($) =>
      seq(
        choice('R"', 'LR"', 'uR"', 'UR"', 'u8R"'),
        choice(
          seq(field('delimiter', $.raw_string_delimiter), '(', $.raw_string_content, ')', $.raw_string_delimiter),
          seq('(', $.raw_string_content, ')')
        ),
        '"'
      ),

    subscript_expression: ($) =>
      prec(PREC.SUBSCRIPT, seq(field('argument', $.expression), field('indices', $.subscript_argument_list))),

    subscript_argument_list: ($) => seq('[', commaSep(choice($.expression, $.initializer_list)), ']'),

    call_expression: ($, /** @type {Rule} */ original) =>
      choice(
        prec.dynamic(
          1,
          choice(
            original,
            seq(
              field('function', choice($.primitive_type, seq(optional('typename'), $.splice_type_specifier))),
              field('arguments', $.argument_list)
            )
          )
        ),
        seq(
          field('function', alias(choice('unsigned', 'signed', 'long', 'short'), $.sized_type_specifier)),
          field('arguments', $.argument_list)
        )
      ),

    co_await_expression: ($) =>
      prec.left(PREC.UNARY, seq(field('operator', 'co_await'), field('argument', $.expression))),

    new_expression: ($) =>
      prec.right(
        PREC.NEW,
        seq(
          optional('::'),
          'new',
          field('placement', optional($.argument_list)),
          field('type', $.type_specifier),
          field('declarator', optional($._new_declarator)),
          field('arguments', optional(choice($.argument_list, $.initializer_list)))
        )
      ),

    // new-declarator: ptr-operator new-declarator? | noptr-new-declarator ([expr.new]). The `&` and `&&` operators
    // are left out, since a new-type-id must denote an object type.
    _new_declarator: ($) => choice(alias($.new_pointer_declarator, $.abstract_pointer_declarator), $.new_declarator),
    new_pointer_declarator: ($) =>
      prec.right(
        seq(
          '*',
          repeat($.attribute_declaration),
          repeat($.ms_pointer_modifier),
          repeat($.type_qualifier),
          field('declarator', optional($._new_declarator))
        )
      ),
    new_declarator: ($) => prec.right(seq('[', field('length', $.expression), ']', optional($.new_declarator))),

    _array_delete_operator: () => prec(PREC.LAMBDA + 1, seq(optional('::'), 'delete', '[', ']')),

    delete_expression: ($) => seq(choice(seq(optional('::'), 'delete'), $._array_delete_operator), $.expression),

    field_expression: ($) =>
      seq(
        prec(PREC.FIELD, seq(field('argument', $.expression), field('operator', choice('.', '->')))),
        field('field', fieldExpressionMember($))
      ),

    type_requirement: ($) => seq('typename', $._class_name),

    compound_requirement: ($) =>
      seq('{', $.expression, '}', optional('noexcept'), optional($.trailing_return_type), ';'),

    _requirement: ($) =>
      choice(alias($.expression_statement, $.simple_requirement), $.type_requirement, $.compound_requirement),

    requirement_seq: ($) => seq('{', repeat($._requirement), '}'),

    constraint_conjunction: ($) =>
      prec.left(
        PREC.LOGICAL_AND,
        seq(
          field('left', $._requirement_clause_constraint),
          field('operator', choice('&&', 'and')),
          field('right', $._requirement_clause_constraint)
        )
      ),

    constraint_disjunction: ($) =>
      prec.left(
        PREC.LOGICAL_OR,
        seq(
          field('left', $._requirement_clause_constraint),
          field('operator', choice('||', 'or')),
          field('right', $._requirement_clause_constraint)
        )
      ),

    _requirement_clause_constraint: ($) =>
      choice(
        $.true,
        $.false,
        $._class_name,
        $.fold_expression,
        $.pack_index_expression,
        $.lambda_expression,
        $.requires_expression,

        seq('(', $.expression, ')'),

        $.constraint_conjunction,
        $.constraint_disjunction
      ),

    requires_clause: ($) => seq('requires', field('constraint', $._requirement_clause_constraint)),

    requires_parameter_list: ($) =>
      seq(
        '(',
        commaSep(choice($.parameter_declaration, $.optional_parameter_declaration, $.variadic_parameter_declaration)),
        ')'
      ),

    requires_expression: ($) =>
      seq(
        'requires',
        field('parameters', optional(alias($.requires_parameter_list, $.parameter_list))),
        field('requirements', $.requirement_seq)
      ),

    lambda_specifier: () => choice('static', 'constexpr', 'consteval', 'mutable'),

    lambda_declarator: ($) =>
      choice(
        seq(
          repeat($.attribute_declaration),
          field('parameters', $.parameter_list),
          repeat($.lambda_specifier),
          optional($._function_exception_specification),
          repeat($.attribute_declaration),
          optional($.trailing_return_type),
          optional($.requires_clause),
          repeat($.contract_specifier)
        ),

        seq(repeat1($.attribute_declaration), repeat($.contract_specifier)),
        repeat1($.contract_specifier),
        seq(repeat($.attribute_declaration), $.trailing_return_type, repeat($.contract_specifier)),
        seq(
          repeat($.attribute_declaration),
          $._function_exception_specification,
          repeat($.attribute_declaration),
          optional($.trailing_return_type),
          repeat($.contract_specifier)
        ),
        seq(
          repeat($.attribute_declaration),
          repeat1($.lambda_specifier),
          optional($._function_exception_specification),
          repeat($.attribute_declaration),
          optional($.trailing_return_type),
          repeat($.contract_specifier)
        )
      ),

    lambda_expression: ($) =>
      seq(
        field('captures', $.lambda_capture_specifier),
        optional(
          seq(field('template_parameters', $.template_parameter_list), optional(field('constraint', $.requires_clause)))
        ),
        optional(field('declarator', $.lambda_declarator)),
        field('body', $.compound_statement)
      ),

    lambda_capture_specifier: ($) =>
      prec(
        PREC.LAMBDA,
        seq(
          '[',
          choice(
            $.lambda_default_capture,
            commaSep($._lambda_capture),
            seq($.lambda_default_capture, ',', commaSep1($._lambda_capture))
          ),
          ']'
        )
      ),

    lambda_default_capture: () => choice('=', '&'),

    _lambda_capture_identifier: ($) =>
      seq(
        optional('&'),
        choice(
          $._contextual_identifier,
          $.qualified_identifier,
          alias($.identifier_parameter_pack_expansion, $.parameter_pack_expansion)
        )
      ),

    lambda_capture_initializer: ($) =>
      seq(optional('&'), optional('...'), field('left', $._contextual_identifier), '=', field('right', $.expression)),

    _lambda_capture: ($) =>
      choice(seq(optional('*'), $.this), $._lambda_capture_identifier, $.lambda_capture_initializer),

    _fold_operator: () => choice(...FOLD_OPERATORS),
    _binary_fold_operator: () =>
      choice(...FOLD_OPERATORS.map((operator) => seq(field('operator', operator), '...', operator))),

    _unary_left_fold: ($) =>
      seq(field('left', '...'), field('operator', $._fold_operator), field('right', $.expression)),
    _unary_right_fold: ($) =>
      seq(field('left', $.expression), field('operator', $._fold_operator), field('right', '...')),
    _binary_fold: ($) => seq(field('left', $.expression), $._binary_fold_operator, field('right', $.expression)),

    fold_expression: ($) => seq('(', choice($._unary_right_fold, $._unary_left_fold, $._binary_fold), ')'),

    parameter_pack_expansion: ($) => prec(-1, seq(field('pattern', $.expression), '...')),

    expression_statement: ($, /** @type {Rule} */ original) =>
      choice(prec(1, prec.dynamic(1, seq($.pack_index_expression, ';'))), original),

    pack_index_expression: ($) => packIndex($, $._contextual_identifier),

    pack_index_type: ($) => packIndex($, $._type_identifier),

    type_parameter_pack_expansion: ($) => seq(field('pattern', $.type_descriptor), '...'),

    identifier_parameter_pack_expansion: ($) => seq(field('pattern', $._contextual_identifier), '...'),

    sizeof_expression: ($) =>
      prec.right(
        PREC.SIZEOF,
        choice(
          seq('sizeof', choice(field('value', $.expression), sizeofTypeOperand($))),
          seq('sizeof', '...', '(', field('value', $._contextual_identifier), ')'),
          // A size cannot take a postfix operator, so one after `sizeof(ns::x)` makes `ns::x` an expression. This
          // outweighs the type reading of the qualified name together with the postfix expression applied to it.
          prec.dynamic(2, seq('sizeof', field('value', $._sizeof_qualified_postfix_expression)))
        )
      ),

    _sizeof_qualified_postfix_expression: ($) =>
      choice(
        alias($.sizeof_operand_call_expression, $.call_expression),
        alias($.sizeof_operand_subscript_expression, $.subscript_expression),
        alias($.sizeof_operand_field_expression, $.field_expression),
        alias($.sizeof_operand_update_expression, $.update_expression)
      ),
    // Outranking `sizeof` itself continues the operand through a following postfix operator instead of ending it there.
    _sizeof_qualified_postfix_base: ($) =>
      prec(
        PREC.CALL,
        choice(
          alias($.parenthesized_qualified_identifier, $.parenthesized_expression),
          $._sizeof_qualified_postfix_expression
        )
      ),
    parenthesized_qualified_identifier: ($) => seq('(', $.qualified_identifier, ')'),
    // Each postfix operator weighs 1 more here than in the generic rule, so that a reading that ends the operand early
    // and applies the remaining operators to the size loses.
    sizeof_operand_call_expression: ($) =>
      prec.dynamic(
        2,
        prec(PREC.CALL, seq(field('function', $._sizeof_qualified_postfix_base), field('arguments', $.argument_list)))
      ),
    sizeof_operand_subscript_expression: ($) =>
      prec.dynamic(
        1,
        prec(
          PREC.SUBSCRIPT,
          seq(field('argument', $._sizeof_qualified_postfix_base), field('indices', $.subscript_argument_list))
        )
      ),
    sizeof_operand_field_expression: ($) =>
      prec.dynamic(
        1,
        seq(
          prec(
            PREC.FIELD,
            seq(field('argument', $._sizeof_qualified_postfix_base), field('operator', choice('.', '->')))
          ),
          field('field', fieldExpressionMember($))
        )
      ),
    sizeof_operand_update_expression: ($) =>
      prec.dynamic(
        1,
        prec.left(
          PREC.UNARY,
          seq(field('argument', $._sizeof_qualified_postfix_base), field('operator', choice('++', '--')))
        )
      ),

    qualified_type_descriptor: ($) =>
      prec(1, field('type', alias($.qualified_type_identifier, $.qualified_identifier))),

    unary_expression: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        prec.left(PREC.UNARY, seq(field('operator', choice('not', 'compl')), field('argument', $.expression)))
      ),

    binary_expression: ($, /** @type {Rule} */ original) => {
      const table = [
        ['<=>', PREC.THREE_WAY],
        ['or', PREC.LOGICAL_OR],
        ['and', PREC.LOGICAL_AND],
        ['bitor', PREC.INCLUSIVE_OR],
        ['xor', PREC.EXCLUSIVE_OR],
        ['bitand', PREC.BITWISE_AND],
        ['not_eq', PREC.EQUAL],
      ];

      return choice(
        original,
        ...table.map(([operator, precedence]) => {
          return prec.left(
            precedence,
            seq(
              field('left', $.expression),
              // @ts-ignore
              field('operator', operator),
              field('right', $.expression)
            )
          );
        })
      );
    },

    cast_expression: (_, /** @type {Rule} */ original) => prec.left(PREC.CAST, original),

    // The compound_statement is added to parse macros taking statements as arguments, e.g. MYFORLOOP(1, 10, i, { foo(i); bar(i); })
    // The operator arguments are added to parse macros taking operators as arguments, e.g. DEFINE_COMPARISON(NE, !=)
    argument_list: ($) => {
      const argument = choice($.expression, $.initializer_list, $.compound_statement, $.operator_argument);
      const anyArgument = choice(argument, alias($._declarator_operator_argument, $.operator_argument));
      return seq('(', optional(choice(argument, seq(anyArgument, repeat1(seq(',', anyArgument))))), ')');
    },

    operator_argument: () =>
      choice(
        '+',
        '-',
        '/',
        '%',
        '^',
        '|',
        '~',
        '!',
        '=',
        '<',
        '>',
        '+=',
        '-=',
        '*=',
        '/=',
        '%=',
        '^=',
        '&=',
        '|=',
        '<<',
        '>>',
        '>>=',
        '<<=',
        '==',
        '!=',
        '<=',
        '>=',
        '<=>',
        '||',
        '++',
        '--',
        '.',
        '.*',
        '->',
        '->*',
        '::'
      ),

    // Accepted only among several arguments: alone in parentheses, these are abstract declarators, as in `T (&)[3]`.
    _declarator_operator_argument: () =>
      choice(
        '*',
        '&',
        // Not the `&&` token: accepting it here would make `&&label` in `f(1, &&label)` lex as one token.
        seq('&', '&')
      ),

    destructor_name: ($) => prec(1, seq('~', choice($._contextual_identifier, $.pack_index_type))),

    _template_destructor_name: ($) => prec(1, seq('~', $._contextual_identifier, $.template_argument_list)),

    compound_literal_expression: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        seq(
          field(
            'type',
            choice(
              $._class_name,
              $.pack_index_type,
              $.primitive_type,
              seq(optional('typename'), $.splice_type_specifier)
            )
          ),
          field('value', $.initializer_list)
        )
      ),

    dependent_identifier: ($) => seq('template', $.template_function),
    dependent_field_identifier: ($) => seq('template', $.template_method),
    dependent_type_identifier: ($) => seq('template', $.template_type),

    _scope_resolution: ($) =>
      prec(
        1,
        seq(
          field(
            'scope',
            optional(
              choice(
                $._namespace_identifier,
                $.template_type,
                $.decltype,
                $.pack_index_type,
                $.splice_expression,
                $.splice_type_specifier,
                alias($.dependent_type_identifier, $.dependent_name)
              )
            )
          ),
          '::'
        )
      ),

    qualified_field_identifier: ($) =>
      seq(
        $._scope_resolution,
        field(
          'name',
          choice(
            alias($.dependent_field_identifier, $.dependent_name),
            alias($.qualified_field_identifier, $.qualified_identifier),
            $.template_method,
            $.destructor_name,
            alias($._template_destructor_name, $.destructor_name),
            prec.dynamic(2, $._field_identifier)
          )
        )
      ),

    qualified_identifier: ($) =>
      seq(
        $._scope_resolution,
        field(
          'name',
          choice(
            alias($.dependent_identifier, $.dependent_name),
            $.qualified_identifier,
            $.template_function,
            prec.dynamic(1, seq(optional('template'), $._contextual_identifier)),
            $.operator_name,
            $.destructor_name,
            alias($._template_destructor_name, $.destructor_name)
          )
        )
      ),

    qualified_type_identifier: ($) =>
      seq(
        $._scope_resolution,
        field(
          'name',
          choice(
            alias($.dependent_type_identifier, $.dependent_name),
            alias($.qualified_type_identifier, $.qualified_identifier),
            $.template_type,
            prec.dynamic(1, $._type_identifier)
          )
        )
      ),

    qualified_operator_cast_identifier: ($) =>
      seq(
        $._scope_resolution,
        field('name', choice(alias($.qualified_operator_cast_identifier, $.qualified_identifier), $.operator_cast))
      ),

    _assignment_left_expression: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES), $.identifier),
        $.pack_index_expression,
        $.qualified_identifier,
        $.user_defined_literal,
        alias($.pointer_to_member_expression, $.binary_expression),
        alias($.named_cast_expression, $.call_expression)
      ),

    expression: ($, /** @type {Rule} */ original) =>
      choice(
        original,
        alias($.pointer_to_member_expression, $.binary_expression),
        alias($.named_cast_expression, $.call_expression),
        alias($.typeid_expression, $.call_expression)
      ),

    // The named casts and `typeid` are keywords where an expression may start, so such a call never reads as a
    // declaration or a type-id there; where only a type name fits, they still lex as one (`static_cast<T> f;` in a
    // class). They keep the shapes of the calls that other names make, which queries already match.
    named_cast_expression: ($) =>
      prec.dynamic(
        1,
        prec(
          PREC.CALL,
          seq(field('function', alias($.named_cast, $.template_function)), field('arguments', $.argument_list))
        )
      ),
    named_cast: ($) =>
      seq(
        field(
          'name',
          choice(
            alias('static_cast', $.identifier),
            alias('dynamic_cast', $.identifier),
            alias('const_cast', $.identifier),
            alias('reinterpret_cast', $.identifier)
          )
        ),
        field('arguments', $.template_argument_list)
      ),
    typeid_expression: ($) =>
      prec.dynamic(
        1,
        prec(
          PREC.CALL,
          seq(
            field('function', alias('typeid', $.identifier)),
            field('arguments', choice($.argument_list, alias($.typeid_type_argument_list, $.argument_list)))
          )
        )
      ),
    // Loses to the expression reading of the same tokens, so that `typeid(std::runtime_error)` keeps its tree and only a
    // type that cannot be an expression, such as `typeid(int)`, reads as one.
    typeid_type_argument_list: ($) => prec.dynamic(-1, seq('(', $.type_descriptor, ')')),

    pointer_to_member_expression: ($) =>
      prec.left(
        PREC.POINTER_TO_MEMBER,
        seq(field('left', $.expression), field('operator', choice('.*', '->*')), field('right', $.expression))
      ),

    assignment_expression: ($) =>
      prec.right(
        PREC.ASSIGNMENT,
        seq(
          field('left', $._assignment_left_expression),
          field('operator', choice(...ASSIGNMENT_OPERATORS)),
          field('right', choice($.expression, $.initializer_list))
        )
      ),

    _assignment_expression_lhs: ($) =>
      seq(
        field('left', $.expression),
        field('operator', choice(...ASSIGNMENT_OPERATORS)),
        field('right', choice($.expression, $.initializer_list))
      ),

    // This prevents an ambiguity between fold expressions
    // and assignment expressions within parentheses.
    parenthesized_expression: ($, /** @type {Rule} */ original) =>
      choice(original, seq('(', alias($._assignment_expression_lhs, $.assignment_expression), ')')),

    reflect_expression: ($) =>
      prec.right(
        seq(
          '^^',
          choice(
            '::',
            $.expression,
            $.type_descriptor,
            prec.dynamic(PREC.CERTAIN_DECLARATION, alias($.built_in_function_type_descriptor, $.type_descriptor))
          )
        )
      ),

    // The operand of `^^` is a type-id, a name, or `::`, never a call, so `^^int()` reflects a function type although
    // the call `int()` covers the same tokens.
    built_in_function_type_descriptor: ($) =>
      seq(
        field('type', $.primitive_type),
        field('declarator', alias($.built_in_function_type_declarator, $.abstract_function_declarator))
      ),
    built_in_function_type_declarator: ($) => $._function_declarator_seq,

    splice_specifier: ($) => seq('[:', $.expression, ':]'),
    _splice_specialization_specifier: ($) => seq($.splice_specifier, $.template_argument_list),

    splice_type_specifier: ($) => prec.right(choice($.splice_specifier, $._splice_specialization_specifier)),

    splice_expression: ($) =>
      prec.right(choice($.splice_specifier, seq('template', $._splice_specialization_specifier))),

    expansion_statement: ($) => seq('template', 'for', '(', $._for_range_loop_body, ')', field('body', $.statement)),

    operator_name: ($) =>
      prec(
        1,
        seq(
          'operator',
          choice(
            'co_await',
            '+',
            '-',
            '*',
            '/',
            '%',
            '^',
            '&',
            '|',
            '~',
            '!',
            '=',
            '<',
            '>',
            '+=',
            '-=',
            '*=',
            '/=',
            '%=',
            '^=',
            '&=',
            '|=',
            '<<',
            '>>',
            '>>=',
            '<<=',
            '==',
            '!=',
            '<=',
            '>=',
            '<=>',
            '&&',
            '||',
            '++',
            '--',
            ',',
            '->*',
            '->',
            '()',
            '[]',
            'xor',
            'bitand',
            'bitor',
            'compl',
            'not',
            'xor_eq',
            'and_eq',
            'or_eq',
            'not_eq',
            'and',
            'or',
            seq(choice('new', 'delete'), optional('[]')),
            seq('""', $._contextual_identifier)
          )
        )
      ),

    this: () => 'this',

    concatenated_string: ($) =>
      prec.right(
        seq(
          choice($._contextual_identifier, $.string_literal, $.raw_string_literal),
          choice($.string_literal, $.raw_string_literal),
          repeat(choice($._contextual_identifier, $.string_literal, $.raw_string_literal))
        )
      ),

    number_literal: () => {
      const sign = /[-+]/;
      const separator = "'";
      const binary = /[01]/;
      const binaryDigits = seq(repeat1(binary), repeat(seq(separator, repeat1(binary))));
      const decimal = /[0-9]/;
      const firstDecimal = /[1-9]/;
      const intDecimalDigits = seq(firstDecimal, repeat(decimal), repeat(seq(separator, repeat1(decimal))));
      const floatDecimalDigits = seq(repeat1(decimal), repeat(seq(separator, repeat1(decimal))));
      const hex = /[0-9a-fA-F]/;
      const hexDigits = seq(repeat1(hex), repeat(seq(separator, repeat1(hex))));
      const octal = /[0-7]/;
      const octalDigits = seq('0', repeat(octal), repeat(seq(separator, repeat1(octal))));
      const hexExponent = seq(/[pP]/, optional(sign), floatDecimalDigits);
      const decimalExponent = seq(/[eE]/, optional(sign), floatDecimalDigits);
      const intSuffix = /(ll|LL)[uU]?|[uU](ll|LL)?|[uU][lL]?|[uU][zZ]?|[lL][uU]?|[zZ][uU]?/;
      const floatSuffix = /([fF](16|32|64|128)?)|[lL]|(bf16|BF16)/;

      return token(
        seq(
          optional(sign),
          choice(
            seq(
              choice(
                seq(choice('0b', '0B'), binaryDigits),
                intDecimalDigits,
                seq(choice('0x', '0X'), hexDigits),
                octalDigits
              ),
              optional(intSuffix)
            ),
            seq(
              choice(
                seq(floatDecimalDigits, decimalExponent),
                seq(floatDecimalDigits, '.', optional(floatDecimalDigits), optional(decimalExponent)),
                seq('.', floatDecimalDigits, optional(decimalExponent)),
                seq(
                  choice('0x', '0X'),
                  choice(hexDigits, seq(hexDigits, '.', optional(hexDigits)), seq('.', hexDigits)),
                  hexExponent
                )
              ),
              optional(floatSuffix)
            )
          )
        )
      );
    },

    literal_suffix: () => token.immediate(/[a-zA-Z_]\w*/),

    user_defined_literal: ($) => seq(choice($.number_literal, $.char_literal, $._string), $.literal_suffix),

    _type_identifier: ($, /** @type {Rule} */ original) =>
      choice(original, alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES), $.type_identifier)),

    _contextual_identifier: ($) =>
      choice($.identifier, alias(choice(...VA_ARG_CONTEXTUAL_IDENTIFIER_NAMES), $.identifier)),

    _field_identifier: ($, /** @type {Rule} */ original) =>
      choice(original, alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES), $.field_identifier)),

    _statement_identifier: ($, /** @type {Rule} */ original) =>
      choice(original, alias(choice(...CONTEXTUAL_IDENTIFIER_NAMES), $.statement_identifier)),

    _namespace_identifier: ($) =>
      choice(
        alias($.identifier, $.namespace_identifier),
        alias(choice(...VA_ARG_CONTEXTUAL_IDENTIFIER_NAMES), $.namespace_identifier)
      ),
  },
});

/**
 * @param {GrammarSymbols<string>} $
 * @returns {Rule}
 */
function fieldExpressionMember($) {
  return choice(
    prec.dynamic(1, $._field_identifier),
    alias($.qualified_field_identifier, $.qualified_identifier),
    $.destructor_name,
    alias($._template_destructor_name, $.destructor_name),
    $.template_method,
    alias($.dependent_field_identifier, $.dependent_name),
    $.operator_name,
    $.splice_expression
  );
}

/**
 * C's declaration uses _declaration_declarator, whose function declarators take macro attributes; that causes a world of
 * pain for C++, so C++ uses the plain _declarator.
 * @param {GrammarSymbols<string>} $
 * @returns {Rule}
 */
function declarationItem($) {
  return choice(seq(optional($.ms_call_modifier), $._declarator, optional($.gnu_asm_expression)), $.init_declarator);
}

/**
 * `sizeof(x)`, `sizeof(x[0])`, and `sizeof(f())` read as either a type or an expression, and an even tie let the other
 * readings of the statement decide, so `sizeof(x)[0]` could apply `[0]` to the size of a type `x`. The type reading
 * loses unless the parenthesized operand is a qualified name, which keeps `sizeof(ns::T)` a type.
 * @param {GrammarSymbols<string>} $
 */
function sizeofTypeOperand($) {
  return choice(
    prec.dynamic(-1, seq('(', field('type', $.type_descriptor), ')')),
    prec.dynamic(1, seq('(', field('type', alias($.qualified_type_descriptor, $.type_descriptor)), ')'))
  );
}

/**
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} self
 * @param {RuleOrLiteral} pointer
 */
function pointerToMember($, self, pointer) {
  return seq($._scope_resolution, field('name', choice(alias(self, $.qualified_identifier), pointer)));
}

/**
 * Replaces the `declarator` field of a declarator rule, failing when the rule has none.
 * @param {Rule} rule
 * @param {Rule} declarator
 * @returns {Rule}
 */
function withDeclarator(rule, declarator) {
  const result = replaceDeclarator(rule, declarator);
  if (JSON.stringify(result) === JSON.stringify(rule)) throw new Error('The rule has no declarator field to replace.');
  return result;
}

/**
 * @param {Rule} rule
 * @param {Rule} declarator
 * @returns {Rule}
 */
function replaceDeclarator(rule, declarator) {
  switch (rule.type) {
    case 'FIELD': {
      return rule.name === 'declarator' ? field('declarator', declarator) : rule;
    }
    case 'SEQ':
    case 'CHOICE': {
      return { ...rule, members: rule.members.map((member) => replaceDeclarator(member, declarator)) };
    }
    case 'PREC':
    case 'PREC_DYNAMIC':
    case 'PREC_LEFT':
    case 'PREC_RIGHT': {
      return { ...rule, content: replaceDeclarator(rule.content, declarator) };
    }
    default: {
      return rule;
    }
  }
}

/**
 * @param {Rule} rule
 */
function isOldStyleFunctionDefinition(rule) {
  return (
    rule.type === 'ALIAS' && rule.content.type === 'SYMBOL' && rule.content.name === '_old_style_function_definition'
  );
}

/**
 * @param {GrammarSymbols<string>} $
 * @param {Rule} pack
 */
function packIndex($, pack) {
  return seq(field('pack', pack), alias($._pack_index_operator, '...'), '[', field('index', $.expression), ']');
}
