/**
 * Cross-references inside a Blueprint document: who refers to an asset, asset type, signal,
 * event, data source, monitor, world object, state or label. Used for "Used by" panels and
 * to list dependencies before a destructive action. Purely a lookup over the document; the
 * backend validator remains the authority on validity.
 */
import type { BlueprintDocument } from '@/api/types';
import type { Dependency } from './ui';

const word = (s: string) => new RegExp(`(^|[^A-Za-z0-9_])${s.replace(/[.*+?^${}()|[\]\\!]/g, '\\$&')}([^A-Za-z0-9_]|$)`);

export function usedByAsset(doc: BlueprintDocument, id: string): Dependency[] {
  const out: Dependency[] = [];
  if (doc.structure.root === id) out.push({ what: 'Root asset of every instance', where: 'Structure', route: 'build/structure' });
  for (const a of doc.structure.assets) if (a.parent === id) out.push({ what: `${a.name} (child)`, where: 'Structure', route: `build/structure?asset=${encodeURIComponent(a.id)}` });
  for (const r of doc.structure.relationships) {
    if (r.source === id || r.target === id) out.push({ what: `${r.source} —${r.type}→ ${r.target}`, where: 'Relationships', route: 'build/structure?tab=relationships' });
  }
  for (const o of doc.world.objects) if (o.asset === id) out.push({ what: o.name || o.id, where: 'World & Layout', route: `build/world?object=${encodeURIComponent(o.id)}` });
  for (const t of doc.data.telemetry) if (t.asset === id) out.push({ what: t.label || t.id, where: 'Telemetry', route: `build/data?telemetry=${encodeURIComponent(t.id)}` });
  for (const e of doc.data.events) if (e.asset === id) out.push({ what: e.label || e.id, where: 'Events', route: `build/data?event=${encodeURIComponent(e.id)}` });
  for (const c of doc.data.commands) if (c.asset === id) out.push({ what: c.label || c.id, where: 'Commands', route: `build/data?command=${encodeURIComponent(c.id)}` });
  for (const p of doc.data.properties) if (p.asset === id) out.push({ what: p.label || p.id, where: 'Static properties', route: 'build/data?tab=properties' });
  if (doc.presentation.importantAssets?.includes(id)) out.push({ what: 'Important asset', where: 'Presentation', route: 'build/presentation' });
  return out;
}

export function usedByAssetType(doc: BlueprintDocument, id: string): Dependency[] {
  return doc.structure.assets.filter((a) => a.type === id).map((a) => ({ what: a.name || a.id, where: 'Assets', route: `build/structure?asset=${encodeURIComponent(a.id)}` }));
}

export function usedByTelemetry(doc: BlueprintDocument, id: string): Dependency[] {
  const out: Dependency[] = [];
  for (const b of doc.connectivity.bindings) if (b.target.kind === 'telemetry' && b.target.id === id) out.push({ what: `Binding ${b.id} ← ${b.source}`, where: 'Connectivity', route: `build/data?tab=connectivity&select=${encodeURIComponent(b.id)}` });
  if (doc.presentation.keyTelemetry?.includes(id)) out.push({ what: 'Key telemetry', where: 'Presentation', route: 'build/presentation' });
  for (const c of doc.presentation.charts ?? []) if (c.telemetry.includes(id)) out.push({ what: `Chart "${c.title}"`, where: 'Presentation', route: 'build/presentation' });
  for (const m of doc.assurance.monitors) if (m.field === id) out.push({ what: m.name || m.id, where: 'Monitors', route: `assurance/monitors?id=${encodeURIComponent(m.id)}` });
  for (const s of doc.scenarios) {
    if (s.steps.some((st) => st.telemetry && id in st.telemetry)) out.push({ what: s.name || s.id, where: 'Scenarios', route: `test/scenarios/${encodeURIComponent(s.id)}` });
  }
  return out;
}

export function usedByEvent(doc: BlueprintDocument, id: string): Dependency[] {
  const out: Dependency[] = [];
  for (const c of doc.data.commands) {
    if (c.acknowledgement?.event === id) out.push({ what: `${c.label || c.id} (acknowledgement)`, where: 'Commands', route: `build/data?command=${encodeURIComponent(c.id)}` });
    if (c.observedConsequence?.event === id) out.push({ what: `${c.label || c.id} (consequence)`, where: 'Commands', route: `build/data?command=${encodeURIComponent(c.id)}` });
  }
  for (const b of doc.connectivity.bindings) if (b.target.kind === 'event' && b.target.id === id) out.push({ what: `Binding ${b.id}`, where: 'Connectivity', route: 'build/data?tab=connectivity' });
  return out;
}

export function usedBySource(doc: BlueprintDocument, id: string): Dependency[] {
  return doc.connectivity.bindings
    .filter((b) => b.source === id)
    .map((b) => ({ what: `Binding ${b.id} → ${b.target.kind} ${b.target.id}`, where: 'Connectivity', route: `build/data?tab=connectivity&select=${encodeURIComponent(b.id)}` }));
}

export function usedByMonitor(doc: BlueprintDocument, id: string): Dependency[] {
  const out: Dependency[] = [];
  for (const r of doc.assurance.requirements) if (r.monitors.includes(id)) out.push({ what: `${r.id} ${r.title}`, where: 'Requirements', route: `assurance/requirements?id=${encodeURIComponent(r.id)}` });
  for (const a of doc.assurance.alerts) if (a.monitor === id) out.push({ what: `Alert ${a.id}: ${a.message}`, where: 'Alert policy', route: `assurance/monitors?id=${encodeURIComponent(id)}` });
  if (doc.presentation.importantMonitors?.includes(id)) out.push({ what: 'Important monitor', where: 'Presentation', route: 'build/presentation' });
  for (const s of doc.scenarios) {
    if (s.steps.some((st) => (st.expect as { monitor?: { id?: string } } | undefined)?.monitor?.id === id)) out.push({ what: s.name || s.id, where: 'Scenarios', route: `test/scenarios/${encodeURIComponent(s.id)}` });
  }
  return out;
}

export function usedByWorldObject(doc: BlueprintDocument, id: string): Dependency[] {
  const out: Dependency[] = [];
  for (const o of doc.world.objects) {
    if ((o.kind === 'edge' || o.kind === 'connector') && (o.geometry.from === id || o.geometry.to === id)) out.push({ what: o.name || o.id, where: 'World connections', route: `build/world?object=${encodeURIComponent(o.id)}` });
  }
  const sim = JSON.stringify(doc.simulation ?? {});
  if (sim.includes(`"${id}"`)) out.push({ what: 'Simulation (mission, start or timeline)', where: 'Preview / Simulation', route: 'test/preview' });
  for (const s of doc.scenarios) {
    if (JSON.stringify(s.steps).includes(`"${id}"`)) out.push({ what: s.name || s.id, where: 'Scenarios', route: `test/scenarios/${encodeURIComponent(s.id)}` });
  }
  return out;
}

export function usedByState(doc: BlueprintDocument, name: string, role: 'pt' | 'dt'): Dependency[] {
  const out: Dependency[] = [];
  if (role === 'dt') {
    if (doc.presentation.states?.[name]) out.push({ what: 'State presentation', where: 'Presentation', route: 'build/presentation' });
    for (const m of doc.assurance.monitors) if (m.property && word(name).test(m.property)) out.push({ what: m.name || m.id, where: 'Monitors', route: `assurance/monitors?id=${encodeURIComponent(m.id)}` });
    for (const r of doc.assurance.requirements) if (r.formal && word(name).test(r.formal)) out.push({ what: `${r.id} ${r.title}`, where: 'Requirements', route: `assurance/requirements?id=${encodeURIComponent(r.id)}` });
    for (const s of doc.scenarios) {
      if (s.steps.some((st) => (st.expect as { location?: string } | undefined)?.location === name)) out.push({ what: s.name || s.id, where: 'Scenarios', route: `test/scenarios/${encodeURIComponent(s.id)}` });
    }
  }
  out.push({ what: `Interpretation entry ${name}`, where: role === 'pt' ? 'I_P' : 'I_D', route: 'semantics/interpretations' });
  return out;
}

export function usedByLabel(doc: BlueprintDocument, label: string, role: 'pt' | 'dt'): Dependency[] {
  const out: Dependency[] = [];
  for (const e of doc.data.events) if (e.formal?.[role] === label) out.push({ what: e.label || e.id, where: 'Data contract events', route: `build/data?event=${encodeURIComponent(e.id)}` });
  if (role === 'dt' && doc.presentation.events?.[label]) out.push({ what: 'Event presentation', where: 'Presentation', route: 'build/presentation' });
  for (const s of doc.scenarios) {
    if (s.steps.some((st) => st.kind === 'event' && st.label === label && (st.level ?? 'dt') === role)) out.push({ what: s.name || s.id, where: 'Scenarios', route: `test/scenarios/${encodeURIComponent(s.id)}` });
  }
  if (label.endsWith('!')) out.push({ what: `Interpretation entry ${label}`, where: role === 'pt' ? 'I_P' : 'I_D', route: 'semantics/interpretations' });
  return out;
}
