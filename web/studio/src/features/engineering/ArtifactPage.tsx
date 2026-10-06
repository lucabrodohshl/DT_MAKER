/** One artefact: description, versions (lineage), dependants, verification history. */
import { Link, useParams } from 'react-router-dom';
import { useArtifact, useEvidenceList } from '@/api/queries';
import { LifecycleBadge, OutcomeBadge, PageHeader, Panel, QueryState, TimeStamp } from '@/design';
import { Crumbs, EvidenceLink, artifactSection, versionRoute } from '@/features/common/links';
import { DependenciesPanel, LineagePanel } from './versionPanels';

export default function ArtifactPage() {
  const { artifactId = '' } = useParams();
  const a = useArtifact(artifactId);
  const ev = useEvidenceList({ artifact: artifactId, limit: 30 });
  return (
    <div className="vts-page">
      <QueryState query={a}>
        {(d) => {
          const section = artifactSection(d.kind);
          const pub = d.versions.find((v) => v.state === 'published');
          const open = d.versions.find((v) => ['draft', 'validating', 'verified'].includes(v.state));
          return (
            <div className="stack">
              <PageHeader
                eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: section[0]!.toUpperCase() + section.slice(1), to: `/studio/${section}` }, { label: d.name }]} />}
                title={d.name}
                meta={
                  <>
                    <span className="mono xsmall">{d.id}</span>
                    {pub && <span className="row">published <Link to={versionRoute(d.kind, pub.ref)}>v{pub.version}</Link></span>}
                    {open && <span className="row">open <Link to={versionRoute(d.kind, open.ref)}>v{open.version}</Link> <LifecycleBadge state={open.state} /></span>}
                  </>
                }
                actions={pub && <Link className="vts-btn vts-btn--primary" to={versionRoute(d.kind, (open ?? pub).ref)}>Open {open ? 'draft' : 'published version'}</Link>}
              />
              {d.description && <p className="small muted">{d.description}</p>}
              <div className="grid-main-side">
                <div className="stack">
                  <Panel title="Versions"><LineagePanel artifactId={d.id} current={(open ?? pub)?.version ?? 0} /></Panel>
                </div>
                <div className="stack">
                  <Panel title="Dependencies"><DependenciesPanel artifactId={d.id} /></Panel>
                  <Panel title="Verification history" flush>
                    <QueryState query={ev}>
                      {(list) => (
                        <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                          {list.items.map((e) => (
                            <li key={e.id} className="stack-sm" style={{ gap: 2 }}>
                              <span className="row-wrap small">
                                <EvidenceLink id={e.id} kind={e.kind} /> {e.kind} <OutcomeBadge outcome={e.outcome} verdict={e.verdict} />
                              </span>
                              <span className="xsmall subtle">
                                {e.inputs.filter((i) => i.artifactId === d.id).map((i) => i.ref).join(', ')} · <TimeStamp value={e.createdAt} />
                              </span>
                            </li>
                          ))}
                          {list.items.length === 0 && <li className="small muted">No checks recorded.</li>}
                        </ul>
                      )}
                    </QueryState>
                  </Panel>
                </div>
              </div>
            </div>
          );
        }}
      </QueryState>
    </div>
  );
}
