import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/computedIncludes.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('retains computed include paths and enclosing C++ initializers across path spellings', () => {
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(preproc_include path: (_) @path) @include (function_definition) @function (declaration declarator: (identifier) @declaration) (declaration declarator: (init_declarator declarator: (identifier) @declaration))'
  );
  const original = 'HEADER EMPTY';
  const start = source.indexOf(original, source.indexOf('#include'));
  try {
    const tree = parser.parse(source)!;
    try {
      expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
      const includes = tree.rootNode.descendantsOfType('preproc_include');
      expect(includes.map((node) => node.childForFieldName('path')?.text)).toEqual([
        '"empty.h"',
        'HEADER',
        'HEADER_OF(empty)',
        'HEADER EMPTY',
        'EMPTY HEADER',
        'HEADER_OF(empty) EMPTY',
        'HEADER_PATH(empty.h) EMPTY',
        'HEADER EMPTY',
        'HEADER _Pragma("GCC diagnostic push") EMPTY',
        'HEADER EMPTY',
        'HEADER_PATH(empty.h) EMPTY',
        'HEADER_OF(empty) EMPTY',
      ]);
      expect(includes.slice(0, 3).map((node) => node.childForFieldName('path')?.type)).toEqual([
        'string_literal',
        'identifier',
        'call_expression',
      ]);
      expect(
        includes.slice(3).every((node) => node.childForFieldName('path')?.type === 'preproc_include_argument')
      ).toBe(true);
      expect(tree.rootNode.descendantsOfType('initializer_list').map((node) => node.firstNamedChild?.type)).toEqual([
        'preproc_include',
        'preproc_include',
      ]);
      const calls = tree.rootNode.descendantsOfType('call_expression');
      expect(
        calls.find((node) => node.childForFieldName('function')?.text === 'take')?.childForFieldName('arguments')
          ?.firstNamedChild?.type
      ).toBe('initializer_list');
      expect(
        query
          .captures(tree.rootNode)
          .filter(({ name }) => name === 'declaration')
          .map(({ node }) => node.text)
      ).toEqual(['local', 'after']);
    } finally {
      tree.delete();
    }
    for (const replacement of [original, 'HEADER /* gap */ EMPTY', 'HEADER \\\n EMPTY']) {
      const nextSource = source.slice(0, start) + replacement + source.slice(start + original.length);
      const next = parser.parse(nextSource)!;
      try {
        expect(next.rootNode.hasError).toBe(false);
        expect(
          query.captures(next.rootNode).find(({ name, node }) => name === 'path' && node.startIndex === start)?.node
            .text
        ).toBe(replacement);
        for (const { node } of query.captures(next.rootNode))
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(next.rootNode.lastNamedChild?.text).toBe('int following() { return 1; }');
      } finally {
        next.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

test('preserves the existing statement-block macro argument extension', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`void block_control() {
  BODY({
#include HEADER_PATH(empty.h) EMPTY
    int local = 0; use(local);
  });
  int after;
}
int following() { return 1; }
`)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const body = tree.rootNode
      .descendantsOfType('call_expression')
      .find((node) => node.childForFieldName('function')?.text === 'BODY');
    expect(body?.childForFieldName('arguments')?.firstNamedChild?.type).toBe('compound_statement');
    expect(body?.descendantsOfType('preproc_include')[0]?.childForFieldName('path')?.text).toBe(
      'HEADER_PATH(empty.h) EMPTY'
    );
    expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 1; }');
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('preserves raw strings and following initializer elements across raw-token spellings', () => {
  const source = fs.readFileSync(path.join(root, 'test/fixtures/rawIncludes.cpp'), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    '(preproc_include path: (preproc_include_argument (raw_string_literal) @raw) @path)'
  );
  const original = 'R"(a"b)"';
  const multiline = 'R"tag(a"b\nc)tag"';
  const start = source.indexOf(original);
  try {
    for (const replacement of [original, multiline]) {
      const nextSource = source.slice(0, start) + replacement + source.slice(start + original.length);
      const tree = parser.parse(nextSource)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(
          tree.rootNode
            .descendantsOfType('initializer_list')
            .map((node) =>
              node.namedChildren.filter((child) => child.type === 'number_literal').map((child) => child.text)
            )
        ).toEqual([['1'], ['2']]);
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'raw')
            .map(({ node }) => node.text)
        ).toEqual([replacement, multiline]);
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'path')
            .map(({ node }) => node.text)
        ).toEqual([`H(/*inside*/ ${replacement}) EMPTY`, `H(${multiline}) EMPTY`]);
        for (const { node } of query.captures(tree.rootNode))
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 3; }');
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});
