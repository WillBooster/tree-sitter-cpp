import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.join(import.meta.dirname, '../..');
await Parser.init();

// The packages' tree-sitter.json can reference only files inside the package: a dependency's directory is not at a
// fixed path relative to it after an install.
// On a fresh runner, cargo first installs the toolchain that rust-toolchain.toml pins. The commands run synchronously,
// so only their own timeout can stop them; the test's is longer so that theirs is reported.
const execOptions = { cwd: Root, encoding: 'utf8', timeout: 240_000 } as const;
const testOptions = { timeout: 300_000 };
const listPublishedFiles = {
  npm: (): string[] => {
    const [pack] = JSON.parse(
      execFileSync('npm', ['pack', '--dry-run', '--json', '--ignore-scripts'], execOptions)
    ) as [{ files: { path: string }[] }];
    return pack.files.map((file) => file.path);
  },
  crate: (): string[] => execFileSync('cargo', ['package', '--list', '--allow-dirty'], execOptions).split('\n'),
};
for (const [packageKind, listFiles] of Object.entries(listPublishedFiles)) {
  test(`publishes tree-sitter.json and scanner files in the ${packageKind} package`, testOptions, () => {
    const published = new Set(listFiles());
    expect(published).toContain('tree-sitter.json');
    if (packageKind === 'crate') {
      for (const file of scannerDependencies()) {
        expect(published, file).toContain(file);
      }
    }
  });
}

function scannerDependencies(): string[] {
  const dependencies = execFileSync('cc', ['-MM', '-Isrc', 'src/scanner.c'], execOptions)
    .replaceAll(/\\\r?\n/g, ' ')
    .split(':')[1]!
    .trim()
    .split(/\s+/);
  return dependencies
    .map((file) => path.relative(Root, path.resolve(Root, file)))
    .filter((file) => file !== '..' && !file.startsWith(`..${path.sep}`) && !path.isAbsolute(file))
    .map((file) => file.split(path.sep).join('/'));
}

test('keeps vendored C scanner headers synchronized with the installed dependency', () => {
  for (const file of ['pragma.h', 'identifier.h', 'keywords.h', 'functionMacro.h', 'conditionalMacro.h']) {
    expect(fs.readFileSync(path.join(Root, 'src', file)), `${file} differs; run script/copy-c-scanner`).toEqual(
      fs.readFileSync(path.join(Root, 'node_modules/@willbooster/tree-sitter-c/src', file))
    );
  }
});

test('preserves template and qualified names in explicit destructor calls', async () => {
  const source = `namespace ns { template<class T> struct Box { ~Box() {} int operator~() const { return 1; } }; }
using ns::Box;
struct Derived : Box<int> { void destroySelf() { Box<int>::~Box<int>(); } };
struct Plain { ~Plain() {} };
void destroy(ns::Box<int>* box, Plain* plain, ns::Box<ns::Box<int>>* nested) {
  box->~Box<int>();
  box->ns::Box<int>::~Box<int>();
  plain->Plain::~Plain();
  nested->~Box<Box<int>>();
}
void destroyReferences(ns::Box<int>& box, Plain& plain) {
  box.~Box<int>();
  plain.Plain::~Plain();
}
template<class T> void destroyDependent(ns::Box<T>* box) { box->~Box<T>(); }
int main() { ~Box<int>(); return ~Box<int>() == 1 ? 0 : 1; }
`;
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  try {
    parser.setLanguage(language);
    const calls = new Query(
      language,
      '(call_expression function: (field_expression argument: (identifier) @object field: (_) @field) @member arguments: (argument_list)) @call'
    );
    try {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError).toBe(false);
        const fields = calls
          .captures(tree.rootNode)
          .filter(({ name }) => name === 'field')
          .map(({ node }) => node);
        expect(fields.map((node) => node.text)).toEqual([
          '~Box<int>',
          'ns::Box<int>::~Box<int>',
          'Plain::~Plain',
          '~Box<Box<int>>',
          '~Box<int>',
          'Plain::~Plain',
          '~Box<T>',
        ]);
        expect(fields.map((node) => node.type)).toEqual([
          'destructor_name',
          'qualified_identifier',
          'qualified_identifier',
          'destructor_name',
          'destructor_name',
          'qualified_identifier',
          'destructor_name',
        ]);
        const qualified = fields[1]!;
        expect(qualified.childForFieldName('scope')?.type).toBe('namespace_identifier');
        expect(qualified.childForFieldName('scope')?.text).toBe('ns');
        const specialized = qualified.childForFieldName('name')!;
        expect(specialized.type).toBe('qualified_identifier');
        expect(specialized.childForFieldName('scope')?.type).toBe('template_type');
        expect(specialized.childForFieldName('scope')?.text).toBe('Box<int>');
        expect(specialized.childForFieldName('name')?.type).toBe('destructor_name');
        expect(specialized.childForFieldName('name')?.text).toBe('~Box<int>');
        expect(fields[3]?.namedChildren.map((node) => node.type)).toEqual(['identifier', 'template_argument_list']);
        expect(fields[3]?.namedChildren[1]?.text).toBe('<Box<int>>');
        const selfCall = tree.rootNode.descendantsOfType('call_expression')[0]!;
        const selfName = selfCall.childForFieldName('function')!;
        expect(selfName.type).toBe('qualified_identifier');
        expect(selfName.childForFieldName('scope')?.type).toBe('template_type');
        expect(selfName.childForFieldName('scope')?.text).toBe('Box<int>');
        expect(selfName.childForFieldName('name')?.type).toBe('destructor_name');
        expect(selfName.childForFieldName('name')?.text).toBe('~Box<int>');
        const unary = tree.rootNode.descendantsOfType('unary_expression');
        expect(unary.map((node) => node.text)).toEqual(['~Box<int>()', '~Box<int>()']);
        expect(unary.map((node) => node.childForFieldName('argument')?.type)).toEqual([
          'call_expression',
          'call_expression',
        ]);
        const unaryCalls = new Query(
          language,
          '(unary_expression argument: (call_expression function: (template_function name: (identifier) @name)))'
        );
        try {
          expect(unaryCalls.captures(tree.rootNode).map(({ node }) => node.text)).toEqual(['Box', 'Box']);
        } finally {
          unaryCalls.delete();
        }
      } finally {
        tree.delete();
      }
    } finally {
      calls.delete();
    }
  } finally {
    parser.delete();
  }
});

test('preserves attributed friend definitions in public trees', { timeout: 30_000 }, async () => {
  const source = `struct Value {
  int number;
  [[nodiscard]] friend int get(Value v) { return v.number; }
  [[nodiscard]] [[maybe_unused]] constexpr friend int twice(Value v) { return v.number * 2; }
  friend int plain(Value v) { return v.number; }
};
static_assert(twice(Value{3}) == 6);
int main() { return get(Value{3}) + plain(Value{3}) == 6 ? 0 : 1; }
`;
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  let tree: Tree | undefined;
  let friends: Query | undefined;
  try {
    parser.setLanguage(language);
    friends = new Query(
      language,
      '(friend_declaration (function_definition declarator: (function_declarator declarator: (identifier) @name) body: (compound_statement)) @function) @friend'
    );
    tree = parser.parse(source)!;
    expect(tree.rootNode.hasError).toBe(false);
    const matches = friends.matches(tree.rootNode);
    expect(matches.map(({ captures }) => captures.find(({ name }) => name === 'name')?.node.text)).toEqual([
      'get',
      'twice',
      'plain',
    ]);
    expect(
      matches.map(({ captures }) =>
        captures
          .find(({ name }) => name === 'friend')!
          .node.namedChildren.filter((node) => node.type === 'attribute_declaration')
          .map((node) => node.text)
      )
    ).toEqual([['[[nodiscard]]'], ['[[nodiscard]]', '[[maybe_unused]]'], []]);

    const recoveryInputs = [
      `struct A {
  [[nodiscard]] friend void f();
  [[nodiscard]] friend void g();
  friend class D;
  friend int h();
};`,
      `struct A {
  [[nodiscard]] friend void f();
  [[maybe_unused]] friend struct S;
  friend class D;
  friend int h();
};`,
    ];
    for (const input of recoveryInputs) {
      const recovery = parser.parse(input)!;
      try {
        expect(recovery.rootNode.hasError).toBe(false);
        const declarations = recovery.rootNode.descendantsOfType('friend_declaration');
        expect(declarations).toHaveLength(4);
        const following = declarations.slice(-2);
        expect(following.map((node) => node.text)).toEqual(['friend class D;', 'friend int h();']);
        expect(following[0]?.namedChildren[0]?.type).toBe('type_identifier');
        expect(following[0]?.namedChildren[0]?.text).toBe('D');
        const declaration = following[1]?.namedChildren[0];
        expect(declaration?.type).toBe('declaration');
        expect(declaration?.childForFieldName('declarator')?.childForFieldName('declarator')?.text).toBe('h');
      } finally {
        recovery.delete();
      }
    }

    const prefix = '[[nodiscard]] ';
    const startIndex = source.indexOf(prefix);
    const withoutAttribute = parser.parse(source.slice(0, startIndex) + source.slice(startIndex + prefix.length))!;
    try {
      expect(withoutAttribute.rootNode.hasError).toBe(false);
    } finally {
      withoutAttribute.delete();
    }
  } finally {
    friends?.delete();
    tree?.delete();
    parser.delete();
  }
});

test('retains every qualified name in multiple using declarations', async () => {
  const source = `namespace source { int left = 1; int right = 2; }
using source::left, source::right;
struct First { int first() const { return 3; } };
struct Second { int second() const { return 4; } };
struct Both : First, Second { using First::first, Second::second; };
int main() { Both value; return left + right + value.first() + value.second() == 10 ? 0 : 1; }
`;
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  try {
    parser.setLanguage(language);
    const query = new Query(
      language,
      '(using_declaration (qualified_identifier scope: (_) @scope name: (identifier) @name) @entry) @using'
    );
    try {
      const tree = parser.parse(source)!;
      try {
        expect(tree.rootNode.hasError).toBe(false);
        const declarations = tree.rootNode.descendantsOfType('using_declaration');
        expect(declarations.map((node) => node.namedChildren.map((child) => child.text))).toEqual([
          ['source::left', 'source::right'],
          ['First::first', 'Second::second'],
        ]);
        expect(declarations.map((node) => node.children.map((child) => child.type))).toEqual([
          ['using', 'qualified_identifier', ',', 'qualified_identifier', ';'],
          ['using', 'qualified_identifier', ',', 'qualified_identifier', ';'],
        ]);
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'name')
            .map(({ node }) => node.text)
        ).toEqual(['left', 'right', 'first', 'second']);
        expect(
          query
            .captures(tree.rootNode)
            .filter(({ name }) => name === 'scope')
            .map(({ node }) => node.text)
        ).toEqual(['source', 'source', 'First', 'Second']);
        const withoutRight = parser.parse(source.replace(', source::right', ''))!;
        try {
          expect(withoutRight.rootNode.hasError).toBe(false);
          expect(
            query
              .captures(withoutRight.rootNode)
              .filter(({ name }) => name === 'name')
              .map(({ node }) => node.text)
          ).toEqual(['left', 'first', 'second']);
        } finally {
          withoutRight.delete();
        }
      } finally {
        tree.delete();
      }
      for (const invalid of ['using namespace source, other;', 'using enum E, F;', 'using source::left,;']) {
        const tree = parser.parse(invalid)!;
        try {
          expect(tree.rootNode.hasError, invalid).toBe(true);
        } finally {
          tree.delete();
        }
      }
    } finally {
      query.delete();
    }
  } finally {
    parser.delete();
  }
});

test('retains dependent using names and pack expansions', async () => {
  const source = `struct Base { using value_type = int; using other_type = int; };
template<class T> struct Derived : T { using typename T::value_type; value_type value = 0; };
struct First { int f(int) { return 0; } };
struct Second { int f(double) { return 1; } };
template<class... Bases> struct Joined : Bases... { using Bases::f...; };
int main() { Derived<Base> derived; Joined<First, Second> joined; return derived.value + joined.f(1); }
`;
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  let tree: Tree | undefined;
  let names: Query | undefined;
  try {
    parser.setLanguage(language);
    names = new Query(
      language,
      '(using_declaration (qualified_identifier scope: (_) @scope name: (identifier) @name)) @using'
    );
    tree = parser.parse(source)!;
    expect(tree.rootNode.hasError).toBe(false);
    const declarations = tree.rootNode.descendantsOfType('using_declaration');
    expect(declarations.map((node) => node.children.map((child) => child.type))).toEqual([
      ['using', 'typename', 'qualified_identifier', ';'],
      ['using', 'qualified_identifier', '...', ';'],
    ]);
    expect(declarations.map((node) => node.namedChildren[0]!.childForFieldName('scope')!.text)).toEqual(['T', 'Bases']);
    expect(declarations.map((node) => node.namedChildren[0]!.childForFieldName('name')!.text)).toEqual([
      'value_type',
      'f',
    ]);
    expect(
      names
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'name')
        .map(({ node }) => node.text)
    ).toEqual(['value_type', 'f']);
    const oldLine = `${source.split('\n')[1]}\n`;
    const renamed = parser.parse(source.replace(oldLine, oldLine.replaceAll('value_type', 'other_type')))!;
    try {
      expect(renamed.rootNode.hasError).toBe(false);
      expect(
        names
          .captures(renamed.rootNode)
          .filter(({ name }) => name === 'name')
          .map(({ node }) => node.text)
      ).toEqual(['other_type', 'f']);
    } finally {
      renamed.delete();
    }
    for (const invalid of [
      'using typename value_type;',
      'using value_type...;',
      'using namespace source...;',
      'using enum E...;',
      'using namespace typename T::value_type;',
      'using Bases::f......;',
    ]) {
      const invalidTree = parser.parse(invalid)!;
      try {
        expect(invalidTree.rootNode.hasError, invalid).toBe(true);
      } finally {
        invalidTree.delete();
      }
    }
  } finally {
    names?.delete();
    tree?.delete();
    parser.delete();
  }
});

test('keeps GNU attributes with friend definitions', async () => {
  const source = `struct Value {
  __attribute__((unused)) friend int first(Value) { return 1; }
  __attribute__((unused)) __attribute__((noinline)) friend int second(Value) { return 2; }
  [[nodiscard]] __attribute__((unused)) friend int third(Value) { return 3; }
  constexpr __attribute__((unused)) friend int before(Value) { return 4; }
  __attribute__((unused)) constexpr friend int after(Value) { return 5; }
  int member = 0;
};
int main() { Value value; return first(value) + second(value) + third(value) + before(value) + after(value) == 15 ? 0 : 1; }
`;
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  let tree: Tree | undefined;
  let attributes: Query | undefined;
  try {
    parser.setLanguage(language);
    attributes = new Query(language, '(friend_declaration (attribute_specifier) @attribute) @friend');
    tree = parser.parse(source)!;
    expect(tree.rootNode.hasError).toBe(false);
    expect(
      attributes
        .captures(tree.rootNode)
        .filter(({ name }) => name === 'attribute')
        .map(({ node }) => ({ text: node.text, parent: node.parent?.type }))
    ).toEqual([
      { text: '__attribute__((unused))', parent: 'friend_declaration' },
      { text: '__attribute__((unused))', parent: 'friend_declaration' },
      { text: '__attribute__((noinline))', parent: 'friend_declaration' },
      { text: '__attribute__((unused))', parent: 'friend_declaration' },
      { text: '__attribute__((unused))', parent: 'friend_declaration' },
      { text: '__attribute__((unused))', parent: 'friend_declaration' },
    ]);
    expect(tree.rootNode.descendantsOfType('field_identifier').map((node) => node.text)).toEqual(['member']);

    const prefix = '__attribute__((unused)) ';
    const startIndex = source.indexOf(prefix);
    const withoutAttribute = parser.parse(source.slice(0, startIndex) + source.slice(startIndex + prefix.length))!;
    try {
      expect(withoutAttribute.rootNode.hasError).toBe(false);
    } finally {
      withoutAttribute.delete();
    }
  } finally {
    attributes?.delete();
    tree?.delete();
    parser.delete();
  }
});
