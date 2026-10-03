const firstLine = (s) => String(s ?? '').split('\n')[0].trim();
const baseName = (p) => String(p ?? '').split(/[\\/]/).filter(Boolean).pop() ?? '';
// Claude in Chrome's tools (navigate, computer, read_page...) all drive the browser.
const BROWSER_TOOL = 'mcp__claude-in-chrome__';

export function describeTool(toolName, toolInput = {}) {
  const name = String(toolName ?? '');
  const input = toolInput ?? {};
  const tool = name.startsWith('mcp__') ? name.split('__').slice(2).join('__') || name : name;
  let det = '';
  switch (name) {
    case 'Bash':
      det = firstLine(input.command);
      break;
    case 'Edit':
    case 'Write':
    case 'Read':
    case 'NotebookEdit':
      det = baseName(input.file_path ?? input.notebook_path);
      break;
    case 'Grep':
    case 'Glob':
      det = firstLine(input.pattern);
      break;
    case 'WebFetch':
      try {
        det = new URL(input.url).host;
      } catch {
        det = '';
      }
      break;
    case 'WebSearch':
      det = firstLine(input.query);
      break;
    case 'Agent':
    case 'Task':
      det = firstLine(input.description);
      break;
    default:
      if (name.startsWith(BROWSER_TOOL)) det = 'browsing';
  }
  return { tool, det };
}
