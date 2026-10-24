import fs from 'node:fs';
import path from 'node:path';

import { Edit, type Node, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import treeSitterJson from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/alternativeLambdaCaptures.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves default, named, initialized and pack reference captures with bitand tokens', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  const tree = parser.parse(source)!;
  const primary = parser.parse(source.replaceAll(/\bbitand\b/g, '&'))!;
  const control = parser.parse(
    'int control() { int bitanderson = 1; auto get = [bitanderson] { return bitanderson; }; const char* text = R"([bitand x])"; return get(); }'
  )!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(primary.rootNode.hasError, primary.rootNode.toString()).toBe(false);
    expect(namedStructure(tree.rootNode)).toEqual(namedStructure(primary.rootNode));
    expect(tree.rootNode.descendantsOfType('lambda_default_capture').map((node) => node.text)).toEqual([
      'bitand',
      'bitand',
      '=',
    ]);
    expect(
      tree.rootNode
        .descendantsOfType('lambda_capture_initializer')
        .map((node) => [node.childForFieldName('left')?.text, node.childForFieldName('right')?.text])
    ).toEqual([
      ['alias', 'x'],
      ['refs', 'values'],
    ]);
    const captures = query.captures(tree.rootNode);
    const tokens = [...source.matchAll(/\bbitand\b/g)];
    expect(tokens).toHaveLength(7);
    for (const match of tokens) {
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
    expect(control.rootNode.hasError, control.rootNode.toString()).toBe(false);
    const protectedCaptures = query.captures(control.rootNode);
    expect(
      protectedCaptures.filter(({ name, node }) => name === 'operator' && node.text.includes('bitand'))
    ).toHaveLength(0);
    expect(
      protectedCaptures.filter(({ name, node }) => name === 'variable' && node.text === 'bitanderson')
    ).toHaveLength(3);
  } finally {
    tree.delete();
    primary.delete();
    control.delete();
    query.delete();
    parser.delete();
  }
});

test('keeps capture roles, physical ranges and queries coherent through incremental edits and undo', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  let current = source;
  let spelling = 'bitand';
  const start = source.indexOf('[bitand]') + 1;
  let tree = parser.parse(current)!;
  try {
    for (const replacement of ['&', 'bitand x', '', 'bitand']) {
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
        const capture = tree.rootNode.descendantsOfType('lambda_capture_specifier')[0]!;
        expect(capture.firstNamedChild?.type).toBe(
          replacement === 'bitand x' ? 'identifier' : replacement ? 'lambda_default_capture' : undefined
        );
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return capture(1) + capturePacks(2, 3); }');
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
