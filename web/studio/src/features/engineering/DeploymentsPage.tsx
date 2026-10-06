/** Engineering › Deployments: append-only history of which package each twin runs. */
import { Undo2 } from 'lucide-react';
import { Link, useSearchParams } from 'react-router-dom';
import { useDeployments, useTwins } from '@/api/queries';
import { EmptyState, HashChip, PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { Crumbs, PackageLink, RefLink } from '@/features/common/links';
import { useTwinScope } from '@/app/twinScope';

export default function DeploymentsPage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  const twin = scope?.twin.id ?? params.get('twin') ?? '';
  const twins = useTwins();
  const q = useDeployments(twin || undefined);
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Deployments' }]} />}
        title="Deployment history"
        meta={<span>Append-only: a rollback is a new deployment; nothing is overwritten or deleted</span>}
        actions={
          <>
            {!scope && <select className="vts-select" value={twin} onChange={(e) => setParams(e.target.value ? { twin: e.target.value } : {})} aria-label="Twin">
              <option value="">All twins</option>
              {twins.data?.map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
            </select>}
            <Link className="vts-btn" to={scope ? `${scope.base}/maintenance/rollback` : `/maintenance/rollback${twin ? `?twin=${encodeURIComponent(twin)}` : ''}`}><Undo2 size={14} /> Rollback</Link>
          </>
        }
      />
      <Panel title="Deployments" flush>
        <QueryState query={q} isEmpty={(d) => d.length === 0} empty={<EmptyState compact title="No deployments" />}>
          {(list) => (
            <table className="vts-table">
              <caption className="sr-only">Deployments</caption>
              <thead>
                <tr>
                  <th scope="col">Deployment</th>
                  <th scope="col">Twin</th>
                  <th scope="col">Package</th>
                  <th scope="col">Ontology</th>
                  <th scope="col">DT model</th>
                  <th scope="col">Interpretation</th>
                  <th scope="col">IR</th>
                  <th scope="col">When / who / why</th>
                </tr>
              </thead>
              <tbody>
                {list.map((d) => (
                  <tr key={d.id}>
                    <td>
                      <span className="mono small strong">{d.id}</span>
                      <div className="row-wrap">
                        {d.current && <StatusBadge tone="ok" label="Current" />}
                        <StatusBadge tone={d.kind === 'rollback' ? 'warning' : 'info'} label={d.kind === 'rollback' ? 'Rollback' : 'Deploy'} />
                      </div>
                    </td>
                    <td>{d.twinId}</td>
                    <td><PackageLink id={d.packageId} />{d.previousPackageId && <div className="xsmall subtle">replaced {d.previousPackageId}</div>}</td>
                    <td>{d.artifacts?.ontology ? <RefLink refId={d.artifacts.ontology} kind="ontology" /> : '—'}</td>
                    <td>{d.artifacts?.dt_model ? <RefLink refId={d.artifacts.dt_model} kind="dt_model" /> : '—'}</td>
                    <td>{d.artifacts?.dt_interpretation ? <RefLink refId={d.artifacts.dt_interpretation} kind="interpretation" /> : '—'}</td>
                    <td><HashChip value={d.irSha256} length={8} /></td>
                    <td className="small">
                      <TimeStamp value={d.deployedAt} /> · {d.deployedBy}
                      {d.reason && <div className="xsmall subtle">{d.reason}</div>}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </QueryState>
      </Panel>
    </div>
  );
}
