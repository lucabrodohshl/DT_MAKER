/** Read-only products of the server compiler, with source tracing back to the model. */
import { useMemo, useState } from 'react';
import { Copy, Download } from 'lucide-react';
import { fromCanonical } from '@/features/engineering/ta/taModel';
import { TaDiagram, type TaSelection } from '@/features/engineering/ta/TaDiagram';
import { useGenerated } from './api';
import { useModel } from './store';
import type { TraceEntry } from './types';

type Tab = 'graph' | 'ontology' | 'theory' | 'automaton' | 'contract';
type Format = 'visual' | 'source';
const TABS: { id: Tab; label: string }[] = [
  { id: 'graph', label: 'Knowledge graph' }, { id: 'ontology', label: 'Ontology' },
  { id: 'theory', label: 'Domain theory' }, { id: 'automaton', label: 'Automaton' },
  { id: 'contract', label: 'Contract' },
];

function download(text: string, name: string, type = 'text/plain') {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function Sources({ entries, onSource }: { entries: TraceEntry[]; onSource: (id: string) => void }) {
  if (entries.length === 0) return <p className="ms-note">Select a model element to see what it produced.</p>;
  return <table className="ms-table"><thead><tr><th>Generated</th><th>Name</th><th>Rule</th></tr></thead><tbody>
    {entries.slice(0, 200).map((e, i) => <tr key={`${e.kind}:${e.name}:${i}`}>
      <td>{e.kind}</td><td className="mono">{e.name}</td>
      <td>{e.sources.map((s, k) => <button key={`${s.id}:${k}`} type="button" className="ms-trace-link" onClick={() => onSource(s.id)} title={`Select source ${s.id}`}>{s.rule}</button>)}</td>
    </tr>)}
  </tbody></table>;
}

export function Generated({ tab, setTab }: { tab: Tab; setTab: (v: Tab) => void }) {
  const m = useModel();
  const [format, setFormat] = useState<Format>('visual');
  const [schema, setSchema] = useState(false);
  const [taSelection, setTaSelection] = useState<TaSelection | null>(null);
  const [copied, setCopied] = useState(false);
  const q = useGenerated(m.id, m.version, m.digest, m.saveState === 'saved' || m.saveState === 'readonly');
  const a = (m.saveState === 'saved' || m.saveState === 'readonly') ? q.data?.artifacts : null;
  const selectedTrace = useMemo(() => a?.trace.filter((e) => e.sources.some((s) => s.id === m.selection)) ?? [], [a, m.selection]);
  const taGraph = useMemo(() => a?.automaton ? fromCanonical(a.automaton) : null, [a]);
  const overlay = useMemo(() => ({
    emphasisLocations: new Set(selectedTrace.filter((e) => e.kind === 'location').map((e) => e.name)),
    emphasis: new Set(selectedTrace.filter((e) => e.kind === 'edge').map((e) => e.name)),
  }), [selectedTrace]);
  const source = !a ? '' : tab === 'graph' ? (schema ? a.schemaTurtle : a.graphTurtle)
    : tab === 'ontology' ? a.schemaTurtle
      : tab === 'theory' ? `${a.ontology}\n\n; Digital Twin interpretation\n${a.dtInterpretation}${a.ptInterpretation ? `\n\n; Physical reference interpretation\n${a.ptInterpretation}` : ''}`
        : tab === 'automaton' ? a.automatonXml
          : JSON.stringify(a.sections, null, 2);
  const ext = tab === 'automaton' ? 'xml' : tab === 'contract' ? 'json' : tab === 'theory' ? 'ont' : 'ttl';
  const traceForTa = a?.trace.find((e) => e.kind === (taSelection?.kind === 'edge' ? 'edge' : 'location') && e.name === taSelection?.id);

  return <div className="ms-gen">
    <div className="ms-gen__bar">
      <div className="ms-gen__tabs" role="tablist" aria-label="Generated representation">
        {TABS.map((t) => <button type="button" role="tab" key={t.id} aria-selected={tab === t.id} onClick={() => { setTab(t.id); setTaSelection(null); }}>{t.label}</button>)}
      </div>
      <span className="grow" />
      {(tab === 'graph' || tab === 'ontology' || tab === 'automaton') && <button type="button" className="vts-btn vts-btn--sm" onClick={() => setFormat(format === 'visual' ? 'source' : 'visual')}>{format === 'visual' ? 'Source' : 'Diagram'}</button>}
      {tab === 'graph' && <button type="button" className="vts-btn vts-btn--sm" onClick={() => setSchema(!schema)}>{schema ? 'Instances' : 'Schema'}</button>}
      <button type="button" className="vts-btn vts-btn--sm" disabled={!a} onClick={() => { void navigator.clipboard.writeText(source).then(() => { setCopied(true); setTimeout(() => setCopied(false), 1500); }); }}><Copy size={13} /> {copied ? 'Copied' : 'Copy'}</button>
      <button type="button" className="vts-btn vts-btn--sm" disabled={!a} onClick={() => download(source, `${m.doc.name}-${tab}.${ext}`, tab === 'automaton' ? 'application/xml' : 'text/plain')}><Download size={13} /> {tab === 'automaton' ? 'Export XML' : 'Download'}</button>
    </div>
    <div className="ms-gen__body">
      {m.saveState !== 'saved' && m.saveState !== 'readonly' && <p role="status" className="ms-note">Saving the current model before showing generated results…</p>}
      {q.isPending && <p className="ms-note">Compiling representations…</p>}
      {q.error && <p role="alert" className="ms-err">{q.error instanceof Error ? q.error.message : 'Could not load generated representations.'}</p>}
      {!q.isPending && !a && !q.error && <div className="ms-empty ms-empty--static"><div><strong>No generated result yet.</strong><span>Fix the model problems below; the server will compile it when it saves.</span></div></div>}
      {a && (format === 'source' || tab === 'theory' || tab === 'contract') && <pre className="ms-src">{source}</pre>}
      {a && format === 'visual' && tab === 'automaton' && taGraph && <div className="ms-ta">
        <TaDiagram graph={taGraph} layoutKey={`${m.id}:${m.version}:${m.digest}`} overlay={overlay} selection={taSelection} onSelect={setTaSelection} height={560} />
        {traceForTa && <div className="ms-trace-detail"><strong>Produced by</strong> {traceForTa.sources.map((s) => <button type="button" key={`${s.id}:${s.rule}`} onClick={() => m.select(s.id)}>{s.rule} · {s.id}</button>)}</div>}
      </div>}
      {a && format === 'visual' && tab === 'graph' && <table className="ms-table"><thead><tr><th>Entity</th><th>Type</th><th>Facts</th></tr></thead><tbody>
        {((schema ? a.jsonld.schema?.['@graph'] : a.jsonld.graph?.['@graph']) ?? []).map((n, i) => <tr key={`${n['@id']}:${i}`} className={selectedTrace.some((t) => t.name === n['@id']) ? 'is-hit' : ''}>
          <td className="mono">{n['@id']}</td><td className="mono">{(n['@type'] ?? []).join(', ')}</td><td>{Object.entries(n).filter(([k]) => k !== '@id' && k !== '@type').map(([k, v]) => <div key={k}><span className="mono">{k}</span> {JSON.stringify(v)}</div>)}</td>
        </tr>)}
      </tbody></table>}
      {a && format === 'visual' && tab === 'ontology' && <table className="ms-table"><thead><tr><th>Kind</th><th>Term</th><th>Source rule</th></tr></thead><tbody>
        {a.trace.filter((t) => ['class', 'property', 'relationship-type'].includes(t.kind)).map((t, i) => <tr key={`${t.kind}:${t.name}:${i}`} className={t.sources.some((s) => s.id === m.selection) ? 'is-hit' : ''}>
          <td>{t.kind}</td><td className="mono">{t.name}</td><td>{t.sources.map((s) => <button type="button" key={s.id} className="ms-trace-link" onClick={() => m.select(s.id)}>{s.rule}</button>)}</td>
        </tr>)}
      </tbody></table>}
      {a && <section className="ms-trace"><h3>Trace · {m.selection ? `${selectedTrace.length} products of selection` : 'select a model element'}</h3><Sources entries={selectedTrace} onSource={m.select} /></section>}
      {a && <div className="ms-gen__digest">Generated by {a.summary.compiler} · SHA-256 {q.data?.digest}</div>}
    </div>
  </div>;
}

export type GeneratedTab = Tab;
