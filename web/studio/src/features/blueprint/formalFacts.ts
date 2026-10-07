/**
 * Facts read from the pinned formal artefacts for cross-referencing in editors: the sync
 * labels and locations of the PT/DT views and the symbols of the ontology. Read-only views of
 * backend responses; nothing here interprets formulas.
 */
import { useMemo } from 'react';
import { useBlueprintModel, useBlueprintSemantics } from '@/api/blueprints';
import type { OntologyStructure } from '@/api/types';
import { useEditor } from './editor';

export interface ModelFacts {
  locations: string[];
  /** Sent labels ("a!"), the observable events of the view. */
  labels: string[];
  clocks: string[];
  present: boolean;
}

export function useModelFacts(role: 'pt' | 'dt'): ModelFacts {
  const e = useEditor();
  const q = useBlueprintModel(e.id, e.version, role);
  return useMemo(() => {
    const m = q.data?.model;
    if (!m) return { locations: [], labels: [], clocks: [], present: false };
    const labels = [...new Set(m.edges.filter((x) => x.sync?.direction === '!').map((x) => `${x.sync!.channel}!`))].sort();
    return { locations: m.locations.map((l) => l.name), labels, clocks: m.clocks.map((c) => c.name), present: true };
  }, [q.data]);
}

export interface OntologySymbols {
  functions: { name: string; signature: string; comment: string }[];
  relations: { name: string; signature: string; comment: string }[];
  sorts: string[];
  present: boolean;
}

export function useOntologySymbols(): OntologySymbols {
  const e = useEditor();
  const q = useBlueprintSemantics(e.id, e.version, 'ontology');
  return useMemo(() => {
    const st = q.data?.artifact?.structure as OntologyStructure | null | undefined;
    if (!st || !('functions' in st)) return { functions: [], relations: [], sorts: [], present: false };
    return {
      functions: st.functions.map((f) => ({ name: f.name, signature: f.signature, comment: f.comment })),
      relations: st.relations.map((r) => ({ name: r.name, signature: r.signature, comment: r.comment })),
      sorts: st.sorts.map((s) => s.name),
      present: true,
    };
  }, [q.data]);
}
