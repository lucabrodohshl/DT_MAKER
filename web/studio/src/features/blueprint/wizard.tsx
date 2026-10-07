/**
 * The guided "New Blueprint" wizard. Steps 1–2 (starting point, identity) are the New Blueprint
 * page; they create the draft. Steps 3–11 walk through the real workspace editors with a step
 * bar (Back, Skip, Next, Save and exit, Jump to advanced), so everything entered in the wizard is
 * the Blueprint itself, autosaved like any edit — nothing is staged in the browser. The active
 * wizard is a per-tab preference (sessionStorage); leaving it never loses work.
 */
import clsx from 'clsx';
import { ArrowLeft, ArrowRight, Check, LogOut, SkipForward, Wrench } from 'lucide-react';
import { useState } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { blueprintRoute } from '@/api/blueprints';
import { Button } from '@/design';
import { useEditor } from './editor';
import { useWorkspace } from './workspace';

export interface WizardStep {
  n: number;
  id: string;
  label: string;
  /** Workspace route of the step (steps 1–2 live on the New Blueprint page). */
  route?: string;
  hint: string;
}

export const WIZARD_STEPS: WizardStep[] = [
  { n: 1, id: 'start', label: 'Starting point', hint: 'Blank, template, clone or import.' },
  { n: 2, id: 'identity', label: 'Identity', hint: 'Name, domain and time base.' },
  { n: 3, id: 'structure', label: 'Structure', route: 'build/structure', hint: 'Asset types and the assets every instance gets (the root asset is the twin itself).' },
  { n: 4, id: 'world', label: 'World & layout', route: 'build/world', hint: 'Where things are: a spatial map, a topology or a diagram. Skip it for twins without a layout.' },
  { n: 5, id: 'data', label: 'Data & connectivity', route: 'build/data', hint: 'Telemetry, events and commands of the twin, and the sources that deliver them.' },
  { n: 6, id: 'pt', label: 'Physical System View', route: 'behavior/pt', hint: 'How the real system behaves over time (a timed automaton), or import its UPPAAL model.' },
  { n: 7, id: 'dt', label: 'Digital Twin View', route: 'behavior/dt', hint: 'The twin’s own abstraction of that behaviour: the states operators see.' },
  { n: 8, id: 'semantics', label: 'Semantics', route: 'semantics/ontology', hint: 'The ontology of the domain, then what each state and event means (interpretations).' },
  { n: 9, id: 'assurance', label: 'Requirements & monitors', route: 'assurance/requirements', hint: 'What must hold, and the monitors that check it on the running twin.' },
  { n: 10, id: 'test', label: 'Scenarios', route: 'test/scenarios', hint: 'Timed test cases built with the kernel’s legal windows.' },
  { n: 11, id: 'verify', label: 'Verify & release', route: 'assurance/verification', hint: 'Run all checks; when the gate is green, build the package and publish.' },
];

const KEY = 'vts.studio.wizard';

interface WizardState {
  id: string;
  version: number;
  step: number;
}

function read(): WizardState | null {
  try {
    const raw = sessionStorage.getItem(KEY);
    return raw ? (JSON.parse(raw) as WizardState) : null;
  } catch {
    return null;
  }
}

function write(s: WizardState | null) {
  try {
    if (s) sessionStorage.setItem(KEY, JSON.stringify(s));
    else sessionStorage.removeItem(KEY);
  } catch {
    /* preference only */
  }
}

/** Start the guided flow for a freshly created draft (called by the New Blueprint page). */
export function startWizard(id: string, version: number) {
  write({ id, version, step: 3 });
}

/** The wizard step a workspace page belongs to (sub-pages and sibling pages of a step included). */
const stepOfRoute = (rel: string): WizardStep | undefined => {
  const path = rel.split('?')[0] ?? '';
  const under = (r: string) => path === r || path.startsWith(`${r}/`);
  const also: Record<string, string[]> = {
    semantics: ['semantics'],
    assurance: ['assurance/monitors', 'assurance/alignment'],
    test: ['test/preview'],
    verify: ['release'],
  };
  return WIZARD_STEPS.find((s) => s.route && (under(s.route) || (also[s.id] ?? []).some(under)));
};

/** Step bar of the guided flow; renders nothing when the wizard is not active for this draft. */
export function WizardBar() {
  const e = useEditor();
  const ws = useWorkspace();
  const navigate = useNavigate();
  const location = useLocation();
  const [state, setState] = useState<WizardState | null>(read);
  const active = !!state && state.id === e.id && state.version === e.version && e.editable;
  if (!active || !state) return null;
  const rel = location.pathname.split(`/v/${e.version}`)[1]?.replace(/^\//, '') ?? '';
  const here = stepOfRoute(rel);
  const current = here?.n ?? state.step;
  const step = WIZARD_STEPS.find((s) => s.n === current) ?? WIZARD_STEPS[2]!;
  const go = (n: number) => {
    const target = WIZARD_STEPS.find((s) => s.n === n);
    if (!target?.route) return;
    const next = { ...state, step: n };
    write(next);
    setState(next);
    navigate(blueprintRoute(e.id, e.version, target.route));
  };
  const exit = async (to: 'overview' | 'advanced') => {
    await e.saveNow();
    write(null);
    setState(null);
    if (to === 'advanced') ws.setExpert(true);
    else navigate(blueprintRoute(e.id, e.version));
  };
  const last = current === WIZARD_STEPS.length;
  return (
    <nav className="vts-wizard" aria-label="New Blueprint wizard">
      <ol className="vts-wizard__steps">
        {WIZARD_STEPS.map((s) => (
          <li key={s.id}>
            <button
              type="button"
              className={clsx('vts-wizard__step', s.n < current && 'is-done')}
              aria-current={s.n === current ? 'step' : undefined}
              disabled={!s.route}
              onClick={() => go(s.n)}
              title={s.hint}
            >
              <span className="vts-wizard__num">{s.n < current || s.n <= 2 ? <Check size={11} aria-hidden="true" /> : s.n}</span>
              <span className="vts-wizard__label">{s.label}</span>
            </button>
          </li>
        ))}
      </ol>
      <div className="vts-wizard__bar">
        <div className="grow" style={{ minWidth: 0 }}>
          <strong className="small">
            Step {current} of {WIZARD_STEPS.length}: {step.label}
          </strong>
          <div className="xsmall muted">{step.hint}</div>
        </div>
        <Button size="sm" variant="ghost" icon={<ArrowLeft size={13} />} disabled={current <= 3} onClick={() => go(current - 1)}>
          Back
        </Button>
        {!last && (
          <Button size="sm" variant="ghost" icon={<SkipForward size={13} />} onClick={() => go(current + 1)}>
            Skip
          </Button>
        )}
        {!last ? (
          <Button size="sm" variant="primary" icon={<ArrowRight size={13} />} onClick={() => void e.saveNow().then(() => go(current + 1))}>
            Next
          </Button>
        ) : (
          <Button size="sm" variant="primary" icon={<Check size={13} />} onClick={() => void exit('overview')}>
            Finish
          </Button>
        )}
        <Button size="sm" variant="ghost" icon={<LogOut size={13} />} onClick={() => void exit('overview')} title="Everything is saved; reopen the draft from Studio any time">
          Save and exit
        </Button>
        <Button size="sm" variant="ghost" icon={<Wrench size={13} />} onClick={() => void exit('advanced')} title="Leave the guided flow and show every field (expert mode)">
          Jump to advanced
        </Button>
      </div>
    </nav>
  );
}
