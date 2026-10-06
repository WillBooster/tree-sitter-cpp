import fs from 'node:fs';
import path from 'node:path';

import { expect, test } from 'vitest';

import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';

import treeSitterJson from '../../tree-sitter.json';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.join(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/definitionDirectives.cpp'), 'utf8');
await Parser.init();

test('highlights definition alternatives without capturing identifier or literal spellings', async () => {
  const parser = new Parser();
  const queries: Query[] = [];
  let tree: Tree | undefined;
  try {
    const language = await loadCurrentWasmBuild();
    parser.setLanguage(language);
    const grammar = treeSitterJson.grammars[0]!;
    for (const kind of ['highlights', 'locals', 'tags', 'injections'] as const) {
      const files = [(grammar as Partial<Record<typeof kind, string | string[]>>)[kind] ?? []].flat();
      queries.push(new Query(language, files.map((file) => fs.readFileSync(path.join(Root, file), 'utf8')).join('\n')));
    }
    tree = parser.parse(Source)!;
    expect(tree.rootNode.hasError).toBe(false);
    const arms = tree.rootNode.descendantsOfType('preproc_elifdef');
    expect(
      arms.map((node) => [
        node.parent?.type,
        node.childForFieldName('name')?.type,
        node.childForFieldName('name')?.text,
        node.childForFieldName('alternative')?.type,
      ])
    ).toEqual([
      ['preproc_if', 'identifier', 'FEATURE', 'preproc_elifdef'],
      ['preproc_elifdef', 'identifier', 'FEATURE', 'preproc_else'],
    ]);
    const highlights = queries[0]!;
    expect(
      highlights
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'keyword')
        .map(({ node }) => node.text)
    ).toEqual(['#if', '#  elifdef', '#\telifndef', '#else', '#endif', 'const', 'return']);
    for (const [index, spelling] of ['#  elifdef', '#\telifndef'].entries()) {
      const capture = highlights
        .captures(tree.rootNode)
        .find(({ name, node }) => name === 'keyword' && node.text === spelling)!;
      const start = Source.indexOf(spelling);
      expect(capture.node.type).toBe(index === 0 ? '#elifdef' : '#elifndef');
      expect(capture.node.isNamed).toBe(false);
      expect(capture.node.parent?.id).toBe(arms[index]!.id);
      expect([capture.node.startIndex, capture.node.endIndex]).toEqual([start, start + spelling.length]);
      expect([capture.node.startPosition, capture.node.endPosition]).toEqual([
        point(Source, start),
        point(Source, start + spelling.length),
      ]);
    }
    expect(
      highlights
        .captures(tree.rootNode)
        .filter(({ name, node }) => name === 'variable' && ['elifdef', 'elifndef'].includes(node.text))
        .map(({ node }) => node.text)
    ).toEqual(['elifdef', 'elifndef', 'elifdef', 'elifndef']);
    expect(
      highlights
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'string')
        .map(({ node }) => node.text)
    ).toContain('"#elifdef #elifndef"');
    expect(
      highlights
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'comment')
        .map(({ node }) => node.text)
    ).toContain('// #elifdef #elifndef remain comment text.');

    let source = Source;
    for (const [before, after, firstType] of [
      ['#  elifdef', '#elifndef', '#elifndef'],
      ['#elifndef FEATURE', '#  elifdef FEATURE', '#elifdef'],
    ] as const) {
      const start = source.indexOf(before);
      expect(start).toBeGreaterThanOrEqual(0);
      const next = source.slice(0, start) + after + source.slice(start + before.length);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + before.length,
          newEndIndex: start + after.length,
          startPosition: point(source, start),
          oldEndPosition: point(source, start + before.length),
          newEndPosition: point(next, start + after.length),
        })
      );
      let incremental: Tree | undefined;
      let fresh: Tree | undefined;
      try {
        incremental = parser.parse(next, tree)!;
        fresh = parser.parse(next)!;
        expect(incremental.rootNode.hasError).toBe(false);
        expect(snapshot(incremental.rootNode)).toEqual(snapshot(fresh.rootNode));
        for (const query of queries) {
          expect(query.captures(incremental.rootNode).map(({ name, node }) => [name, snapshot(node)])).toEqual(
            query.captures(fresh.rootNode).map(({ name, node }) => [name, snapshot(node)])
          );
        }
        expect(
          highlights
            .captures(incremental.rootNode)
            .filter(({ name, node }) => name === 'keyword' && ['#elifdef', '#elifndef'].includes(node.type))
            .map(({ node }) => node.type)
        ).toEqual([firstType, '#elifndef']);
        tree.delete();
        tree = incremental;
        incremental = undefined;
        source = next;
      } finally {
        incremental?.delete();
        fresh?.delete();
      }
    }
    expect(source).toBe(Source);
  } finally {
    tree?.delete();
    for (const query of queries) query.delete();
    parser.delete();
  }
});

function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}

function snapshot(node: Node): unknown {
  return {
    type: node.type,
    named: node.isNamed,
    extra: node.isExtra,
    missing: node.isMissing,
    error: node.hasError,
    start: node.startIndex,
    end: node.endIndex,
    startPosition: node.startPosition,
    endPosition: node.endPosition,
    children: node.children.map((child, index) => [node.fieldNameForChild(index), snapshot(child)]),
  };
}
