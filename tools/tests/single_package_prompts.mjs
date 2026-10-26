// Harness for tools/workflows/single-package.js (IW-2 test): runs the workflow script with a stub agent() and prints the
// prompts it would send, as JSON. Usage: node single_package_prompts.mjs <brief.json> [<brief.json> ...]
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'

const here = dirname(fileURLToPath(import.meta.url))
const src = readFileSync(join(here, '..', 'workflows', 'single-package.js'), 'utf8').replace(/^export const meta/m, 'const meta')
const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor
const run = new AsyncFunction('args', 'agent', 'phase', 'log', 'parallel', src)

const out = []
for (const path of process.argv.slice(2)) {
  const args = JSON.parse(readFileSync(path, 'utf8'))
  const prompts = []
  const agent = async (prompt, opts) => {
    prompts.push({ prompt, model: opts.model || null, schema_keys: Object.keys(opts.schema.properties) })
    return { done: true, commits: [], ci: 'not pushed', summary: 'stub', not_done: [] }
  }
  await run(args, agent, () => {}, () => {}, async (fs) => Promise.all(fs.map(f => f())))
  out.push({ brief: path, prompts })
}
process.stdout.write(JSON.stringify(out))
