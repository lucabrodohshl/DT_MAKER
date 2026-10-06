/**
 * Global search (⌘K or "/"): assets, twins, artefacts and versions ("process-pump@2"),
 * ontology symbols and axioms, interpretation entries, telemetry channels, evidence,
 * packages, deployments and changes. Results come from GET /api/v1/search.
 */
import * as RadixDialog from '@radix-ui/react-dialog';
import { Blocks, Boxes, CircleGauge, FileCheck2, GitPullRequest, Package, Rocket, Search, Sigma, SplitSquareHorizontal, Workflow, type LucideIcon } from 'lucide-react';
import { useId, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { useSearch } from '@/api/queries';

const KIND_ICON: Record<string, LucideIcon> = {
  asset: Boxes,
  twin: Workflow,
  ontology: Blocks,
  ontology_version: Blocks,
  ontology_symbol: Sigma,
  ontology_axiom: Sigma,
  interpretation: SplitSquareHorizontal,
  interpretation_version: SplitSquareHorizontal,
  interpretation_entry: SplitSquareHorizontal,
  pt_model: Workflow,
  dt_model: Workflow,
  telemetry_channel: CircleGauge,
  evidence: FileCheck2,
  package: Package,
  deployment: Rocket,
  change: GitPullRequest,
};

const KIND_LABEL: Record<string, string> = {
  asset: 'Asset',
  twin: 'Twin',
  ontology: 'Ontology',
  ontology_version: 'Ontology version',
  ontology_symbol: 'Ontology symbol',
  ontology_axiom: 'Axiom',
  interpretation: 'Interpretation',
  interpretation_version: 'Interpretation version',
  interpretation_entry: 'Proposition / event',
  pt_model: 'PT model',
  dt_model: 'DT model',
  telemetry_channel: 'Telemetry',
  evidence: 'Evidence',
  package: 'Package',
  deployment: 'Deployment',
  change: 'Change',
};

export function SearchPalette({ open, onOpenChange }: { open: boolean; onOpenChange: (o: boolean) => void }) {
  const [q, setQ] = useState('');
  const [active, setActive] = useState(0);
  const navigate = useNavigate();
  const search = useSearch(q);
  const hits = search.data?.hits ?? [];
  const listId = useId();

  const setOpen = (o: boolean) => {
    if (!o) {
      setQ('');
      setActive(0);
    }
    onOpenChange(o);
  };

  const go = (route: string) => {
    setOpen(false);
    navigate(route);
  };

  return (
    <RadixDialog.Root open={open} onOpenChange={setOpen}>
      <RadixDialog.Portal>
        <RadixDialog.Overlay className="vts-overlay" />
        <RadixDialog.Content className="vts-palette" aria-label="Global search">
          <RadixDialog.Title className="sr-only">Global search</RadixDialog.Title>
          <RadixDialog.Description className="sr-only">Type at least two characters; use arrow keys and Enter.</RadixDialog.Description>
          <div className="row" style={{ paddingLeft: 14 }}>
            <Search size={18} aria-hidden="true" className="subtle" />
            <input
              autoFocus
              value={q}
              onChange={(e) => { setQ(e.target.value); setActive(0); }}
              placeholder="Search: Pump P-101, bearing_temp, process-pump@2, EV-0012, CHG-0002…"
              role="combobox"
              aria-expanded={hits.length > 0}
              aria-controls={listId}
              aria-activedescendant={hits[active] ? `${listId}-${active}` : undefined}
              onKeyDown={(e) => {
                if (e.key === 'ArrowDown') {
                  e.preventDefault();
                  setActive((a) => Math.min(a + 1, hits.length - 1));
                } else if (e.key === 'ArrowUp') {
                  e.preventDefault();
                  setActive((a) => Math.max(a - 1, 0));
                } else if (e.key === 'Enter' && hits[active]) {
                  e.preventDefault();
                  go(hits[active]!.route);
                }
              }}
            />
          </div>
          <ul id={listId} role="listbox" aria-label="Results">
            {q.trim().length < 2 && <li className="small subtle" style={{ padding: 10 }}>Type at least two characters.</li>}
            {q.trim().length >= 2 && search.isFetching && hits.length === 0 && <li className="small subtle" style={{ padding: 10 }}>Searching…</li>}
            {q.trim().length >= 2 && !search.isFetching && hits.length === 0 && <li className="small subtle" style={{ padding: 10 }}>No results.</li>}
            {hits.map((h, i) => {
              const Icon = KIND_ICON[h.kind] ?? Search;
              return (
                <li key={`${h.kind}-${h.id}-${i}`} id={`${listId}-${i}`} role="option" aria-selected={i === active} onMouseEnter={() => setActive(i)}>
                  <a
                    href={h.route}
                    onClick={(e) => {
                      e.preventDefault();
                      go(h.route);
                    }}
                  >
                    <Icon size={16} aria-hidden="true" className="subtle" />
                    <span className="grow" style={{ minWidth: 0 }}>
                      <span className="strong truncate" style={{ display: 'block' }}>{h.title}</span>
                      <span className="xsmall subtle truncate" style={{ display: 'block' }}>{h.subtitle}</span>
                    </span>
                    <span className="vts-tag">{KIND_LABEL[h.kind] ?? h.kind}</span>
                  </a>
                </li>
              );
            })}
          </ul>
        </RadixDialog.Content>
      </RadixDialog.Portal>
    </RadixDialog.Root>
  );
}
