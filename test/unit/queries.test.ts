import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { expect, test } from 'vitest';

import { Edit, Parser, Query } from '@willbooster/web-tree-sitter';

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
