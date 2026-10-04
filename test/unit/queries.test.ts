import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { expect, test } from 'vitest';

import { Edit, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';

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
test('compiles the queries that tree-sitter.json lists', async () => {
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

test(
  'preserves attributed friend definitions in public trees and shipped queries after edits',
  { timeout: 30_000 },
  async () => {
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
    let tags: Query | undefined;
    let highlights: Query | undefined;
    try {
      parser.setLanguage(language);
      friends = new Query(
        language,
        '(friend_declaration (function_definition declarator: (function_declarator declarator: (identifier) @name) body: (compound_statement)) @function) @friend'
      );
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
      expect(
        tags
          .captures(tree.rootNode)
          .filter(({ name }) => name === 'name')
          .map(({ node }) => node.text)
      ).toEqual(['Value', 'get', 'twice', 'plain', 'main']);
      expect(
        highlights.captures(tree.rootNode).filter(({ name, node }) => name === 'keyword' && node.text === 'friend')
      ).toHaveLength(3);

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
          expect(
            tags.captures(recovery.rootNode).find(({ name, node }) => name === 'name' && node.text === 'h')?.node
              .startIndex
          ).toBe(input.indexOf('h();'));
          expect(
            highlights
              .captures(recovery.rootNode)
              .filter(({ name, node }) => name === 'keyword' && node.text === 'friend')
              .map(({ node }) => input.slice(node.startIndex, node.endIndex))
          ).toEqual(['friend', 'friend', 'friend', 'friend']);
        } finally {
          recovery.delete();
        }
      }

      const prefix = '[[nodiscard]] ';
      const startIndex = source.indexOf(prefix);
      const withoutAttribute = source.slice(0, startIndex) + source.slice(startIndex + prefix.length);
      const editedInputs = [withoutAttribute, source];
      for (const [index, input] of editedInputs.entries()) {
        const removing = index === 0;
        tree.edit(
          new Edit({
            startIndex,
            oldEndIndex: startIndex + (removing ? prefix.length : 0),
            newEndIndex: startIndex + (removing ? 0 : prefix.length),
            startPosition: { row: 2, column: 2 },
            oldEndPosition: { row: 2, column: 2 + (removing ? prefix.length : 0) },
            newEndPosition: { row: 2, column: 2 + (removing ? 0 : prefix.length) },
          })
        );
        let edited: Tree | undefined;
        let fresh: Tree | undefined;
        try {
          edited = parser.parse(input, tree)!;
          fresh = parser.parse(input)!;
          expect(edited.rootNode.hasError).toBe(false);
          expect(edited.rootNode.toString()).toBe(fresh.rootNode.toString());
          for (const query of [friends, tags, highlights]) {
            const captures = (parsed: Tree): { name: string; text: string; startIndex: number; endIndex: number }[] =>
              query.captures(parsed.rootNode).map(({ name, node }) => ({
                name,
                text: node.text,
                startIndex: node.startIndex,
                endIndex: node.endIndex,
              }));
            expect(captures(edited)).toEqual(captures(fresh));
          }
          tree.delete();
          tree = edited;
          edited = undefined;
        } finally {
          edited?.delete();
          fresh?.delete();
        }
      }
    } finally {
      highlights?.delete();
      tags?.delete();
      friends?.delete();
      tree?.delete();
      parser.delete();
    }
  }
);

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
        const editedSource = source.replace(', source::right', '');
        const startIndex = source.indexOf(', source::right');
        const startPosition = { row: 1, column: startIndex - source.indexOf('using source') };
        tree.edit(
          new Edit({
            startIndex,
            oldEndIndex: startIndex + ', source::right'.length,
            newEndIndex: startIndex,
            startPosition,
            oldEndPosition: { row: 1, column: startPosition.column + ', source::right'.length },
            newEndPosition: startPosition,
          })
        );
        const edited = parser.parse(editedSource, tree)!;
        try {
          expect(edited.rootNode.hasError).toBe(false);
          expect(
            query
              .captures(edited.rootNode)
              .filter(({ name }) => name === 'name')
              .map(({ node }) => node.text)
          ).toEqual(['left', 'first', 'second']);
          const fresh = parser.parse(editedSource)!;
          try {
            const captures = (input: typeof fresh): { name: string; text: string; start: number; end: number }[] =>
              query
                .captures(input.rootNode)
                .map(({ name, node }) => ({ name, text: node.text, start: node.startIndex, end: node.endIndex }));
            expect(captures(edited)).toEqual(captures(fresh));
          } finally {
            fresh.delete();
          }
        } finally {
          edited.delete();
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

test('retains dependent using names and pack expansions through query edits', async () => {
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
  let highlights: Query | undefined;
  try {
    parser.setLanguage(language);
    names = new Query(
      language,
      '(using_declaration (qualified_identifier scope: (_) @scope name: (identifier) @name)) @using'
    );
    highlights = new Query(
      language,
      queryPaths('highlights')
        .map((file) => fs.readFileSync(path.join(Root, file), 'utf8'))
        .join('\n')
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
    expect(
      highlights
        .captures(tree.rootNode)
        .filter(({ name, node }) => name === 'keyword' && node.text === 'typename')
        .map(({ node }) => [node.startIndex, node.endIndex])
    ).toEqual([[source.indexOf('typename'), source.indexOf('typename') + 'typename'.length]]);
    const oldLine = `${source.split('\n')[1]}\n`;
    const newLine = oldLine.replaceAll('value_type', 'other_type');
    const startIndex = source.indexOf(oldLine);
    const editedSource = source.replace(oldLine, newLine);
    tree.edit(
      new Edit({
        startIndex,
        oldEndIndex: startIndex + oldLine.length,
        newEndIndex: startIndex + newLine.length,
        startPosition: { row: 1, column: 0 },
        oldEndPosition: { row: 2, column: 0 },
        newEndPosition: { row: 2, column: 0 },
      })
    );
    let edited: Tree | undefined;
    let fresh: Tree | undefined;
    try {
      edited = parser.parse(editedSource, tree)!;
      fresh = parser.parse(editedSource)!;
      expect(edited.rootNode.hasError).toBe(false);
      expect(edited.rootNode.toString()).toBe(fresh.rootNode.toString());
      expect(
        names
          .captures(edited.rootNode)
          .filter(({ name }) => name === 'name')
          .map(({ node }) => node.text)
      ).toEqual(['other_type', 'f']);
      for (const query of [names, highlights]) {
        const captures = (parsed: Tree): { name: string; text: string; start: number; end: number }[] =>
          query
            .captures(parsed.rootNode)
            .map(({ name, node }) => ({ name, text: node.text, start: node.startIndex, end: node.endIndex }));
        expect(captures(edited)).toEqual(captures(fresh));
      }
    } finally {
      edited?.delete();
      fresh?.delete();
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
    highlights?.delete();
    names?.delete();
    tree?.delete();
    parser.delete();
  }
});
