import fs from 'node:fs';

import { Edit, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();
const source = fs.readFileSync(new URL('../fixtures/classMacros.cpp', import.meta.url), 'utf8');

test('preserves macro class definitions, ordinary declarators, and class tags after edits', () => {
  const parser = new Parser().setLanguage(language);
  const tags = new Query(language, fs.readFileSync(new URL('../../queries/tags.scm', import.meta.url), 'utf8'));
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
    const startIndex = source.indexOf('Derived');
    tree.edit(
      new Edit({
        startIndex,
        oldEndIndex: startIndex + 7,
        newEndIndex: startIndex + 7,
        startPosition: { row: 3, column: 10 },
        oldEndPosition: { row: 3, column: 17 },
        newEndPosition: { row: 3, column: 17 },
      })
    );
    const editedSource = source.replace('Derived', 'Renamed');
    const edited = parser.parse(editedSource, tree)!;
    const fresh = parser.parse(editedSource)!;
    try {
      expect(edited.rootNode.hasError, edited.rootNode.toString()).toBe(false);
      expect(edited.rootNode.toString()).toBe(fresh.rootNode.toString());
      const definitions = tags
        .captures(edited.rootNode)
        .filter(({ name }) => name === 'definition.class')
        .map(({ node }) => node.childForFieldName('name')?.text);
      expect(definitions).toContain('Renamed');
      expect(definitions).toContain('Plain');
      expect(definitions).toContain('Payload');
      expect(definitions).not.toContain('API');
      const functionNames = tags
        .captures(edited.rootNode)
        .filter(({ name }) => name === 'definition.function')
        .map(({ node }) => node.childForFieldName('declarator')?.text);
      expect(functionNames).toContain('ordinaryFunction');
      expect(functionNames).toContain('ordinaryElaboratedFunction');
    } finally {
      fresh.delete();
      edited.delete();
    }
  } finally {
    tree.delete();
    tags.delete();
    parser.delete();
  }
});

test('retains macro function fields and query ranges after body edits', () => {
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
    const startIndex = source.indexOf('return 1;') + 'return '.length;
    tree.edit(
      new Edit({
        startIndex,
        oldEndIndex: startIndex + 1,
        newEndIndex: startIndex + 1,
        startPosition: { row: 20, column: 32 },
        oldEndPosition: { row: 20, column: 33 },
        newEndPosition: { row: 20, column: 33 },
      })
    );
    const editedSource = source.replace('return 1;', 'return 2;');
    const edited = parser.parse(editedSource, tree)!;
    const fresh = parser.parse(editedSource)!;
    try {
      expect(edited.rootNode.hasError, edited.rootNode.toString()).toBe(false);
      expect(edited.rootNode.toString()).toBe(fresh.rootNode.toString());
      const captures = (parsed: typeof tree): [string, string, number, number][] =>
        query.captures(parsed.rootNode).map(({ name, node }) => [name, node.text, node.startIndex, node.endIndex]);
      expect(captures(edited)).toEqual(captures(fresh));
      expect(
        query
          .captures(edited.rootNode)
          .filter(({ name }) => name === 'body')
          .map(({ node }) => node.text)
      ).toEqual(['{ return 2; }', '{ return Type{}; }', '{ return Type{}; }']);
    } finally {
      fresh.delete();
      edited.delete();
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
