/** Executions of one twin (from its runtime), with links to ledger and replay. */
import { History } from 'lucide-react';
import { Link } from 'react-router-dom';
import { useExecutions } from '@/runtime/client';
import { EmptyState, HashChip, Panel, QueryState, StatusBadge } from '@/design';
import { useStudioPackageForHash } from './ledger';
import { PackageLink } from '@/features/common/links';

function PackageCell({ twinId, hash }: { twinId: string; hash: string }) {
  const pkg = useStudioPackageForHash(twinId, hash);
  return pkg ? <PackageLink id={pkg.id} /> : <HashChip value={hash} label="package hash" />;
}

export function ExecutionsPanel({ twinId, title = 'Executions' }: { twinId: string; title?: string }) {
  const execs = useExecutions(twinId);
  return (
    <Panel title={title} subtitle="Recorded by the twin's runtime; each has its own tamper-evident ledger" flush>
      <QueryState query={execs} isEmpty={(d) => d.length === 0} empty={<EmptyState compact title="No executions recorded" />}>
        {(list) => (
          <table className="vts-table">
            <caption className="sr-only">Executions</caption>
            <thead>
              <tr>
                <th scope="col">Execution</th>
                <th scope="col">Status</th>
                <th scope="col">Package</th>
                <th scope="col" className="num">Records</th>
                <th scope="col">Last state</th>
                <th scope="col" />
              </tr>
            </thead>
            <tbody>
              {list.map((e) => (
                <tr key={e.session}>
                  <td>
                    <Link to={`/twins/${encodeURIComponent(twinId)}/audit/executions/${encodeURIComponent(e.session)}`} className="mono small">
                      {e.session}
                    </Link>
                    <div className="xsmall subtle">{e.started}</div>
                  </td>
                  <td>{e.current ? <StatusBadge tone="info" label="Running" /> : e.ended ? <StatusBadge tone="neutral" label="Ended" /> : <StatusBadge tone="warning" label="Not closed" />}</td>
                  <td><PackageCell twinId={twinId} hash={e.package_hash} /></td>
                  <td className="num">{e.records}</td>
                  <td>{e.last_location ?? '—'}</td>
                  <td>
                    {e.replayable ? (
                      <Link className="vts-btn vts-btn--sm" to={`/twins/${encodeURIComponent(twinId)}/audit/executions/${encodeURIComponent(e.session)}/replay`}>
                        <History size={13} /> Replay
                      </Link>
                    ) : (
                      <span className="xsmall subtle" title="The package recorded in this ledger is not available to the runtime">Not replayable</span>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </QueryState>
    </Panel>
  );
}
