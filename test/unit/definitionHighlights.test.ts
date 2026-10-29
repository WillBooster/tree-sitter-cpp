import fs from 'node:fs';
import path from 'node:path';

import { type Language, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { afterAll, beforeAll, expect, test } from 'vitest';

import treeSitterJson from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.join(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/definitionDirectives.cpp'), 'utf8');
await Parser.init();

let language: Language;
const queries: Query[] = [];
beforeAll(async () => {
  language = await loadCurrentWasmBuild();
  const grammar = treeSitterJson.grammars[0]!;
  for (const kind of ['highlights', 'locals', 'tags', 'injections'] as const) {
    const files = [(grammar as Partial<Record<typeof kind, string | string[]>>)[kind] ?? []].flat();
    queries.push(new Query(language, files.map((file) => fs.readFileSync(path.join(Root, file), 'utf8')).join('\n')));
  }
}, 30_000);
afterAll(() => {
  for (const query of queries) query.delete();
});

test('highlights definition alternatives without capturing identifier or literal spellings', () => {
  const parser = new Parser();
  let tree: Tree | undefined;
  try {
    parser.setLanguage(language);
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

    const start = Source.indexOf('#  elifdef');
    const respelled = parser.parse(Source.slice(0, start) + '#elifndef' + Source.slice(start + '#  elifdef'.length))!;
    try {
      expect(respelled.rootNode.hasError).toBe(false);
      expect(
        highlights
          .captures(respelled.rootNode)
          .filter(({ name, node }) => name === 'keyword' && ['#elifdef', '#elifndef'].includes(node.type))
          .map(({ node }) => node.type)
      ).toEqual(['#elifndef', '#elifndef']);
    } finally {
      respelled.delete();
    }
  } finally {
    tree?.delete();
    parser.delete();
  }
});

function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}
