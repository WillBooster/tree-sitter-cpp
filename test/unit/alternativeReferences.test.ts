import fs from 'node:fs';
import path from 'node:path';

import { Edit, type Node, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import treeSitterJson from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/alternativeReferences.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('retains reference declarators, qualifiers and operator highlights with alternative spellings', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  const tree = parser.parse(source)!;
  const primary = parser.parse(source.replaceAll(/\bbitand\b/g, '&').replaceAll(/\band\b/g, '&&'))!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(primary.rootNode.hasError, primary.rootNode.toString()).toBe(false);
    expect(namedStructure(tree.rootNode)).toEqual(namedStructure(primary.rootNode));
    const captures = query.captures(tree.rootNode);
    for (const match of source.matchAll(/\b(?:bitand|and)\b/g)) {
      const atToken = captures.filter(
        ({ node }) => node.startIndex === match.index && node.endIndex === match.index + match[0].length
      );
      expect(
        atToken.filter(({ name }) => name === 'operator'),
        match[0]
      ).toHaveLength(1);
      expect(
        atToken.filter(({ name }) => name === 'variable'),
        match[0]
      ).toHaveLength(0);
    }
    expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
  } finally {
    tree.delete();
    primary.delete();
    query.delete();
    parser.delete();
  }
});

test('matches fresh ranges and captures while a reference token changes spelling and is removed', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  let current = source;
  let spelling = 'bitand';
  const start = source.indexOf('bitand left');
  let tree = parser.parse(current)!;
  try {
    for (const replacement of ['&', 'and', '', 'bitand']) {
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
        const captures = (root: Node): unknown => query.captures(root).map(({ name, node }) => [name, metadata(node)]);
        expect(captures(tree.rootNode)).toEqual(captures(fresh.rootNode));
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 0; }');
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
