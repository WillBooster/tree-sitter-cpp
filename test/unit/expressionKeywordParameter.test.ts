import { expect, test } from 'vitest';
import fs from 'node:fs';
import path from 'node:path';

interface Rule {
  type: string;
  name?: string;
  value?: string;
  members?: Rule[];
  content?: Rule;
}

const Grammar = JSON.parse(fs.readFileSync(path.join(import.meta.dirname, '../../src/grammar.json'), 'utf8')) as {
  rules: Record<string, Rule>;
};

// A keyword that can start an expression but not a parameter lexes as a type name in a parameter list, where it would
// let the function reading of `long(n)(sizeof(b));` win; `expression_keyword_parameter` must expect every such keyword.
test('expects every keyword that starts an expression but not a parameter', () => {
  const expected = [...firstWords('expression')].filter((word) => !ParameterWords.has(word)).toSorted();
  const offered = firstWords('expression_keyword_parameter');
  expect(expected.filter((word) => !offered.has(word))).toEqual([]);
});

// `Foo* p(nullptr);` reads as a variable because its argument starts with a keyword that no parameter starts with; a
// keyword that can start a parameter, such as `__extension__`, would turn `Foo* p(__extension__ T);` into a variable.
// `this` starts an explicit object parameter only when a declaration follows it, which no keyword-led argument has.
test('starts the keyword-led arguments only with keywords that no parameter starts with', () => {
  const words = [...firstWords('_keyword_led_expression')].filter((word) => word !== 'this');
  expect(words.filter((word) => ParameterWords.has(word))).toEqual([]);
});

const ParameterWords = new Set(
  [
    'parameter_declaration',
    'optional_parameter_declaration',
    'explicit_object_parameter_declaration',
    'variadic_parameter_declaration',
  ].flatMap((name) => [...firstWords(name)])
);

function firstWords(name: string): Set<string> {
  // `first()` must treat undefined names as empty because external tokens have no rule, but a renamed rule queried
  // here would otherwise turn these tests into vacuous passes.
  if (!Grammar.rules[name]) throw new Error(`grammar.json has no rule "${name}"`);
  const { words } = first({ type: 'SYMBOL', name }, new Map(), new Set());
  return new Set([...words].filter((word) => /^[A-Za-z_]\w*$/.test(word)));
}

function first(
  rule: Rule,
  memo: Map<string, { words: Set<string>; nullable: boolean }>,
  visiting: Set<string>
): { words: Set<string>; nullable: boolean } {
  switch (rule.type) {
    case 'BLANK': {
      return { words: new Set(), nullable: true };
    }
    case 'STRING': {
      return { words: new Set([rule.value ?? '']), nullable: false };
    }
    case 'PATTERN': {
      return { words: new Set(), nullable: false };
    }
    case 'SYMBOL': {
      const name = rule.name ?? '';
      const cached = memo.get(name);
      if (cached) return cached;
      const definition = Grammar.rules[name];
      if (!definition || visiting.has(name)) return { words: new Set(), nullable: false };
      visiting.add(name);
      const result = first(definition, memo, visiting);
      visiting.delete(name);
      memo.set(name, result);
      return result;
    }
    case 'SEQ': {
      const words = new Set<string>();
      for (const member of rule.members ?? []) {
        const result = first(member, memo, visiting);
        for (const word of result.words) words.add(word);
        if (!result.nullable) return { words, nullable: false };
      }
      return { words, nullable: true };
    }
    case 'CHOICE': {
      const words = new Set<string>();
      let nullable = false;
      for (const member of rule.members ?? []) {
        const result = first(member, memo, visiting);
        for (const word of result.words) words.add(word);
        nullable ||= result.nullable;
      }
      return { words, nullable };
    }
    case 'REPEAT': {
      return { ...first(rule.content as Rule, memo, visiting), nullable: true };
    }
    default: {
      return first(rule.content as Rule, memo, visiting);
    }
  }
}
