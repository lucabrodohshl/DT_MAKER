/**
 * Information architecture of a Blueprint version in Studio (relative to
 * /studio/blueprints/:id/v/:version). One definition drives the sidebar, the breadcrumbs,
 * the command palette and the "next step" links of the overview.
 *
 * The four model families stay separate: Build (asset/structural, world, data), Behavior
 * (the two timed automata), Semantics (ontology and interpretations) and Assurance.
 */
import {
  Boxes,
  ClipboardCheck,
  Database,
  FlaskConical,
  Gauge,
  GitCompareArrows,
  LayoutDashboard,
  Map as MapIcon,
  Network,
  Package,
  PanelsTopLeft,
  PlayCircle,
  Rocket,
  ScrollText,
  ShieldCheck,
  Sigma,
  SplitSquareHorizontal,
  Workflow,
  Layers,
  ListChecks,
  type LucideIcon,
} from 'lucide-react';

export interface BpNavItem {
  /** Path relative to the version ("" = overview). */
  to: string;
  label: string;
  icon: LucideIcon;
  /** Status section ids (GET .../status) summarised by this item's badge. */
  sections?: string[];
  /** Short description (command palette, empty states). */
  hint: string;
}

export interface BpNavGroup {
  id: string;
  label: string;
  items: BpNavItem[];
}

export const BLUEPRINT_NAV: BpNavGroup[] = [
  { id: 'overview', label: '', items: [{ to: '', label: 'Overview', icon: LayoutDashboard, hint: 'Completeness, next steps and the release gate' }] },
  {
    id: 'build',
    label: 'Build',
    items: [
      { to: 'build/structure', label: 'Structure', icon: Boxes, sections: ['structure'], hint: 'Asset types, assets and relationships' },
      { to: 'build/world', label: 'World & Layout', icon: MapIcon, sections: ['world'], hint: 'Layers, geometry, topology and asset placement' },
      { to: 'build/data', label: 'Data & Connectivity', icon: Database, sections: ['data', 'connectivity'], hint: 'Data contract, sources and bindings' },
      { to: 'build/presentation', label: 'Presentation', icon: PanelsTopLeft, sections: ['presentation'], hint: 'How Operate shows the twin (no formal effect)' },
    ],
  },
  {
    id: 'behavior',
    label: 'Behavior',
    items: [
      { to: 'behavior/pt', label: 'Physical System View', icon: Workflow, sections: ['pt_model'], hint: 'V_P: the physical system as a timed automaton' },
      { to: 'behavior/dt', label: 'Digital Twin View', icon: Workflow, sections: ['dt_model'], hint: 'V_D: the twin behaviour the runtime executes' },
    ],
  },
  {
    id: 'semantics',
    label: 'Semantics',
    items: [
      { to: 'semantics/ontology', label: 'Ontology', icon: Sigma, sections: ['ontology'], hint: 'Domain theory K: sorts, functions, relations, axioms' },
      { to: 'semantics/interpretations', label: 'Interpretations', icon: SplitSquareHorizontal, sections: ['interpretations'], hint: 'Meaning of states and events (I_P, I_D)' },
      { to: 'semantics/binding', label: 'Cross-layer binding', icon: Layers, hint: 'World → data → semantics → behaviour, end to end' },
    ],
  },
  {
    id: 'assurance',
    label: 'Assurance',
    items: [
      { to: 'assurance/requirements', label: 'Requirements', icon: ListChecks, sections: ['requirements'], hint: 'What must hold, traced to monitors' },
      { to: 'assurance/monitors', label: 'Monitors', icon: Gauge, sections: ['monitors'], hint: 'Runtime monitors and alert policy' },
      { to: 'assurance/alignment', label: 'Alignment', icon: GitCompareArrows, hint: 'Semantic alignment of PT and DT views (aligner)' },
      { to: 'assurance/verification', label: 'Verification', icon: ShieldCheck, hint: 'Every check and its evidence' },
    ],
  },
  {
    id: 'test',
    label: 'Test',
    items: [
      { to: 'test/scenarios', label: 'Scenario Builder', icon: FlaskConical, sections: ['scenarios'], hint: 'Timed scenarios with kernel timing windows' },
      { to: 'test/preview', label: 'Preview / Simulation', icon: PlayCircle, hint: 'Run the twin in an isolated Studio preview' },
    ],
  },
  {
    id: 'release',
    label: 'Release',
    items: [
      { to: 'release/package', label: 'Package', icon: Package, hint: 'Release readiness, verified core and deployment bundle' },
      { to: 'release/instances', label: 'Instances', icon: Network, hint: 'Twins created from this Blueprint' },
      { to: 'release/deployment', label: 'Deployment', icon: Rocket, hint: 'Deploy instances and open them in Operate' },
    ],
  },
];

export const ALL_ITEMS: BpNavItem[] = BLUEPRINT_NAV.flatMap((g) => g.items);

/** Group and item matching a version-relative path (longest prefix wins). */
export function locateBp(rel: string): { group: BpNavGroup; item: BpNavItem } | null {
  let best: { group: BpNavGroup; item: BpNavItem } | null = null;
  for (const g of BLUEPRINT_NAV) {
    for (const i of g.items) {
      const match = i.to === '' ? rel === '' : rel === i.to || rel.startsWith(`${i.to}/`);
      if (match && (!best || i.to.length > best.item.to.length)) best = { group: g, item: i };
    }
  }
  return best;
}

/** Icons shared by Studio pages. */
export const STUDIO_ICONS = { verification: ShieldCheck, audit: ScrollText, readiness: ClipboardCheck } as const;
