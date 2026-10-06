/**
 * Navigation of a twin workspace (relative to /twins/:twinId). One definition drives the
 * sidebar, the collapsed icon rail and the breadcrumbs.
 */
import {
  Activity,
  Boxes,
  BookOpenCheck,
  Cpu,
  Database,
  FlaskConical,
  History,
  LayoutDashboard,
  Network,
  Radar,
  ShieldCheck,
  Workflow,
  Wrench,
  type LucideIcon,
} from 'lucide-react';

export interface TwinNavItem {
  /** Path relative to the workspace ("" = Overview). */
  to: string;
  label: string;
}

export interface TwinNavGroup {
  id: string;
  label: string;
  icon: LucideIcon;
  /** Path of the group's landing page (used by the collapsed rail). */
  to: string;
  items?: TwinNavItem[];
}

export const TWIN_NAV: TwinNavGroup[] = [
  { id: 'overview', label: 'Overview', icon: LayoutDashboard, to: '' },
  { id: 'assets', label: 'Assets', icon: Boxes, to: 'assets' },
  { id: 'knowledge', label: 'Knowledge graph', icon: Network, to: 'knowledge' },
  {
    id: 'operations',
    label: 'Operations',
    icon: Activity,
    to: 'operations/live',
    items: [
      { to: 'operations/live', label: 'Live monitoring' },
      { to: 'operations/telemetry', label: 'Telemetry' },
      { to: 'operations/events', label: 'Events & alerts' },
    ],
  },
  {
    id: 'behavior',
    label: 'Behaviour',
    icon: Workflow,
    to: 'behavior',
    items: [
      { to: 'behavior', label: 'Current state' },
      { to: 'behavior/model', label: 'Behavioural model' },
      { to: 'behavior/facts', label: 'Semantic facts' },
      { to: 'behavior/conformance', label: 'Conformance' },
    ],
  },
  {
    id: 'predict',
    label: 'Predict',
    icon: Radar,
    to: 'predict/what-if',
    items: [
      { to: 'predict/what-if', label: 'What-if & simulation' },
      { to: 'predict/predictions', label: 'Predictions' },
      { to: 'predict/planning', label: 'Planning' },
    ],
  },
  {
    id: 'audit',
    label: 'Audit',
    icon: BookOpenCheck,
    to: 'audit',
    items: [
      { to: 'audit', label: 'Timeline' },
      { to: 'audit/provenance', label: 'Decision provenance' },
      { to: 'audit/replay', label: 'Replay' },
      { to: 'audit/ledger', label: 'Ledger' },
    ],
  },
  {
    id: 'engineering',
    label: 'Engineering',
    icon: ShieldCheck,
    to: 'engineering',
    items: [
      { to: 'engineering/models', label: 'Models' },
      { to: 'engineering/ontology', label: 'Ontology' },
      { to: 'engineering/interpretations', label: 'Interpretations' },
      { to: 'engineering/verification', label: 'Verification' },
      { to: 'engineering/package', label: 'Package' },
      { to: 'engineering/deployment', label: 'Deployment' },
    ],
  },
  {
    id: 'maintenance',
    label: 'Maintenance',
    icon: Wrench,
    to: 'maintenance',
    items: [
      { to: 'maintenance', label: 'Changes' },
      { to: 'maintenance/impact', label: 'Impact analysis' },
      { to: 'maintenance/versions', label: 'Versions' },
      { to: 'maintenance/readiness', label: 'Release readiness' },
      { to: 'maintenance/rollback', label: 'Rollback' },
    ],
  },
  {
    id: 'admin',
    label: 'Administration',
    icon: Cpu,
    to: 'admin/data-sources',
    items: [
      { to: 'admin/data-sources', label: 'Data sources' },
      { to: 'admin/runtime', label: 'Runtime health' },
      { to: 'admin/storage', label: 'Storage & retention' },
      { to: 'admin/logs', label: 'System logs' },
    ],
  },
];

/** Icons used elsewhere for the same areas. */
export const AREA_ICON = { history: History, simulation: FlaskConical, data: Database } as const;

/** Group and item matching a workspace-relative path (longest prefix wins). */
export function locate(rel: string): { group: TwinNavGroup; item?: TwinNavItem } | null {
  let best: { group: TwinNavGroup; item?: TwinNavItem; len: number } | null = null;
  for (const g of TWIN_NAV) {
    const candidates: { to: string; item?: TwinNavItem }[] = [{ to: g.to }, ...(g.items ?? []).map((i) => ({ to: i.to, item: i }))];
    for (const c of candidates) {
      const match = c.to === '' ? rel === '' : rel === c.to || rel.startsWith(`${c.to}/`);
      if (match && (!best || c.to.length > best.len || (c.to.length === best.len && c.item && !best.item))) best = { group: g, item: c.item, len: c.to.length };
    }
  }
  return best ? { group: best.group, item: best.item } : null;
}
