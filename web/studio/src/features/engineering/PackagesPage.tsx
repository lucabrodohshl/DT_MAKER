/** Engineering › Verified packages. */
import type { ColumnDef } from '@tanstack/react-table';
import { useNavigate } from 'react-router-dom';
import { usePackages } from '@/api/queries';
import type { PackageRecord } from '@/api/types';
import { DataTable, EmptyState, HashChip, PageHeader, Panel, QueryState, StatusBadge, TimeStamp } from '@/design';
import { Crumbs } from '@/features/common/links';

export default function PackagesPage() {
  const q = usePackages();
  const navigate = useNavigate();
  const columns: ColumnDef<PackageRecord, unknown>[] = [
    { header: 'Package', accessorKey: 'id', cell: (c) => <span className="mono strong">{String(c.getValue())}</span> },
    { header: 'Twin', accessorKey: 'twinId' },
    { header: 'Model version', accessorKey: 'modelVersion' },
    { header: 'Ontology', id: 'ont', accessorFn: (p) => p.bindings.find((b) => b.role === 'ontology')?.ref ?? '', cell: (c) => <span className="mono small">{String(c.getValue())}</span> },
    { header: 'State', accessorKey: 'state', cell: (c) => <StatusBadge tone={c.getValue() === 'released' ? 'ok' : 'info'} label={c.getValue() === 'released' ? 'Released' : 'Built (not released)'} /> },
    { header: 'Package hash', id: 'h', enableSorting: false, cell: (c) => <HashChip value={c.row.original.packageHash} length={10} /> },
    { header: 'Built', accessorKey: 'createdAt', cell: (c) => <TimeStamp value={String(c.getValue())} /> },
  ];
  return (
    <div className="vts-page">
      <PageHeader eyebrow={<Crumbs items={[{ label: 'Studio', to: '/studio' }, { label: 'Packages' }]} />} title="Verified Twin Packages" meta={<span>Immutable bundles of IR, sources, interpretations, ontology and evidence; content-hashed</span>} />
      <Panel title="Packages" flush>
        <QueryState query={q}>
          {(list) => <DataTable caption="Packages" data={list} columns={columns} getRowId={(p) => p.id} onRowClick={(p) => navigate(`/studio/packages/${p.id}`)} empty={<EmptyState compact title="No packages" />} />}
        </QueryState>
      </Panel>
    </div>
  );
}
