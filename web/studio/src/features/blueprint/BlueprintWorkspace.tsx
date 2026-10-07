/**
 * Studio workspace of one Blueprint version: left navigation (Build / Behavior / Semantics /
 * Assurance / Test / Release, each item with its section status), a top bar with the
 * Blueprint, version, lifecycle, save, validation and release state and the main actions,
 * the routed editor, and a bottom drawer with Problems (validation findings that link to
 * their source) and Output (every check run, with its evidence).
 */
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import clsx from 'clsx';
import {
  ArrowLeft,
  ChevronDown,
  ChevronRight,
  CircleHelp,
  FlaskConical,
  GitBranchPlus,
  PanelBottomClose,
  PanelBottomOpen,
  PanelLeftClose,
  PanelLeftOpen,
  PlayCircle,
  Redo2,
  Rocket,
  Save,
  ShieldCheck,
  Undo2,
} from 'lucide-react';
import { useEffect, useState } from 'react';
import { Link, NavLink, Outlet, useLocation, useNavigate, useParams } from 'react-router-dom';
import { blueprintApi, blueprintRoute, useBlueprint, useBlueprintStatus, useBlueprintValidation, useBlueprintVersion, useBlueprintMutation } from '@/api/blueprints';
import type { SectionFinding, SectionState } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, Skeleton, StatusBadge, TimeStamp } from '@/design';
import { EditorProvider, useEditor } from './editor';
import { BLUEPRINT_NAV, locateBp } from './nav';
import { ReleaseVerdictBadge, SaveIndicator, SectionDot, VersionStateBadge } from './status';
import { WizardBar } from './wizard';
import { WorkspaceProvider, useWorkspace, type OutputEntry } from './workspace';
import './blueprint.css';

/** Route a finding to the page that edits it (with the element selected). */
export function findingRoute(f: SectionFinding): string {
  const sel = f.target ? `?select=${encodeURIComponent(f.target)}` : '';
  switch (f.section) {
    case 'identity':
      return `build/structure${sel}`;
    case 'structure':
      return `build/structure${sel}`;
    case 'world':
      return `build/world${f.target ? `?object=${encodeURIComponent(f.target)}` : ''}`;
    case 'data':
      return `build/data${sel}`;
    case 'connectivity':
      return `build/data?tab=connectivity${f.target ? `&select=${encodeURIComponent(f.target)}` : ''}`;
    case 'presentation':
      return 'build/presentation';
    case 'assurance':
      return /^REQ|requirement/i.test(f.target) ? `assurance/requirements${sel}` : `assurance/monitors${sel}`;
    case 'simulation':
      return 'test/preview';
    case 'scenarios':
      return `test/scenarios${f.target ? `/${encodeURIComponent(f.target)}` : ''}`;
    default:
      return '';
  }
}

function Sidebar({ collapsed, onToggle }: { collapsed: boolean; onToggle: () => void }) {
  const e = useEditor();
  const status = useBlueprintStatus(e.id, e.version);
  const byId = new Map<string, SectionState>((status.data?.sections ?? []).map((s) => [s.id, s.state]));
  const worst = (ids: string[] | undefined): SectionState | undefined => {
    if (!ids || ids.length === 0) return undefined;
    const states = ids.map((i) => byId.get(i)).filter((s): s is SectionState => !!s);
    if (states.length === 0) return undefined;
    for (const s of ['errors', 'warnings', 'empty', 'complete'] as SectionState[]) if (states.includes(s)) return s;
    return undefined;
  };
  const base = blueprintRoute(e.id, e.version);
  return (
    <aside className="vts-bp-side" aria-label="Blueprint navigation">
      <div className="row" style={{ justifyContent: collapsed ? 'center' : 'space-between' }}>
        {!collapsed && (
          <Link to="/studio" className="vts-bp-back">
            <ArrowLeft size={14} aria-hidden="true" /> Studio home
          </Link>
        )}
        <button type="button" className="vts-bp-collapse" onClick={onToggle} aria-label={collapsed ? 'Expand navigation' : 'Collapse navigation'} aria-expanded={!collapsed}>
          {collapsed ? <PanelLeftOpen size={16} /> : <PanelLeftClose size={16} />}
        </button>
      </div>
      <nav aria-label={`${e.detail.blueprint.name} sections`}>
        {BLUEPRINT_NAV.map((g) => (
          <div key={g.id} className="vts-bp-group">
            {g.label && !collapsed && <div className="vts-bp-group__label">{g.label}</div>}
            {g.items.map((i) => {
              const state = worst(i.sections);
              return (
                <NavLink
                  key={i.to}
                  to={i.to ? `${base}/${i.to}` : base}
                  end={i.to === ''}
                  className={({ isActive }) => clsx('vts-bp-link', isActive && 'active')}
                  title={collapsed ? `${g.label ? `${g.label} · ` : ''}${i.label}` : i.hint}
                >
                  <i.icon size={16} aria-hidden="true" />
                  {!collapsed && <span className="grow truncate">{i.label}</span>}
                  {!collapsed && state && <SectionDot state={state} />}
                </NavLink>
              );
            })}
          </div>
        ))}
      </nav>
      {!collapsed && (
        <p className="xsmall subtle vts-bp-side__note">
          <ShieldCheck size={13} aria-hidden="true" /> Formal statements come only from the aligner, compiler and kernel; Studio shows their evidence.
        </p>
      )}
    </aside>
  );
}

function VersionSwitcher() {
  const e = useEditor();
  const navigate = useNavigate();
  const location = useLocation();
  const bp = useBlueprint(e.id);
  const rel = location.pathname.split(`/v/${e.version}`)[1] ?? '';
  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <button type="button" className="vts-bp-version" aria-label={`Version ${e.detail.label}, switch version`}>
          <span className="mono">{e.detail.label}</span>
          <ChevronDown size={13} aria-hidden="true" />
        </button>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content className="vts-menu" align="start" sideOffset={6}>
          <DropdownMenu.Label className="vts-menu__label">Versions</DropdownMenu.Label>
          {(bp.data?.versions ?? []).map((v) => (
            <DropdownMenu.Item
              key={v.version}
              className={clsx('vts-menu__item', v.version === e.version && 'is-current')}
              onSelect={() => navigate(`${blueprintRoute(e.id, v.version)}${rel}`)}
            >
              <span className="mono">{v.label}</span>
              <span className="grow xsmall subtle">{v.note || (v.state === 'published' ? `published ${v.publishedAt?.slice(0, 10) ?? ''}` : '')}</span>
              <VersionStateBadge state={v.state} />
            </DropdownMenu.Item>
          ))}
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
  );
}

function CreateDraftDialog({ open, onOpenChange }: { open: boolean; onOpenChange: (o: boolean) => void }) {
  const e = useEditor();
  const navigate = useNavigate();
  const [note, setNote] = useState('');
  const create = useBlueprintMutation((n: string) => blueprintApi.createDraft(e.id, e.version, n));
  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title={`Create a draft from ${e.detail.label}`}
      description="Published versions are immutable. The draft starts as a copy of this version; instances keep running their version until you upgrade them."
      footer={
        <>
          <Button onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button
            variant="primary"
            loading={create.isPending}
            onClick={() =>
              create.mutate(note.trim() || `Draft from ${e.detail.label}`, {
                onSuccess: (v) => {
                  onOpenChange(false);
                  navigate(blueprintRoute(e.id, v.version));
                },
              })
            }
          >
            Create draft
          </Button>
        </>
      }
    >
      <div className="stack-sm">
        <label className="vts-field">
          <span className="vts-label">What will this version change?</span>
          <input className="vts-input" value={note} onChange={(ev) => setNote(ev.target.value)} placeholder="e.g. Add a door-open interlock" autoFocus />
        </label>
        {create.error && <ErrorBlock error={create.error} compact />}
      </div>
    </Dialog>
  );
}

function TopBar() {
  const e = useEditor();
  const ws = useWorkspace();
  const status = useBlueprintStatus(e.id, e.version);
  const validation = useBlueprintValidation(e.id, e.version);
  const navigate = useNavigate();
  const [draftOpen, setDraftOpen] = useState(false);
  const errors = validation.data?.errors ?? 0;
  const warnings = validation.data?.warnings ?? 0;
  const verdict = status.data?.readiness.verdict;
  const blockers = status.data?.readiness.items.filter((i) => i.blocking && i.state !== 'pass').length ?? 0;
  const base = blueprintRoute(e.id, e.version);
  const validate = async () => {
    ws.setDrawer('problems');
    await e.saveNow();
    await ws.runCheck('formal').catch(() => undefined);
  };
  return (
    <header className="vts-bp-head">
      <div className="vts-bp-head__row">
        <nav aria-label="Breadcrumb" className="vts-bp-crumbs">
          <Link to="/studio">Studio</Link>
          <ChevronRight size={12} aria-hidden="true" />
          <Link to="/studio?view=blueprints">Blueprints</Link>
          <ChevronRight size={12} aria-hidden="true" />
        </nav>
        <h1 className="vts-bp-title">{e.detail.blueprint.name}</h1>
        <VersionSwitcher />
        <VersionStateBadge state={e.detail.state} />
        <span className="vts-bp-head__sep" aria-hidden="true" />
        <SaveIndicator state={e.saveState} title={e.saveError ?? undefined} />
        <button
          type="button"
          className="vts-bp-chip"
          onClick={() => ws.setDrawer(ws.drawer === 'problems' ? null : 'problems')}
          aria-expanded={ws.drawer === 'problems'}
          title="Validation findings of every section"
        >
          {validation.isPending ? (
            <span className="small muted">Validating…</span>
          ) : errors + warnings === 0 ? (
            <StatusBadge tone="ok" label="Valid" />
          ) : (
            <>
              {errors > 0 && <StatusBadge tone="critical" label={`${errors} error${errors === 1 ? '' : 's'}`} />}
              {warnings > 0 && <StatusBadge tone="warning" label={`${warnings} warning${warnings === 1 ? '' : 's'}`} />}
            </>
          )}
        </button>
        <Link to={`${base}/release/package`} className="vts-bp-chip" title={verdict === 'blocked' ? `${blockers} release blocker(s)` : 'Release readiness'}>
          <ReleaseVerdictBadge verdict={verdict} />
          {verdict === 'blocked' && <span className="xsmall subtle">{blockers} blocker{blockers === 1 ? '' : 's'}</span>}
        </Link>
        <div className="grow" />
        <div className="row">
          {e.editable && (
            <>
              <Button size="sm" variant="ghost" iconOnly icon={<Undo2 size={15} />} disabled={!e.canUndo} onClick={e.undo} title={e.undoLabel ? `Undo: ${e.undoLabel} (Ctrl+Z)` : 'Undo (Ctrl+Z)'}>
                Undo
              </Button>
              <Button size="sm" variant="ghost" iconOnly icon={<Redo2 size={15} />} disabled={!e.canRedo} onClick={e.redo} title={e.redoLabel ? `Redo: ${e.redoLabel} (Ctrl+Shift+Z)` : 'Redo (Ctrl+Shift+Z)'}>
                Redo
              </Button>
              <Button size="sm" icon={<Save size={14} />} disabled={e.saveState === 'saved' || e.saveState === 'saving'} onClick={() => void e.saveNow()} title="Save now (Ctrl+S); drafts also save automatically">
                Save
              </Button>
            </>
          )}
          <Button size="sm" icon={<ShieldCheck size={14} />} loading={ws.running.has('formal')} onClick={() => void validate()} title="Save, re-validate every section and the formal artefacts, and show the problems">
            Validate
          </Button>
          <Button size="sm" icon={<PlayCircle size={14} />} onClick={() => navigate(`${base}/test/preview`)} title="Run this version in an isolated Studio preview">
            Preview
          </Button>
          {e.editable ? (
            <Button size="sm" variant={verdict === 'ready' ? 'primary' : 'secondary'} icon={<Rocket size={14} />} onClick={() => navigate(`${base}/release/package`)}>
              Release
            </Button>
          ) : (
            <Button size="sm" variant="primary" icon={<GitBranchPlus size={14} />} onClick={() => setDraftOpen(true)}>
              Create draft from this version
            </Button>
          )}
          <DropdownMenu.Root>
            <DropdownMenu.Trigger asChild>
              <Button size="sm" variant="ghost" iconOnly icon={<CircleHelp size={15} />}>
                Workspace options
              </Button>
            </DropdownMenu.Trigger>
            <DropdownMenu.Portal>
              <DropdownMenu.Content className="vts-menu" align="end" sideOffset={6}>
                <DropdownMenu.Label className="vts-menu__label">Editing mode</DropdownMenu.Label>
                <DropdownMenu.CheckboxItem className="vts-menu__item" checked={!ws.expert} onCheckedChange={() => ws.setExpert(false)}>
                  Guided — explanations and essentials {!ws.expert ? '✓' : ''}
                </DropdownMenu.CheckboxItem>
                <DropdownMenu.CheckboxItem className="vts-menu__item" checked={ws.expert} onCheckedChange={() => ws.setExpert(true)}>
                  Expert — every field, JSON views {ws.expert ? '✓' : ''}
                </DropdownMenu.CheckboxItem>
                <DropdownMenu.Separator className="vts-divider" />
                <DropdownMenu.Item className="vts-menu__item" asChild>
                  <a href="/docs/studio/studio-overview.html" target="_blank" rel="noopener">Studio manual</a>
                </DropdownMenu.Item>
                <DropdownMenu.Item className="vts-menu__item" asChild>
                  <a href="/docs/studio/tutorial-first-twin.html" target="_blank" rel="noopener">Tutorial: build your first twin</a>
                </DropdownMenu.Item>
              </DropdownMenu.Content>
            </DropdownMenu.Portal>
          </DropdownMenu.Root>
        </div>
      </div>
      <CreateDraftDialog open={draftOpen} onOpenChange={setDraftOpen} />
    </header>
  );
}

function Banners() {
  const e = useEditor();
  const [draftOpen, setDraftOpen] = useState(false);
  if (e.saveState === 'conflict') {
    return (
      <div className="vts-bp-banner">
        <Callout
          tone="critical"
          title="This draft was changed elsewhere"
          action={<Button size="sm" onClick={e.reload}>Discard my edits and reload</Button>}
        >
          {e.saveError} Your unsaved edits are kept on screen; nothing was overwritten.
        </Callout>
      </div>
    );
  }
  if (e.saveState === 'error') {
    return (
      <div className="vts-bp-banner">
        <Callout tone="critical" title="Save failed" action={<Button size="sm" onClick={() => void e.saveNow()}>Retry</Button>}>
          {e.saveError}
        </Callout>
      </div>
    );
  }
  if (!e.editable) {
    return (
      <div className="vts-bp-banner">
        <Callout
          tone="neutral"
          title={`${e.detail.label} is published and read-only`}
          action={
            <Button size="sm" variant="primary" icon={<GitBranchPlus size={14} />} onClick={() => setDraftOpen(true)}>
              Create draft from this version
            </Button>
          }
        >
          Published versions never change: instances and packages refer to them. Changes go into a new draft version.
        </Callout>
        <CreateDraftDialog open={draftOpen} onOpenChange={setDraftOpen} />
      </div>
    );
  }
  return null;
}

function OutputRow({ o }: { o: OutputEntry }) {
  const [open, setOpen] = useState(false);
  const tone = o.outcome === 'pass' ? 'ok' : o.outcome === 'fail' || o.outcome === 'error' ? 'critical' : 'info';
  const label = o.outcome === 'running' ? 'Running' : o.outcome === 'info' ? 'Done' : o.outcome.toUpperCase();
  return (
    <li className="vts-bp-out">
      <div className="row-wrap small">
        <StatusBadge tone={tone} label={label} spin={o.outcome === 'running'} />
        <strong>{o.title}</strong>
        <span className="muted grow">{o.summary}</span>
        {o.evidenceId && <Link to={`/studio/verification/${encodeURIComponent(o.evidenceId)}`} className="mono xsmall">{o.evidenceId}</Link>}
        <TimeStamp value={o.at} className="xsmall subtle" />
        {o.detail !== undefined && (
          <button type="button" className="vts-linkbtn xsmall" onClick={() => setOpen((x) => !x)} aria-expanded={open}>
            {open ? 'Hide details' : 'Details'}
          </button>
        )}
      </div>
      {open && <pre className="vts-code" style={{ maxHeight: 220, marginTop: 6 }}>{JSON.stringify(o.detail, null, 2)}</pre>}
    </li>
  );
}

function BottomDrawer() {
  const e = useEditor();
  const ws = useWorkspace();
  const validation = useBlueprintValidation(e.id, e.version);
  const navigate = useNavigate();
  const findings = validation.data?.findings ?? [];
  const base = blueprintRoute(e.id, e.version);
  const errors = findings.filter((f) => f.severity === 'error').length;
  return (
    <section className={clsx('vts-bp-drawer', ws.drawer && 'is-open')} aria-label="Problems and output">
      <div className="vts-bp-drawer__bar" role="tablist" aria-label="Drawer">
        <button type="button" role="tab" aria-selected={ws.drawer === 'problems'} className="vts-tab" onClick={() => ws.setDrawer(ws.drawer === 'problems' ? null : 'problems')}>
          Problems <span className="vts-tag">{findings.length}</span>
          {errors > 0 && <span className="sr-only">{errors} errors</span>}
        </button>
        <button type="button" role="tab" aria-selected={ws.drawer === 'output'} className="vts-tab" onClick={() => ws.setDrawer(ws.drawer === 'output' ? null : 'output')}>
          Output <span className="vts-tag">{ws.output.length}</span>
          {ws.running.size > 0 && <StatusBadge tone="info" label="running" spin />}
        </button>
        <div className="grow" />
        <Button size="sm" variant="ghost" iconOnly icon={ws.drawer ? <PanelBottomClose size={15} /> : <PanelBottomOpen size={15} />} onClick={() => ws.setDrawer(ws.drawer ? null : 'problems')}>
          {ws.drawer ? 'Close drawer' : 'Open drawer'}
        </Button>
      </div>
      {ws.drawer === 'problems' && (
        <div className="vts-bp-drawer__body">
          {findings.length === 0 ? (
            <EmptyState compact title="No problems">Every section of {e.detail.label} validates.</EmptyState>
          ) : (
            <table className="vts-table">
              <thead>
                <tr>
                  <th>Severity</th>
                  <th>Section</th>
                  <th>Element</th>
                  <th>Problem</th>
                  <th>Code</th>
                </tr>
              </thead>
              <tbody>
                {findings.map((f, i) => (
                  <tr key={i} className="is-clickable" onClick={() => navigate(`${base}/${findingRoute(f)}`)}>
                    <td>
                      <StatusBadge tone={f.severity === 'error' ? 'critical' : 'warning'} label={f.severity === 'error' ? 'Error' : 'Warning'} />
                    </td>
                    <td className="small">{f.section}</td>
                    <td className="mono xsmall">{f.target || '—'}</td>
                    <td className="small">
                      <Link to={`${base}/${findingRoute(f)}`} onClick={(ev) => ev.stopPropagation()}>{f.message}</Link>
                      {f.path && <div className="xsmall subtle mono">{f.path}</div>}
                    </td>
                    <td className="mono xsmall">{f.code}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}
      {ws.drawer === 'output' && (
        <div className="vts-bp-drawer__body">
          {ws.output.length === 0 ? (
            <EmptyState compact title="No checks run in this session">
              Checks (validation, alignment, compilation, scenario tests, packaging) write their results here, with links to the evidence.
            </EmptyState>
          ) : (
            <ul className="vts-list">{ws.output.map((o) => <OutputRow key={o.id} o={o} />)}</ul>
          )}
        </div>
      )}
    </section>
  );
}

function Frame() {
  const [collapsed, setCollapsed] = useState(() => {
    try {
      return localStorage.getItem('vts.bp.nav') === 'collapsed';
    } catch {
      return false;
    }
  });
  const toggle = () =>
    setCollapsed((c) => {
      try {
        localStorage.setItem('vts.bp.nav', c ? 'open' : 'collapsed');
      } catch {
        /* preference */
      }
      return !c;
    });
  const e = useEditor();
  const location = useLocation();
  const rel = location.pathname.split(`/v/${e.version}`)[1]?.replace(/^\//, '') ?? '';
  const here = locateBp(rel);
  useEffect(() => {
    document.title = `${here?.item.label ?? 'Studio'} · ${e.detail.blueprint.name} ${e.detail.label} — Verified Twin Studio`;
  }, [here, e.detail.blueprint.name, e.detail.label]);
  return (
    <div className={clsx('vts-bp', collapsed && 'is-collapsed')}>
      <Sidebar collapsed={collapsed} onToggle={toggle} />
      <div className="vts-bp-main">
        <TopBar />
        <WizardBar />
        <Banners />
        <div className="vts-bp-body">
          <Outlet />
        </div>
        <BottomDrawer />
      </div>
    </div>
  );
}

export default function BlueprintWorkspace() {
  const { bpId, version } = useParams();
  const v = Number(version);
  const q = useBlueprintVersion(bpId, Number.isFinite(v) ? v : undefined);
  if (q.isPending) {
    return (
      <div className="vts-page" role="status" aria-label="Loading Blueprint">
        <Skeleton lines={6} />
      </div>
    );
  }
  if (q.isError || !q.data) {
    return (
      <div className="vts-page">
        <ErrorBlock error={q.error} onRetry={() => void q.refetch()} />
        <p className="small" style={{ textAlign: 'center' }}>
          <Link to="/studio">Back to Studio</Link>
        </p>
      </div>
    );
  }
  return (
    <WorkspaceProvider key={`${bpId}@${v}`} id={q.data.blueprintId} version={q.data.version}>
      <EditorProvider key={`${bpId}@${v}`} detail={q.data}>
        <Frame />
      </EditorProvider>
    </WorkspaceProvider>
  );
}

/** Small "Run" button bound to a check, with the running state of the workspace. */
export function RunCheckButton({ check, label, variant = 'secondary', body }: { check: Parameters<ReturnType<typeof useWorkspace>['runCheck']>[0]; label: string; variant?: 'primary' | 'secondary'; body?: Record<string, unknown> }) {
  const ws = useWorkspace();
  const e = useEditor();
  return (
    <Button
      size="sm"
      variant={variant}
      icon={<FlaskConical size={14} />}
      loading={ws.running.has(check)}
      onClick={() => {
        void e.saveNow().then(() => ws.runCheck(check, body).catch(() => undefined));
      }}
    >
      {label}
    </Button>
  );
}
