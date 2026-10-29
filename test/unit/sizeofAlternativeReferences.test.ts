import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import config from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/sizeofAlternativeReferences.cpp'), 'utf8');
await Parser.init();

test('keeps alternative sizeof reference operands equivalent to primary tokens without losing macro or value operands', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const split = source.indexOf('#define SINGLE');
  const primarySource =
    source
      .slice(0, split)
      .replaceAll(/\bbitand\b/g, '&')
      .replaceAll(/\band\b/g, '&&') + source.slice(split);
  const primary = parser.parse(primarySource)!;
  const queries = configuredQueries(language);
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(primary.rootNode.hasError, primary.rootNode.toString()).toBe(false);
    expect(namedStructure(tree.rootNode)).toEqual(namedStructure(primary.rootNode));
    const operands = tree.rootNode
      .descendantsOfType('sizeof_expression')
      .filter((node) => /\b(?:bitand|and)\b/.test(node.text));
    const expectedOperands = [...source.matchAll(/(?<=static_assert\()sizeof\(.+?\)(?= ==)/g)];
    expect(expectedOperands).toHaveLength(22);
    expect(operands).toHaveLength(expectedOperands.length);
    for (const [index, operand] of operands.entries()) {
      const expected = expectedOperands[index]!;
      const start = expected.index;
      const end = start + expected[0].length;
      const type = operand.childForFieldName('type');
      expect(type?.type).toBe('type_descriptor');
      expect(type?.descendantsOfType('abstract_reference_declarator')).toHaveLength(1);
      expect(operand.childForFieldName('value')).toBeNull();
      expect([operand.startIndex, operand.endIndex, operand.startPosition, operand.endPosition, operand.text]).toEqual([
        start,
        end,
        point(source, start),
        point(source, end),
        expected[0],
      ]);
    }
    const highlights = queries[0]!.captures(tree.rootNode);
    for (const match of source.slice(0, split).matchAll(/\b(?:bitand|and)\b/g)) {
      const captures = highlights.filter(
        ({ node }) => node.startIndex === match.index && node.endIndex === match.index + match[0].length
      );
      expect(captures.filter(({ name }) => name === 'operator')).toHaveLength(1);
      expect(captures.filter(({ name }) => name === 'variable')).toHaveLength(0);
    }
    const calls = tree.rootNode
      .descendantsOfType('call_expression')
      .filter((node) => node.childForFieldName('function')?.text === 'SINGLE');
    expect(calls.map((node) => node.childForFieldName('arguments')?.firstNamedChild?.type)).toEqual([
      'identifier',
      'identifier',
    ]);
    expect(calls.map((node) => node.childForFieldName('arguments')?.firstNamedChild?.text)).toEqual(['and', 'bitand']);
    const values = tree.rootNode.descendantsOfType('sizeof_expression').filter((node) => node.text.includes('(value)'));
    expect(values.map((node) => node.childForFieldName('value')?.type)).toEqual([
      'parenthesized_expression',
      'parenthesized_expression',
    ]);
    expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
  } finally {
    for (const query of queries) query.delete();
    tree.delete();
    primary.delete();
    parser.delete();
  }
});

test('retains singleton macro calls in direct initializers and genuine sizeof value operands', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  const queries = configuredQueries(language);
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const names = ['directAnd', 'directBitand', 'localAnd', 'localBitand'];
    const initializers = tree.rootNode
      .descendantsOfType('init_declarator')
      .filter((node) => names.includes(node.childForFieldName('declarator')?.text ?? ''));
    expect(initializers.map((node) => node.childForFieldName('declarator')?.type)).toEqual(
      names.map(() => 'identifier')
    );
    expect(initializers.map((node) => node.childForFieldName('declarator')?.text)).toEqual(names);
    expect(initializers.map((node) => node.descendantsOfType('call_expression')[0]?.text)).toEqual([
      'ID(and)',
      'ID(bitand)',
      'ID(and)',
      'ID(bitand)',
    ]);
    const macroCalls = tree.rootNode
      .descendantsOfType('call_expression')
      .filter((node) => node.childForFieldName('function')?.text === 'ID');
    expect(macroCalls.map((node) => node.childForFieldName('arguments')?.firstNamedChild?.type)).toEqual(
      Array.from({ length: 6 }, () => 'identifier')
    );
    const operands = tree.rootNode
      .descendantsOfType('sizeof_expression')
      .filter((node) => ['sizeof(function())', 'sizeof(values[0])'].includes(node.text));
    expect(operands.map((node) => node.childForFieldName('value')?.type)).toEqual([
      'parenthesized_expression',
      'parenthesized_expression',
    ]);
    const tags = queries[1]!.captures(tree.rootNode);
    expect(tags.filter(({ name, node }) => name === 'name' && names.includes(node.text))).toHaveLength(0);
    expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
  } finally {
    for (const query of queries) query.delete();
    tree.delete();
    parser.delete();
  }
});

test('parses every spelling of the first sizeof reference operand', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const start = source.indexOf('bitand');
  try {
    for (const replacement of ['&', 'and']) {
      const next = source.slice(0, start) + replacement + source.slice(start + 'bitand'.length);
      const tree = parser.parse(next)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

function configuredQueries(language: ConstructorParameters<typeof Query>[0]): Query[] {
  return (['highlights', 'tags', 'injections'] as const).map((key) => {
    const files = config.grammars[0]![key];
    return new Query(
      language,
      (typeof files === 'string' ? [files] : files)
        .map((file) => fs.readFileSync(path.join(root, file), 'utf8'))
        .join('\n')
    );
  });
}
function namedStructure(node: Node): unknown {
  return [
    node.type,
    ...node.children.flatMap((child, index) =>
      child.isNamed ? [[node.fieldNameForChild(index), namedStructure(child)]] : []
    ),
  ];
}
function point(text: string, index: number): { row: number; column: number } {
  const lines = text.slice(0, index).split('\n');
  return { row: lines.length - 1, column: lines.at(-1)!.length };
}
