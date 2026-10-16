/** Engineering › Ontologies / Interpretations / Models: list of versioned artefacts. */
import { useState } from 'react';
import type { ColumnDef } from '@tanstack/react-table';
import { useLocation, useNavigate } from 'react-router-dom';
import { api } from '@/api/client';
import { useArtifacts, useEngineeringMutation } from '@/api/queries';
import type { ArtifactDetail, ArtifactKind, ArtifactSummary, VersionDetail } from '@/api/types';
import { Button, DataTable, Dialog, EmptyState, ErrorBlock, LifecycleBadge, PageHeader, Panel, QueryState, TimeStamp } from '@/design';
import { Crumbs, RefLink } from '@/features/common/links';

const SECTIONS: Record<string, { title: string; kinds: ArtifactKind[]; intro: string }> = {
  ontologies: { title: 'Ontologies', kinds: ['ontology'], intro: 'Formal theories with sorts, functions, relations and axioms. More expressive than the asset knowledge graph.' },
  interpretations: { title: 'Interpretations', kinds: ['interpretation'], intro: 'Meaning of each behavioural label (location or event) as a formula over an ontology version.' },
  models: { title: 'Timed automata', kinds: ['pt_model', 'dt_model'], intro: 'Physical and digital behaviour views. The digital view compiles into the running twin.' },
};

type SavePicker = (options: { suggestedName: string; types: { description: string; accept: Record<string, string[]> }[] }) => Promise<{ createWritable: () => Promise<{ write: (data: Blob) => Promise<void>; close: () => Promise<void> }> }>;

async function saveFile(content: string, name: string, mime: string) {
  const blob = new Blob([content], { type: mime });
  const picker = (window as Window & { showSaveFilePicker?: SavePicker }).showSaveFilePicker;
  if (picker) {
    const handle = await picker({ suggestedName: name, types: [{ description: 'Twin artefact export', accept: { [mime]: [`.${name.split('.').at(-1)}`] } }] });
    const writable = await handle.createWritable();
    await writable.write(blob);
    await writable.close();
  } else {
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 30_000);
  }
}

async function exportArtefacts(items: ArtifactSummary[], fullHistory: boolean) {
  const result = await Promise.all(items.map(async (a) => {
    const detail = await api.get<ArtifactDetail>(`/artifacts/${encodeURIComponent(a.id)}`);
    const versions = fullHistory ? detail.versions : detail.versions.slice(0, 1);
    return { id: a.id, kind: a.kind, name: a.name, description: a.description,
      versions: await Promise.all(versions.map(async (v) => {
        const source = await api.get<VersionDetail>(`/artifacts/${encodeURIComponent(a.id)}/versions/${v.version}`);
        return { version: v.version, state: v.state, refs: v.refs, content: source.content };
      })) };
  }));
  const one = result[0];
  if (result.length === 1 && !fullHistory && one?.versions[0]) {
    const ext = one.kind === 'ontology' ? 'ont' : one.kind === 'interpretation' ? 'interp' : 'xml';
    await saveFile(one.versions[0].content, `${one.id}@${one.versions[0].version}.${ext}`, 'text/plain');
  } else {
    await saveFile(JSON.stringify({ format: 'twin-artifacts/1', artifacts: result }, null, 2),
      result.length === 1 ? `${one!.id}-history.json` : `twin-artifacts-${new Date().toISOString().slice(0, 10)}.json`, 'application/json');
  }
}

export default function ArtifactListPage() {
  const section = useLocation().pathname.split('/')[2] ?? 'ontologies';
  const s = SECTIONS[section] ?? SECTIONS.ontologies!;
  const q = useArtifacts();
  const navigate = useNavigate();
  const [selected, setSelected] = useState<Set<string>>(() => new Set());
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [exportOpen, setExportOpen] = useState(false);
  const [fullHistory, setFullHistory] = useState(false);
  const [exportBusy, setExportBusy] = useState(false);
  const [exportError, setExportError] = useState<Error | null>(null);
  const visible = (q.data ?? []).filter((a) => s.kinds.includes(a.kind));
  const chosen = visible.filter((a) => selected.has(a.id));
  const allSelected = visible.length > 0 && visible.every((a) => selected.has(a.id));
  const archive = useEngineeringMutation((ids: string[]) => api.post<{ archived: string[] }>('/artifacts/archive', { ids }));
  const toggle = (id: string) => setSelected((old) => { const next = new Set(old); if (next.has(id)) next.delete(id); else next.add(id); return next; });
  const columns: ColumnDef<ArtifactSummary, unknown>[] = [
    { id: 'select', enableSorting: false,
      header: () => <input type="checkbox" aria-label={`Select all ${s.title.toLowerCase()}`} checked={allSelected} onChange={() => setSelected(allSelected ? new Set() : new Set(visible.map((a) => a.id)))} onClick={(e) => e.stopPropagation()} />,
      cell: (c) => <input type="checkbox" aria-label={`Select ${c.row.original.name}`} checked={selected.has(c.row.original.id)} onChange={() => toggle(c.row.original.id)} onClick={(e) => e.stopPropagation()} onKeyDown={(e) => e.stopPropagation()} /> },
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
        <div className="row" style={{ padding: '10px 14px' }}>
          <span className="small">{chosen.length} selected</span><span className="grow" />
          <Button size="sm" disabled={!chosen.length} onClick={() => { setExportError(null); setExportOpen(true); }}>Export…</Button>
          <Button size="sm" variant="danger" disabled={!chosen.length} onClick={() => { archive.reset(); setConfirmDelete(true); }}>Delete…</Button>
        </div>
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
      <Dialog open={confirmDelete} onOpenChange={setConfirmDelete} title={`Delete ${chosen.length} selected ${chosen.length === 1 ? 'artefact' : 'artefacts'}?`}
        description="These entries leave the library. Saved versions and references used by published or running twins remain available.">
        <div className="stack"><p>{chosen.map((a) => a.name).join(', ')}</p>{archive.error && <ErrorBlock error={archive.error} compact />}
          <div className="row"><span className="grow" /><Button onClick={() => setConfirmDelete(false)}>Cancel</Button>
            <Button variant="danger" loading={archive.isPending} onClick={() => archive.mutate(chosen.map((a) => a.id), { onSuccess: () => { setSelected(new Set()); setConfirmDelete(false); } })}>Delete from library</Button></div></div>
      </Dialog>
      <Dialog open={exportOpen} onOpenChange={setExportOpen} title={`Export ${chosen.length} selected ${chosen.length === 1 ? 'artefact' : 'artefacts'}`}
        description="Choose a save location. Browsers without a save picker use their configured download location.">
        <div className="stack"><p>{chosen.map((a) => a.name).join(', ')}</p>
          <label className="row small"><input type="checkbox" checked={fullHistory} onChange={(e) => setFullHistory(e.target.checked)} /> Include every version (JSON bundle)</label>
          {chosen.length > 1 && <p className="small subtle">Multiple artefacts are saved together as one portable JSON bundle.</p>}
          {exportError && <ErrorBlock error={exportError} compact />}
          <div className="row"><span className="grow" /><Button onClick={() => setExportOpen(false)}>Cancel</Button>
            <Button variant="primary" loading={exportBusy} onClick={() => { setExportBusy(true); setExportError(null); void exportArtefacts(chosen, fullHistory).then(() => setExportOpen(false)).catch((e) => { if (!(e instanceof DOMException && e.name === 'AbortError')) setExportError(e instanceof Error ? e : new Error(String(e))); }).finally(() => setExportBusy(false)); }}>Choose location and save</Button></div>
        </div>
      </Dialog>
    </div>
  );
}
