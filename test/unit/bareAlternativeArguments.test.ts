import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/bareAlternativeArguments.cpp'), 'utf8');
const primarySpellings = new Map([
  ['and', '&&'],
  ['or', '||'],
  ['bitand', '&'],
  ['bitor', '|'],
  ['xor', '^'],
  ['not_eq', '!='],
  ['and_eq', '&='],
  ['or_eq', '|='],
  ['xor_eq', '^='],
  ['not', '!'],
  ['compl', '~'],
]);
const wordPattern = /\b(?:and|or|bitand|bitor|xor|not_eq|and_eq|or_eq|xor_eq|not|compl)\b/g;
await Parser.init();
const language = await loadCurrentWasmBuild();

test('exposes alternative words as operator arguments with primary-equivalent named structure', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const primary = parser.parse(source.replaceAll(wordPattern, (word) => primarySpellings.get(word)!))!;
  const abstractTypes = parser.parse('using L = int(bitand)[3]; using R = int(and)[3]; using F = void(bitand)(int);')!;
  const control = parser.parse('int anderson; int bitanderson; const char* text = R"(TAKE(and, bitand, xor_eq))";')!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(primary.rootNode.hasError, primary.rootNode.toString()).toBe(false);
    expect(namedStructure(tree.rootNode)).toEqual(namedStructure(primary.rootNode));
    for (const match of source.matchAll(wordPattern)) {
      const token = tree.rootNode.descendantForIndex(match.index, match.index + match[0].length)!;
      expect(token.parent?.type, match[0]).toBe('operator_argument');
      expect(token.text).toBe(match[0]);
    }
    expect(abstractTypes.rootNode.hasError, abstractTypes.rootNode.toString()).toBe(false);
    expect(abstractTypes.rootNode.descendantsOfType('operator_argument')).toEqual([]);
    expect(abstractTypes.rootNode.descendantsOfType('abstract_reference_declarator')).toHaveLength(3);
    expect(control.rootNode.hasError, control.rootNode.toString()).toBe(false);
    expect(control.rootNode.descendantsOfType('operator_argument')).toEqual([]);
  } finally {
    tree.delete();
    primary.delete();
    abstractTypes.delete();
    control.delete();
    parser.delete();
  }
});

test('keeps the following function after each macro argument operator spelling', () => {
  const parser = new Parser().setLanguage(language);
  const start = source.indexOf('TAKE(and') + 'TAKE('.length;
  try {
    for (const spelling of ['and', '&&']) {
      const tree = parser.parse(source.slice(0, start) + spelling + source.slice(start + 'and'.length))!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return branch; }');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('keeps singleton word macro arguments clean for one or many arguments', () => {
  const original = fs.readFileSync(path.join(root, 'test/fixtures/singleAlternativeArguments.cpp'), 'utf8');
  const parser = new Parser().setLanguage(language);
  const start = original.indexOf('OP(and)') + 'OP('.length;
  try {
    for (const argument of ['and', 'bitand', 'and, bitand', '']) {
      const source = original.slice(0, start) + argument + original.slice(start + 'and'.length);
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
        expect(tree.rootNode.descendantsOfType('declaration').map((node) => node.text)).toEqual([
          'int y = SOME(and) + 1;',
          'int z = 1;',
        ]);
        expect(tree.rootNode.descendantsOfType('if_statement')).toHaveLength(1);
        const call = tree.rootNode.descendantsOfType('call_expression')[0]!.childForFieldName('arguments')!;
        expect(call.descendantsOfType('operator_argument').map((node) => node.text)).toEqual(
          argument.includes(',') ? ['and', 'bitand'] : []
        );
        if (argument !== 'and') continue;
        for (const match of source.matchAll(/\b(?:and|bitand)\b/g)) {
          const token = tree.rootNode.descendantForIndex(match.index, match.index + match[0].length)!;
          expect(token.type).toBe('identifier');
        }
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

function namedStructure(node: Node): unknown {
  return [
    node.type,
    ...node.children.flatMap((child, index) =>
      child.isNamed ? [[node.fieldNameForChild(index), namedStructure(child)]] : []
    ),
  ];
}
