import fs from 'node:fs';
import path from 'node:path';

import { Edit, type Node, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const source = fs.readFileSync(path.join(import.meta.dirname, '../fixtures/parenthesizedDeclarators.cpp'), 'utf8');
await Parser.init();

test('exposes enclosed declarators through parentheses in declarations and abstract types', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const query = new Query(
    language,
    `[(parenthesized_declarator declarator: (_) @declarator)
      (abstract_parenthesized_declarator declarator: (_) @declarator)]`
  );
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const parenthesized = tree.rootNode.descendantsOfType([
      'parenthesized_declarator',
      'abstract_parenthesized_declarator',
    ]);
    expect(parenthesized.length).toBeGreaterThan(10);
    expect(query.captures(tree.rootNode).map(({ node }) => [node.type, node.text])).toEqual(
      parenthesized.map((node) => {
        const enclosed = node.childForFieldName('declarator');
        expect(enclosed?.equals(node.firstNamedChild!)).toBe(true);
        expect(enclosed!.startIndex).toBeGreaterThan(node.startIndex);
        expect(enclosed!.endIndex).toBeLessThan(node.endIndex);
        return [enclosed!.type, enclosed!.text];
      })
    );
    expect(parenthesized.map((node) => node.childForFieldName('declarator')!.text)).toContain('*pointer');
    expect(parenthesized.map((node) => node.childForFieldName('declarator')!.text)).toContain('Owner::*');

    const index = source.indexOf('pointer');
    const replacement = 'renamedPointer';
    const changed = source.slice(0, index) + replacement + source.slice(index + 'pointer'.length);
    tree.edit(
      new Edit({
        startIndex: index,
        oldEndIndex: index + 'pointer'.length,
        newEndIndex: index + replacement.length,
        startPosition: { row: 1, column: 6 },
        oldEndPosition: { row: 1, column: 13 },
        newEndPosition: { row: 1, column: 6 + replacement.length },
      })
    );
    const incremental = parser.parse(changed, tree)!;
    const fresh = parser.parse(changed)!;
    try {
      expect(incremental.rootNode.hasError).toBe(false);
      expect(fieldTree(incremental.rootNode)).toEqual(fieldTree(fresh.rootNode));
      expect(query.captures(incremental.rootNode).some(({ node }) => node.text === '*renamedPointer')).toBe(true);
    } finally {
      incremental.delete();
      fresh.delete();
    }
  } finally {
    query.delete();
    tree.delete();
    parser.delete();
  }
});

function fieldTree(node: Node): unknown {
  return [
    node.type,
    node.startIndex,
    node.endIndex,
    node.namedChildren.map((child, index) => [node.fieldNameForNamedChild(index), fieldTree(child)]),
  ];
}
