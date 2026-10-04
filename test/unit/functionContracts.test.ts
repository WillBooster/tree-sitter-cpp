import { Language, Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

test('exposes contract predicates as canonical expressions and postcondition result bindings', async () => {
  await Parser.init();
  const language = await Language.load('tree-sitter-cpp.wasm');
  const parser = new Parser();
  parser.setLanguage(language);
  const query = new Query(
    language,
    '(contract_specifier condition: (expression) @predicate) (contract_specifier result: (identifier) @result) (expression/binary_expression) @binary'
  );
  const source = `int f(const int x)
    pre [[maybe_unused]] (x > 0)
    post [[maybe_unused]] (result [[maybe_unused]]: result > 0)
    post(true) { return x; }
int following() { return 1; }`;
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const captures = query.captures(tree.rootNode);
    expect(captures.filter(({ name }) => name === 'predicate').map(({ node }) => node.text)).toEqual([
      'x > 0',
      'result > 0',
      'true',
    ]);
    expect(captures.filter(({ name }) => name === 'result').map(({ node }) => node.text)).toEqual(['result']);
    expect(captures.filter(({ name }) => name === 'binary').map(({ node }) => node.text)).toEqual([
      'x > 0',
      'result > 0',
    ]);
    expect(tree.rootNode.namedChildren.map((node) => node.type)).toEqual([
      'function_definition',
      'function_definition',
    ]);
    expect(tree.rootNode.namedChildren.at(-1)?.childForFieldName('declarator')?.text).toBe('following()');
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('rejects malformed result bindings, empty predicates and misplaced contract suffixes', async () => {
  await Parser.init();
  const language = await Language.load('tree-sitter-cpp.wasm');
  const parser = new Parser();
  parser.setLanguage(language);
  try {
    for (const source of [
      'int f(int x) pre(result: result > 0) { return x; }',
      'int f() pre() { return 1; }',
      'int f() post(result:) { return 1; }',
      'int f() post(result result > 0) { return 1; }',
      'int f(int x) pre(x, true) { return x; }',
      'template<class T> int f(T x) pre(x > 0) requires true { return x; }',
      'auto lambda = [] pre(true) noexcept { return 1; };',
    ]) {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError, source).toBe(true);
      } finally {
        tree.delete();
      }
    }
  } finally {
    parser.delete();
  }
});
