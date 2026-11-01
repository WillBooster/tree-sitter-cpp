import fs from 'node:fs';

import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();
const source = fs.readFileSync(new URL('../fixtures/classMacros.cpp', import.meta.url), 'utf8');

test('preserves macro class definitions and ordinary declarators', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(
      tree.rootNode
        .descendantsOfType(['class_specifier', 'struct_specifier', 'union_specifier'])
        .filter((node) => node.childForFieldName('body'))
        .map((node) => node.childForFieldName('name')?.text)
    ).toEqual(['Base', 'Derived', 'Plain', 'Payload', 'Value', 'Type', 'Member']);
    expect(
      tree.rootNode.descendantsOfType('init_declarator').map((node) => node.childForFieldName('declarator')?.text)
    ).toEqual(['object', 'copied']);
    expect(
      tree.rootNode.descendantsOfType('function_definition').map((node) => {
        const declarator = node.childForFieldName('declarator')!;
        return declarator.type === 'identifier' ? declarator.text : declarator.descendantsOfType('identifier')[0]?.text;
      })
    ).toEqual([
      'parenthesized',
      'nested',
      'returnPointer',
      'returnReference',
      'returnArrayPointer',
      'memberPointer',
      'PRIMITIVE_FUNCTION',
      'USER_FUNCTION',
      'ELABORATED_FUNCTION',
      'ordinaryFunction',
      'ordinaryElaboratedFunction',
    ]);
    const renamed = parser.parse(source.replace('Derived', 'Renamed'))!;
    try {
      expect(renamed.rootNode.hasError, renamed.rootNode.toString()).toBe(false);
    } finally {
      renamed.delete();
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('retains macro function fields and ranges with changed bodies', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(function_definition type: (_) @type declarator: (identifier) @declarator body: (compound_statement) @body) @function'
  );
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const functions = tree.rootNode
      .descendantsOfType('function_definition')
      .filter((node) => node.childForFieldName('declarator')?.type === 'identifier');
    expect(
      functions.map((node) => ['type', 'declarator', 'body'].map((field) => node.childForFieldName(field)?.text))
    ).toEqual([
      ['int', 'PRIMITIVE_FUNCTION', '{ return 1; }'],
      ['Type', 'USER_FUNCTION', '{ return Type{}; }'],
      ['class Type', 'ELABORATED_FUNCTION', '{ return Type{}; }'],
    ]);
    for (const node of functions) {
      const type = node.childForFieldName('type')!;
      const declarator = node.childForFieldName('declarator')!;
      const body = node.childForFieldName('body')!;
      const start = source.indexOf(`${type.text} ${declarator.text} ${body.text}`);
      expect(node.startIndex).toBe(start);
      expect(declarator.startIndex).toBe(start + type.text.length + 1);
      expect(declarator.endIndex).toBe(declarator.startIndex + declarator.text.length);
      expect(body.startIndex).toBe(declarator.endIndex + 1);
      expect(node.endIndex).toBe(body.startIndex + body.text.length);
    }
    const changed = parser.parse(source.replace('return 1;', 'return 2;'))!;
    try {
      expect(changed.rootNode.hasError, changed.rootNode.toString()).toBe(false);
      expect(
        query
          .captures(changed.rootNode)
          .filter(({ name }) => name === 'body')
          .map(({ node }) => node.text)
      ).toEqual(['{ return 2; }', '{ return Type{}; }', '{ return Type{}; }']);
    } finally {
      changed.delete();
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('retains nested template argument types inside function bodies', () => {
  const nestedSource = fs.readFileSync(new URL('../fixtures/nestedTemplateStatements.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(template_argument_list (type_descriptor type: (qualified_identifier name: (template_type) @nested)))'
  );
  const tree = parser.parse(nestedSource)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(query.captures(tree.rootNode).map(({ node }) => node.text)).toEqual([
      'vector<std::string>',
      'set<std::string>',
      'unordered_set<std::string>',
      'bar<baz::qux>',
    ]);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('keeps nonempty elaborated-type variable initializers as declarations', () => {
  const input = fs.readFileSync(new URL('../fixtures/elaboratedInitializers.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(declaration type: (class_specifier) declarator: (init_declarator declarator: (identifier) @variable value: (initializer_list)))'
  );
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(query.captures(tree.rootNode).map(({ node }) => node.text)).toEqual([
      'object',
      'defaultItem',
      'designated',
      'nested',
      'invoked',
      'parenthesized',
      'innerDesignated',
      'copied',
      'empty',
    ]);
    expect(tree.rootNode.descendantsOfType('class_specifier').some((node) => node.childForFieldName('body'))).toBe(
      false
    );
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('retains nested template expression types and ranges inside function bodies', () => {
  const input = fs.readFileSync(new URL('../fixtures/nestedTemplateExpressions.cpp', import.meta.url), 'utf8');
  const first = 'std::cout << std::is_same<Plain, A<Nested<C>>>::value;';
  const second = 'std::cout << std::is_same<ns::A<std::vector<std::uint8_t>>, Plain>::value;';
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    `(template_argument_list (type_descriptor type: (template_type) @nested))
(template_argument_list (type_descriptor type: (qualified_identifier name: (template_type) @nested)))`
  );
  try {
    for (const [removed, expected] of [
      [second, ['A<Nested<C>>', 'Nested<C>']],
      [first, ['A<std::vector<std::uint8_t>>']],
    ] as const) {
      const current = input.replace(removed, '');
      const tree = parser.parse(current)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(tree.rootNode.descendantsOfType('binary_expression')).toHaveLength(1);
        expect(tree.rootNode.descendantsOfType('comma_expression')).toHaveLength(0);
        const captures = query.captures(tree.rootNode);
        expect(captures.map(({ node }) => node.text)).toEqual(expect.arrayContaining([...expected]));
        for (const { node } of captures) {
          const start = current.indexOf(node.text, current.indexOf('int main()'));
          expect(node.startIndex).toBe(start);
          expect(node.endIndex).toBe(start + node.text.length);
        }
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

test('preserves braced arguments after nested qualified templates', () => {
  const input = fs.readFileSync(new URL('../fixtures/nestedTemplateInitializers.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(input)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const call = tree.rootNode.descendantsOfType('call_expression')[0]!;
    expect(call.childForFieldName('function')?.childForFieldName('name')?.childForFieldName('name')?.text).toBe(
      'to_array'
    );
    const argumentsNode = call.childForFieldName('arguments')!;
    expect(argumentsNode.namedChildren.map((node) => node.type)).toEqual(['initializer_list']);
    expect(argumentsNode.namedChildren[0]!.namedChildren.map((node) => node.text)).toEqual([
      '{1, {"first"}}',
      '{2, {"second"}}',
      '{3, {"third"}}',
    ]);
    expect(tree.rootNode.descendantsOfType('compound_literal_expression')).toHaveLength(0);
    const scopedType = tree.rootNode
      .descendantsOfType('type_descriptor')
      .find((node) => node.text === 'std::string_view')!;
    expect(scopedType.childForFieldName('type')?.childForFieldName('name')?.text).toBe('string_view');
  } finally {
    tree.delete();
    parser.delete();
  }
});
