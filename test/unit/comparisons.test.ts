import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';
import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('keeps greater-equal intact beside possible template names', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`bool compare(int a, int b, int c) {
  if (a < b >= c) return true;
  a < b >= c;
  return a < b >= c;
}`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(tree.rootNode.descendantsOfType('assignment_expression')).toHaveLength(0);
    expect(tree.rootNode.descendantsOfType('template_function')).toHaveLength(0);
    expect(
      tree.rootNode
        .descendantsOfType('binary_expression')
        .filter((node) => node.childForFieldName('operator')?.text === '>=')
        .map((node) => node.childForFieldName('left')?.text)
    ).toEqual(['a < b', 'a < b', 'a < b']);
  } finally {
    tree.delete();
    parser.delete();
  }
});
