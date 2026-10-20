import { Edit, Parser } from '@willbooster/web-tree-sitter';
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

test('preserves split template closers before comparison tokens during edits', () => {
  const parser = new Parser().setLanguage(language);
  const prefix = 'template<class> struct Box {}; template<class> constexpr int value = 2; bool f() { return ';
  let expression = 'value<Box<int>>>=1';
  let tree = parser.parse(prefix + expression + '; }')!;
  try {
    for (const nextExpression of [
      'value<Box<int>>>=2',
      'value<Box<int> > >=1',
      'value<Box<int>>>=1',
      'value<int>>1',
      'value<Box<int>>>=1',
    ]) {
      let unchangedPrefix = 0;
      while (
        unchangedPrefix < Math.min(expression.length, nextExpression.length) &&
        expression[unchangedPrefix] === nextExpression[unchangedPrefix]
      )
        unchangedPrefix += 1;
      const startIndex = prefix.length + unchangedPrefix;
      tree.edit(
        new Edit({
          startIndex,
          oldEndIndex: prefix.length + expression.length,
          newEndIndex: prefix.length + nextExpression.length,
          startPosition: { row: 0, column: startIndex },
          oldEndPosition: { row: 0, column: prefix.length + expression.length },
          newEndPosition: { row: 0, column: prefix.length + nextExpression.length },
        })
      );
      const source = prefix + nextExpression + '; }';
      const incremental = parser.parse(source, tree)!;
      tree.delete();
      tree = incremental;
      expression = nextExpression;
      const fresh = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(false);
        expect(tree.rootNode.toString()).toBe(fresh.rootNode.toString());
        expect(tree.rootNode.descendantsOfType('assignment_expression')).toHaveLength(0);
        const comparison = tree.rootNode.descendantsOfType('return_statement')[0]!.namedChildren[0]!;
        expect(comparison.type).toBe('binary_expression');
        expect(comparison.childForFieldName('left')?.type).toBe('template_function');
        expect(comparison.childForFieldName('left')?.endIndex).toBe(
          prefix.length + expression.slice(0, expression.lastIndexOf('>')).trimEnd().length
        );
      } finally {
        fresh.delete();
      }
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

test('preserves pragma extras beside template closers and expression operators', () => {
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(`template<class> struct Box {};
Box<int _Pragma(/*spacing*/ "GCC diagnostic push")> box;
bool compare(int a, int b, int c) {
  return a _Pragma(/*spacing*/ "GCC diagnostic push") + b _Pragma(/*spacing*/ "GCC diagnostic push") > c;
}
int shift(int a, int b) { return a >> b; }`)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('pragma_operator')).toHaveLength(3);
    const returns = tree.rootNode.descendantsOfType('return_statement');
    expect(returns.map((node) => node.namedChildren[0]?.childForFieldName('operator')?.text)).toEqual(['>', '>>']);
    expect(
      tree.rootNode.descendantsOfType('template_type').map((node) => node.childForFieldName('name')?.text)
    ).toEqual(['Box']);
  } finally {
    tree.delete();
    parser.delete();
  }
});
