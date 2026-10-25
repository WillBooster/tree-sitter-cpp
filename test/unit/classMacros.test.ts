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
      tree.rootNode
        .descendantsOfType('function_definition')
        .map((node) => node.childForFieldName('declarator')?.descendantsOfType('identifier')[0]?.text)
    ).toEqual(['parenthesized', 'nested', 'returnPointer', 'returnReference', 'returnArrayPointer', 'memberPointer']);
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
