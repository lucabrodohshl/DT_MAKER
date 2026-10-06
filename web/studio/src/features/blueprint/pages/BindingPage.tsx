/**
 * Semantics → Cross-layer binding: every observable signal and event of the twin read across
 * the four layers that give it meaning — World (where it is), Data (what is measured and
 * where it comes from), Semantics (the ontology symbol, or the formal labels) and Behavior
 * (the DT / PT states and events whose interpretation formulas use it). A broken link in the
 * chain is shown in place, with a link to the editor that closes it.
 *
 * Read-only: it is computed from the working copy and the parsed structure of the pinned
 * ontology and interpretations returned by the backend; nothing here evaluates a formula.
 */
import { ArrowRight, Filter } from 'lucide-react';
import { Fragment, useMemo, useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { blueprintRoute, useBlueprintSemantics } from '@/api/blueprints';
import type { InterpretationStructure, OntologyStructure } from '@/api/types';
import { Callout, Segmented, StatusBadge } from '@/design';
import { useEditor } from '../editor';
import { useModelFacts } from '../formalFacts';
import { EdPage } from '../ui';

type Entry = InterpretationStructure['entries'][number];
type Tone = 'ok' | 'gap' | 'error';

interface Cell {
  tone: Tone;
  content: ReactNode;
}

interface Row {
  key: string;
  kind: 'telemetry' | 'event' | 'symbol';
  asset: string | null;
  cells: [Cell, Cell, Cell, Cell];
  gaps: number;
}

function Missing({ children, to }: { children: ReactNode; to?: string }) {
  return (
    <>
      <span>{children}</span>
      {to && (
        <Link to={to} className="xsmall" style={{ fontStyle: 'normal' }}>
          Fix
        </Link>
      )}
    </>
  );
}

function useStructure<T>(role: 'ontology' | 'pt_interpretation' | 'dt_interpretation'): T | null {
  const e = useEditor();
  const q = useBlueprintSemantics(e.id, e.version, role);
  return (q.data?.artifact?.structure as T | null | undefined) ?? null;
}

export default function BindingPage() {
  const e = useEditor();
  const doc = e.doc;
  const route = (sub: string) => blueprintRoute(e.id, e.version, sub);
  const ontology = useStructure<OntologyStructure>('ontology');
  const dtInterp = useStructure<InterpretationStructure>('dt_interpretation');
  const ptInterp = useStructure<InterpretationStructure>('pt_interpretation');
  const dt = useModelFacts('dt');
  const pt = useModelFacts('pt');
  const [filter, setFilter] = useState<'all' | 'gaps'>('all');

  const rows = useMemo(() => {
    const assetName = (id: string | undefined) => doc.structure.assets.find((a) => a.id === id)?.name ?? id ?? '';
    const objectsOf = (asset: string | undefined) => (asset ? doc.world.objects.filter((o) => o.asset === asset) : []);
    const symbolInfo = (name: string) =>
      ontology?.functions.find((f) => f.name === name)?.signature ?? ontology?.relations.find((r) => r.name === name)?.signature ?? null;
    const users = (interp: InterpretationStructure | null, symbol: string) => (interp?.entries ?? []).filter((x) => x.symbols.includes(symbol));

    const worldCell = (asset: string | undefined): Cell => {
      if (!asset) return { tone: 'gap', content: <Missing to={route('build/data')}>Not attached to an asset</Missing> };
      const objs = objectsOf(asset);
      if (objs.length === 0) {
        return {
          tone: doc.world.objects.length === 0 ? 'ok' : 'gap',
          content: <Missing to={route('build/world')}>{doc.world.objects.length === 0 ? 'No world defined' : `No world object for ${assetName(asset)}`}</Missing>,
        };
      }
      return {
        tone: 'ok',
        content: objs.map((o) => (
          <Link key={o.id} to={route(`build/world?object=${encodeURIComponent(o.id)}`)}>
            {o.name || o.id} <span className="xsmall subtle">({o.semanticType})</span>
          </Link>
        )),
      };
    };
    const behaviorCell = (symbol: string | null, labels?: { pt?: string; dt?: string }): Cell => {
      const lines: ReactNode[] = [];
      const show = (role: 'pt' | 'dt', entries: Entry[]) =>
        entries.forEach((x) =>
          lines.push(
            <span key={`${role}-${x.key}`}>
              <StatusBadge tone={role === 'dt' ? 'info' : 'neutral'} label={role.toUpperCase()} />{' '}
              <Link to={route(x.isEvent ? `behavior/${role}` : `behavior/${role}?state=${encodeURIComponent(x.key)}`)} className="mono">
                {x.key}
              </Link>{' '}
              <span className="xsmall subtle">{x.isEvent ? 'event' : 'state'}</span>
            </span>,
          ),
        );
      if (symbol) {
        show('dt', users(dtInterp, symbol));
        show('pt', users(ptInterp, symbol));
      }
      if (labels) {
        for (const role of ['dt', 'pt'] as const) {
          const l = labels[role];
          if (!l) continue;
          const facts = role === 'dt' ? dt : pt;
          if (!facts.present) continue;
          if (!facts.labels.includes(l)) {
            return { tone: 'error', content: <Missing to={route(`behavior/${role}`)}>{`${l} is not an event of the ${role === 'dt' ? 'Digital Twin' : 'Physical System'} View`}</Missing> };
          }
          lines.push(
            <span key={`t-${role}`}>
              <StatusBadge tone={role === 'dt' ? 'info' : 'neutral'} label={role.toUpperCase()} />{' '}
              <Link to={route(`behavior/${role}`)} className="mono">
                {l}
              </Link>{' '}
              <span className="xsmall subtle">transition label</span>
            </span>,
          );
        }
      }
      if (lines.length === 0) {
        return {
          tone: 'gap',
          content: <Missing to={route('semantics/interpretations')}>{symbol ? 'No state or event is interpreted with this symbol' : 'Not linked to the behaviour'}</Missing>,
        };
      }
      return { tone: 'ok', content: lines };
    };

    const out: Row[] = [];
    for (const t of doc.data.telemetry) {
      const binding = doc.connectivity.bindings.find((b) => b.target.kind === 'telemetry' && b.target.id === t.id);
      const source = binding ? doc.connectivity.sources.find((s) => s.id === binding.source) : undefined;
      const data: Cell = {
        tone: binding ? 'ok' : 'gap',
        content: (
          <>
            <Link to={route(`build/data?telemetry=${encodeURIComponent(t.id)}`)}>{t.label || t.id}</Link>
            <span className="xsmall subtle mono">
              {t.id} · {t.type}
              {t.unit ? ` · ${t.unit}` : ''}
            </span>
            {binding ? (
              <span className="xsmall">
                from <Link to={route(`build/data?tab=connectivity&source=${encodeURIComponent(binding.source)}`)}>{source?.name ?? binding.source}</Link>
                {binding.select.field ? <span className="mono"> .{binding.select.field}</span> : binding.select.topic ? <span className="mono"> {binding.select.topic}</span> : null}
              </span>
            ) : (
              <span className="xsmall" style={{ fontStyle: 'italic' }}>
                No binding: no live value <Link to={route('build/data?tab=connectivity')}>Bind</Link>
              </span>
            )}
          </>
        ),
      };
      const sym = t.ontologySymbol || null;
      const signature = sym ? symbolInfo(sym) : null;
      const semantics: Cell = !sym
        ? { tone: 'gap', content: <Missing to={route(`build/data?telemetry=${encodeURIComponent(t.id)}`)}>No ontology symbol: the signal has no formal meaning</Missing> }
        : ontology && !signature
          ? { tone: 'error', content: <Missing to={route('semantics/ontology')}>{`${sym} is not declared in the ontology`}</Missing> }
          : {
              tone: 'ok',
              content: (
                <>
                  <Link to={route('semantics/ontology')} className="mono">
                    {sym}
                  </Link>
                  {signature && <span className="xsmall subtle mono">{signature}</span>}
                </>
              ),
            };
      const cells: Row['cells'] = [worldCell(t.asset), data, semantics, sym ? behaviorCell(sym) : { tone: 'gap', content: <Missing>—</Missing> }];
      out.push({ key: `t:${t.id}`, kind: 'telemetry', asset: t.asset ?? null, cells, gaps: cells.filter((c) => c.tone !== 'ok').length });
    }
    for (const ev of doc.data.events) {
      const formal = ev.formal ?? {};
      const data: Cell = {
        tone: 'ok',
        content: (
          <>
            <Link to={route(`build/data?event=${encodeURIComponent(ev.id)}`)}>{ev.label || ev.id}</Link>
            <span className="xsmall subtle mono">{ev.id} · event</span>
          </>
        ),
      };
      const has = formal.pt || formal.dt;
      const interp = (role: 'pt' | 'dt') => {
        const l = formal[role];
        if (!l) return null;
        const entry = (role === 'dt' ? dtInterp : ptInterp)?.entries.find((x) => x.key === l);
        return entry;
      };
      const semantics: Cell = !has
        ? { tone: 'gap', content: <Missing to={route(`build/data?event=${encodeURIComponent(ev.id)}`)}>No formal label: conformance cannot check it</Missing> }
        : {
            tone: (['pt', 'dt'] as const).some((r) => formal[r] && (r === 'dt' ? dtInterp : ptInterp) && !interp(r)) ? 'gap' : 'ok',
            content: (['dt', 'pt'] as const)
              .filter((r) => formal[r])
              .map((r) => {
                const entry = interp(r);
                return (
                  <span key={r}>
                    <span className="mono">{formal[r]}</span>{' '}
                    {entry ? (
                      <span className="xsmall subtle mono" title={entry.formula}>
                        ≡ {entry.formula.length > 46 ? `${entry.formula.slice(0, 45)}…` : entry.formula}
                      </span>
                    ) : (
                      <span className="xsmall" style={{ fontStyle: 'italic' }}>
                        not interpreted in I_{r === 'dt' ? 'D' : 'P'} <Link to={route(`semantics/interpretations?view=${r}`)}>Fix</Link>
                      </span>
                    )}
                  </span>
                );
              }),
          };
      const cells: Row['cells'] = [worldCell(ev.asset), data, semantics, has ? behaviorCell(null, formal) : { tone: 'gap', content: <Missing>—</Missing> }];
      out.push({ key: `e:${ev.id}`, kind: 'event', asset: ev.asset ?? null, cells, gaps: cells.filter((c) => c.tone !== 'ok').length });
    }
    // Ontology symbols no signal observes: the twin's meaning depends on them, but no data feeds them.
    const observed = new Set(doc.data.telemetry.map((t) => t.ontologySymbol).filter(Boolean));
    for (const s of [...(ontology?.functions ?? []), ...(ontology?.relations ?? [])]) {
      if (observed.has(s.name)) continue;
      const usedBy = [...users(dtInterp, s.name), ...users(ptInterp, s.name)];
      const isConstant = (ontology?.axioms ?? []).some((a) => a.symbols.includes(s.name) && /^\(=\s+\S+\s+-?[\d.]+\)$/.test(a.formula.trim()));
      if (usedBy.length === 0 || isConstant) continue;
      const cells: Row['cells'] = [
        { tone: 'ok', content: <span className="subtle">—</span> },
        { tone: 'gap', content: <Missing to={route('build/data')}>No signal observes this symbol</Missing> },
        {
          tone: 'ok',
          content: (
            <>
              <Link to={route('semantics/ontology')} className="mono">
                {s.name}
              </Link>
              <span className="xsmall subtle mono">{s.signature}</span>
            </>
          ),
        },
        behaviorCell(s.name),
      ];
      out.push({ key: `s:${s.name}`, kind: 'symbol', asset: null, cells, gaps: 1 });
    }
    return out;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [doc, ontology, dtInterp, ptInterp, dt, pt, e.id, e.version]);

  const groups = useMemo(() => {
    const byAsset = new Map<string, Row[]>();
    for (const r of rows) {
      if (filter === 'gaps' && r.gaps === 0) continue;
      const k = r.kind === 'symbol' ? '~unobserved' : r.asset ?? '~none';
      byAsset.set(k, [...(byAsset.get(k) ?? []), r]);
    }
    const order = doc.structure.assets.map((a) => a.id);
    return [...byAsset.entries()].sort(([a], [b]) => {
      const ia = order.indexOf(a);
      const ib = order.indexOf(b);
      return (ia < 0 ? 1e6 : ia) - (ib < 0 ? 1e6 : ib) || a.localeCompare(b);
    });
  }, [rows, filter, doc.structure.assets]);

  const totalGaps = rows.filter((r) => r.gaps > 0).length;
  const errors = rows.filter((r) => r.cells.some((c) => c.tone === 'error')).length;
  const groupTitle = (k: string) =>
    k === '~unobserved' ? 'Ontology symbols without a signal' : k === '~none' ? 'Not attached to an asset' : (doc.structure.assets.find((a) => a.id === k)?.name ?? k);

  return (
    <EdPage
      title="Cross-layer binding"
      wide
      description="Each signal and event of the twin across World → Data → Semantics → Behavior. Gaps show where a layer does not connect to the next."
      actions={<Segmented value={filter} onChange={setFilter} options={[{ id: 'all', label: 'All' }, { id: 'gaps', label: `Gaps (${totalGaps})` }]} label="Filter" />}
      guide={
        <>
          A signal is meaningful to the verified twin when it is placed in the world (an asset with a world object), delivered by a data source, named by an ontology symbol and used by the interpretation of a
          Digital Twin View state or event. Events link through their formal labels. This view only reads the Blueprint; use the links to fix a gap.
        </>
      }
    >
      <div className="row-wrap">
        <StatusBadge tone="neutral" label={`${doc.data.telemetry.length} signals · ${doc.data.events.length} events`} />
        {totalGaps === 0 ? <StatusBadge tone="ok" label="Every chain is complete" /> : <StatusBadge tone="warning" icon={Filter} label={`${totalGaps} with gaps`} />}
        {errors > 0 && <StatusBadge tone="critical" label={`${errors} broken reference(s)`} />}
        {!ontology && <StatusBadge tone="neutral" label="No ontology pinned" />}
      </div>
      {rows.length === 0 ? (
        <Callout tone="info" title="Nothing to bind yet">
          Define telemetry and events in <Link to={route('build/data')}>Data &amp; Connectivity</Link>; they appear here with their world placement and meaning.
        </Callout>
      ) : (
        <div className="vts-xl" role="table" aria-label="Cross-layer binding">
          <div role="row" style={{ display: 'contents' }}>
            {['World', 'Data', 'Semantics', 'Behavior'].map((h, i) => (
              <div key={h} className="vts-xl__head" role="columnheader">
                {h}
                {i < 3 && <ArrowRight size={12} aria-hidden="true" className="vts-xl__arrow" />}
              </div>
            ))}
          </div>
          {groups.map(([k, list]) => (
            <Fragment key={k}>
              <div className="vts-xl__group" role="row">
                <span role="cell">{groupTitle(k)}</span>
              </div>
              {list.map((r) => (
                <div key={r.key} role="row" style={{ display: 'contents' }}>
                  {r.cells.map((c, i) => (
                    <div key={i} role="cell" className={`vts-xl__cell${c.tone === 'gap' ? ' is-gap' : c.tone === 'error' ? ' is-error' : ''}`}>
                      {c.content}
                    </div>
                  ))}
                </div>
              ))}
            </Fragment>
          ))}
          {groups.length === 0 && (
            <div className="vts-xl__group" role="row">
              <span role="cell">No gaps: every signal and event is connected across the four layers.</span>
            </div>
          )}
        </div>
      )}
    </EdPage>
  );
}
