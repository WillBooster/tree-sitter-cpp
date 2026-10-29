import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser, Query } from '@willbooster/web-tree-sitter';
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

test('keeps capture roles after capture token spelling changes and removal', () => {
  const parser = new Parser().setLanguage(language);
  const start = source.indexOf('[bitand]') + 1;
  try {
    for (const replacement of ['&', 'bitand x', '']) {
      const next = source.slice(0, start) + replacement + source.slice(start + 'bitand'.length);
      const tree = parser.parse(next)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const capture = tree.rootNode.descendantsOfType('lambda_capture_specifier')[0]!;
        expect(capture.firstNamedChild?.type).toBe(
          replacement === 'bitand x' ? 'identifier' : replacement ? 'lambda_default_capture' : undefined
        );
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return capture(1) + capturePacks(2, 3); }');
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
