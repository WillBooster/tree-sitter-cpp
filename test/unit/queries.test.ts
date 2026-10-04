import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { expect, test } from 'vitest';

import { Parser, Query } from '@willbooster/web-tree-sitter';

import treeSitterJson from '../../tree-sitter.json';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.join(import.meta.dirname, '../..');
const CQueries = path.join(Root, 'node_modules/@willbooster/tree-sitter-c/queries');
const QueryKinds = ['highlights', 'injections', 'locals', 'tags'] as const;
const [grammar] = treeSitterJson.grammars;
await Parser.init();

const queryPaths = (kind: (typeof QueryKinds)[number]): string[] =>
  [(grammar as Partial<Record<(typeof QueryKinds)[number], string | string[]>>)[kind] ?? []].flat();

// Tools that read tree-sitter.json, such as the tree-sitter CLI, compile each kind of query from its files
// concatenated, so a node type that a grammar change removes must fail here rather than in them.
test('compiles the queries that tree-sitter.json lists', { timeout: 30_000 }, async () => {
  const language = await loadCurrentWasmBuild();
  for (const kind of QueryKinds) {
    const source = queryPaths(kind)
      .map((file) => fs.readFileSync(path.join(Root, file), 'utf8'))
      .join('\n');
    expect(() => new Query(language, source), `${kind} queries`).not.toThrow();
  }
});

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
  test(`publishes every query file that tree-sitter.json references in the ${packageKind} package`, testOptions, () => {
    const published = new Set(listFiles());
    expect(published).toContain('tree-sitter.json');
    for (const kind of QueryKinds) {
      for (const file of queryPaths(kind)) {
        expect(published, file).toContain(file);
      }
    }
  });
}

// queries/c/ copies the C grammar's queries, which this grammar extends.
test('keeps queries/c/ identical to the queries of @willbooster/tree-sitter-c', () => {
  for (const file of fs.readdirSync(path.join(Root, 'queries/c'))) {
    expect(
      fs.readFileSync(path.join(Root, 'queries/c', file), 'utf8'),
      `queries/c/${file} differs; run script/copy-c-queries`
    ).toBe(fs.readFileSync(path.join(CQueries, file), 'utf8'));
  }
});

test('matches sized keywords in macro arguments through the expression supertype', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  parser.setLanguage(language);
  const tree = parser.parse('#define DECLARE(type, name) type name\nvoid f() { DECLARE(long, value); }')!;
  const query = new Query(language, '(call_expression arguments: (argument_list (expression) @argument))');
  try {
    expect(tree.rootNode.hasError).toBe(false);
    expect(query.captures(tree.rootNode).map(({ node }) => node.text)).toEqual(['long', 'value']);
  } finally {
    query.delete();
    tree.delete();
    parser.delete();
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
        let tags: Query | undefined;
        let highlights: Query | undefined;
        try {
          tags = new Query(
            language,
            queryPaths('tags')
              .map((file) => fs.readFileSync(path.join(Root, file), 'utf8'))
              .join('\n')
          );
          highlights = new Query(
            language,
            queryPaths('highlights')
              .map((file) => fs.readFileSync(path.join(Root, file), 'utf8'))
              .join('\n')
          );
          expect(
            tags
              .captures(tree.rootNode)
              .filter(({ name }) => name === 'definition.function')
              .map(({ node }) => node.childForFieldName('declarator')?.text)
          ).toEqual(['destroySelf', 'destroy', 'destroyReferences', 'destroyDependent', 'main']);
          const unaryFunctionNames = highlights
            .captures(tree.rootNode)
            .filter(
              ({ name, node }) =>
                name === 'function' && node.text === 'Box' && node.parent?.type === 'template_function'
            );
          expect([...new Set(unaryFunctionNames.map(({ node }) => node.startIndex))]).toEqual([
            source.indexOf('~Box<int>();', source.indexOf('int main')) + 1,
            source.indexOf('return ~Box<int>()') + 'return ~'.length,
          ]);
        } finally {
          highlights?.delete();
          tags?.delete();
        }
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
