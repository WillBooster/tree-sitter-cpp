import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { type AstCapture, captureNodes, collectCaptures, fieldChild, fieldChildren } from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/preprocessorWordOperators.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('parses preprocessing operator precedence, source ranges and branches for each spelling', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const original = '1 or 0 and 0';
  const start = source.indexOf(original);
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    assertWordOperatorStructure(tree.rootNode);
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
        const captures = operatorAndFunctionCaptures(variant.rootNode);
        expect(captureNodes(captures, 'name').map((node) => node.text)).toEqual([
          'logical',
          'bitwise',
          'unary',
          'first_branch',
          'second_branch',
          'ordinary_identifier',
          'macro_branch',
          'following',
        ]);
        for (const { node } of captures) expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(variant.rootNode.lastNamedChild?.text).toBe('int following() { return 4; }');
      } finally {
        variant.delete();
      }
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('parses discarded preprocessing macro arguments as bare tokens and unary expressions', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const argumentText of ['not', 'not FLAG', 'compl']) {
      const tree = parser.parse(`#define F(x) 1\n#if F(${argumentText})\nint selected;\n#endif\nint following;\n`)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const argument = tree.rootNode.descendantsOfType('argument_list')[0]!;
        expect(argument.text).toBe(`(${argumentText})`);
        expect(argument.firstNamedChild?.text).toBe(argumentText);
        const names = tree.rootNode
          .descendantsOfType('declaration')
          .flatMap((node) => fieldChildren(node, 'declarator', 'identifier'));
        expect(names.map((node) => node.text)).toEqual(['selected', 'following']);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following;');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

function assertWordOperatorStructure(rootNode: Node): void {
  const conditions = rootNode.descendantsOfType('preproc_if').map((node) => node.childForFieldName('condition'));
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
  const macro = rootNode
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
  expect(macro.namedChildren.slice(0, 2).map((node) => node.type)).toEqual(['operator_argument', 'operator_argument']);
  expect(
    rootNode
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
}

function operatorAndFunctionCaptures(rootNode: Node): AstCapture[] {
  return collectCaptures(rootNode, (node, emit) => {
    if (node.type === 'binary_expression' || node.type === 'unary_expression') {
      emit('operator', fieldChild(node, 'operator'));
    }
    if (node.type === 'function_declarator') {
      emit('name', fieldChild(node, 'declarator', 'identifier'));
    }
  });
}
