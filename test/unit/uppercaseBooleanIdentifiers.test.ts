import { type Node, Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { type AstCapture, collectCaptures, fieldChild } from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const source = `int evaluate(int TRUE, int FALSE) {
  TRUE = FALSE;
  return TRUE ? FALSE : int(true && !false);
}
int following() { return 42; }
`;
await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves uppercase boolean variable reads and writes beside boolean literals', () => {
  const parser = new Parser().setLanguage(language);
  const start = source.indexOf('TRUE ?');
  try {
    for (const replacement of ['TRUE', 'true']) {
      const nextSource = source.slice(0, start) + replacement + source.slice(start + 4);
      const tree = parser.parse(nextSource)!;
      try {
        expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
        const captures = booleanReferenceCaptures(tree.rootNode);
        expect(captures.map(({ name, node }) => [name, node.type, node.text])).toEqual([
          ['write', 'identifier', 'TRUE'],
          ['read', 'identifier', 'FALSE'],
          ['condition', replacement === 'TRUE' ? 'identifier' : 'true', replacement],
          ['read', 'identifier', 'FALSE'],
        ]);
        for (const { node } of captures) {
          expect(nextSource.slice(node.startIndex, node.endIndex)).toBe(node.text);
        }
        expect(tree.rootNode.descendantsOfType('true').map((node) => node.text)).toEqual(
          replacement === 'true' ? ['true', 'true'] : ['true']
        );
        expect(tree.rootNode.descendantsOfType('false').map((node) => node.text)).toEqual(['false']);
        expect(tree.rootNode.lastNamedChild?.text).toBe('int following() { return 42; }');
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});

function booleanReferenceCaptures(rootNode: Node): AstCapture[] {
  return collectCaptures(rootNode, (node, emit) => {
    if (node.type === 'assignment_expression') {
      const left = fieldChild(node, 'left', 'identifier');
      const right = fieldChild(node, 'right', 'identifier');
      if (left && right) {
        emit('write', left);
        emit('read', right);
      }
    }
    if (node.type === 'conditional_expression') {
      const condition = fieldChild(node, 'condition', ['identifier', 'true']);
      const consequence = fieldChild(node, 'consequence', 'identifier');
      if (condition && consequence) {
        emit('condition', condition);
        emit('read', consequence);
      }
    }
  });
}
