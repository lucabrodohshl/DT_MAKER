/** Audit › Execution ledger: choose a twin, then an execution. */
import { useSearchParams } from 'react-router-dom';
import { useTwins } from '@/api/queries';
import { PageHeader, QueryState, RuntimeUnavailable, Panel } from '@/design';
import { Crumbs, LearnMore } from '@/features/common/links';
import { ExecutionsPanel } from './ExecutionsPanel';
import { useTwinScope } from '@/app/twinScope';

export default function LedgerPage() {
  const [params, setParams] = useSearchParams();
  const scope = useTwinScope();
  const twins = useTwins();
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Audit', to: '/audit' }, { label: 'Execution ledger' }]} />}
        title="Execution ledger"
        meta={<><span>Cryptographically tamper-evident, append-only record of every semantic step (kept by the runtime)</span><LearnMore page="audit-and-replay.html#execution-ledger" /></>}
      />
      <QueryState query={twins}>
        {(list) => {
          const twinId = scope?.twin.id ?? params.get('twin') ?? list.find((t) => t.runtimeUrl)?.id ?? list[0]?.id ?? '';
          const twin = list.find((t) => t.id === twinId);
          return (
            <div className="stack">
              {!scope && <label className="row small">
                Twin
                <select className="vts-select" value={twinId} onChange={(e) => setParams({ twin: e.target.value })}>
                  {list.map((t) => (
                    <option key={t.id} value={t.id}>
                      {t.name}
                      {t.runtimeUrl ? '' : ' (runtime not connected)'}
                    </option>
                  ))}
                </select>
              </label>}
              {twin && !twin.runtimeUrl ? (
                <Panel title="Executions"><RuntimeUnavailable what="Ledgers are written and served by the twin's runtime." /></Panel>
              ) : twin ? (
                <ExecutionsPanel twinId={twin.id} />
              ) : null}
            </div>
          );
        }}
      </QueryState>
    </div>
  );
}
