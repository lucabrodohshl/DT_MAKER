/**
 * Command palette and global search (⌘K or "/"). Commands: global actions (new Blueprint, import,
 * your twins, Studio) and, inside a Blueprint workspace, "Go to" every section of the open
 * version. Search: Blueprints and their elements (asset types, assets, world objects, signals,
 * events, commands, sources, requirements, monitors, scenarios, model states), twins, assets,
 * artefacts and versions ("process-pump@2"), ontology symbols and axioms, interpretation
 * entries, telemetry channels, evidence, packages, deployments and changes — ranked by the
 * backend (GET /api/v1/search).
 */
import * as RadixDialog from '@radix-ui/react-dialog';
import { ArrowRight, Bell, Blocks, Boxes, CircleGauge, ClipboardCheck, FileCheck2, FileUp, FlaskConical, GitPullRequest, LayoutGrid, Map as MapIcon, Package, Plus, Radio, Rocket, Search, Shapes, Sigma, SplitSquareHorizontal, TerminalSquare, Workflow, type LucideIcon } from 'lucide-react';
import { useId, useMemo, useState } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { ALL_ITEMS } from '@/features/blueprint/nav';
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
  blueprint: LayoutGrid,
  asset_type: Shapes,
  blueprint_asset: Boxes,
  world_object: MapIcon,
  telemetry: CircleGauge,
  event: Radio,
  command: TerminalSquare,
  data_source: CircleGauge,
  requirement: ClipboardCheck,
  monitor: Bell,
  scenario: FlaskConical,
  state: Workflow,
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
  blueprint: 'Blueprint',
  asset_type: 'Asset type',
  blueprint_asset: 'Blueprint asset',
  world_object: 'World object',
  telemetry: 'Signal',
  event: 'Event',
  command: 'Command',
  data_source: 'Data source',
  requirement: 'Requirement',
  monitor: 'Monitor',
  scenario: 'Scenario',
  state: 'Model state',
};

interface Command {
  id: string;
  label: string;
  hint: string;
  route: string;
  icon: LucideIcon;
}

const GLOBAL_COMMANDS: Command[] = [
  { id: 'new-bp', label: 'New Blueprint', hint: 'Guided wizard', route: '/studio/new', icon: Plus },
  { id: 'studio', label: 'Studio: Twin Blueprints', hint: 'Blueprint library', route: '/studio', icon: LayoutGrid },
  { id: 'twins', label: 'Your twins', hint: 'Operate', route: '/twins', icon: Boxes },
  { id: 'template', label: 'New Blueprint from a template', hint: 'Mobile robot, process equipment…', route: '/studio/new?mode=template', icon: Plus },
  { id: 'import', label: 'Import a twin (Blueprint bundle)', hint: 'twin-blueprint-bundle/1', route: '/studio/new?mode=import', icon: FileUp },
  { id: 'formal', label: 'Import formal models', hint: 'UPPAAL views, ontology, interpretations', route: '/studio/new?mode=formal', icon: FileUp },
  { id: 'instantiate', label: 'Instantiate a published Blueprint', hint: 'Create a twin instance', route: '/twins?create=instantiate', icon: Rocket },
];

export function SearchPalette({ open, onOpenChange }: { open: boolean; onOpenChange: (o: boolean) => void }) {
  const [q, setQ] = useState('');
  const [active, setActive] = useState(0);
  const navigate = useNavigate();
  const search = useSearch(q);
  const location = useLocation();
  const listId = useId();
  const commands = useMemo(() => {
    const m = /^\/studio\/blueprints\/([^/]+)\/v\/(\d+)/.exec(location.pathname);
    const local: Command[] = m
      ? ALL_ITEMS.map((i) => ({ id: `go-${i.to}`, label: `Go to ${i.label}`, hint: i.hint, route: `/studio/blueprints/${m[1]}/v/${m[2]}${i.to ? `/${i.to}` : ''}`, icon: i.icon }))
      : [];
    const all = [...local, ...GLOBAL_COMMANDS];
    const t = q.trim().replace(/^>/, '').trim().toLowerCase();
    if (!t) return all.slice(0, 8);
    return all.filter((c) => `${c.label} ${c.hint}`.toLowerCase().includes(t)).slice(0, 6);
  }, [location.pathname, q]);
  const searchHits = q.trim().startsWith('>') ? [] : (search.data?.hits ?? []);
  const hits = [...commands.map((c) => ({ kind: 'command_palette', id: c.id, title: c.label, subtitle: c.hint, route: c.route, icon: c.icon })), ...searchHits.map((h) => ({ ...h, icon: undefined as LucideIcon | undefined }))];

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
              placeholder="Search or run a command: Pump P-101, bearing_temp, HEATING, REQ-S1, > New Blueprint…"
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
            {q.trim().length < 2 && commands.length === 0 && <li className="small subtle" style={{ padding: 10 }}>Type at least two characters.</li>}
            {q.trim().length >= 2 && search.isFetching && hits.length === 0 && <li className="small subtle" style={{ padding: 10 }}>Searching…</li>}
            {q.trim().length >= 2 && !search.isFetching && hits.length === 0 && <li className="small subtle" style={{ padding: 10 }}>No results.</li>}
            {hits.map((h, i) => {
              const Icon = h.icon ?? KIND_ICON[h.kind] ?? Search;
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
                    {h.kind === 'command_palette' ? (
                      <ArrowRight size={14} aria-label="Command" className="subtle" />
                    ) : (
                      <span className="vts-tag">{KIND_LABEL[h.kind] ?? h.kind}</span>
                    )}
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
