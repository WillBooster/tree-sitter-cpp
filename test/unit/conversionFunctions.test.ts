import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import { type Node, Parser, type Tree } from '@willbooster/web-tree-sitter';
import { test } from 'vitest';

import { fieldChild, pointAt } from '../helpers/ast.js';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.resolve(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/conversionFunctions.cpp'), 'utf8');
await Parser.init();

test('parses conversion declarations', async () => {
  const parser = new Parser();
  let tree: Tree | undefined;
  try {
    const language = await loadCurrentWasmBuild();
    parser.setLanguage(language);
    const minimal = parser.parse('struct A { operator int(); }; inline A::operator int() { return 42; }')!;
    try {
      assert.equal(minimal.rootNode.hasError, false);
      const definitions = conversionDefinitions(minimal.rootNode);
      assert.equal(definitions.length, 1);
      assert.equal(definitions[0]!.childForFieldName('declarator')?.childForFieldName('name')?.type, 'operator_cast');
      assert.equal(definitions[0]!.childForFieldName('type') ?? undefined, undefined);
    } finally {
      minimal.delete();
    }
    tree = parser.parse(Source)!;
    assert.equal(tree.rootNode.hasError, false, tree.rootNode.toString());
    const definitions = conversionDefinitions(tree.rootNode);
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
        [pointAt(Source, declarator.startIndex), pointAt(Source, declarator.endIndex)]
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
    parser.delete();
  }
});

function conversionDefinitions(root: Node): Node[] {
  return root.descendantsOfType('function_definition').filter((node) => {
    const declarator = fieldChild(node, 'declarator');
    return (
      declarator?.type === 'operator_cast' ||
      (declarator?.type === 'qualified_identifier' && fieldChild(declarator, 'name')?.type === 'operator_cast')
    );
  });
}
