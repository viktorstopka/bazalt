// Growable port groups (SIGNAL_TYPES.md §6) — the UI half of
// engine/include/bazalt/engine/graph/PortGroups.h, which explains the whole
// mechanism. In short: a node like math.add declares its inputs as a group
// (`in.0`, `in.1`, ...) and its default descriptor lists only the minimum
// (two). The engine derives how many ports a placed node really has from its
// connections, so the UI must (a) reveal a fresh empty "spare" port once the
// last one is wired, so there is somewhere to drop the next cable, and (b)
// resolve a port id that the default descriptor doesn't list (the spare) back
// to a real descriptor, for wire-drag hit testing and canConnect prediction.
//
// Port ids are `idPrefix + index` with the index in canonical decimal — the
// same rule the engine enforces, so an id names exactly one port and
// "in.01" never resolves here while being rejected there.
import type { NodeDescriptor, PortDescriptor, PortGroup } from './descriptorTypes'

/** Mirrors PortGroups.h's parsePortGroupIndex: the index after `idPrefix`, or -1. */
export function parseGroupIndex(portId: string, idPrefix: string): number {
  if (!idPrefix || !portId.startsWith(idPrefix)) return -1
  const suffix = portId.slice(idPrefix.length)
  if (suffix.length === 0 || suffix.length > 4 || !/^[0-9]+$/.test(suffix)) return -1
  const index = Number(suffix)
  return String(index) === suffix ? index : -1 // rejects "01"
}

function groupsOf(inputs: readonly PortDescriptor[]): PortGroup[] {
  const groups: PortGroup[] = []
  for (const port of inputs) {
    if (port.group && !groups.some((g) => g.idPrefix === port.group!.idPrefix)) groups.push(port.group)
  }
  return groups
}

/** "In 1" -> "In 4" for index 3; a single letter continues the alphabet ("A" -> "D",
    logic.switch); any other label with no trailing number gets one appended. */
function relabel(templateLabel: string, index: number): string {
  if (/^[A-Z]$/.test(templateLabel)) return String.fromCharCode(templateLabel.charCodeAt(0) + index)
  const human = String(index + 1)
  return /\d+$/.test(templateLabel) ? templateLabel.replace(/\d+$/, human) : `${templateLabel} ${human}`.trim()
}

/** The index-0 port of each group, in declaration order — the pattern one
    "row" of the group repeats (mix.sum's `in.0` then its `level.0`).
*/
function rowTemplates(inputs: readonly PortDescriptor[]): PortDescriptor[] {
  return inputs.filter((p) => p.group && parseGroupIndex(p.id, p.group.idPrefix) === 0)
}

function synthesize(template: PortDescriptor, index: number): PortDescriptor {
  const group = template.group!
  return { ...template, id: group.idPrefix + index, label: relabel(template.label, index) }
}

/** A real descriptor for `portId` if it names a member of one of the node's
    input groups within [0, maxCount) — including members the default
    descriptor doesn't list. Undefined otherwise.
*/
export function synthesizeGroupPort(descriptor: Pick<NodeDescriptor, 'inputs'>, portId: string): PortDescriptor | undefined {
  for (const template of rowTemplates(descriptor.inputs)) {
    const group = template.group!
    const index = parseGroupIndex(portId, group.idPrefix)
    if (index >= 0 && index < group.maxCount) return synthesize(template, index)
  }
  return undefined
}

/** `descriptor` with its growable groups sized for a placed instance: every
    index up to the highest connected one, plus one spare for the next cable
    (capped at the group's maximum, never below its minimum). A hole — an
    unconnected index between wired ones, left by a removed cable — is shown as
    an ordinary empty port. Returns `descriptor` itself when it has no groups.
*/
export function withRevealedGroupPorts(descriptor: NodeDescriptor, connectedPortIds: ReadonlySet<string> | undefined): NodeDescriptor {
  const groups = groupsOf(descriptor.inputs)
  if (groups.length === 0) return descriptor

  const templates = rowTemplates(descriptor.inputs)
  if (templates.length === 0) return descriptor

  // One shared index range across every group on the node (mix.sum's `in.N`
  // and `level.N` grow together), so a cable on any of them counts.
  const { minCount, maxCount, autoRevealOnLastConnected } = groups[0]
  let highest = -1
  for (const id of connectedPortIds ?? []) {
    for (const group of groups) highest = Math.max(highest, parseGroupIndex(id, group.idPrefix))
  }

  const spare = autoRevealOnLastConnected ? highest + 2 : highest + 1
  const visibleCount = Math.min(maxCount, Math.max(minCount, spare))

  const fixed = descriptor.inputs.filter((p) => !p.group)
  const grouped: PortDescriptor[] = []
  for (let index = 0; index < visibleCount; index++) {
    for (const template of templates) grouped.push(synthesize(template, index))
  }

  return { ...descriptor, inputs: [...fixed, ...grouped] }
}
