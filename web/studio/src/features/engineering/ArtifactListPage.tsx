/** Engineering › Ontologies / Interpretations / Models: list of versioned artefacts. */
import type { ColumnDef } from '@tanstack/react-table';
import { useLocation, useNavigate } from 'react-router-dom';
import { useArtifacts } from '@/api/queries';
import type { ArtifactKind, ArtifactSummary } from '@/api/types';
import { DataTable, EmptyState, LifecycleBadge, PageHeader, Panel, QueryState, TimeStamp } from '@/design';
import { Crumbs, RefLink } from '@/features/common/links';

const SECTIONS: Record<string, { title: string; kinds: ArtifactKind[]; intro: string }> = {
  ontologies: { title: 'Ontologies', kinds: ['ontology'], intro: 'Formal domain theories K = (S, F, R, Δ) used by interpretation and alignment — distinct from the asset knowledge graph.' },
  interpretations: { title: 'Interpretations', kinds: ['interpretation'], intro: 'Meaning of each behavioural label (location or event) as a formula over an ontology version.' },
  models: { title: 'Models', kinds: ['pt_model', 'dt_model'], intro: 'Physical-Twin and Digital-Twin behavioural views (timed automata). The DT view is compiled to the executed Twin IR.' },
};

export default function ArtifactListPage() {
  const section = useLocation().pathname.split('/')[2] ?? 'ontologies';
  const s = SECTIONS[section] ?? SECTIONS.ontologies!;
  const q = useArtifacts();
  const navigate = useNavigate();
  const columns: ColumnDef<ArtifactSummary, unknown>[] = [
    { header: 'Name', accessorKey: 'name', cell: (c) => <span className="strong">{c.row.original.name}</span> },
    { header: 'Identifier', accessorKey: 'id', cell: (c) => <span className="mono small">{String(c.getValue())}</span> },
    ...(section === 'models' ? [{ header: 'View', accessorKey: 'kind', cell: (c: { getValue: () => unknown }) => (c.getValue() === 'dt_model' ? 'Digital Twin' : 'Physical Twin') } as ColumnDef<ArtifactSummary, unknown>] : []),
    { header: 'Published', id: 'pub', accessorFn: (a) => a.published?.version ?? 0, cell: (c) => (c.row.original.published ? <RefLink refId={c.row.original.published.ref} kind={c.row.original.kind} /> : <span className="subtle">none</span>) },
    { header: 'Open draft', id: 'open', accessorFn: (a) => a.open?.version ?? 0, cell: (c) => (c.row.original.open ? <span className="row"><RefLink refId={c.row.original.open.ref} kind={c.row.original.kind} /> <LifecycleBadge state={c.row.original.open.state} /></span> : <span className="subtle">—</span>) },
    { header: 'Versions', accessorKey: 'versionCount', meta: { align: 'right' } },
    { header: 'Last change', id: 'upd', accessorFn: (a) => a.latest?.updatedAt ?? '', cell: (c) => <TimeStamp value={String(c.getValue())} relative /> },
  ];
  return (
    <div className="vts-page">
      <PageHeader eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: s.title }]} />} title={s.title} meta={<span>{s.intro}</span>} />
      <Panel title={`${s.title}`} flush>
        <QueryState query={q}>
          {(all) => (
            <DataTable
              caption={s.title}
              data={all.filter((a) => s.kinds.includes(a.kind))}
              columns={columns}
              getRowId={(a) => a.id}
              onRowClick={(a) => navigate(`/studio/${section}/${encodeURIComponent(a.id)}`)}
              empty={<EmptyState compact title={`No ${s.title.toLowerCase()} yet`} />}
            />
          )}
        </QueryState>
      </Panel>
    </div>
  );
}
