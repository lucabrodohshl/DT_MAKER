/** History & audit of the asset's twin: executions, deployments, engineering events. */
import { Link } from 'react-router-dom';
import { useDeployments } from '@/api/queries';
import { EmptyState, Panel, QueryState, RuntimeUnavailable, StatusBadge, TimeStamp } from '@/design';
import { useAssetContext } from '@/features/assets/AssetLayout';
import { PackageLink } from '@/features/common/links';
import { AuditTable } from './EngineeringAuditPage';
import { ExecutionsPanel } from './ExecutionsPanel';

export default function AssetHistoryTab() {
  const { twin, runtimeConnected } = useAssetContext();
  const deployments = useDeployments(twin?.id);
  if (!twin) return <EmptyState title="No digital twin" />;
  return (
    <div className="grid-main-side">
      <div className="stack">
        {runtimeConnected ? <ExecutionsPanel twinId={twin.id} /> : <Panel title="Executions"><RuntimeUnavailable what="Executions and their ledgers are kept by the twin's runtime." /></Panel>}
        <Panel title="Engineering events for this twin" flush>
          <AuditTable subject={twin.id} />
        </Panel>
      </div>
      <Panel title="Deployment history" actions={<Link className="small" to={`/maintenance/rollback?twin=${encodeURIComponent(twin.id)}`}>Rollback</Link>} flush>
        <QueryState query={deployments} isEmpty={(d) => d.length === 0} empty={<EmptyState compact title="Never deployed" />}>
          {(list) => (
            <ul className="vts-list" style={{ padding: '0 var(--s-4)' }}>
              {list.map((d) => (
                <li key={d.id} className="stack-sm" style={{ gap: 2 }}>
                  <span className="row-wrap small">
                    {d.current && <StatusBadge tone="ok" label="Current" />}
                    <StatusBadge tone={d.kind === 'rollback' ? 'warning' : 'info'} label={d.kind} />
                    <PackageLink id={d.packageId} />
                  </span>
                  <span className="xsmall subtle">
                    ontology {d.artifacts?.ontology ?? '?'} · <TimeStamp value={d.deployedAt} /> by {d.deployedBy}
                    {d.reason ? ` — ${d.reason}` : ''}
                  </span>
                </li>
              ))}
            </ul>
          )}
        </QueryState>
      </Panel>
    </div>
  );
}
