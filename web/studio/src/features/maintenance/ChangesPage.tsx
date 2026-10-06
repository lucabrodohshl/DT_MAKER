/** Maintenance › Change workspace: list of changes and creation of a new one. */
import { GitPullRequest, Plus } from 'lucide-react';
import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { api } from '@/api/client';
import { useChanges, useEngineeringMutation, useTwins } from '@/api/queries';
import type { Change } from '@/api/types';
import { Button, Dialog, EmptyState, ErrorBlock, PageHeader, Panel, QueryState, Segmented, StatusBadge, TimeStamp } from '@/design';
import { Crumbs } from '@/features/common/links';
import { useTwinScope } from '@/app/twinScope';

export function NewChangeDialog({ open, onOpenChange, defaultTwin }: { open: boolean; onOpenChange: (o: boolean) => void; defaultTwin?: string }) {
  const twins = useTwins();
  const [twin, setTwin] = useState(defaultTwin ?? '');
  const [title, setTitle] = useState('');
  const [description, setDescription] = useState('');
  const navigate = useNavigate();
  const create = useEngineeringMutation((b: { twinId: string; title: string; description: string }) => api.post<Change>('/changes', b));
  const twinId = twin || twins.data?.[0]?.id || '';
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title="Open a change"
      description="A change groups coordinated drafts (ontology, interpretations, models) of one twin through validation, refinement, alignment, packaging and release."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button variant="primary" disabled={!title.trim() || !twinId} loading={create.isPending}
            onClick={() => create.mutate({ twinId, title: title.trim(), description }, { onSuccess: (c) => { onOpenChange(false); navigate(`/studio/changes/${c.id}`); } })}>
            Open change
          </Button>
        </>
      }
    >
      <div className="stack">
        <label className="vts-field">
          <span>Twin</span>
          <select className="vts-select" value={twinId} onChange={(e) => setTwin(e.target.value)}>
            {twins.data?.map((t) => <option key={t.id} value={t.id}>{t.name}</option>)}
          </select>
        </label>
        <label className="vts-field">
          <span>Title</span>
          <input className="vts-input" value={title} onChange={(e) => setTitle(e.target.value)} placeholder="e.g. Datasheet revision C" />
        </label>
        <label className="vts-field">
          <span>Motivation</span>
          <textarea className="vts-textarea" value={description} onChange={(e) => setDescription(e.target.value)} />
        </label>
        {create.error && <ErrorBlock error={create.error} compact />}
      </div>
    </Dialog>
  );
}

export default function ChangesPage() {
  const scope = useTwinScope();
  // In a twin, show everything (open, released, abandoned): an empty "open" list hides the history.
  const [state, setState] = useState<'open' | 'all'>(scope ? 'all' : 'open');
  const all = useChanges(state === 'open' ? 'open' : undefined);
  const q = scope ? { ...all, data: all.data?.filter((c) => c.twinId === scope.twin.id) } as typeof all : all;
  const [newOpen, setNewOpen] = useState(false);
  return (
    <div className="vts-page">
      <PageHeader
        eyebrow={<Crumbs items={[{ label: 'Maintenance' }, { label: 'Change workspace' }]} />}
        title={scope ? 'Changes' : 'Change workspace'}
        meta={<span>{scope ? `Changes of ${scope.twin.name} in progress or released. Editing happens in Studio. ` : ''}Evolve ontologies, interpretations and models safely: edit → validate → refinement → impact → alignment → compile → package → release → deploy</span>}
        actions={<Button variant="primary" icon={<Plus size={14} />} onClick={() => setNewOpen(true)}>New change</Button>}
      />
      <Panel title="Changes" actions={<Segmented label="Show" value={state} onChange={setState} options={[{ id: 'open', label: 'Open' }, { id: 'all', label: 'All' }]} />} flush>
        <QueryState query={q} isEmpty={(d) => d.length === 0} empty={<EmptyState compact icon={<GitPullRequest size={24} />} title="No changes" action={<Button size="sm" onClick={() => setNewOpen(true)}>Open a change</Button>} />}>
          {(list) => (
            <table className="vts-table">
              <caption className="sr-only">Changes</caption>
              <thead><tr><th scope="col">Change</th><th scope="col">Twin</th><th scope="col">Artefacts</th><th scope="col">State</th><th scope="col">Opened</th></tr></thead>
              <tbody>
                {list.map((c) => (
                  <tr key={c.id}>
                    <td><Link to={`/studio/changes/${c.id}`} className="strong">{c.title}</Link><div className="mono xsmall subtle">{c.id}</div></td>
                    <td>{c.twinId}</td>
                    <td className="mono small">{c.artifacts.join(', ') || '—'}</td>
                    <td><StatusBadge tone={c.state === 'open' ? 'info' : c.state === 'released' ? 'ok' : 'neutral'} label={c.state} /></td>
                    <td className="small"><TimeStamp value={c.createdAt} /> by {c.createdBy}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </QueryState>
      </Panel>
      <NewChangeDialog open={newOpen} onOpenChange={setNewOpen} defaultTwin={scope?.twin.id} />
    </div>
  );
}
