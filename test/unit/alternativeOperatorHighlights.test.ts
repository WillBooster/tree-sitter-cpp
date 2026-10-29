import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import treeSitterJson from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'test/fixtures/alternativeOperators.cpp'), 'utf8');
await Parser.init();
const language = await loadCurrentWasmBuild();

test('highlights alternative operators in expressions, overloads and macro expressions', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    const operators = [
      ...tree.rootNode
        .descendantsOfType(['binary_expression', 'unary_expression', 'assignment_expression'])
        .map((node) => node.childForFieldName('operator')!),
      ...tree.rootNode.descendantsOfType('operator_name').map((node) => node.lastChild!),
    ];
    expect(operators).toHaveLength(33);
    const captures = query.captures(tree.rootNode);
    for (const operator of operators) {
      expect(
        captures.filter(({ name, node }) => name === 'operator' && node.id === operator.id),
        operator.text
      ).toHaveLength(1);
    }
    const protectedNodes = tree.rootNode.descendantsOfType([
      'preproc_arg',
      'string_literal',
      'raw_string_literal',
      'comment',
    ]);
    expect(protectedNodes).toHaveLength(4);
    for (const protectedNode of protectedNodes) {
      expect(
        captures.filter(
          ({ name, node }) =>
            name === 'operator' &&
            node.startIndex >= protectedNode.startIndex &&
            node.endIndex <= protectedNode.endIndex
        )
      ).toHaveLength(0);
    }
    for (const name of ['anderson', 'notable', 'orchid']) {
      expect(captures.filter(({ name: capture, node }) => capture === 'variable' && node.text === name)).toHaveLength(
        1
      );
      expect(captures.filter(({ name: capture, node }) => capture === 'operator' && node.text === name)).toHaveLength(
        0
      );
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('captures each spelling of a word operator', () => {
  const parser = new Parser().setLanguage(language);
  const query = highlights();
  const prefix = 'bool changed(bool left, bool right) { return left ';
  const suffix = ' right; }\n';
  try {
    for (const spelling of ['&&', '', 'or', 'and']) {
      const tree = parser.parse(prefix + spelling + suffix)!;
      try {
        expect(tree.rootNode.hasError).toBe(spelling === '');
        const captures = query.captures(tree.rootNode).map(({ name, node }) => [name, node.text]);
        if (spelling)
          expect(captures.filter(([name, text]) => name === 'operator' && text === spelling)).toHaveLength(1);
      } finally {
        tree.delete();
      }
    }
  } finally {
    query.delete();
    parser.delete();
  }
});

function highlights(): Query {
  return new Query(
    language,
    treeSitterJson.grammars[0]!.highlights.map((file) => fs.readFileSync(path.join(root, file), 'utf8')).join('\n')
  );
}
