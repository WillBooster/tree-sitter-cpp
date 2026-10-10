import fs from 'node:fs';
import path from 'node:path';

import { expect, test } from 'vitest';

import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';

import treeSitterJson from '../../tree-sitter.json';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.join(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/constructorPreproc.cpp'), 'utf8');
await Parser.init();

test('keeps conditional constructor initializers, following tags and edits in their public parents', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  const queries: Query[] = [];
  let tree: Tree | undefined;
  try {
    parser.setLanguage(language);
    for (const kind of ['highlights', 'tags']) {
      const files = kind === 'highlights' ? treeSitterJson.grammars[0]!.highlights : [treeSitterJson.grammars[0]!.tags];
      queries.push(new Query(language, files.map((file) => fs.readFileSync(path.join(Root, file), 'utf8')).join('\n')));
    }
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
    const names = queries[1]!
      .captures(tree.rootNode)
      .filter(({ name }) => name === 'name')
      .map(({ node }) => node.text);
    expect(names).toEqual([
      'Leading',
      'Leading',
      'Trailing',
      'Trailing',
      'AllGuarded',
      'AllGuarded',
      'Nested',
      'Nested',
      'Base',
      'Base',
      'Out',
      'Out',
      'Out',
      'after',
      'main',
    ]);
    const directives = queries[0]!.captures(tree.rootNode).filter(({ name }) => name === 'keyword');
    expect(directives.map(({ node }) => node.text)).toContain('#elif');
    expect(directives.map(({ node }) => node.text)).toContain('#ifdef');
    expect(directives.map(({ node }) => node.text)).toContain('#ifndef');

    const conditional = Source.slice(Source.indexOf('#if SELECT'), Source.indexOf('#endif') + '#endif'.length);
    const plain = '    , b(n + 1)';
    let source = Source;
    for (const [before, afterText, hasError] of [
      [conditional, plain, false],
      [plain, conditional, false],
      ['#endif', '#endi', true],
      ['#endi\n', '#endif\n', false],
    ] as const) {
      const startIndex = source.indexOf(before);
      expect(startIndex).toBeGreaterThanOrEqual(0);
      const nextSource = source.slice(0, startIndex) + afterText + source.slice(startIndex + before.length);
      tree.edit(
        new Edit({
          startIndex,
          oldEndIndex: startIndex + before.length,
          newEndIndex: startIndex + afterText.length,
          startPosition: point(source, startIndex),
          oldEndPosition: point(source, startIndex + before.length),
          newEndPosition: point(nextSource, startIndex + afterText.length),
        })
      );
      let incremental: Tree | undefined;
      let fresh: Tree | undefined;
      try {
        incremental = parser.parse(nextSource, tree)!;
        fresh = parser.parse(nextSource)!;
        expect(incremental.rootNode.hasError).toBe(hasError);
        expect(snapshot(incremental.rootNode)).toEqual(snapshot(fresh.rootNode));
        for (const query of queries) {
          expect(query.captures(incremental.rootNode).map(({ name, node }) => [name, snapshot(node)])).toEqual(
            query.captures(fresh.rootNode).map(({ name, node }) => [name, snapshot(node)])
          );
        }
        tree.delete();
        tree = incremental;
        incremental = undefined;
        source = nextSource;
      } finally {
        incremental?.delete();
        fresh?.delete();
      }
    }
    expect(source).toBe(Source);
  } finally {
    tree?.delete();
    for (const query of queries) query.delete();
    parser.delete();
  }
}, 30_000);

function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}

function snapshot(node: Node): unknown {
  return {
    type: node.type,
    named: node.isNamed,
    extra: node.isExtra,
    missing: node.isMissing,
    error: node.hasError,
    start: node.startIndex,
    end: node.endIndex,
    startPosition: node.startPosition,
    endPosition: node.endPosition,
    children: node.children.map((child, index) => [node.fieldNameForChild(index), snapshot(child)]),
  };
}
