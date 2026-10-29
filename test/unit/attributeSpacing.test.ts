import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/spacedAttributes.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves C++ declarators and query ranges across attribute bracket spacing', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(attribute_declaration) @attribute (qualified_identifier) @qualified (reference_declarator) @reference'
  );
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('attribute_declaration')).toHaveLength(11);
    expect(
      tree.rootNode
        .descendantsOfType('reference_declarator')
        .map(
          (node) =>
            node
              .descendantsOfType(['identifier', 'field_identifier', 'type_identifier'])
              .find((child) => ['reference', 'field', 'Reference'].includes(child.text))?.text
        )
    ).toEqual(['reference', 'field', 'Reference']);
    expect(tree.rootNode.descendantsOfType('lambda_expression')).toHaveLength(1);
    const allocation = tree.rootNode.descendantsOfType('new_expression')[0]!;
    expect(allocation.childForFieldName('declarator')?.text).toBe('* [[clang::annotate_type("ptr")]] [3]');
    expect(allocation.descendantsOfType('new_declarator')[0]?.childForFieldName('length')?.text).toBe('3');
    expect(tree.rootNode.descendantsOfType('abstract_array_declarator').map((node) => node.text)).toEqual(['[20]']);
    expect(tree.rootNode.descendantsOfType('array_declarator').map((node) => node.text)).toEqual([
      'array[[maybe_unused]][3]',
    ]);
    expect(
      tree.rootNode
        .descendantsOfType('attribute_declaration')
        .filter((node) => node.childForFieldName('namespace'))
        .map((node) => node.childForFieldName('namespace')?.text)
    ).toEqual(['gnu', 'gnu', 'gnu']);
    const before = '[ /*left*/ [ maybe_unused ] /*right*/ ]';
    const compact = source.replace(before, '[[maybe_unused]]');
    for (const current of [source, compact]) {
      const parsed = parser.parse(current)!;
      try {
        expect(parsed.rootNode.hasError).toBe(false);
        for (const { node } of query.captures(parsed.rootNode))
          expect(current.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(parsed.rootNode.lastNamedChild?.text).toBe(
          'int indexed(int *values) { return values[([] { return 0; }())]; }'
        );
      } finally {
        parsed.delete();
      }
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('separates annotation brackets without losing enum and class attributes', () => {
  const parser = new Parser().setLanguage(language);
  const source = 'struct [ /*gap*/ [=1] /*gap*/ ] Holder {}; enum class [ [=2] ] Choice { value }; int after;';
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const attributes = tree.rootNode.descendantsOfType('attribute_declaration');
    expect(attributes).toHaveLength(2);
    expect(attributes.map((node) => node.parent?.type)).toEqual(['struct_specifier', 'enum_specifier']);
    for (const node of attributes)
      expect(node.children.filter((child) => !child.isNamed).map((child) => child.text)).toEqual(['[', '[', ']', ']']);
    expect(tree.rootNode.lastNamedChild?.text).toBe('int after;');
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('keeps long qualified declarator attribute lists flat with linear parse growth', () => {
  const parser = new Parser().setLanguage(language);
  const parse = (count: number): number => {
    const start = process.threadCpuUsage();
    const tree = parser.parse(
      `namespace N { extern int value; } int N::value ${'[[maybe_unused]] '.repeat(count)} = 0; int after() { return N::value; }`
    )!;
    const cpu = process.threadCpuUsage(start);
    try {
      expect(tree.rootNode.hasError).toBe(false);
      const groups = tree.rootNode.descendantsOfType('attributed_declarator');
      expect(groups).toHaveLength(1);
      expect(groups[0]!.children.filter((node) => node.type === 'attribute_declaration')).toHaveLength(count);
      expect(tree.rootNode.lastNamedChild?.text).toBe('int after() { return N::value; }');
    } finally {
      tree.delete();
    }
    return cpu.user + cpu.system;
  };
  try {
    parse(1000);
    const samples = Array.from({ length: 4 }, () => [parse(200), parse(1000)] as const);
    const small = Math.min(...samples.map(([small]) => small));
    const large = Math.min(...samples.map(([, large]) => large));
    const diagnostic = JSON.stringify({ node: process.version, samples, small, large });
    expect(large / small, diagnostic).toBeLessThan(12);
    expect(large, diagnostic).toBeLessThan(2_000_000);
  } finally {
    parser.delete();
  }
});
