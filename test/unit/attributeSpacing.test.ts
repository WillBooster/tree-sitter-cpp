import fs from 'node:fs';
import path from 'node:path';
import { expect, test } from 'vitest';
import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/spacedAttributes.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves C++ declarators and query ranges through attribute bracket edits', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(attribute_declaration) @attribute (qualified_identifier) @qualified (reference_declarator) @reference'
  );
  let tree = parser.parse(source)!;
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
    const start = source.indexOf(before);
    let current = source;
    let spelling = before;
    for (const replacement of ['[[maybe_unused]]', before]) {
      const nextSource = current.slice(0, start) + replacement + current.slice(start + spelling.length);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + spelling.length,
          newEndIndex: start + replacement.length,
          startPosition: { row: 1, column: 13 },
          oldEndPosition: { row: 1, column: 13 + spelling.length },
          newEndPosition: { row: 1, column: 13 + replacement.length },
        })
      );
      const next = parser.parse(nextSource, tree)!;
      tree.delete();
      tree = next;
      const fresh = parser.parse(nextSource)!;
      try {
        expect(tree.rootNode.hasError).toBe(false);
        expect(metadata(tree.rootNode)).toEqual(metadata(fresh.rootNode));
        expect(captures(query, tree)).toEqual(captures(query, fresh));
        for (const { node } of query.captures(tree.rootNode))
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(tree.rootNode.lastNamedChild?.text).toBe(
          'int indexed(int *values) { return values[([] { return 0; }())]; }'
        );
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

function captures(query: Query, tree: Tree): unknown[] {
  return query.captures(tree.rootNode).map(({ name, node }) => [name, metadata(node)]);
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
    let small = Infinity,
      large = Infinity;
    for (let run = 0; run < 4; run++) {
      small = Math.min(small, parse(200));
      large = Math.min(large, parse(1000));
    }
    expect(large / small).toBeLessThan(12);
    expect(large).toBeLessThan(2_000_000);
  } finally {
    parser.delete();
  }
});
