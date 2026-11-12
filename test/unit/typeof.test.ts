import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import { type Language, type Node, Parser, type Tree } from '@willbooster/web-tree-sitter';
import { beforeAll, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const root = path.resolve(import.meta.dirname, '../..');
let language: Language;

beforeAll(async () => {
  await Parser.init();
  language = await loadCurrentWasmBuild();
}, 30_000);

test.each(['typeofCpp.cpp', 'typeofNames.cpp', 'typeofGccNames.cpp'] as const)(
  'preserves typeof type fields and contextual names (%s)',
  (file) => {
    const parser = new Parser();
    try {
      parser.setLanguage(language);
      const source = fs.readFileSync(path.join(root, 'test/fixtures', file), 'utf8');
      let tree: Tree | undefined;
      let changed: Tree | undefined;
      try {
        tree = parser.parse(source) ?? undefined;
        assert.ok(tree);
        assert.equal(tree.rootNode.hasError, false, tree.rootNode.toString());
        if (file === 'typeofCpp.cpp') {
          assertTypeofSpecifiers(tree.rootNode, source);
        } else {
          assertContextualTypeofNames(tree.rootNode, source, file);
        }
        const [before, after] = replacementForFixture(file);
        const start = source.indexOf(before);
        changed = parser.parse(source.slice(0, start) + after + source.slice(start + before.length)) ?? undefined;
        assert.ok(changed);
        assert.equal(changed.rootNode.hasError, false);
      } finally {
        tree?.delete();
        changed?.delete();
      }
    } finally {
      parser.delete();
    }
  }
);

function assertTypeofSpecifiers(rootNode: Node, source: string): void {
  const specifiers = rootNode.descendantsOfType('typeof_specifier');
  assert.equal(specifiers.length, 11);
  assert.ok(specifiers.every((node) => source.slice(node.startIndex, node.endIndex) === node.text));
  const pointer = rootNode
    .descendantsOfType('declaration')
    .find((node) => node.childForFieldName('declarator')?.childForFieldName('declarator')?.text === 'pointer');
  assert.equal(pointer?.childForFieldName('type')?.type, 'typeof_specifier');
  assert.equal(pointer?.childForFieldName('type')?.text, '__typeof__(int*)');
  const member = rootNode
    .descendantsOfType('field_declaration')
    .find((node) => node.childForFieldName('declarator')?.text === 'member');
  assert.equal(member?.childForFieldName('type')?.text, '__typeof__(source)');
  const cast = rootNode.descendantsOfType('cast_expression')[0];
  assert.equal(cast?.childForFieldName('type')?.childForFieldName('type')?.type, 'typeof_specifier');
}

function assertContextualTypeofNames(
  rootNode: Node,
  source: string,
  file: 'typeofNames.cpp' | 'typeofGccNames.cpp'
): void {
  assert.equal(rootNode.descendantsOfType('typeof_specifier').length, 0);
  const declaration = rootNode
    .descendantsOfType('declaration')
    .find(
      (node) =>
        node.childForFieldName('declarator')?.childForFieldName('declarator')?.childForFieldName('declarator')?.text ===
        'pointer'
    );
  const typeNames =
    file === 'typeofGccNames.cpp'
      ? (['__typeof_unqual', '__typeof_unqual__'] as const)
      : (['typeof', 'typeof_unqual'] as const);
  assert.equal(declaration?.childForFieldName('type')?.type, 'type_identifier');
  assert.equal(declaration?.childForFieldName('type')?.text, typeNames[0]);
  const conversions = rootNode.descendantsOfType('operator_cast');
  assert.deepEqual(
    conversions.map((node) => node.childForFieldName('type')?.text),
    [...typeNames]
  );
  assert.ok(
    conversions.every(
      (node) =>
        node.childForFieldName('type')?.type === 'type_identifier' &&
        node.childForFieldName('declarator')?.type === 'abstract_function_declarator'
    )
  );
  assertContextualOperations(rootNode, source, typeNames);
  if (file === 'typeofGccNames.cpp') {
    assertGccUnqualStatements(rootNode);
  }
}

function assertContextualOperations(rootNode: Node, source: string, typeNames: readonly [string, string]): void {
  const operations = rootNode
    .descendantsOfType('function_definition')
    .find((node) => node.childForFieldName('declarator')?.childForFieldName('declarator')?.text === 'operations');
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
}

function assertGccUnqualStatements(rootNode: Node): void {
  assert.deepEqual(
    rootNode.descendantsOfType('statement_identifier').map((node) => node.text),
    ['__typeof_unqual', '__typeof_unqual']
  );
  const counter = rootNode
    .descendantsOfType('assignment_expression')
    .find((node) => node.childForFieldName('left')?.text === '__typeof_unqual');
  assert.equal(counter?.childForFieldName('left')?.type, 'identifier');
  assert.equal(counter?.childForFieldName('right')?.text, '1');
}

function replacementForFixture(file: 'typeofCpp.cpp' | 'typeofNames.cpp' | 'typeofGccNames.cpp'): [string, string] {
  if (file === 'typeofCpp.cpp') return ['__typeof__(source) value', '__typeof__(source + 1) value'];
  if (file === 'typeofNames.cpp') return ['operator typeof() const noexcept', 'operator typeof() const'];
  return ['__typeof_unqual(x) * y', '__typeof_unqual(x) & y'];
}
