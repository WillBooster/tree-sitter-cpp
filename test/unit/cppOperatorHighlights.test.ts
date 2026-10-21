import fs from 'node:fs';
import path from 'node:path';
import { expect, test } from 'vitest';
import { Parser, Query, type Node } from '@willbooster/web-tree-sitter';

import treeSitterJson from '../../tree-sitter.json';
import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();

test('highlights C++ operators in expressions, overloads and macro arguments without capturing raw text', async () => {
  const root = path.resolve(import.meta.dirname, '../..');
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    treeSitterJson.grammars[0]!.highlights.map((file) => fs.readFileSync(path.join(root, file), 'utf8')).join('\n')
  );
  const tree = parser.parse(fs.readFileSync(path.join(root, 'test/fixtures/cppOperators.cpp'), 'utf8'))!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const expressions = tree.rootNode.descendantsOfType('binary_expression');
    const overloads = tree.rootNode.descendantsOfType('operator_name');
    const arguments_ = tree.rootNode.descendantsOfType('operator_argument');
    expect([expressions.length, overloads.length, arguments_.length]).toEqual([4, 1, 3]);
    const operators: Node[] = [
      ...expressions.map((node) => node.childForFieldName('operator')!),
      ...overloads.map((node) => node.lastChild!),
      ...arguments_.map((node) => node.firstChild!),
    ];
    const captures = query.captures(tree.rootNode);
    for (const operator of operators) {
      expect(captures.filter(({ name, node }) => name === 'operator' && node.id === operator.id)).toHaveLength(1);
    }
    const protectedNodes = tree.rootNode.descendantsOfType(['preproc_arg', 'string_literal', 'comment']);
    expect(protectedNodes).toHaveLength(3);
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
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});
