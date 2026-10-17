// The seam tests (wiki/plans/Release.md R0.5): the production UI in headless
// Chromium against the real plugin layer (SeamHost). Each scenario gets a
// fresh host, a fresh page and an empty sample library.
//
//   node tests-seam/run.mjs [--host <SeamHost>] [--dist <ui/dist>] [--only <name>]
//
// Needs Playwright with Chromium (npm i -D playwright in tests-seam/, or a
// global install). Exit code 0 only when every scenario passed.
import { spawn } from 'node:child_process'
import { createRequire } from 'node:module'
import { mkdtempSync, rmSync, existsSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { scenarios } from './scenarios.mjs'

const require = createRequire(import.meta.url)
const { chromium } = require('playwright')

const here = path.dirname(fileURLToPath(import.meta.url))
const root = path.resolve(here, '..')
const args = process.argv.slice(2)
const arg = (name, fallback) => {
  const i = args.indexOf(name)
  return i >= 0 ? args[i + 1] : fallback
}
const exe = process.platform === 'win32' ? 'SeamHost.exe' : 'SeamHost'
const hostCandidates = [
  path.join(root, 'build/tests-seam/SeamHost_artefacts/Debug', exe),
  path.join(root, 'build/tests-seam/SeamHost_artefacts', exe),
  path.join(root, 'build/tests-seam/SeamHost_artefacts/Release', exe),
]
const hostPath = arg('--host', hostCandidates.find((p) => existsSync(p)))
const distPath = arg('--dist', path.join(root, 'ui/dist'))
const only = arg('--only', null)

if (!hostPath || !existsSync(hostPath)) {
  console.error('seam: no SeamHost found; build it (cmake --build build --target SeamHost) or pass --host')
  process.exit(2)
}

function startHost(port) {
  const home = mkdtempSync(path.join(tmpdir(), 'bazalt-seam-'))
  const child = spawn(hostPath, [distPath, String(port)], {
    env: { ...process.env, HOME: home, APPDATA: home, XDG_CONFIG_HOME: path.join(home, '.config') },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  let log = ''
  child.stdout.on('data', (d) => (log += d))
  child.stderr.on('data', (d) => (log += d))
  const ready = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('host did not start:\n' + log)), 20000)
    child.stdout.on('data', () => {
      if (log.includes('seam: serving')) {
        clearTimeout(timer)
        resolve()
      }
    })
    child.on('exit', (code) => reject(new Error(`host exited (${code}):\n` + log)))
  })
  const stop = async () => {
    if (child.exitCode === null) {
      try {
        await fetch(`http://127.0.0.1:${port}/__seam/quit`, { method: 'POST' })
      } catch {}
      await new Promise((resolve) => {
        const t = setTimeout(() => (child.kill('SIGKILL'), resolve()), 5000)
        child.on('exit', () => (clearTimeout(t), resolve()))
      })
    }
    rmSync(home, { recursive: true, force: true })
  }
  return { ready, stop, log: () => log, child }
}

/** What a scenario gets: the page, and ways to look at the engine. */
function seamApi(page, base) {
  const api = {
    page,
    base,
    invoke: (name, ...params) => page.evaluate(([n, p]) => window.__seamInvoke(n, ...p), [name, params]),
    async snapshot() {
      return JSON.parse(await api.invoke('graphGetSnapshot'))
    },
    async nodeIds() {
      return page.$$eval('[data-node-id]', (els) => [...new Set(els.map((e) => e.dataset.nodeId))])
    },
    async note(note, on = true, velocity = 1) {
      await fetch(`${base}/__seam/note`, { method: 'POST', body: JSON.stringify({ note, on, velocity }) })
    },
    async level() {
      return Number(await (await fetch(`${base}/__seam/level`)).text())
    },
    /** Waits for the engine's output peak to satisfy `test` (it updates every ~100 ms). */
    async waitForLevel(test, what, timeoutMs = 3000) {
      const end = Date.now() + timeoutMs
      let last = 0
      while (Date.now() < end) {
        last = await api.level()
        if (test(last)) return last
        await page.waitForTimeout(100)
      }
      throw new Error(`output level ${last} never became ${what}`)
    },
    /** Shift+A at (x, y), type `query`, Enter, click to place. Returns the new node's id. */
    async addNode(query, x, y) {
      const before = new Set(await api.nodeIds())
      await page.mouse.move(x, y)
      await page.keyboard.press('Shift+A')
      await page.locator('.add-menu-search').fill(query)
      await page.keyboard.press('Enter')
      await page.mouse.move(x + 5, y + 5)
      await page.mouse.move(x, y)
      await page.mouse.click(x, y)
      for (let i = 0; i < 50; i++) {
        const added = (await api.nodeIds()).filter((id) => !before.has(id))
        if (added.length > 0) return added.length === 1 ? added[0] : added
        await page.waitForTimeout(50)
      }
      throw new Error(`placing "${query}" added no node`)
    },
    port(nodeId, portId, direction) {
      return page.locator(`[data-port-anchor][data-node-id="${nodeId}"][data-port-id="${portId}"][data-direction="${direction}"]`).first()
    },
    /** Drags a cable from an output port to an input port, as a person would. */
    async connect(fromNode, fromPort, toNode, toPort) {
      const from = await api.port(fromNode, fromPort, 'output').boundingBox()
      const to = await api.port(toNode, toPort, 'input').boundingBox()
      if (!from || !to) throw new Error(`port not on screen: ${fromNode}.${fromPort} or ${toNode}.${toPort}`)
      await page.mouse.move(from.x + from.width / 2, from.y + from.height / 2)
      await page.mouse.down()
      await page.mouse.move((from.x + to.x) / 2, (from.y + to.y) / 2, { steps: 5 })
      await page.mouse.move(to.x + to.width / 2, to.y + to.height / 2, { steps: 5 })
      await page.mouse.up()
      await page.waitForTimeout(150)
    },
    async settle() {
      await page.evaluate(() => window.__seamPending())
      await page.waitForTimeout(150)
    },
  }
  return api
}

function expect(condition, message) {
  if (!condition) throw new Error(message)
}

const selected = scenarios.filter((s) => !only || s.name.includes(only))
const browser = await chromium.launch()
let failures = 0
let port = 5190
for (const scenario of selected) {
  const host = startHost(port)
  const base = `http://127.0.0.1:${port}`
  port++
  const pageErrors = []
  const started = Date.now()
  let page
  try {
    await host.ready
    page = await browser.newPage({ viewport: { width: 1400, height: 900 } })
    // Only the host: fonts and anything else external stay out.
    await page.route('**/*', (route) => (route.request().url().startsWith(base) ? route.continue() : route.abort()))
    page.on('pageerror', (e) => pageErrors.push(e.message))
    page.on('console', (m) => {
      if (m.type() === 'error' && !m.text().startsWith('Failed to load resource')) pageErrors.push(m.text())
    })
    await page.goto(base + '/')
    await page.locator('[data-node-id]').first().waitFor({ timeout: 10000 })
    await scenario.run(seamApi(page, base), expect)
    expect(pageErrors.length === 0, 'page errors:\n  ' + pageErrors.join('\n  '))
    console.log(`  ok    ${scenario.name} (${Date.now() - started} ms)`)
  } catch (e) {
    failures++
    console.log(`  FAIL  ${scenario.name}\n        ${String(e.message ?? e).split('\n').join('\n        ')}`)
    if (page) await page.screenshot({ path: path.join(tmpdir(), `seam-${scenario.name}.png`) }).catch(() => {})
  } finally {
    await page?.close().catch(() => {})
    await host.stop()
  }
}
await browser.close()
console.log(failures === 0 ? `seam: all ${selected.length} passed` : `seam: ${failures} of ${selected.length} failed`)
process.exit(failures === 0 ? 0 : 1)
