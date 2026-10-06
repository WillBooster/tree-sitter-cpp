import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { type Language, Parser, Query, Edit, type Node, type Tree } from '@willbooster/web-tree-sitter';
import { afterAll, beforeAll, test } from 'vitest';
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
let language: Language;
const queries: Query[] = [];

beforeAll(async () => {
  await Parser.init();
  language = await loadCurrentWasmBuild();
  queries.push(new Query(language, '(type_specifier/typeof_specifier) @type'));
  queries.push(
    new Query(
      language,
      ['queries/c/highlights.scm', 'queries/highlights.scm']
        .map((f) => fs.readFileSync(path.join(root, f), 'utf8'))
        .join('\n')
    )
  );
  queries.push(new Query(language, fs.readFileSync(path.join(root, 'queries/tags.scm'), 'utf8')));
}, 30_000);

afterAll(() => {
  for (const query of queries) query.delete();
});

test.each(['typeofCpp.cpp', 'typeofNames.cpp', 'typeofGccNames.cpp'])(
  'preserves typeof type fields, configured captures and contextual names after edits (%s)',
  (file) => {
    const [types, highlights, tags] = queries;
    assert.ok(types && highlights && tags);
    const parser = new Parser();
    try {
      parser.setLanguage(language);
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
          const typeNames =
            file === 'typeofGccNames.cpp' ? ['__typeof_unqual', '__typeof_unqual__'] : ['typeof', 'typeof_unqual'];
          const conversions = tree.rootNode.descendantsOfType('operator_cast');
          assert.deepEqual(
            conversions.map((node) => node.childForFieldName('type')?.text),
            typeNames
          );
          assert.ok(
            conversions.every(
              (node) =>
                node.childForFieldName('type')?.type === 'type_identifier' &&
                node.childForFieldName('declarator')?.type === 'abstract_function_declarator'
            )
          );
          const operations = tree.rootNode
            .descendantsOfType('function_definition')
            .find(
              (node) => node.childForFieldName('declarator')?.childForFieldName('declarator')?.text === 'operations'
            );
          assert.ok(operations);
          const binaries = operations.descendantsOfType('binary_expression');
          assert.deepEqual(
            binaries.map((node) => node.childForFieldName('operator')?.text),
            ['*', '&', '*', '&']
          );
          assert.deepEqual(
            binaries.map((node) => node.childForFieldName('left')?.childForFieldName('function')?.text),
            [typeNames[0], typeNames[0], typeNames[1], typeNames[1]]
          );
          assert.ok(
            binaries.every(
              (node) =>
                node.parent?.type === 'expression_statement' &&
                node.childForFieldName('left')?.type === 'call_expression' &&
                node.childForFieldName('right')?.text === 'y' &&
                source.slice(node.startIndex, node.endIndex) === node.text
            )
          );
          assert.ok(typeNames.every((name) => names.has(name)) && names.has('operations'));
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
        const marker =
          file === 'typeofCpp.cpp'
            ? '__typeof__(source) value'
            : file === 'typeofNames.cpp'
              ? 'operator typeof() const noexcept'
              : '__typeof_unqual(x) * y';
        const prefix =
          file === 'typeofCpp.cpp'
            ? '__typeof__('
            : file === 'typeofNames.cpp'
              ? 'operator typeof() const'
              : '__typeof_unqual(x) ';
        const start = source.indexOf(marker) + prefix.length;
        const old = file === 'typeofCpp.cpp' ? 'source' : file === 'typeofNames.cpp' ? ' noexcept' : '*';
        const replacement = file === 'typeofCpp.cpp' ? 'source + 1' : file === 'typeofNames.cpp' ? '' : '&';
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
    } finally {
      parser.delete();
    }
  }
);
