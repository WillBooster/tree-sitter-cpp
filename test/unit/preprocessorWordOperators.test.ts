import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/preprocessorWordOperators.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('parses preprocessing operator precedence, source ranges and branches for each spelling', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(binary_expression operator: _ @operator) (unary_expression operator: _ @operator) (function_declarator declarator: (identifier) @name)'
  );
  const tree = parser.parse(source)!;
  const original = '1 or 0 and 0';
  const start = source.indexOf(original);
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
    for (const replacement of [original, '1 || 0 && 0', '1 or 0 /* gap */ and 0']) {
      const nextSource = source.slice(0, start) + replacement + source.slice(start + original.length);
      const variant = parser.parse(nextSource)!;
      try {
        expect(variant.rootNode.hasError, variant.rootNode.toString()).toBe(false);
        const condition = variant.rootNode.descendantsOfType('preproc_if')[0]!.childForFieldName('condition')!;
        expect([condition.startIndex, condition.endIndex, condition.text]).toEqual([
          start,
          start + replacement.length,
          replacement,
        ]);
        expect(
          query
            .captures(variant.rootNode)
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
        for (const { node } of query.captures(variant.rootNode))
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(variant.rootNode.lastNamedChild?.text).toBe('int following() { return 4; }');
      } finally {
        variant.delete();
      }
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('parses discarded preprocessing macro arguments as bare tokens and unary expressions', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(language, '(argument_list) @arguments (declaration declarator: (identifier) @name)');
  try {
    for (const argumentText of ['not', 'not FLAG', 'compl']) {
      const tree = parser.parse(`#define F(x) 1\n#if F(${argumentText})\nint selected;\n#endif\nint following;\n`)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const argument = tree.rootNode.descendantsOfType('argument_list')[0]!;
        expect(argument.text).toBe(`(${argumentText})`);
        expect(argument.firstNamedChild?.text).toBe(argumentText);
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'name')
            .map(({ node }) => node.text)
        ).toEqual(['selected', 'following']);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following;');
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});
