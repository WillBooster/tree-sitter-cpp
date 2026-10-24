import fs from 'node:fs';
import path from 'node:path';

import { Edit, type Node, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
const source = `int evaluate(int TRUE, int FALSE) {
  TRUE = FALSE;
  return TRUE ? FALSE : int(true && !false);
}
int following() { return 42; }
`;
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves uppercase boolean variable reads and writes through literal edits', () => {
  const parser = new Parser().setLanguage(language);
  const highlights = new Query(
    language,
    ['queries/c/highlights.scm', 'queries/highlights.scm']
      .map((file) => fs.readFileSync(path.join(root, file), 'utf8'))
      .join('\n')
  );
  const references = new Query(
    language,
    '(assignment_expression left: (identifier) @write right: (identifier) @read) ' +
      '(conditional_expression condition: [(identifier) (true)] @condition consequence: (identifier) @read)'
  );
  let tree = parser.parse(source)!;
  const start = source.indexOf('TRUE ?');
  let spelling = 'TRUE';
  try {
    for (const replacement of ['TRUE', 'true', 'TRUE']) {
      const nextSource = source.slice(0, start) + replacement + source.slice(start + 4);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + spelling.length,
          newEndIndex: start + replacement.length,
          startPosition: { row: 2, column: 9 },
          oldEndPosition: { row: 2, column: 9 + spelling.length },
          newEndPosition: { row: 2, column: 9 + replacement.length },
        })
      );
      const next = parser.parse(nextSource, tree)!;
      tree.delete();
      tree = next;
      spelling = replacement;
      const fresh = parser.parse(nextSource)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        expect(metadata(tree.rootNode)).toEqual(metadata(fresh.rootNode));
        for (const query of [references, highlights]) {
          const captures = (current: typeof tree): unknown[] =>
            query
              .captures(current.rootNode)
              .map(({ name, node }) => [
                name,
                node.type,
                node.text,
                node.startIndex,
                node.endIndex,
                node.startPosition,
                node.endPosition,
              ]);
          expect(captures(tree)).toEqual(captures(fresh));
        }
        const captures = references.captures(tree.rootNode);
        expect(captures.map(({ name, node }) => [name, node.type, node.text])).toEqual([
          ['write', 'identifier', 'TRUE'],
          ['read', 'identifier', 'FALSE'],
          ['condition', replacement === 'TRUE' ? 'identifier' : 'true', replacement],
          ['read', 'identifier', 'FALSE'],
        ]);
        const highlighted = highlights.captures(tree.rootNode);
        for (const { node } of captures) {
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
          if (node.type === 'identifier') {
            expect(highlighted.filter((c) => c.name === 'variable' && c.node.id === node.id)).toHaveLength(1);
          }
        }
        expect(tree.rootNode.descendantsOfType('true').map((node) => node.text)).toEqual(
          replacement === 'true' ? ['true', 'true'] : ['true']
        );
        expect(tree.rootNode.descendantsOfType('false').map((node) => node.text)).toEqual(['false']);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 42; }');
      } finally {
        fresh.delete();
      }
    }
  } finally {
    tree.delete();
    references.delete();
    highlights.delete();
    parser.delete();
  }
});

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
