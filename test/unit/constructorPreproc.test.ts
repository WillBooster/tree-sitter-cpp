import fs from 'node:fs';
import path from 'node:path';

import { Parser, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.join(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/constructorPreproc.cpp'), 'utf8');
await Parser.init();

test('keeps conditional constructor initializers and following definitions in their public parents', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  let tree: Tree | undefined;
  try {
    parser.setLanguage(language);
    tree = parser.parse(Source)!;
    expect(tree.rootNode.hasError).toBe(false);
    const lists = tree.rootNode.descendantsOfType('field_initializer_list');
    expect(lists).toHaveLength(5);
    for (const list of lists) {
      expect(['function_definition', 'try_statement']).toContain(list.parent?.type);
      expect(list.descendantsOfType(['preproc_if', 'preproc_ifdef']).length).toBeGreaterThan(0);
      for (const condition of list.descendantsOfType('preproc_if')) {
        expect(condition.childForFieldName('condition')?.type).toBe('binary_expression');
        expect(condition.childForFieldName('alternative')?.type).toMatch(/^preproc_(else|elif|elifdef)$/);
      }
    }
    expect(lists[0]!.descendantsOfType('field_initializer').map((node) => node.namedChild(0)?.text)).toEqual([
      'a',
      'b',
      'b',
      'b',
      'c',
    ]);
    const definitionArms = lists[1]!.descendantsOfType('preproc_elifdef');
    expect(
      definitionArms.map((node) => ({
        parent: node.parent?.type,
        nameType: node.childForFieldName('name')?.type,
        name: node.childForFieldName('name')?.text,
        initializers: node.namedChildren
          .filter((child) => child.type === 'field_initializer')
          .map((child) => child.text),
        alternative: node.childForFieldName('alternative')?.type,
      }))
    ).toEqual([
      {
        parent: 'preproc_if',
        nameType: 'identifier',
        name: 'FEATURE',
        initializers: ['a{n + 1}'],
        alternative: 'preproc_elifdef',
      },
      {
        parent: 'preproc_elifdef',
        nameType: 'identifier',
        name: 'FEATURE',
        initializers: ['a(n + 2)'],
        alternative: 'preproc_else',
      },
    ]);
    expect(lists[4]!.parent?.childForFieldName('body')?.type).toBe('compound_statement');
    const after = tree.rootNode.namedChildren.find(
      (node) => node.type === 'function_definition' && node.childForFieldName('declarator')?.text === 'after()'
    );
    expect(after?.startIndex).toBe(Source.indexOf('int after()'));
    expect(after?.endIndex).toBe(Source.indexOf('\nint main()'));
    const conditional = Source.slice(Source.indexOf('#if SELECT'), Source.indexOf('#endif') + '#endif'.length);
    for (const [before, afterText, hasError] of [
      [conditional, '    , b(n + 1)', false],
      ['#endif', '#endi', true],
    ] as const) {
      const startIndex = Source.indexOf(before);
      expect(startIndex).toBeGreaterThanOrEqual(0);
      const variant = parser.parse(Source.slice(0, startIndex) + afterText + Source.slice(startIndex + before.length))!;
      try {
        expect(variant.rootNode.hasError).toBe(hasError);
      } finally {
        variant.delete();
      }
    }
  } finally {
    tree?.delete();
    parser.delete();
  }
}, 30_000);
