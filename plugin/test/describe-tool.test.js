import { test } from 'node:test';
import assert from 'node:assert/strict';
import { describeTool } from '../lib/describe-tool.js';

const cases = [
  ['Bash', { command: 'npm test\necho done' }, { tool: 'Bash', det: 'npm test' }],
  ['Edit', { file_path: '/repo/src/Header.tsx' }, { tool: 'Edit', det: 'Header.tsx' }],
  ['Write', { file_path: 'C:\\repo\\a.txt' }, { tool: 'Write', det: 'a.txt' }],
  ['Read', { file_path: '/x/y.md' }, { tool: 'Read', det: 'y.md' }],
  ['NotebookEdit', { notebook_path: '/n/b.ipynb' }, { tool: 'NotebookEdit', det: 'b.ipynb' }],
  ['Grep', { pattern: 'TODO' }, { tool: 'Grep', det: 'TODO' }],
  ['Glob', { pattern: '**/*.ts' }, { tool: 'Glob', det: '**/*.ts' }],
  ['WebFetch', { url: 'https://docs.example.com/a?b=1' }, { tool: 'WebFetch', det: 'docs.example.com' }],
  ['WebFetch', { url: 'not a url' }, { tool: 'WebFetch', det: '' }],
  ['WebSearch', { query: 'esp8266 st7789' }, { tool: 'WebSearch', det: 'esp8266 st7789' }],
  ['Agent', { description: 'Find usages' }, { tool: 'Agent', det: 'Find usages' }],
  ['Task', { description: 'Refactor' }, { tool: 'Task', det: 'Refactor' }],
  ['mcp__github__create_issue', { title: 'x' }, { tool: 'create_issue', det: '' }],
  ['SomethingNew', undefined, { tool: 'SomethingNew', det: '' }],
  [undefined, undefined, { tool: '', det: '' }],
];

for (const [name, input, expected] of cases) {
  test(`describeTool(${name})`, () => {
    assert.deepEqual(describeTool(name, input), expected);
  });
}
