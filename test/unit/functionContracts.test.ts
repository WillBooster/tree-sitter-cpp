import { readFileSync } from 'node:fs';
import { Parser, Query } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

test('exposes contract predicates as canonical expressions and postcondition result bindings', async () => {
  await Parser.init();
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  parser.setLanguage(language);
  const query = new Query(
    language,
    '(contract_specifier condition: (expression) @predicate) (contract_specifier result: (identifier) @result) (expression/binary_expression) @binary'
  );
  const highlights = new Query(
    language,
    readFileSync(new URL('../../queries/highlights.scm', import.meta.url), 'utf8')
  );
  const source = `int f(const int x)
    pre [[maybe_unused]] (x > 0)
    post [[maybe_unused]] (result [[maybe_unused]]: result > 0)
    post(true) { return x; }
int following() { int pre = 1, post = 2; return pre + post; }
void destroy(int* pre, int* post) { delete[] pre; ::delete[] post; }`;
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
      'pre + post',
    ]);
    const contractKeywords = highlights
      .captures(tree.rootNode)
      .filter(({ name, node }) => name === 'keyword' && ['pre', 'post'].includes(node.text));
    expect(contractKeywords.map(({ node }) => node.text)).toEqual(['pre', 'post', 'post']);
    expect(contractKeywords.every(({ node }) => !node.isNamed)).toBe(true);
    expect(tree.rootNode.namedChildren.map((node) => node.type)).toEqual([
      'function_definition',
      'function_definition',
      'function_definition',
    ]);
    expect(tree.rootNode.namedChildren.at(-2)?.childForFieldName('declarator')?.text).toBe('following()');
    const destroy = tree.rootNode.namedChildren.at(-1)!;
    expect(
      destroy
        .descendantsOfType('delete_expression')
        .map((node) => node.namedChildren.map((child) => [child.type, child.text]))
    ).toEqual([[['identifier', 'pre']], [['identifier', 'post']]]);
  } finally {
    tree.delete();
    query.delete();
    highlights.delete();
    parser.delete();
  }
});

test('rejects malformed result bindings, empty predicates and misplaced contract suffixes', async () => {
  await Parser.init();
  const language = await loadCurrentWasmBuild();
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

test('preserves contextual type names and pointer-to-member trailing return queries', async () => {
  await Parser.init();
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  parser.setLanguage(language);
  const query = new Query(
    language,
    `(parameter_declaration type: (type_identifier) @parameter)
    (trailing_return_type (type_descriptor declarator: (abstract_function_declarator) @return))`
  );
  const highlights = new Query(
    language,
    readFileSync(new URL('../../queries/highlights.scm', import.meta.url), 'utf8')
  );
  const tree = parser.parse(`struct pre { int f() const; }; using post = int;
    void accepts(pre, post);
    void use() { pre value; post count(1); }
    auto member() -> int(pre::*)() const;`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(query.captures(tree.rootNode).map(({ name, node }) => [name, node.type, node.text])).toEqual([
      ['parameter', 'type_identifier', 'pre'],
      ['parameter', 'type_identifier', 'post'],
      ['return', 'abstract_function_declarator', '(pre::*)() const'],
    ]);
    expect(tree.rootNode.descendantsOfType('init_declarator').map((node) => node.text)).toEqual(['count(1)']);
    expect(
      highlights
        .captures(tree.rootNode)
        .filter(({ name, node }) => name === 'keyword' && ['pre', 'post'].includes(node.text))
    ).toEqual([]);
  } finally {
    tree.delete();
    query.delete();
    highlights.delete();
    parser.delete();
  }
});
