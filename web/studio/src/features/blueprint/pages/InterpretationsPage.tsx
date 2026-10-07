/**
 * Semantics → Interpretations: I_P and I_D give every location and every sent event of the
 * PT and DT views a formula over the ontology. Coverage (mapped / unmapped), invalid entries
 * (strict-parser diagnostics) and the ontology version they are validated against are shown;
 * the alignment checks the two interpretations against each other.
 */
import { useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import type { InterpretationStructure, SemanticsView } from '@/api/types';
import { Button, StatusBadge, Tabs } from '@/design';
import type { SymbolEntry } from '@/editor/CodeEditor';
import { useEditor } from '../editor';
import { useOntologySymbols } from '../formalFacts';
import { SemanticsEditor } from '../semantics/SemanticsEditor';
import { EdPage, InspectorSection, Pane } from '../ui';

function CoverageAside({ view, onInsert }: { view: SemanticsView; onInsert: (keys: string[]) => void }) {
  const e = useEditor();
  const coverage = view.coverage ?? [];
  const st = view.artifact?.structure as InterpretationStructure | null | undefined;
  const invalid = new Set((view.artifact?.diagnostics ?? []).filter((d) => d.severity === 'error').map((d) => d.span.line));
  const entryLine = new Map((st && 'entries' in st ? st.entries : []).map((x) => [x.key, x.span.line]));
  const unmapped = coverage.filter((c) => !c.mapped);
  const extra = st && 'entries' in st ? st.entries.filter((x) => !coverage.some((c) => c.key === x.key)) : [];
  return (
    <Pane className="vts-inspector" title="Coverage">
      <div className="row-wrap">
        <StatusBadge tone={unmapped.length ? 'warning' : 'ok'} label={`${coverage.length - unmapped.length}/${coverage.length} mapped`} />
        {view.validatedAgainstPinnedOntology === false && <StatusBadge tone="warning" label="Validated against another ontology" />}
        {view.validatedAgainstPinnedOntology && <StatusBadge tone="ok" label={`Over ${view.ontologyPinned}`} />}
      </div>
      <table className="vts-table" style={{ marginTop: 8 }}>
        <thead><tr><th>Element</th><th>Kind</th><th>State</th></tr></thead>
        <tbody>
          {coverage.map((c) => {
            const line = entryLine.get(c.key);
            const bad = line !== undefined && invalid.has(line);
            return (
              <tr key={c.key}>
                <td className="mono small">{c.key}</td>
                <td className="xsmall">{c.kind}</td>
                <td>{bad ? <StatusBadge tone="critical" label="Invalid" /> : c.mapped ? <StatusBadge tone="ok" label="Mapped" /> : <StatusBadge tone="warning" label="Unmapped" />}</td>
              </tr>
            );
          })}
          {extra.map((x) => (
            <tr key={x.key}>
              <td className="mono small">{x.key}</td>
              <td className="xsmall">{x.isEvent ? 'event' : 'location'}</td>
              <td><StatusBadge tone="neutral" label="Not in the view" /></td>
            </tr>
          ))}
        </tbody>
      </table>
      {unmapped.length > 0 && e.editable && (
        <InspectorSection title="Unmapped elements">
          <p className="xsmall subtle">Every location and sent event needs a formula; the alignment cannot hold otherwise.</p>
          <Button size="sm" onClick={() => onInsert(unmapped.map((u) => u.key))}>Insert commented templates</Button>
        </InspectorSection>
      )}
    </Pane>
  );
}

export default function InterpretationsPage() {
  const e = useEditor();
  const [params, setParams] = useSearchParams();
  const role = params.get('view') === 'pt' ? 'pt_interpretation' : 'dt_interpretation';
  const onto = useOntologySymbols();
  const [views, setViews] = useState<Record<string, SemanticsView | undefined>>({});
  const [inserted, setInserted] = useState(0);
  const symbols: SymbolEntry[] = useMemo(
    () => [
      ...onto.functions.map((f) => ({ name: f.name, kind: 'function' as const, detail: f.signature, span: { line: 0, column: 0, length: 0 } })),
      ...onto.relations.map((r) => ({ name: r.name, kind: 'relation' as const, detail: r.signature, span: { line: 0, column: 0, length: 0 } })),
    ],
    [onto],
  );
  return (
    <EdPage
      title="Interpretations"
      description="The meaning of each state and event of the two behavioural views as a formula over the ontology (I_P for the Physical System View, I_D for the Digital Twin View)."
      guide={<>Write one line per element: <code>HEATING : (and heater_on (not door_open))</code> for a state, <code>high_temperature! : (&gt; temperature temperature_limit)</code> for an event. Symbols autocomplete from the ontology (Ctrl+Space). The alignment then checks that both views mean the same.</>}
    >
      <Tabs
        label="Interpretation"
        value={role}
        onChange={(r) => setParams({ view: r === 'pt_interpretation' ? 'pt' : 'dt' }, { replace: true })}
        tabs={[
          { id: 'dt_interpretation', label: 'I_D — Digital Twin View' },
          { id: 'pt_interpretation', label: 'I_P — Physical System View' },
        ]}
      />
      <SemanticsEditor
        key={`${role}-${inserted}`}
        role={role}
        symbols={symbols}
        onView={(v) => setViews((x) => (x[role] === v ? x : { ...x, [role]: v }))}
        aside={(view) => (
          <CoverageAside
            view={view}
            onInsert={async (keys) => {
              const content = `${view.artifact?.content ?? ''}\n\n; --- to complete (one line per element) ---\n${keys.map((k) => `; ${k} : <formula over ${view.ontologyPinned ?? 'the ontology'}>`).join('\n')}\n`;
              const { blueprintApi } = await import('@/api/blueprints');
              await e.enqueue((rev) => blueprintApi.saveSemantics(e.id, e.version, role, rev, { content, validate: false } as never));
              setInserted((n) => n + 1);
            }}
          />
        )}
      />
      {views[role] === undefined && null}
    </EdPage>
  );
}
