import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { Parser, Query, Edit, type Node, type Tree } from '@willbooster/web-tree-sitter';
import { test } from 'vitest';
import { loadCurrentWasmBuild } from './wasmBuild.js';
const root = path.resolve(import.meta.dirname, '../..');
const snapshot = (node: Node): unknown => ({
  type: node.type,
  named: node.isNamed,
  extra: node.isExtra,
  missing: node.isMissing,
  error: node.hasError,
  start: node.startIndex,
  end: node.endIndex,
  startPosition: node.startPosition,
  endPosition: node.endPosition,
  children: node.children.map((n, i) => ({ field: node.fieldNameForChild(i), node: snapshot(n) })),
});
await Parser.init();
test('preserves typeof type fields, configured captures and contextual names after edits', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser();
  let types: Query | undefined, highlights: Query | undefined, tags: Query | undefined;
  try {
    parser.setLanguage(language);
    types = new Query(language, '(type_specifier/typeof_specifier) @type');
    highlights = new Query(
      language,
      ['queries/c/highlights.scm', 'queries/highlights.scm']
        .map((f) => fs.readFileSync(path.join(root, f), 'utf8'))
        .join('\n')
    );
    tags = new Query(language, fs.readFileSync(path.join(root, 'queries/tags.scm'), 'utf8'));
    for (const file of ['typeofCpp.cpp', 'typeofNames.cpp', 'typeofGccNames.cpp']) {
      const source = fs.readFileSync(path.join(root, 'test/fixtures', file), 'utf8');
      let tree: Tree | undefined,
        edited: Tree | undefined,
        fresh: Tree | undefined,
        restored: Tree | undefined,
        restoredFresh: Tree | undefined;
      try {
        tree = parser.parse(source) ?? undefined;
        assert.ok(tree);
        assert.equal(tree.rootNode.hasError, false, tree.rootNode.toString());
        const captures: {
          name: string;
          text: string;
          type: string;
          start: number;
          end: number;
          parent: string | undefined;
        }[] = types.captures(tree.rootNode).map(({ name, node }) => ({
          name,
          text: node.text,
          type: node.type,
          start: node.startIndex,
          end: node.endIndex,
          parent: node.parent?.type,
        }));
        const names: Set<string> = new Set(
          tags
            .captures(tree.rootNode)
            .filter((c) => c.name === 'name')
            .map((c) => c.node.text)
        );
        if (file === 'typeofCpp.cpp') {
          assert.equal(captures.length, 11);
          assert.ok(captures.every((c) => c.type === 'typeof_specifier' && source.slice(c.start, c.end) === c.text));
          const declarations = tree.rootNode.descendantsOfType('declaration');
          const pointer = declarations.find(
            (n) => n.childForFieldName('declarator')?.childForFieldName('declarator')?.text === 'pointer'
          );
          assert.equal(pointer?.childForFieldName('type')?.type, 'typeof_specifier');
          assert.equal(pointer?.childForFieldName('type')?.text, '__typeof__(int*)');
          const member = tree.rootNode
            .descendantsOfType('field_declaration')
            .find((n) => n.childForFieldName('declarator')?.text === 'member');
          assert.equal(member?.childForFieldName('type')?.text, '__typeof__(source)');
          const cast = tree.rootNode.descendantsOfType('cast_expression')[0];
          assert.equal(cast?.childForFieldName('type')?.childForFieldName('type')?.type, 'typeof_specifier');
          assert.deepEqual(
            highlights
              .captures(tree.rootNode)
              .filter((c) => c.name === 'keyword' && c.node.text === '__typeof__')
              .map((c) => c.node.text),
            Array.from({ length: 11 }, () => '__typeof__')
          );
          assert.ok(names.has('echo') && names.has('other') && names.has('main'));
          assert.ok(!names.has('__typeof__'));
        } else {
          assert.equal(captures.length, 0);
          assert.ok(
            !highlights
              .captures(tree.rootNode)
              .some(
                (c) =>
                  c.name === 'keyword' &&
                  ['typeof', 'typeof_unqual', '__typeof_unqual', '__typeof_unqual__'].includes(c.node.text)
              )
          );
          const declaration = tree.rootNode
            .descendantsOfType('declaration')
            .find(
              (n) =>
                n.childForFieldName('declarator')?.childForFieldName('declarator')?.childForFieldName('declarator')
                  ?.text === 'pointer'
            );
          assert.equal(declaration?.childForFieldName('type')?.type, 'type_identifier');
          assert.equal(
            declaration?.childForFieldName('type')?.text,
            file === 'typeofGccNames.cpp' ? '__typeof_unqual' : 'typeof'
          );
          if (file === 'typeofGccNames.cpp') {
            assert.deepEqual(
              tree.rootNode.descendantsOfType('statement_identifier').map((n) => n.text),
              ['__typeof_unqual', '__typeof_unqual']
            );
            const counter = tree.rootNode
              .descendantsOfType('assignment_expression')
              .find((n) => n.childForFieldName('left')?.text === '__typeof_unqual');
            assert.equal(counter?.childForFieldName('left')?.type, 'identifier');
            assert.equal(counter?.childForFieldName('right')?.text, '1');
          }
        }
        const original = snapshot(tree.rootNode);
        const name = file === 'typeofGccNames.cpp' ? '__typeof_unqual' : 'typeof';
        const marker = file === 'typeofCpp.cpp' ? '__typeof__(source) value' : `${name}* pointer`;
        const start = source.indexOf(marker) + (file === 'typeofCpp.cpp' ? '__typeof__('.length : 0);
        const old = file === 'typeofCpp.cpp' ? 'source' : name;
        const replacement =
          file === 'typeofCpp.cpp'
            ? 'source + 1'
            : file === 'typeofGccNames.cpp'
              ? '__typeof_unqual__'
              : 'typeof_unqual';
        const changed = source.slice(0, start) + replacement + source.slice(start + old.length);
        const position = (i: number): { row: number; column: number } => {
          const lines = source.slice(0, i).split('\n');
          return { row: lines.length - 1, column: lines.at(-1)!.length };
        };
        const a = position(start),
          b = position(start + old.length),
          c = { row: a.row, column: a.column + replacement.length };
        tree.edit(
          new Edit({
            startIndex: start,
            oldEndIndex: start + old.length,
            newEndIndex: start + replacement.length,
            startPosition: a,
            oldEndPosition: b,
            newEndPosition: c,
          })
        );
        edited = parser.parse(changed, tree) ?? undefined;
        fresh = parser.parse(changed) ?? undefined;
        assert.ok(edited);
        assert.ok(fresh);
        assert.deepEqual(snapshot(edited.rootNode), snapshot(fresh.rootNode));
        assert.equal(edited.rootNode.hasError, false);
        edited.edit(
          new Edit({
            startIndex: start,
            oldEndIndex: start + replacement.length,
            newEndIndex: start + old.length,
            startPosition: a,
            oldEndPosition: c,
            newEndPosition: b,
          })
        );
        restored = parser.parse(source, edited) ?? undefined;
        restoredFresh = parser.parse(source) ?? undefined;
        assert.ok(restored);
        assert.ok(restoredFresh);
        assert.deepEqual(snapshot(restored.rootNode), snapshot(restoredFresh.rootNode));
        assert.deepEqual(snapshot(restored.rootNode), original);
      } finally {
        tree?.delete();
        edited?.delete();
        fresh?.delete();
        restored?.delete();
        restoredFresh?.delete();
      }
    }
  } finally {
    types?.delete();
    highlights?.delete();
    tags?.delete();
    parser.delete();
  }
});
