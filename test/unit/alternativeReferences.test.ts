import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser, Query } from '@willbooster/web-tree-sitter';
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

test('parses a reference token after spelling changes and removal', () => {
  const parser = new Parser().setLanguage(language);
  const start = source.indexOf('bitand left');
  try {
    for (const replacement of ['&', 'and', '']) {
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

function namedStructure(node: Node): unknown {
  return [
    node.type,
    ...node.children.flatMap((child, index) =>
      child.isNamed ? [[node.fieldNameForChild(index), namedStructure(child)]] : []
    ),
  ];
}

function highlights(): Query {
  return new Query(
    language,
    treeSitterJson.grammars[0]!.highlights.map((file) => fs.readFileSync(path.join(root, file), 'utf8')).join('\n')
  );
}
