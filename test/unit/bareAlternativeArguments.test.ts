import fs from 'node:fs';
import path from 'node:path';
import { Edit, Parser, Query, type Node } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import treeSitterJson from '../../tree-sitter.json';
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
  const query = highlights();
  const tree = parser.parse(source)!;
  const primary = parser.parse(source.replace(wordPattern, (word) => primarySpellings.get(word)!))!;
  const abstractTypes = parser.parse('using L = int(bitand)[3]; using R = int(and)[3]; using F = void(bitand)(int);')!;
  const control = parser.parse('int anderson; int bitanderson; const char* text = R"(TAKE(and, bitand, xor_eq))";')!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(primary.rootNode.hasError, primary.rootNode.toString()).toBe(false);
    expect(namedStructure(tree.rootNode)).toEqual(namedStructure(primary.rootNode));
    const captures = query.captures(tree.rootNode);
    for (const match of source.matchAll(wordPattern)) {
      const token = tree.rootNode.descendantForIndex(match.index, match.index + match[0].length)!;
      expect(token.parent?.type, match[0]).toBe('operator_argument');
      expect(token.text).toBe(match[0]);
      const at = captures.filter(
        ({ node }) => node.startIndex === match.index && node.endIndex === match.index + match[0].length
      );
      expect(
        at.filter(({ name }) => name === 'operator'),
        match[0]
      ).toHaveLength(1);
      expect(
        at.filter(({ name }) => name === 'variable'),
        match[0]
      ).toHaveLength(0);
    }
    expect(abstractTypes.rootNode.hasError, abstractTypes.rootNode.toString()).toBe(false);
    expect(abstractTypes.rootNode.descendantsOfType('operator_argument')).toEqual([]);
    expect(abstractTypes.rootNode.descendantsOfType('abstract_reference_declarator')).toHaveLength(3);
    expect(control.rootNode.hasError, control.rootNode.toString()).toBe(false);
    expect(control.rootNode.descendantsOfType('operator_argument')).toEqual([]);
    expect(
      query
        .captures(control.rootNode)
        .filter(({ name, node }) => name === 'operator' && primarySpellings.has(node.text))
    ).toEqual([]);
  } finally {
    tree.delete();
    primary.delete();
    abstractTypes.delete();
    control.delete();
    query.delete();
    parser.delete();
  }
});

test('preserves full metadata and captures through a macro argument spelling edit and undo', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  let current = source;
  let spelling = 'and';
  const start = source.indexOf('TAKE(and') + 'TAKE('.length;
  let tree = parser.parse(current)!;
  try {
    for (const replacement of ['&&', 'and']) {
      const next = current.slice(0, start) + replacement + current.slice(start + spelling.length);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + spelling.length,
          newEndIndex: start + replacement.length,
          startPosition: point(current, start),
          oldEndPosition: point(current, start + spelling.length),
          newEndPosition: point(next, start + replacement.length),
        })
      );
      const incremental = parser.parse(next, tree)!;
      tree.delete();
      tree = incremental;
      const fresh = parser.parse(next)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(metadata(tree.rootNode)).toEqual(metadata(fresh.rootNode));
        const captures = (node: Node): unknown => query.captures(node).map(({ name, node }) => [name, metadata(node)]);
        expect(captures(tree.rootNode)).toEqual(captures(fresh.rootNode));
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return branch; }');
      } finally {
        fresh.delete();
      }
      current = next;
      spelling = replacement;
    }
    expect(current).toBe(source);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});
test('keeps singleton word macro arguments clean through one-to-many argument edits and undo', () => {
  const original = fs.readFileSync(path.join(root, 'test/fixtures/singleAlternativeArguments.cpp'), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  let current = original;
  let spelling = 'and';
  const start = original.indexOf('OP(and)') + 'OP('.length;
  let tree = parser.parse(current)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const captures = query.captures(tree.rootNode);
    for (const match of original.matchAll(/\b(?:and|bitand)\b/g)) {
      const token = tree.rootNode.descendantForIndex(match.index, match.index + match[0].length)!;
      expect(token.type).toBe('identifier');
      const at = captures.filter(
        ({ node }) => node.startIndex === match.index && node.endIndex === match.index + match[0].length
      );
      expect(at.filter(({ name }) => name === 'operator')).toEqual([]);
      expect(at.filter(({ name }) => name === 'variable')).toHaveLength(1);
    }
    for (const replacement of ['bitand', 'and, bitand', '', 'and']) {
      const next = current.slice(0, start) + replacement + current.slice(start + spelling.length);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + spelling.length,
          newEndIndex: start + replacement.length,
          startPosition: point(current, start),
          oldEndPosition: point(current, start + spelling.length),
          newEndPosition: point(next, start + replacement.length),
        })
      );
      const incremental = parser.parse(next, tree)!;
      tree.delete();
      tree = incremental;
      const fresh = parser.parse(next)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(metadata(tree.rootNode)).toEqual(metadata(fresh.rootNode));
        const captureMetadata = (node: Node): unknown =>
          query.captures(node).map(({ name, node }) => [name, metadata(node)]);
        expect(captureMetadata(tree.rootNode)).toEqual(captureMetadata(fresh.rootNode));
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
        expect(tree.rootNode.descendantsOfType('declaration').map((node) => node.text)).toEqual([
          'int y = SOME(and) + 1;',
          'int z = 1;',
        ]);
        expect(tree.rootNode.descendantsOfType('if_statement')).toHaveLength(1);
        const call = tree.rootNode.descendantsOfType('call_expression')[0]!.childForFieldName('arguments')!;
        expect(call.descendantsOfType('operator_argument').map((node) => node.text)).toEqual(
          replacement.includes(',') ? ['and', 'bitand'] : []
        );
      } finally {
        fresh.delete();
      }
      current = next;
      spelling = replacement;
    }
    expect(current).toBe(original);
  } finally {
    tree.delete();
    query.delete();
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
function point(text: string, index: number): { row: number; column: number } {
  const lines = text.slice(0, index).split('\n');
  return { row: lines.length - 1, column: lines.at(-1)!.length };
}
function highlights(): Query {
  return new Query(
    language,
    treeSitterJson.grammars[0]!.highlights.map((file) => fs.readFileSync(path.join(root, file), 'utf8')).join('\n')
  );
}
