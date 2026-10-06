/**
 * Refactorings over a Blueprint document: renaming an element updates every reference to it
 * across sections, in one undoable edit (EditorApi.updateDoc). Formal artefacts (models,
 * interpretations) are never rewritten here: renaming a state or label is done in the model
 * editor and the validator reports interpretation entries that must follow.
 */
import type { BlueprintDocument } from '@/api/types';

type Changes = Partial<BlueprintDocument>;

export function renameAsset(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  const swap = (x: string | undefined | null) => (x === from ? to : x);
  return {
    structure: {
      ...doc.structure,
      root: doc.structure.root === from ? to : doc.structure.root,
      assets: doc.structure.assets.map((a) => ({ ...a, id: a.id === from ? to : a.id, parent: swap(a.parent) ?? undefined })),
      relationships: doc.structure.relationships.map((r) => ({ ...r, source: swap(r.source)!, target: swap(r.target)! })),
    },
    world: { ...doc.world, objects: doc.world.objects.map((o) => (o.asset === from ? { ...o, asset: to } : o)) },
    data: {
      ...doc.data,
      properties: doc.data.properties.map((p) => (p.asset === from ? { ...p, asset: to } : p)),
      telemetry: doc.data.telemetry.map((t) => (t.asset === from ? { ...t, asset: to } : t)),
      events: doc.data.events.map((e) => (e.asset === from ? { ...e, asset: to } : e)),
      commands: doc.data.commands.map((c) => (c.asset === from ? { ...c, asset: to } : c)),
    },
    presentation: { ...doc.presentation, importantAssets: (doc.presentation.importantAssets ?? []).map((a) => (a === from ? to : a)) },
  };
}

export function renameAssetType(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    structure: {
      ...doc.structure,
      assetTypes: doc.structure.assetTypes.map((t) => (t.id === from ? { ...t, id: to } : t)),
      assets: doc.structure.assets.map((a) => (a.type === from ? { ...a, type: to } : a)),
    },
  };
}

export function renameTelemetry(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    data: { ...doc.data, telemetry: doc.data.telemetry.map((t) => (t.id === from ? { ...t, id: to } : t)) },
    connectivity: {
      ...doc.connectivity,
      bindings: doc.connectivity.bindings.map((b) => (b.target.kind === 'telemetry' && b.target.id === from ? { ...b, target: { ...b.target, id: to } } : b)),
    },
    presentation: {
      ...doc.presentation,
      keyTelemetry: (doc.presentation.keyTelemetry ?? []).map((k) => (k === from ? to : k)),
      charts: (doc.presentation.charts ?? []).map((c) => ({ ...c, telemetry: c.telemetry.map((k) => (k === from ? to : k)) })),
    },
    assurance: { ...doc.assurance, monitors: doc.assurance.monitors.map((m) => (m.field === from ? { ...m, field: to } : m)) },
    scenarios: doc.scenarios.map((s) => ({
      ...s,
      steps: s.steps.map((st) =>
        st.telemetry && from in st.telemetry
          ? { ...st, telemetry: Object.fromEntries(Object.entries(st.telemetry).map(([k, v]) => [k === from ? to : k, v])) }
          : st,
      ),
    })),
  };
}

export function renameEvent(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    data: {
      ...doc.data,
      events: doc.data.events.map((e) => (e.id === from ? { ...e, id: to } : e)),
      commands: doc.data.commands.map((c) => ({
        ...c,
        acknowledgement: c.acknowledgement?.event === from ? { ...c.acknowledgement, event: to } : c.acknowledgement,
        observedConsequence: c.observedConsequence?.event === from ? { ...c.observedConsequence, event: to } : c.observedConsequence,
      })),
    },
    connectivity: {
      ...doc.connectivity,
      bindings: doc.connectivity.bindings.map((b) => (b.target.kind === 'event' && b.target.id === from ? { ...b, target: { ...b.target, id: to } } : b)),
    },
  };
}

export function renameSource(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    connectivity: {
      sources: doc.connectivity.sources.map((s) => (s.id === from ? { ...s, id: to } : s)),
      bindings: doc.connectivity.bindings.map((b) => (b.source === from ? { ...b, source: to } : b)),
    },
  };
}

export function renameMonitor(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    assurance: {
      ...doc.assurance,
      monitors: doc.assurance.monitors.map((m) => (m.id === from ? { ...m, id: to } : m)),
      requirements: doc.assurance.requirements.map((r) => ({ ...r, monitors: r.monitors.map((m) => (m === from ? to : m)) })),
      alerts: doc.assurance.alerts.map((a) => (a.monitor === from ? { ...a, monitor: to } : a)),
    },
    presentation: { ...doc.presentation, importantMonitors: (doc.presentation.importantMonitors ?? []).map((m) => (m === from ? to : m)) },
  };
}

export function renameRequirement(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    assurance: {
      ...doc.assurance,
      requirements: doc.assurance.requirements.map((r) => (r.id === from ? { ...r, id: to } : r)),
      monitors: doc.assurance.monitors.map((m) => (m.requirement === from ? { ...m, requirement: to } : m)),
    },
  };
}

export function renameWorldObject(doc: BlueprintDocument, from: string, to: string): Changes {
  if (from === to) return {};
  return {
    world: {
      ...doc.world,
      objects: doc.world.objects.map((o) => ({
        ...o,
        id: o.id === from ? to : o.id,
        geometry: (o.kind === 'edge' || o.kind === 'connector') ? { ...o.geometry, from: o.geometry.from === from ? to : o.geometry.from, to: o.geometry.to === from ? to : o.geometry.to } : o.geometry,
      })),
    },
  };
}

/** Remove an asset and its descendants (references elsewhere are reported by validation). */
export function deleteAsset(doc: BlueprintDocument, id: string): Changes {
  const doomed = new Set<string>([id]);
  for (let changed = true; changed; ) {
    changed = false;
    for (const a of doc.structure.assets) {
      if (a.parent && doomed.has(a.parent) && !doomed.has(a.id)) {
        doomed.add(a.id);
        changed = true;
      }
    }
  }
  return {
    structure: {
      ...doc.structure,
      root: doomed.has(doc.structure.root) ? '' : doc.structure.root,
      assets: doc.structure.assets.filter((a) => !doomed.has(a.id)),
      relationships: doc.structure.relationships.filter((r) => !doomed.has(r.source) && !doomed.has(r.target)),
    },
  };
}
