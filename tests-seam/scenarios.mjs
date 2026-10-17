// Seam scenarios (wiki/plans/Release.md R0.5). Each one runs against a fresh
// SeamHost and page (run.mjs); `seam` is the helper API defined there. Add a
// scenario here whenever a bug turns up in the seam between the UI and C++.

/** The ValueSlider of one port's fallback value on a node card. */
function slider(seam, nodeId, portId) {
  return seam.page.locator(`.node-row:has([data-node-id="${nodeId}"][data-port-id="${portId}"]) .value-slider`).first()
}

/** Click a slider, type a value, Enter: the person's way to enter a value. */
async function typeValue(seam, nodeId, portId, text) {
  const s = slider(seam, nodeId, portId)
  await s.click()
  const input = s.locator('.value-slider-input')
  await input.waitFor()
  await input.fill(text)
  await input.press('Enter')
  await seam.settle()
}

function nodeOf(snapshot, id) {
  return snapshot.nodes.find((n) => n.id === id)
}

function hasConnection(snapshot, from, fromPort, to, toPort) {
  return snapshot.connections.some((c) => c.fromNodeId === from && c.fromPortId === fromPort && c.toNodeId === to && c.toPortId === toPort)
}

/** A Sine wired into Master Out, placed and wired through the UI. */
async function sineIntoMaster(seam) {
  const sine = await seam.addNode('Sine', 420, 300)
  await seam.connect(sine, 'out', 'masterOut', 'in')
  return sine
}

export const scenarios = [
  {
    name: 'boot: the page shows the engine graph and the Add menu lists real nodes',
    async run(seam, expect) {
      expect((await seam.nodeIds()).includes('masterOut'), 'no Master Out on the canvas')
      await seam.page.mouse.move(400, 300)
      await seam.page.keyboard.press('Shift+A')
      await seam.page.locator('.add-menu-search').fill('Sine')
      const rows = await seam.page.locator('.add-menu-results').innerText()
      expect(rows.includes('Sine'), 'the Add menu has no Sine: ' + rows.slice(0, 200))
      await seam.page.keyboard.press('Escape')
    },
  },
  {
    name: 'wire: a cable drawn in the UI reaches the engine and plays',
    async run(seam, expect) {
      const sine = await sineIntoMaster(seam)
      await seam.settle()
      const snap = await seam.snapshot()
      expect(nodeOf(snap, sine)?.type === 'source.sine', `the engine has no Sine '${sine}'`)
      expect(hasConnection(snap, sine, 'out', 'masterOut', 'in'), 'the cable never reached the engine')
      await seam.waitForLevel((l) => l > 0.05, 'audible')
    },
  },
  {
    name: 'typed value: what you type is what is stored and shown (R0.3)',
    async run(seam, expect) {
      const sine = await sineIntoMaster(seam)
      await typeValue(seam, sine, 'source.sine.frequency', '261.63')
      const raw = await seam.invoke('graphGetSnapshot')
      expect(raw.includes('"source.sine.frequency": 261.63') || raw.includes('"source.sine.frequency":261.63'),
             'stored as ' + (raw.match(/"source\.sine\.frequency":\s*[-0-9.e]+/) ?? ['nothing'])[0])
      const shown = await slider(seam, sine, 'source.sine.frequency').innerText()
      expect(shown.includes('261.63'), 'the slider shows ' + shown)
      await typeValue(seam, sine, 'source.sine.amplitude', '0.3')
      expect(nodeOf(await seam.snapshot(), sine).parameters['source.sine.amplitude'] === 0.3, 'amplitude 0.3 not stored as 0.3')
    },
  },
  {
    name: 'reload: the page reopened on the same engine shows the same patch',
    async run(seam, expect) {
      const sine = await sineIntoMaster(seam)
      await typeValue(seam, sine, 'source.sine.amplitude', '0.7')
      await seam.page.reload()
      await seam.page.locator(`[data-node-id="${sine}"]`).first().waitFor({ timeout: 10000 })
      const shown = await slider(seam, sine, 'source.sine.amplitude').innerText()
      expect(shown.includes('0.7'), 'after reload the slider shows ' + shown)
      expect(seam.page.locator('canvas').first() !== null, 'no canvas')
    },
  },
  {
    name: 'many edits: after a burst of commands the newest plan plays',
    async run(seam, expect) {
      const sine = await sineIntoMaster(seam)
      await seam.waitForLevel((l) => l > 0.05, 'audible')
      // No block in between on purpose: the swapper must never stall on a stale plan.
      const burst = []
      for (let i = 0; i < 40; i++) burst.push(seam.invoke('graphSetParameterValue', sine, 'source.sine.amplitude', i % 2 ? 1 : 0.5))
      burst.push(seam.invoke('graphSetParameterValue', sine, 'source.sine.amplitude', 0))
      const results = await Promise.all(burst)
      expect(results.every((r) => r.success), 'a command failed: ' + JSON.stringify(results.find((r) => !r.success)))
      await seam.waitForLevel((l) => l < 0.001, 'silent')
      await seam.invoke('graphSetParameterValue', sine, 'source.sine.amplitude', 1)
      await seam.waitForLevel((l) => l > 0.05, 'audible again')
    },
  },
  {
    name: 'nested populations: a cable from one Population into another is allowed',
    async run(seam, expect) {
      const outer = await seam.addNode('Population', 300, 250)
      const inner = await seam.addNode('Population', 700, 450)
      await seam.connect(outer, 'pitch', inner, 'life.population.pitch')
      await seam.settle()
      expect(hasConnection(await seam.snapshot(), outer, 'pitch', inner, 'life.population.pitch'),
             'the editor refused (or never sent) the cable')
    },
  },
  {
    name: 'sample drop: a WAV dropped on the canvas becomes a Sample node',
    async run(seam, expect) {
      const before = new Set(await seam.nodeIds())
      await seam.page.evaluate(() => {
        // 0.25 s of a 440 Hz sine, 16-bit mono WAV.
        const rate = 44100, frames = rate / 4
        const bytes = new DataView(new ArrayBuffer(44 + frames * 2))
        const text = (offset, s) => [...s].forEach((c, i) => bytes.setUint8(offset + i, c.charCodeAt(0)))
        text(0, 'RIFF'); bytes.setUint32(4, 36 + frames * 2, true); text(8, 'WAVE')
        text(12, 'fmt '); bytes.setUint32(16, 16, true); bytes.setUint16(20, 1, true); bytes.setUint16(22, 1, true)
        bytes.setUint32(24, rate, true); bytes.setUint32(28, rate * 2, true); bytes.setUint16(32, 2, true); bytes.setUint16(34, 16, true)
        text(36, 'data'); bytes.setUint32(40, frames * 2, true)
        for (let i = 0; i < frames; i++) bytes.setInt16(44 + i * 2, Math.round(16000 * Math.sin((2 * Math.PI * 440 * i) / rate)), true)
        const file = new File([bytes.buffer], 'tone.wav', { type: 'audio/wav' })
        const transfer = new DataTransfer()
        transfer.items.add(file)
        const target = document.elementFromPoint(500, 600)
        for (const type of ['dragenter', 'dragover', 'drop'])
          target.dispatchEvent(new DragEvent(type, { bubbles: true, cancelable: true, clientX: 500, clientY: 600, dataTransfer: transfer }))
      })
      let added = []
      for (let i = 0; i < 60 && added.length === 0; i++) {
        await seam.page.waitForTimeout(100)
        added = (await seam.nodeIds()).filter((id) => !before.has(id))
      }
      expect(added.length === 1, 'the drop added ' + added.length + ' nodes')
      const node = nodeOf(await seam.snapshot(), added[0])
      expect(node?.type === 'data.sample', 'the dropped file became ' + node?.type)
    },
  },
]
