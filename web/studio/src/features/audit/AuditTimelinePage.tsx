/** Audit timeline: engineering events, deployments, and per-twin executions. */
import { Link } from 'react-router-dom';
import { useDeployments, useTwins } from '@/api/queries';
import { PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { Crumbs, PackageLink } from '@/features/common/links';
import { AuditTable, AuditVerifyPanel } from './EngineeringAuditPage';
import { ExecutionsPanel } from './ExecutionsPanel';
import { useTwinScope } from '@/app/twinScope';

export default function AuditTimelinePage() {
  const scope = useTwinScope();
  const twins = useTwins();
  const deployments = useDeployments(scope?.twin.id);
  return (
    <div className="vts-page">
      <PageHeader eyebrow={<Crumbs items={[{ label: 'Audit' }, { label: 'Timeline' }]} />} title="Audit timeline" meta={<span>Operational executions, deployments and engineering changes</span>} />
      <div className="grid-main-side">
        <div className="stack">
          {twins.data?.filter((t) => t.runtimeUrl && (!scope || t.id === scope.twin.id)).map((t) => <ExecutionsPanel key={t.id} twinId={t.id} title={`Executions — ${t.name}`} />)}
          <Panel title="Engineering events" actions={<Link to="/studio/audit" className="small">Filter & details</Link>} flush>
            <AuditTable subject={scope?.twin.id} />
          </Panel>
        </div>
        <div className="stack">
          <Panel title="Deployments" flush>
            <QueryState query={deployments}>
              {(list) => (
                <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
                  {list.slice(0, 20).map((d) => (
                    <li key={d.id} className="stack-sm" style={{ gap: 2 }}>
                      <span className="row-wrap small">
                        <StatusBadge tone={d.kind === 'rollback' ? 'warning' : 'info'} label={d.kind === 'rollback' ? 'Rollback' : 'Deploy'} />
                        <strong>{d.twinId}</strong> → <PackageLink id={d.packageId} />
                      </span>
                      <span className="xsmall subtle">
                        <TimeStamp value={d.deployedAt} /> by {d.deployedBy}
                        {d.reason ? ` — ${d.reason}` : ''}
                      </span>
                    </li>
                  ))}
                </ul>
              )}
            </QueryState>
          </Panel>
          <AuditVerifyPanel />
        </div>
      </div>
    </div>
  );
}
