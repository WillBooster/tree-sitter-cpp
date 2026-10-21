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
    expect(conditions[4]?.type).toBe('identifier');
    expect(conditions[4]?.text).toBe('andrew');
    const macro = tree.rootNode
      .descendantsOfType('call_expression')
      .find((node) => node.childForFieldName('arguments')?.text.startsWith('(not,'))!
      .childForFieldName('arguments')!;
    expect(macro.namedChildren.map((node) => node.text)).toEqual([
      'not',
      'compl',
      'and',
      'or',
      'bitand',
      'bitor',
      'xor',
      'not_eq',
      'and_eq',
      'or_eq',
      'xor_eq',
      '!',
      '~',
      '*',
      '&',
      '&&',
      '<=>',
    ]);
    expect(macro.namedChildren.slice(0, 2).map((node) => node.type)).toEqual([
      'operator_argument',
      'operator_argument',
    ]);
    expect(
      tree.rootNode
        .descendantsOfType('call_expression')
        .filter((node) => ['(not)', '(compl)'].includes(node.childForFieldName('arguments')?.text ?? ''))
        .map((node) => [
          node.childForFieldName('arguments')!.text,
          node.childForFieldName('arguments')!.firstNamedChild!.type,
        ])
    ).toEqual([
      ['(not)', 'operator_argument'],
      ['(compl)', 'operator_argument'],
    ]);
    for (const replacement of ['1 || 0 && 0', '1 or 0 /* gap */ and 0', original]) {
      const edited = editSource(parser, tree, current, spelling, replacement);
      tree = edited.tree;
      const nextSource = edited.source;
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
        ).toEqual([
          'logical',
          'bitwise',
          'unary',
          'first_branch',
          'second_branch',
          'ordinary_identifier',
          'macro_branch',
          'following',
        ]);
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

test('edits discarded preprocessing macro arguments between bare tokens and unary expressions', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(argument_list) @arguments (declaration declarator: (identifier) @name)');
  const source = '#define F(x) 1\n#if F(not)\nint selected;\n#endif\nint following;\n';
  let current = source;
  let spelling = 'not';
  let tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    for (const replacement of ['not FLAG', 'compl', 'not']) {
      const edited = editSource(parser, tree, current, spelling, replacement);
      tree = edited.tree;
      const fresh = parser.parse(edited.source)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const argument = tree.rootNode.descendantsOfType('argument_list')[0]!;
        expect(argument.text).toBe(`(${replacement})`);
        expect(argument.firstNamedChild?.text).toBe(replacement);
        expect(metadata(tree.rootNode)).toEqual(metadata(fresh.rootNode));
        expect(captures(query, tree)).toEqual(captures(query, fresh));
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'name')
            .map(({ node }) => node.text)
        ).toEqual(['selected', 'following']);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following;');
      } finally {
        fresh.delete();
      }
      current = edited.source;
      spelling = replacement;
    }
    expect(current).toBe(source);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

function editSource(
  parser: Parser,
  tree: Tree,
  source: string,
  before: string,
  after: string
): { source: string; tree: Tree } {
  const start = source.indexOf(before);
  expect(start).toBeGreaterThanOrEqual(0);
  const nextSource = source.slice(0, start) + after + source.slice(start + before.length);
  tree.edit(
    new Edit({
      startIndex: start,
      oldEndIndex: start + before.length,
      newEndIndex: start + after.length,
      startPosition: pointAt(source, start),
      oldEndPosition: pointAt(source, start + before.length),
      newEndPosition: pointAt(nextSource, start + after.length),
    })
  );
  const next = parser.parse(nextSource, tree)!;
  tree.delete();
  return { source: nextSource, tree: next };
}

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
