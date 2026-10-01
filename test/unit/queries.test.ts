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
