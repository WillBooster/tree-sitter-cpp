import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { fieldChild } from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const source = fs.readFileSync(path.join(import.meta.dirname, '../fixtures/parenthesizedDeclarators.cpp'), 'utf8');
await Parser.init();

test('exposes enclosed declarators through parentheses in declarations and abstract types', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const parenthesized = tree.rootNode.descendantsOfType([
      'parenthesized_declarator',
      'abstract_parenthesized_declarator',
    ]);
    expect(parenthesized.length).toBeGreaterThan(10);
    expect(enclosedDeclarators(tree.rootNode).map((node) => [node.type, node.text])).toEqual(
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
    const renamed = parser.parse(source.slice(0, index) + 'renamedPointer' + source.slice(index + 'pointer'.length))!;
    try {
      expect(renamed.rootNode.hasError).toBe(false);
      expect(enclosedDeclarators(renamed.rootNode).some((node) => node.text === '*renamedPointer')).toBe(true);
    } finally {
      renamed.delete();
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

function enclosedDeclarators(root: Node): Node[] {
  return root
    .descendantsOfType(['parenthesized_declarator', 'abstract_parenthesized_declarator'])
    .flatMap((node) => fieldChild(node, 'declarator') ?? []);
}
