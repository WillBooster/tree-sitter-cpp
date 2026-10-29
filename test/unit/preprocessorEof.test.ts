import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

const directives = [
  '#define X 1',
  '#define F(x) x',
  '#include <header>',
  '#undef X',
  '#pragma once',
  '#line 17 "file"',
];

test('terminates final C++ directives without inventing physical source characters', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const directive of directives) {
      for (const ending of ['', '\\\n', '\\\r\n']) {
        const source = directive + ending;
        const tree = parser.parse(source)!;
        try {
          expect(tree.rootNode.hasError, JSON.stringify(source)).toBe(false);
          expect(tree.rootNode.text).toBe(source);
          expect(tree.rootNode.endIndex).toBe(source.length);
          expect(tree.rootNode.namedChildren).toHaveLength(1);
          expect(tree.rootNode.namedChildren[0]!.endIndex).toBe(source.length);
          if (directive === '#define X 1') {
            expect(tree.rootNode.namedChildren[0]!.childForFieldName('value')?.text).toBe('1' + ending);
          }
        } finally {
          tree.delete();
        }
      }
    }
  } finally {
    parser.delete();
  }
});

test('parses final directives with and without physical endings after a declaration', () => {
  const parser = new Parser().setLanguage(language);
  const prefix = 'int before;\n#define X 1';
  try {
    for (const ending of ['\n', '', '\\\n', '\\\r\n', '\nint after;\n']) {
      const source = prefix + ending;
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, JSON.stringify(source)).toBe(false);
        expect(tree.rootNode.text).toBe(source);
        expect(tree.rootNode.descendantsOfType('declaration').map((node) => node.text)).toEqual(
          ending.includes('int after;') ? ['int before;', 'int after;'] : ['int before;']
        );
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

test('retains errors for unfinished directive syntax at EOF', () => {
  const parser = new Parser().setLanguage(language);
  try {
    for (const source of ['#if 1\nint value;', '#define X( 1', '#include "unfinished', '#define X 1 /* unfinished']) {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(true);
        expect(tree.rootNode.endIndex).toBe(source.length);
      } finally {
        tree.delete();
      }
    }
    const empty = parser.parse('')!;
    try {
      expect(empty.rootNode.hasError).toBe(false);
      expect(empty.rootNode.endIndex).toBe(0);
    } finally {
      empty.delete();
    }
  } finally {
    parser.delete();
  }
});
