/**
 * One execution: summary, chain verification, tamper drill (on a copy), and its
 * ledger records (filterable, paged) with full record details.
 */
import { useMutation, useQuery } from '@tanstack/react-query';
import type { ColumnDef } from '@tanstack/react-table';
import { Download, FlaskConical, History } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useParams, useSearchParams } from 'react-router-dom';
import { useTwin } from '@/api/queries';
import { runtimeApi, useLedger } from '@/runtime/client';
import type { Execution, LedgerPage, LedgerRecord, LedgerVerification } from '@/runtime/types';
import { Button, Callout, DataTable, HashChip, KeyValue, PageHeader, Panel, QueryState, StatusBadge, Segmented, downloadText } from '@/design';
import { Crumbs, PackageLink } from '@/features/common/links';
import { KIND_LABEL, RecordDrawer, VerificationResult, VerifyButton, kindTone, recordSummary, useStudioPackageForHash } from './ledger';

export default function ExecutionPage() {
  const { twinId = '', session = '' } = useParams();
  const [params] = useSearchParams();
  const twin = useTwin(twinId);
  const exec = useQuery({
    queryKey: ['runtime', twinId, 'execution', session],
    queryFn: () => runtimeApi(twinId).get<Execution>(`/runtime/executions/${encodeURIComponent(session)}`),
  });
  const [kind, setKind] = useState<string>(params.get('kind') ?? '');
  const [page, setPage] = useState(0);
  const pageSize = 200;
  const ledger = useLedger(twinId, session, page * pageSize, kind, pageSize);
  const [selected, setSelected] = useState<LedgerRecord | null>(null);
  const [text, setText] = useState('');
  const studioPkg = useStudioPackageForHash(twinId, exec.data?.package_hash);
  const drill = useMutation({
    mutationFn: () => runtimeApi(twinId).post<LedgerVerification & { altered_line?: number; note?: string }>('/runtime/ledger/tamper-drill', { session }),
  });

  const records = useMemo(() => {
    const all = ledger.data?.records ?? [];
    const q = text.trim().toLowerCase();
    return q ? all.filter((r) => recordSummary(r).toLowerCase().includes(q)) : all;
  }, [ledger.data, text]);

  const columns: ColumnDef<LedgerRecord, unknown>[] = [
    { header: '#', accessorKey: 'seq', meta: { align: 'right', width: 60 } },
    {
      header: 'Kind',
      id: 'kind',
      accessorFn: (r) => r.body.kind,
      cell: (c) => <StatusBadge tone={kindTone(c.row.original.body.kind)} label={KIND_LABEL[c.row.original.body.kind] ?? c.row.original.body.kind} />,
    },
    { header: 'Logical time', id: 't', accessorFn: (r) => r.body.time_after ?? r.body.at ?? 0, cell: (c) => <span className="num small">{String(c.getValue())} ticks</span> },
    { header: 'Record', id: 'summary', accessorFn: (r) => recordSummary(r), cell: (c) => <span className="small">{String(c.getValue())}</span> },
    { header: 'Hash', id: 'hash', enableSorting: false, cell: (c) => <HashChip value={c.row.original.hash} length={10} /> },
  ];

  // The whole execution, exactly as the runtime returns it (paged forward from the start).
  const exportLedger = useMutation({
    mutationFn: async () => {
      const records: LedgerRecord[] = [];
      let since = 0;
      for (;;) {
        const page = await runtimeApi(twinId).get<LedgerPage>('/runtime/ledger', { session, since: String(since), limit: '1000' });
        records.push(...page.records);
        if (page.records.length < 1000) return { twinId, session, ledger: page.ledger, total_records: page.total_records, records };
        since = page.records[page.records.length - 1]!.seq + 1;
      }
    },
    onSuccess: (doc) => downloadText(`ledger-${twinId}-${session}.json`, 'application/json', JSON.stringify(doc, null, 2)),
  });

  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Audit', to: '/audit' }, { label: 'Executions', to: `/audit/ledger?twin=${encodeURIComponent(twinId)}` }, { label: session }]} />}
        title={`Execution ${session}`}
        meta={twin.data && <span>{twin.data.name}</span>}
        actions={
          <>
            <Button icon={<Download size={14} />} loading={exportLedger.isPending} onClick={() => exportLedger.mutate()}>
              Export ledger (JSON)
            </Button>
            <Link className="vts-btn vts-btn--primary" to={`/twins/${encodeURIComponent(twinId)}/audit/executions/${encodeURIComponent(session)}/replay`}>
              <History size={14} /> Replay
            </Link>
          </>
        }
      />
      <div className="grid-main-side">
        <div className="stack">
          <Panel
            title="Ledger records"
            subtitle={ledger.data ? `${ledger.data.matched ?? records.length} matching of ${ledger.data.total_records ?? '?'} records` : undefined}
            actions={
              <>
                <input className="vts-input" placeholder="Search transitions/events…" value={text} onChange={(e) => setText(e.target.value)} aria-label="Search records" />
                <Segmented
                  label="Record kind"
                  value={kind}
                  onChange={(k) => {
                    setKind(k);
                    setPage(0);
                  }}
                  options={[
                    { id: '', label: 'All' },
                    { id: 'step', label: 'Transitions' },
                    { id: 'reject', label: 'Refused' },
                    { id: 'context', label: 'Context' },
                    { id: 'alarm', label: 'Alarms' },
                  ]}
                />
              </>
            }
            footer={
              <div className="row-between">
                <span>Records are shown exactly as recorded; select one for all hashes and the canonical JSON.</span>
                <span className="row">
                  <Button size="sm" disabled={page === 0} onClick={() => setPage((p) => p - 1)}>Previous</Button>
                  <Button size="sm" disabled={(ledger.data?.records.length ?? 0) < pageSize} onClick={() => setPage((p) => p + 1)}>Next</Button>
                </span>
              </div>
            }
            flush
          >
            <QueryState query={ledger}>
              {() => (
                <DataTable
                  caption="Ledger records"
                  data={records}
                  columns={columns}
                  getRowId={(r) => String(r.seq)}
                  onRowClick={setSelected}
                  selectedId={selected ? String(selected.seq) : null}
                  maxHeight={620}
                />
              )}
            </QueryState>
          </Panel>
        </div>
        <div className="stack">
          <Panel title="Execution">
            <QueryState query={exec}>
              {(e) => (
                <KeyValue
                  compact
                  items={[
                    ['Status', e.current ? <StatusBadge key="s" tone="info" label="Running" /> : e.ended ? <StatusBadge key="s" tone="neutral" label="Ended" /> : <StatusBadge key="s" tone="warning" label="Not closed" />],
                    ['Model', `${e.model_id ?? '?'} ${e.model_version ?? ''}`],
                    ['Package', studioPkg ? <PackageLink key="p" id={studioPkg.id} /> : <HashChip key="p" value={e.package_hash} />],
                    ['Package hash', <HashChip key="ph" value={e.package_hash} />],
                    ['Records', String(e.records)],
                    ['Started', e.started],
                    ['Last state', e.last_location ?? '—'],
                    ['Replayable', e.replayable ? 'Yes — the recorded package is available' : 'No — recorded package not available'],
                  ]}
                />
              )}
            </QueryState>
          </Panel>
          <Panel title="Tamper-evidence">
            <div className="stack">
              <VerifyButton twinId={twinId} session={session} />
              <hr className="vts-divider" />
              <p className="small muted">
                Tamper drill: the runtime alters one field in a <strong>copy</strong> of this ledger and verifies the copy. The
                real ledger is never modified.
              </p>
              <Button icon={<FlaskConical size={14} />} loading={drill.isPending} onClick={() => drill.mutate()}>
                Run tamper drill on a copy
              </Button>
              {drill.data && (
                <div className="stack-sm">
                  <VerificationResult result={drill.data} error={null} />
                  {drill.data.valid && <Callout tone="warning">The altered copy still verified — this would indicate a verifier defect.</Callout>}
                </div>
              )}
              {drill.error && <VerificationResult result={undefined} error={drill.error} />}
            </div>
          </Panel>
        </div>
      </div>
      <RecordDrawer record={selected} open={!!selected} onOpenChange={(o) => !o && setSelected(null)} studioPackage={studioPkg} />
    </div>
  );
}
