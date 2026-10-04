import { readFileSync } from 'node:fs';
import { Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
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
int contextual() post(pre: pre > 0) post(post: post > 0) { return 1; }
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
      'pre > 0',
      'post > 0',
    ]);
    expect(captures.filter(({ name }) => name === 'result').map(({ node }) => node.text)).toEqual([
      'result',
      'pre',
      'post',
    ]);
    expect(captures.filter(({ name }) => name === 'binary').map(({ node }) => node.text)).toEqual([
      'x > 0',
      'result > 0',
      'pre > 0',
      'post > 0',
      'pre + post',
    ]);
    const contractKeywords = highlights
      .captures(tree.rootNode)
      .filter(({ name, node }) => name === 'keyword' && ['pre', 'post'].includes(node.text));
    expect(contractKeywords.map(({ node }) => node.text)).toEqual(['pre', 'post', 'post', 'post', 'post']);
    expect(contractKeywords.every(({ node }) => !node.isNamed)).toBe(true);
    expect(tree.rootNode.namedChildren.map((node) => node.type)).toEqual([
      'function_definition',
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
    (trailing_return_type (type_descriptor declarator: (abstract_function_declarator) @return))
    (trailing_return_type (type_descriptor declarator: (abstract_array_declarator) @array))
    (assignment_expression left: (subscript_expression indices: (subscript_argument_list (identifier) @index))) @assignment`
  );
  const highlights = new Query(
    language,
    readFileSync(new URL('../../queries/highlights.scm', import.meta.url), 'utf8')
  );
  const tree = parser.parse(`struct pre { int f() const; }; using post = int;
    void accepts(pre, post);
    void use() { pre value; post count(1); }
    auto member() -> int(pre::*)() const;
    auto reference() -> int(&)[3]; auto pointer() -> int(*)[3][4];
    void indices(int* a, int pre, int post) { a[pre] = 1; a[post] += 2; }`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const captures = query.captures(tree.rootNode);
    expect(
      captures
        .filter(({ name }) => ['parameter', 'return'].includes(name))
        .map(({ name, node }) => [name, node.type, node.text])
    ).toEqual([
      ['parameter', 'type_identifier', 'pre'],
      ['parameter', 'type_identifier', 'post'],
      ['return', 'abstract_function_declarator', '(pre::*)() const'],
    ]);
    expect(captures.filter(({ name }) => name === 'array').map(({ node }) => [node.type, node.text])).toEqual([
      ['abstract_array_declarator', '(&)[3]'],
      ['abstract_array_declarator', '(*)[3][4]'],
    ]);
    expect(captures.filter(({ name }) => name === 'assignment').map(({ node }) => node.text)).toEqual([
      'a[pre] = 1',
      'a[post] += 2',
    ]);
    expect(captures.filter(({ name }) => name === 'index').map(({ node }) => [node.type, node.text])).toEqual([
      ['identifier', 'pre'],
      ['identifier', 'post'],
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

test('keeps contract fields outside pointer and reference trailing returns', async () => {
  await Parser.init();
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  parser.setLanguage(language);
  const query = new Query(
    language,
    `(trailing_return_type (type_descriptor declarator: (_) @return))
    (contract_specifier condition: (expression) @condition)
    (contract_specifier result: (identifier) @result)`
  );
  const tree = parser.parse(`struct Box { int value; };
    auto pointer(int* const p) -> int* pre(p != nullptr) post(result: result == p) { return p; }
    auto reference(int& r) -> int& pre(true) { return r; }
    auto member() -> int Box::* pre(true) { return &Box::value; }
    auto cv_member() -> int Box::* const pre(true) { return &Box::value; }
    auto closure = [](int& r) -> int& post(result: true) { return r; };`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const captures = query.captures(tree.rootNode);
    expect(captures.filter(({ name }) => name === 'return').map(({ node }) => [node.type, node.text])).toEqual([
      ['abstract_pointer_declarator', '*'],
      ['abstract_reference_declarator', '&'],
      ['qualified_identifier', 'Box::*'],
      ['qualified_identifier', 'Box::* const'],
      ['abstract_reference_declarator', '&'],
    ]);
    expect(captures.filter(({ name }) => name === 'condition').map(({ node }) => node.text)).toEqual([
      'p != nullptr',
      'result == p',
      'true',
      'true',
      'true',
      'true',
    ]);
    expect(captures.filter(({ name }) => name === 'result').map(({ node }) => node.text)).toEqual(['result', 'result']);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});

test('keeps bare trailing arrays in public return descriptors and function tags', async () => {
  await Parser.init();
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  let returns: Query | undefined;
  let tags: Query | undefined;
  let tree: Tree | undefined;
  try {
    parser.setLanguage(language);
    returns = new Query(
      language,
      '(trailing_return_type (type_descriptor declarator: (abstract_array_declarator) @return))'
    );
    tags = new Query(language, readFileSync(new URL('../../queries/tags.scm', import.meta.url), 'utf8'));
    tree = parser.parse(`auto array() -> int[3];
      auto matrix() -> int[3][4];
      auto array_definition() -> int[3] { return {}; }
      auto matrix_definition() -> int[3][4] { return {}; }`)!;
    expect(tree.rootNode.hasError).toBe(false);
    expect(returns.captures(tree.rootNode).map(({ node }) => node.text)).toEqual(['[3]', '[3][4]', '[3]', '[3][4]']);
    expect(
      tags
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'definition.function')
        .map(({ node }) => node.text)
    ).toEqual([
      'array() -> int[3]',
      'matrix() -> int[3][4]',
      'array_definition() -> int[3]',
      'matrix_definition() -> int[3][4]',
    ]);
  } finally {
    tree?.delete();
    returns?.delete();
    tags?.delete();
    parser.delete();
  }
});
