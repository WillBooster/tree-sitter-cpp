import { Buffer } from 'node:buffer';

import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const parser = new Parser();
parser.setLanguage(await loadCurrentWasmBuild());

// Consumers parse files being edited, so recovering from many errors must stay linear: ten times the lines take about
// ten times as long, against a hundred times for quadratic recovery. The ratio catches a cost that grows faster than
// the input even on a slow CI runner; it would pass a uniformly slower parser, so a throughput bound also limits
// CPU time to 25 microseconds per input byte: two seconds for the original 80 KB control.
// The parses are timed in the CPU time of their thread: wall-clock time is inflated unevenly by the test files running alongside, and the process's
// CPU time also counts the engine's background threads, which compile the Wasm build and collect garbage during the
// parses. 2,000 and 20,000 lines measured after warm-up parses and in alternation, each keeping its fastest run, give
// 10 to 13 locally; 18 leaves a margin over that and fails for growth faster than about n^1.25.
test('recovers from an error on each line in linear time', { timeout: 60_000 }, () => {
  expectLinearParseTime('$ a\n'.repeat(2000), '$ a\n'.repeat(20_000), true);
});

test.each(['plain', 'conditional', 'null-directive'])(
  'parses %s bare-marker chains in linear time',
  { timeout: 60_000 },
  (context) => {
    const input = (count: number): string => {
      const body =
        (context === 'null-directive' ? 'marker\n#\n' : 'marker\n').repeat(count) +
        'template<class T, class U> class Channel {};\n';
      return (
        '#define marker\n' +
        (context === 'conditional' ? '#if defined(ACTIVE)\nmarker\n#include "marker.h"\n' + body + '#endif\n' : body)
      );
    };
    expectLinearParseTime(input(2000), input(20_000), false);
  }
);

test(
  'parses null directives and comments between a declaration type and its declarator in linear time',
  { timeout: 60_000 },
  () => {
    // The declarations before the gap keep the small input above the CPU timer's millisecond resolution on CI.
    const [short, long] = [2000, 20_000].map(
      (count) => `${'int y;\n'.repeat(count)}using T = int;\nT\n${'#\n// note\n'.repeat(count)}x;\n`
    );
    expectLinearParseTime(short!, long!, false);
  }
);

test('parses conditional initializer branches in linear time', { timeout: 60_000 }, () => {
  const block = '{ int value =\n#if FEATURE\n#if EMPTY\n#endif\n1\n#else\n2\n#endif\n; }\n';
  const input = (count: number): string => `void conditional() {\n${block.repeat(count)}}`;
  expectLinearParseTime(input(2000), input(20_000), false);
});

test('recovers from malformed nested initializer alternatives in linear time', { timeout: 60_000 }, () => {
  expectLinearParseTime(malformedInitializerAlternatives(2000), malformedInitializerAlternatives(20_000), true);
});

test.each(['empty', 'value', 'padded-stride', 'padded-hash'])(
  'parses deeply nested %s initializer groups in linear time',
  { timeout: 60_000 },
  (kind) => {
    expectLinearParseTime(nestedInitializerGroups(kind, 2000), nestedInitializerGroups(kind, 20_000), false);
  }
);

function nestedInitializerGroups(kind: string, count: number): string {
  if (kind === 'empty' || kind === 'value') {
    return (
      'int value =\n#if A\n' +
      '#if E\n'.repeat(count) +
      (kind === 'value' ? '1\n' : '') +
      '#endif\n'.repeat(count) +
      '#else\n2\n#endif\n;'
    );
  }
  const capacity = 1 << Math.ceil(Math.log2(count + 1));
  const mask = capacity * 2 - 1;
  const parts = ['int value =\n#if A\n'];
  let length = parts[0]!.length;
  for (let i = 0; i < count; i++) {
    let pad = 0;
    if (kind === 'padded-stride') {
      pad = (64 - (length & 63)) & 63;
    } else {
      while (((Math.imul(length + pad, 0x9e_37_79_b1) ^ ((length + pad) >>> 16)) & mask) >= capacity >> 3) {
        pad++;
      }
    }
    const piece = ' '.repeat(pad) + '#if E\n';
    parts.push(piece);
    length += piece.length;
  }
  parts.push('#endif\n'.repeat(count), '#else\n2\n#endif\n;');
  return parts.join('');
}

test.each(['elif', 'elifdef', 'elifndef'])(
  'recovers from invalid #%s initializer bodies in linear time',
  { timeout: 60_000 },
  (directive) => {
    const input = (count: number): string =>
      'int value =\n#if A\n1\n' + `#${directive} B\na b\n`.repeat(count) + '#endif\n;';
    expectLinearParseTime(input(2000), input(20_000), true);
  }
);

function malformedInitializerAlternatives(count: number): string {
  return 'int value =\n#if A\n1\n' + '#if E\n#else\n#elif F\n'.repeat(count) + '#endif\n'.repeat(count) + '#endif\n;';
}

function expectLinearParseTime(small: string, large: string, hasError: boolean): void {
  parseCpuTime(large, hasError);
  parseCpuTime(large, hasError);
  let smallFastest = Infinity;
  let largeFastest = Infinity;
  for (let run = 0; run < 5; run++) {
    smallFastest = Math.min(smallFastest, parseCpuTime(small, hasError));
    largeFastest = Math.min(largeFastest, parseCpuTime(large, hasError));
  }
  const largeBytes = Buffer.byteLength(large);
  const timings = `small=${smallFastest}us large=${largeFastest}us, bytes=${Buffer.byteLength(small)}/${largeBytes}`;
  expect(largeFastest / smallFastest, timings).toBeLessThan(18);
  // process.threadCpuUsage reports microseconds.
  expect(largeFastest, timings).toBeLessThan(largeBytes * 25);
}

function parseCpuTime(source: string, expectedError: boolean): number {
  const start = process.threadCpuUsage();
  const tree = parser.parse(source);
  const { system, user } = process.threadCpuUsage(start);
  if (!tree) throw new Error('The parser returned no tree');
  const { hasError } = tree.rootNode;
  tree.delete();
  expect(hasError).toBe(expectedError);
  return system + user;
}
