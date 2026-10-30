import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/alternativeLambdaCaptures.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves default, named, initialized and pack reference captures with bitand tokens', () => {
  const parser = new Parser().setLanguage(language);
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
    expect(control.rootNode.hasError, control.rootNode.toString()).toBe(false);
  } finally {
    tree.delete();
    primary.delete();
    control.delete();
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
