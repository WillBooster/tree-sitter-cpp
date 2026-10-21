import fs from 'node:fs';
import path from 'node:path';
import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/preprocessorWordOperators.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves preprocessing operator precedence, source ranges and branches through spelling edits', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(binary_expression operator: _ @operator) (unary_expression operator: _ @operator) (function_declarator declarator: (identifier) @name)'
  );
  const highlights = new Query(
    language,
    ['queries/c/highlights.scm', 'queries/highlights.scm']
      .map((file) => fs.readFileSync(path.join(root, file), 'utf8'))
      .join('\n')
  );
  let current = source;
  let tree = parser.parse(source)!;
  const original = '1 or 0 and 0';
  const start = source.indexOf(original);
  let spelling = original;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const conditions = tree.rootNode.descendantsOfType('preproc_if').map((node) => node.childForFieldName('condition'));
    const logical = conditions[0]!;
    expect(logical.childForFieldName('operator')?.text).toBe('or');
    expect(logical.childForFieldName('right')?.childForFieldName('operator')?.text).toBe('and');
    const bitwise = conditions[1]!.childForFieldName('left')!.namedChild(0)!;
    expect(bitwise.childForFieldName('operator')?.text).toBe('bitor');
    expect(bitwise.childForFieldName('right')?.childForFieldName('operator')?.text).toBe('xor');
    expect(bitwise.childForFieldName('right')?.childForFieldName('right')?.childForFieldName('operator')?.text).toBe(
      'bitand'
    );
    const unary = conditions[2]!;
    expect(unary.childForFieldName('operator')?.text).toBe('and');
    expect(unary.childForFieldName('left')?.childForFieldName('operator')?.text).toBe('not');
    expect(unary.childForFieldName('right')?.childForFieldName('operator')?.text).toBe('not_eq');
    expect(unary.childForFieldName('right')?.childForFieldName('left')?.childForFieldName('operator')?.text).toBe(
      'compl'
    );
    expect(conditions.at(-1)?.type).toBe('identifier');
    expect(conditions.at(-1)?.text).toBe('andrew');
    for (const replacement of ['1 || 0 && 0', '1 or 0 /* gap */ and 0', original]) {
      const nextSource = current.slice(0, start) + replacement + current.slice(start + spelling.length);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + spelling.length,
          newEndIndex: start + replacement.length,
          startPosition: pointAt(current, start),
          oldEndPosition: pointAt(current, start + spelling.length),
          newEndPosition: pointAt(nextSource, start + replacement.length),
        })
      );
      const incremental = parser.parse(nextSource, tree)!;
      tree.delete();
      tree = incremental;
      const fresh = parser.parse(nextSource)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const condition = tree.rootNode.descendantsOfType('preproc_if')[0]!.childForFieldName('condition')!;
        expect([condition.startIndex, condition.endIndex, condition.text]).toEqual([
          start,
          start + replacement.length,
          replacement,
        ]);
        expect(metadata(tree.rootNode)).toEqual(metadata(fresh.rootNode));
        expect(captures(query, tree)).toEqual(captures(query, fresh));
        expect(captures(highlights, tree)).toEqual(captures(highlights, fresh));
        const highlighted = highlights.captures(tree.rootNode);
        for (const { name, node } of query.captures(tree.rootNode).filter(({ name }) => name === 'operator')) {
          expect(
            highlighted.filter(
              (capture) =>
                capture.name === name &&
                capture.node.startIndex === node.startIndex &&
                capture.node.endIndex === node.endIndex
            )
          ).toHaveLength(1);
        }
        expect(highlighted.some(({ name, node }) => name === 'operator' && node.text === 'andrew')).toBe(false);
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'name')
            .map(({ node }) => node.text)
        ).toEqual(['logical', 'bitwise', 'unary', 'first_branch', 'second_branch', 'ordinary_identifier', 'following']);
        for (const { node } of query.captures(tree.rootNode))
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 4; }');
      } finally {
        fresh.delete();
      }
      current = nextSource;
      spelling = replacement;
    }
    expect(current).toBe(source);
  } finally {
    tree.delete();
    query.delete();
    highlights.delete();
    parser.delete();
  }
});

function captures(query: Query, tree: Tree): unknown[] {
  return query.captures(tree.rootNode).map(({ name, node }) => [name, metadata(node)]);
}
function pointAt(text: string, index: number): { row: number; column: number } {
  const lines = text.slice(0, index).split('\n');
  return { row: lines.length - 1, column: lines.at(-1)!.length };
}
function metadata(node: Node): unknown {
  return [
    node.type,
    node.isNamed,
    node.isExtra,
    node.isMissing,
    node.hasError,
    node.startIndex,
    node.endIndex,
    node.startPosition,
    node.endPosition,
    node.children.map((child, index) => [node.fieldNameForChild(index), metadata(child)]),
  ];
}
