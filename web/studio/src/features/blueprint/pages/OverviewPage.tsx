/**
 * Blueprint overview: what this version is, how complete it is, what to do next, the state
 * of every section and the release gate. Everything shown is the backend's status for the
 * exact pinned inputs (GET .../status).
 */
import { ArrowRight, ExternalLink, ShieldCheck } from 'lucide-react';
import { Link } from 'react-router-dom';
import { blueprintRoute, useBlueprintStatus, useInstances } from '@/api/blueprints';
import type { GateItem } from '@/api/types';
import { HashChip, KeyValue, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { versionRoute } from '@/features/common/links';
import { useEditor } from '../editor';
import { ALL_ITEMS } from '../nav';
import { GateBadge, ReleaseVerdictBadge, SectionStateBadge, VersionStateBadge } from '../status';
import { EdPage } from '../ui';

export function GateList({ items, base }: { items: GateItem[]; base: string }) {
  return (
    <ul className="vts-gate" aria-label="Release gate">
      {items.map((g) => (
        <li key={g.id}>
          <span aria-hidden="true" />
          <strong>{g.title}</strong>
          <span className="small muted" style={{ minWidth: 0, overflowWrap: 'anywhere' }}>
            {g.detail}
            {g.evidenceId && (
              <>
                {' '}
                <Link to={`/studio/verification/${encodeURIComponent(g.evidenceId)}`} className="mono xsmall">{g.evidenceId}</Link>
              </>
            )}
          </span>
          <span className="row" style={{ justifyContent: 'flex-end' }}>
            <GateBadge state={g.state} label={g.state === 'pass' && g.id === 'alignment' && g.detail.startsWith('PASS') ? g.detail.split(' (')[0] : undefined} />
            {g.state !== 'pass' && g.state !== 'not_applicable' && (
              <Link to={`${base}/${g.fix}`} className="xsmall">Fix</Link>
            )}
          </span>
        </li>
      ))}
    </ul>
  );
}

export default function OverviewPage() {
  const e = useEditor();
  const status = useBlueprintStatus(e.id, e.version);
  const instances = useInstances(e.id);
  const base = blueprintRoute(e.id, e.version);
  const identity = e.doc.identity;
  return (
    <EdPage
      title={identity.name || e.detail.blueprint.name}
      description={identity.description || 'No description yet — add one in Structure → Identity.'}
      actions={<VersionStateBadge state={e.detail.state} size="lg" />}
      guide={
        <>
          A Blueprint defines a <strong>type</strong> of twin: its structure, world, data contract, the two behavioural views, their
          semantics and how they are assured. Work through the sections on the left; the release gate below turns green only on real
          evidence (aligner, compiler, scenario tests). Publish, then create instances of it.
        </>
      }
    >
      <QueryState query={status} skeletonLines={8}>
        {(s) => {
          const pct = Math.round((s.completeness.complete / Math.max(1, s.completeness.total)) * 100);
          return (
            <>
              <div className="grid-main-side">
                <Panel title="Completeness" subtitle={`${s.completeness.complete} of ${s.completeness.total} sections complete`}>
                  <div className="stack">
                    <div className="vts-progress" role="progressbar" aria-valuenow={pct} aria-valuemin={0} aria-valuemax={100} aria-label="Blueprint completeness">
                      <span style={{ width: `${pct}%` }} />
                    </div>
                    {s.completeness.next.length > 0 ? (
                      <div className="stack-sm">
                        <span className="vts-label">Next steps</span>
                        {s.completeness.next.map((n) => (
                          <Link key={n.section} to={`${base}/${n.route}`} className="vts-link-row">
                            <SectionStateBadge state={n.state} />
                            <strong className="small">{n.title}</strong>
                            <span className="small muted grow truncate">{n.detail}</span>
                            <ArrowRight size={14} aria-hidden="true" />
                          </Link>
                        ))}
                      </div>
                    ) : (
                      <p className="small">Every section is complete.</p>
                    )}
                  </div>
                </Panel>
                <Panel title="This version">
                  <KeyValue
                    compact
                    items={[
                      ['Version', <span key="v" className="row"><span className="mono">{e.detail.label}</span><VersionStateBadge state={e.detail.state} /></span>],
                      ['Note', e.detail.note || '—'],
                      ['Domain', identity.domain],
                      ['Runtime mode', identity.runtimeMode === 'cosimulation' ? 'Co-simulation (twin drives a simulator)' : 'Monitor (twin follows a data source)'],
                      ['Model id', <span key="m" className="mono">{identity.modelId}</span>],
                      ['Time', `1 ${identity.timeUnit} = ${identity.ticksPerUnit} ticks`],
                      ['Updated', <span key="u"><TimeStamp value={e.detail.updatedAt} relative /> by {e.detail.updatedBy}</span>],
                      ['Document', <HashChip key="h" value={e.detail.documentSha256} label="document hash" />],
                      ['Instances', instances.data ? <Link key="i" to={`${base}/release/instances`}>{instances.data.length} instance{instances.data.length === 1 ? '' : 's'}</Link> : '…'],
                    ]}
                  />
                </Panel>
              </div>

              <section aria-labelledby="sections-title" className="stack-sm">
                <h3 id="sections-title" className="vts-section-title">Sections</h3>
                <div className="vts-ov-sections">
                  {s.sections.map((sec) => {
                    const nav = ALL_ITEMS.find((i) => i.to && sec.route.startsWith(i.to));
                    const Icon = nav?.icon;
                    return (
                      <Link key={sec.id} to={`${base}/${sec.route}`} className="vts-ov-card">
                        <div className="vts-ov-card__top">
                          {Icon && <Icon size={16} aria-hidden="true" className="subtle" />}
                          <strong className="grow">{sec.title}</strong>
                          <SectionStateBadge state={sec.state} />
                        </div>
                        <span className="small muted">{sec.summary}</span>
                        {sec.detail && <span className="xsmall" style={{ color: sec.state === 'errors' ? 'var(--crit)' : 'var(--warn)' }}>{sec.detail}</span>}
                      </Link>
                    );
                  })}
                </div>
              </section>

              <Panel
                title={<span className="row"><ShieldCheck size={16} aria-hidden="true" /> Release gate</span>}
                subtitle="Computed from evidence for exactly the pinned artefacts and this document"
                actions={<ReleaseVerdictBadge verdict={s.readiness.verdict} size="lg" />}
              >
                <GateList items={s.readiness.items} base={base} />
                {s.readiness.verdict === 'ready' ? (
                  <p className="small" style={{ marginTop: 10 }}>
                    Every gate passes. <Link to={`${base}/release/package`}>Review the package and publish {e.detail.label}</Link>.
                  </p>
                ) : null}
              </Panel>

              <div className="grid-2">
                <Panel title="Formal artefacts pinned by this version" subtitle="The verified core is built from exactly these versions">
                  <ul className="vts-list small">
                    {(['pt_model', 'dt_model', 'ontology', 'pt_interpretation', 'dt_interpretation'] as const).map((role) => {
                      const a = e.detail.artifacts[role];
                      return (
                        <li key={role} className="row-wrap">
                          <span className="grow">{role.replace('_', ' ')}</span>
                          {a ? (
                            <>
                              <Link to={versionRoute(a.kind, a.ref)} className="mono xsmall">{a.ref}</Link>
                              <StatusBadge tone={a.state === 'published' ? 'ok' : a.state === 'verified' ? 'formal' : 'info'} label={a.state} />
                            </>
                          ) : (
                            <span className="subtle">not defined</span>
                          )}
                        </li>
                      );
                    })}
                  </ul>
                </Panel>
                <Panel title="Trust boundary" subtitle="What is formally verified and what is only integrity-protected">
                  <div className="stack-sm small">
                    <p>
                      <StatusBadge tone="formal" icon={ShieldCheck} label="Verified core" /> PT and DT views, ontology, interpretations, the
                      alignment evidence, the translation-validated Twin IR and the monitor definitions.
                    </p>
                    <p>
                      <StatusBadge tone="neutral" label="Integrity only" /> Structure, world and layout, data contract and connectivity,
                      presentation, scenarios and simulator inputs: versioned and hash-protected, not formally verified.
                    </p>
                    <Link to={`${base}/release/package`} className="small">
                      Package details <ExternalLink size={12} aria-hidden="true" />
                    </Link>
                  </div>
                </Panel>
              </div>
            </>
          );
        }}
      </QueryState>
    </EdPage>
  );
}
