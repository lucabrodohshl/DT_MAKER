/**
 * Semantics → Ontology: the domain theory K (sorts, functions, relations, axioms) that gives
 * the twin's states and events their meaning. Distinct from the asset knowledge graph of the
 * structure. Edited as source with the strict parser's diagnostics, validated by the
 * ontology services, and checked for refinement against the last published version.
 */
import { GitPullRequestArrow, Sigma } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import type { OntologyStructure, SemanticsView, Span } from '@/api/types';
import { Button, Callout, StatusBadge } from '@/design';
import type { SymbolEntry } from '@/editor/CodeEditor';
import { useEditor } from '../editor';
import { SemanticsEditor } from '../semantics/SemanticsEditor';
import { EdPage, InspectorSection, Pane } from '../ui';
import { useWorkspace } from '../workspace';

export default function OntologyPage() {
  const e = useEditor();
  const ws = useWorkspace();
  const [view, setView] = useState<SemanticsView | undefined>();
  const [reveal, setReveal] = useState<{ span: Span; nonce: number } | null>(null);
  const [refinement, setRefinement] = useState<{ verdict?: string; id?: string; summary?: string; error?: string } | null>(null);
  const st = view?.artifact?.structure as OntologyStructure | null | undefined;
  const symbols: SymbolEntry[] = useMemo(() => {
    if (!st || !('sorts' in st)) return [];
    return [
      ...st.sorts.map((s) => ({ name: s.name, kind: 'sort' as const, detail: 'sort', span: s.span })),
      ...st.functions.map((f) => ({ name: f.name, kind: 'function' as const, detail: f.signature, span: f.span })),
      ...st.relations.map((r) => ({ name: r.name, kind: 'relation' as const, detail: r.signature, span: r.span })),
      ...st.axioms.map((a) => ({ name: a.id, kind: 'axiom' as const, detail: a.formula, span: a.span })),
    ];
  }, [st]);
  const hasPublished = e.detail.parent !== null;
  return (
    <EdPage
      title="Ontology"
      description="The domain theory K: what the measured quantities and facts are, and the axioms that relate them. Interpretations give each state and event a formula over this vocabulary."
      actions={
        hasPublished && (
          <Button
            size="sm"
            icon={<GitPullRequestArrow size={14} />}
            loading={ws.running.has('refinement')}
            onClick={async () => {
              await e.saveNow();
              try {
                const r = await ws.runCheck('refinement');
                setRefinement({ verdict: String(r.verdict ?? ''), id: typeof r.id === 'string' ? r.id : undefined, summary: typeof r.summary === 'string' ? r.summary : undefined });
              } catch (err) {
                setRefinement({ error: err instanceof Error ? err.message : String(err) });
              }
            }}
          >
            Check refinement vs. published version
          </Button>
        )
      }
      guide={
        <>
          Declare <strong>sorts</strong> (e.g. <code>sort Temperature</code>), <strong>functions</strong> for measured quantities and constants (<code>fun temperature : Temperature</code>),
          <strong> relations</strong> for facts (<code>rel heater_on :</code>) and <strong>axioms</strong> (<code>axiom limit : (= temperature_limit 80)</code>). This is not the asset graph:
          assets and relationships live in Structure.
        </>
      }
    >
      {refinement && (
        refinement.error ? <Callout tone="critical" title="Refinement check could not run">{refinement.error}</Callout> : (
          <Callout tone={refinement.verdict === 'valid_refinement' ? 'ok' : refinement.verdict === 'not_a_refinement' ? 'critical' : 'warning'} title={`Refinement: ${refinement.verdict?.replace(/_/g, ' ')}`}>
            {refinement.summary} {refinement.id && <Link to={`/studio/refinement/${refinement.id}`}>Open the evidence</Link>}
          </Callout>
        )
      )}
      <SemanticsEditor
        role="ontology"
        symbols={symbols}
        onView={setView}
        reveal={reveal}
        aside={() => (
          <Pane className="vts-inspector" title={<span className="row"><Sigma size={14} aria-hidden="true" /> Structure</span>}>
            {!st || !('sorts' in st) ? (
              <p className="small muted">The structure appears once the source parses.</p>
            ) : (
              <>
                {(['sorts', 'functions', 'relations', 'axioms'] as const).map((k) => (
                  <InspectorSection key={k} title={`${k} (${st[k].length})`}>
                    <ul className="vts-findings">
                      {st[k].map((x) => {
                        const name = 'name' in x ? x.name : x.id;
                        const detail = 'signature' in x ? x.signature : 'formula' in x ? x.formula : '';
                        return (
                          <li key={name}>
                            <button type="button" className="vts-linkbtn mono" onClick={() => setReveal({ span: x.span, nonce: Date.now() })}>{name}</button>
                            {detail && <span className="subtle mono" style={{ overflowWrap: 'anywhere' }}> {detail}</span>}
                          </li>
                        );
                      })}
                    </ul>
                  </InspectorSection>
                ))}
                <StatusBadge tone="formal" label="Part of the verified core" />
              </>
            )}
          </Pane>
        )}
      />
    </EdPage>
  );
}
