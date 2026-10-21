import fs from 'node:fs';
import path from 'node:path';
import { Edit, Parser, Query, type Node } from '@willbooster/web-tree-sitter';
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
    expect(operands).toHaveLength(16);
    for (const operand of operands) {
      const type = operand.childForFieldName('type');
      expect(type?.type).toBe('type_descriptor');
      expect(type?.descendantsOfType('abstract_reference_declarator')).toHaveLength(1);
      expect(operand.childForFieldName('value')).toBeNull();
      expect(source.slice(operand.startIndex, operand.endIndex)).toBe(operand.text);
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

test('preserves fresh and incremental sizeof metadata and all configured captures through reference spelling edits and undo', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const queries = configuredQueries(language);
  const start = source.indexOf('bitand');
  let current = source;
  let spelling = 'bitand';
  let tree = parser.parse(current)!;
  try {
    for (const replacement of ['&', 'and', 'bitand']) {
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
        for (const query of queries) {
          const captures = (node: Node): unknown =>
            query.captures(node).map(({ name, node }) => [name, metadata(node)]);
          expect(captures(tree.rootNode)).toEqual(captures(fresh.rootNode));
        }
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
      } finally {
        fresh.delete();
      }
      current = next;
      spelling = replacement;
    }
    expect(current).toBe(source);
  } finally {
    for (const query of queries) query.delete();
    tree.delete();
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
