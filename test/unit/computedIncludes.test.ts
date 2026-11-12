import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import {
  type AstCapture,
  captureNodes,
  childrenOfType,
  collectCaptures,
  fieldChild,
  fieldChildren,
} from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/computedIncludes.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('retains computed include paths and enclosing C++ initializers across path spellings', () => {
  const parser = new Parser().setLanguage(language);
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
        captureNodes(includeAndDeclarationCaptures(tree.rootNode), 'declaration').map((node) => node.text)
      ).toEqual(['local', 'after']);
    } finally {
      tree.delete();
    }
    for (const replacement of [original, 'HEADER /* gap */ EMPTY', 'HEADER \\\n EMPTY']) {
      const nextSource = source.slice(0, start) + replacement + source.slice(start + original.length);
      const next = parser.parse(nextSource)!;
      try {
        expect(next.rootNode.hasError).toBe(false);
        const captures = includeAndDeclarationCaptures(next.rootNode);
        expect(captures.find(({ name, node }) => name === 'path' && node.startIndex === start)?.node.text).toBe(
          replacement
        );
        for (const { node } of captures) expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(next.rootNode.lastNamedChild?.text).toBe('int following() { return 1; }');
      } finally {
        next.delete();
      }
    }
  } finally {
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
  const rawSource = fs.readFileSync(path.join(root, 'test/fixtures/rawIncludes.cpp'), 'utf8');
  const parser = new Parser().setLanguage(language);
  const original = 'R"(a"b)"';
  const multiline = 'R"tag(a"b\nc)tag"';
  const start = rawSource.indexOf(original);
  try {
    for (const replacement of [original, multiline]) {
      const nextSource = rawSource.slice(0, start) + replacement + rawSource.slice(start + original.length);
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
        const captures = collectCaptures(tree.rootNode, (node, emit) => {
          if (node.type !== 'preproc_include') return;
          const pathNode = fieldChild(node, 'path', 'preproc_include_argument');
          for (const raw of childrenOfType(pathNode, 'raw_string_literal')) {
            emit('path', pathNode);
            emit('raw', raw);
          }
        });
        expect(captureNodes(captures, 'raw').map((node) => node.text)).toEqual([replacement, multiline]);
        expect(captureNodes(captures, 'path').map((node) => node.text)).toEqual([
          `H(/*inside*/ ${replacement}) EMPTY`,
          `H(${multiline}) EMPTY`,
        ]);
        for (const { node } of captures) expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 3; }');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

function includeAndDeclarationCaptures(rootNode: Node): AstCapture[] {
  return collectCaptures(rootNode, (node, emit) => {
    if (node.type === 'preproc_include') {
      const pathNode = fieldChild(node, 'path');
      if (pathNode) {
        emit('include', node);
        emit('path', pathNode);
      }
    } else if (node.type === 'function_definition') {
      emit('function', node);
    } else if (node.type === 'declaration') {
      for (const declarator of fieldChildren(node, 'declarator')) {
        if (declarator.type === 'identifier') emit('declaration', declarator);
        if (declarator.type === 'init_declarator')
          emit('declaration', fieldChild(declarator, 'declarator', 'identifier'));
      }
    }
  });
}
