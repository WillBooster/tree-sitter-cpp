import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const parser = new Parser();
parser.setLanguage(await loadCurrentWasmBuild());

// Consumers parse files being edited, so recovering from many errors must stay linear: ten times the lines take about
// ten times as long, against a hundred times for quadratic recovery. The ratio catches a cost that grows faster than
// the input even on a slow CI runner; it would pass a parser that is uniformly slower, so the larger parse also has a
// generous ceiling, about 35 times the 0.06 s of CPU time it takes here. The parses are timed in the CPU time of the
// thread that runs them: wall-clock time is inflated unevenly by the test files running alongside, and the process's
// CPU time also counts the engine's background threads, which compile the Wasm build and collect garbage during the
// parses. 2,000 and 20,000 lines measured after warm-up parses and in alternation, each keeping its fastest run, give
// 10 to 13 locally; 18 leaves a margin over that and fails for growth faster than about n^1.25.
test('recovers from an error on each line in linear time', { timeout: 60_000 }, () => {
  expectLinearParseTime('$ a\n'.repeat(2000), '$ a\n'.repeat(20_000), true);
});

test.each(['plain', 'conditional'])('parses %s bare-marker chains in linear time', { timeout: 60_000 }, (context) => {
  const input = (count: number): string => {
    const body = 'marker\n'.repeat(count) + 'template<class T, class U> class Channel {};\n';
    return (
      '#define marker\n' +
      (context === 'conditional' ? '#if defined(ACTIVE)\nmarker\n#include "marker.h"\n' + body + '#endif\n' : body)
    );
  };
  expectLinearParseTime(input(2000), input(20_000), false);
});

function expectLinearParseTime(small: string, large: string, hasError: boolean): void {
  parseCpuTime(large, hasError);
  parseCpuTime(large, hasError);
  let smallFastest = Infinity;
  let largeFastest = Infinity;
  for (let run = 0; run < 5; run++) {
    smallFastest = Math.min(smallFastest, parseCpuTime(small, hasError));
    largeFastest = Math.min(largeFastest, parseCpuTime(large, hasError));
  }
  expect(largeFastest / smallFastest).toBeLessThan(18);
  // process.threadCpuUsage reports microseconds.
  expect(largeFastest).toBeLessThan(2_000_000);
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
