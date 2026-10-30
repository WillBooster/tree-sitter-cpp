import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.resolve(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/conversionFunctions.cpp'), 'utf8');
await Parser.init();

test('parses conversion declarations', async () => {
  const parser = new Parser();
  let query: Query | undefined;
  let tree: Tree | undefined;
  try {
    const language = await loadCurrentWasmBuild();
    parser.setLanguage(language);
    query = new Query(
      language,
      '(function_definition declarator: (qualified_identifier name: (operator_cast) @cast)) @definition (function_definition declarator: (operator_cast) @cast) @definition'
    );
    const minimal = parser.parse('struct A { operator int(); }; inline A::operator int() { return 42; }')!;
    try {
      assert.equal(minimal.rootNode.hasError, false);
      const definitions = query.captures(minimal.rootNode).filter((c) => c.name === 'definition');
      assert.equal(definitions.length, 1);
      assert.equal(
        definitions[0]!.node.childForFieldName('declarator')?.childForFieldName('name')?.type,
        'operator_cast'
      );
      assert.equal(definitions[0]!.node.childForFieldName('type') ?? undefined, undefined);
    } finally {
      minimal.delete();
    }
    tree = parser.parse(Source)!;
    assert.equal(tree.rootNode.hasError, false, tree.rootNode.toString());
    const definitions = query
      .captures(tree.rootNode)
      .filter((c) => c.name === 'definition')
      .map((c) => c.node);
    assert.equal(definitions.length, 16);
    for (const definition of definitions) {
      assert.equal(definition.childForFieldName('type') ?? undefined, undefined);
      const declarator = definition.childForFieldName('declarator')!;
      const cast = declarator.type === 'operator_cast' ? declarator : declarator.childForFieldName('name')!;
      assert.equal(cast.type, 'operator_cast');
      assert.ok(cast.childForFieldName('type'));
      assert.ok(cast.childForFieldName('declarator')?.descendantsOfType('parameter_list').length);
      assert.equal(Source.slice(declarator.startIndex, declarator.endIndex), declarator.text);
      assert.deepEqual(
        [declarator.startPosition, declarator.endPosition],
        [point(Source, declarator.startIndex), point(Source, declarator.endIndex)]
      );
    }
    const friends = tree.rootNode.descendantsOfType('friend_declaration');
    assert.equal(friends.length, 2);
    for (const friend of friends) {
      const declaration = friend.namedChildren[0]!;
      assert.equal(declaration.type, 'declaration');
      assert.equal(declaration.childForFieldName('type') ?? undefined, undefined);
      assert.equal(declaration.childForFieldName('declarator')?.childForFieldName('name')?.type, 'operator_cast');
    }
    const casts = tree.rootNode.descendantsOfType('operator_cast');
    assert.equal(casts.length, 30);
    for (const [before, after, hasError] of [
      ['inline Scalar::operator int() const', '[[nodiscard]] inline Scalar::operator int() const', false],
      ['friend Pointer::operator int*() const;', 'friend Pointer::operator int*() const', true],
    ] as const) {
      const start = Source.indexOf(before);
      assert.ok(start !== -1, before);
      const variant = parser.parse(Source.slice(0, start) + after + Source.slice(start + before.length))!;
      try {
        assert.equal(variant.rootNode.hasError, hasError);
      } finally {
        variant.delete();
      }
    }
  } finally {
    tree?.delete();
    query?.delete();
    parser.delete();
  }
});

function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}
