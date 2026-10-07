/**
 * Twin workspace (/twins/:twinId/...): everything below is scoped to ONE twin.
 *
 *  - context header: ← Twins, twin switcher, name, type, status, package;
 *  - collapsible left sidebar (icons + tooltips when collapsed);
 *  - breadcrumbs derived from the navigation definition;
 *  - the page, which reads the twin from useTwinScope() / the outlet context.
 */
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import { ArrowLeft, ChevronDown, ChevronRight, PanelLeftClose, PanelLeftOpen, Search, PencilRuler } from 'lucide-react';
import clsx from 'clsx';
import { useEffect, useMemo, useState } from 'react';
import { Link, NavLink, Outlet, useLocation, useParams } from 'react-router-dom';
import { useAsset, useTwin, useTwins } from '@/api/queries';
import { TrustBadge, QueryState, Tooltip, EmptyState } from '@/design';
import { useDisclosure } from '@/app/disclosure';
import { TwinScopeContext, recentTwins, rememberTwin, twinRoute, type TwinWorkspace } from '@/app/twinScope';
import { TWIN_NAV, locate } from './twinNav';
import { ModeBadge, OperationalBadge, overallTrust, useLiveStatus } from './status';
import './twins.css';

const COLLAPSE_KEY = 'vts.twinNavCollapsed';
function readCollapsed(): boolean {
  try {
    return localStorage.getItem(COLLAPSE_KEY) === '1';
  } catch {
    return false;
  }
}

function TwinSwitcher({ current }: { current: string }) {
  const twins = useTwins();
  const [q, setQ] = useState('');
  const recent = recentTwins().filter((id) => id !== current);
  const list = (twins.data ?? []).filter((t) => !q || `${t.name} ${t.id}`.toLowerCase().includes(q.toLowerCase()));
  const byId = new Map((twins.data ?? []).map((t) => [t.id, t]));
  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <button type="button" className="vts-twin-switch" aria-label="Switch twin">
          <ChevronDown size={14} aria-hidden="true" />
        </button>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content className="vts-menu vts-twin-menu" align="start" sideOffset={6}>
          <div className="vts-twin-menu__search">
            <Search size={14} aria-hidden="true" />
            <input value={q} onChange={(e) => setQ(e.target.value)} placeholder="Search twins…" aria-label="Search twins" onKeyDown={(e) => e.stopPropagation()} />
          </div>
          {!q && recent.length > 0 && (
            <>
              <DropdownMenu.Label className="vts-menu__label">Recent</DropdownMenu.Label>
              {recent.filter((id) => byId.has(id)).map((id) => (
                <DropdownMenu.Item key={id} className="vts-menu__item" asChild>
                  <Link to={twinRoute(id)}>{byId.get(id)!.name}</Link>
                </DropdownMenu.Item>
              ))}
              <DropdownMenu.Separator className="vts-divider" />
            </>
          )}
          <DropdownMenu.Label className="vts-menu__label">{q ? 'Matching twins' : 'All twins'}</DropdownMenu.Label>
          {list.map((t) => (
            <DropdownMenu.Item key={t.id} className={clsx('vts-menu__item', t.id === current && 'is-current')} asChild>
              <Link to={twinRoute(t.id)}>{t.name}</Link>
            </DropdownMenu.Item>
          ))}
          {list.length === 0 && <div className="vts-menu__empty">No twin matches “{q}”.</div>}
          <DropdownMenu.Separator className="vts-divider" />
          <DropdownMenu.Item className="vts-menu__item" asChild>
            <Link to="/twins">View all twins</Link>
          </DropdownMenu.Item>
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
  );
}

/** Sections an engineer needs but an operator does not (progressive disclosure, not permissions). */
const ENGINEER_ONLY = new Set(['engineering', 'maintenance', 'admin']);

function Sidebar({ base, collapsed }: { base: string; collapsed: boolean }) {
  const location = useLocation();
  const { engineering, setDisclosure } = useDisclosure();
  const rel = decodeURIComponent(location.pathname.slice(base.length).replace(/^\//, ''));
  const here = locate(rel);
  const groups = TWIN_NAV.filter((g) => engineering || !ENGINEER_ONLY.has(g.id) || here?.group.id === g.id);
  return (
    <nav className="vts-twin-nav" aria-label="Twin workspace">
      {!engineering && !collapsed && (
        <button type="button" className="vts-twin-nav__more" onClick={() => setDisclosure('engineering')} title="Show engineering, maintenance and administration sections">
          Engineering, maintenance and administration are in the Engineer view
        </button>
      )}
      {groups.map((g) => {
        const active = here?.group.id === g.id;
        const to = g.to ? `${base}/${g.to}` : base;
        const link = (
          <NavLink to={to} end={!g.to} className={clsx('vts-twin-nav__group', active && 'active')} aria-current={active && !g.items ? 'page' : undefined}>
            <g.icon size={17} aria-hidden="true" />
            {!collapsed && <span>{g.label}</span>}
            {!collapsed && g.items && <ChevronRight size={13} className={clsx('vts-twin-nav__chev', active && 'open')} aria-hidden="true" />}
          </NavLink>
        );
        return (
          <div key={g.id}>
            {collapsed ? <Tooltip content={g.label} side="right">{link}</Tooltip> : link}
            {!collapsed && active && g.items && (
              <ul className="vts-twin-nav__items">
                {g.items.map((i) => (
                  <li key={i.to}>
                    <NavLink
                      to={`${base}/${i.to}`}
                      end
                      className={() => clsx('vts-twin-nav__item', here?.item?.to === i.to && 'active')}
                      aria-current={here?.item?.to === i.to ? 'page' : undefined}
                    >
                      {i.label}
                    </NavLink>
                  </li>
                ))}
              </ul>
            )}
          </div>
        );
      })}
    </nav>
  );
}

/** Breadcrumb trail: Twins / <twin> / <area> / <page> [/ <detail>]. */
function TwinCrumbs({ base, twinName }: { base: string; twinName: string }) {
  const location = useLocation();
  const rel = decodeURIComponent(location.pathname.slice(base.length).replace(/^\//, ''));
  const here = locate(rel);
  const items: { label: string; to?: string }[] = [{ label: 'Twins', to: '/twins' }, { label: twinName, to: base }];
  if (here && here.group.id !== 'overview') {
    items.push({ label: here.group.label, to: `${base}/${here.group.to}` });
    if (here.item && here.item.label !== here.group.label) items.push({ label: here.item.label, to: `${base}/${here.item.to}` });
    const covered = here.item?.to ?? here.group.to;
    const rest = rel.slice(covered.length).replace(/^\//, '');
    if (rest) items.push({ label: rest.split('/').slice(-1)[0] ?? rest });
  }
  return (
    <nav aria-label="Breadcrumb" className="vts-crumbs">
      <ol>
        {items.map((c, i) => (
          <li key={i}>
            {c.to && i < items.length - 1 ? <Link to={c.to}>{c.label}</Link> : <span aria-current={i === items.length - 1 ? 'page' : undefined}>{c.label}</span>}
          </li>
        ))}
      </ol>
    </nav>
  );
}

export default function TwinLayout() {
  const { twinId = '' } = useParams();
  const twin = useTwin(twinId);
  const asset = useAsset(twin.data?.assetId ?? undefined);
  const live = useLiveStatus(twin.data);
  const [collapsed, setCollapsed] = useState(readCollapsed);
  const base = twinRoute(twinId);

  useEffect(() => {
    if (twin.data) rememberTwin(twin.data.id);
  }, [twin.data]);

  const toggle = () =>
    setCollapsed((c) => {
      try {
        localStorage.setItem(COLLAPSE_KEY, c ? '0' : '1');
      } catch {
        /* preference only */
      }
      return !c;
    });

  const workspace = useMemo<TwinWorkspace | null>(
    () =>
      twin.data && asset.data
        ? { twin: twin.data, asset: asset.data, runtimeConnected: !!twin.data.runtimeConnected || !!twin.data.runtimeUrl, base }
        : null,
    [twin.data, asset.data, base],
  );

  return (
    <div className={clsx('vts-twin-ws', collapsed && 'is-collapsed')}>
      <aside className="vts-twin-side" aria-label="Twin navigation">
        <button type="button" className="vts-twin-side__toggle" onClick={toggle} aria-expanded={!collapsed} aria-label={collapsed ? 'Expand navigation' : 'Collapse navigation'} title={collapsed ? 'Expand navigation' : 'Collapse navigation'}>
          {collapsed ? <PanelLeftOpen size={17} /> : <PanelLeftClose size={17} />}
          {!collapsed && <span>Collapse</span>}
        </button>
        <Sidebar base={base} collapsed={collapsed} />
      </aside>
      <div className="vts-twin-main">
        <QueryState query={twin} skeletonLines={3}>
          {(t) => {
            const trust = overallTrust(t);
            return (
              <header className="vts-twin-head">
                <div className="vts-twin-head__row">
                  <Link to="/twins" className="vts-back"><ArrowLeft size={15} aria-hidden="true" /> Twins</Link>
                  <span className="vts-twin-head__sep" aria-hidden="true" />
                  <h1 className="vts-twin-head__name">
                    <Link to={base}>{t.name}</Link>
                    <TwinSwitcher current={t.id} />
                  </h1>
                  {asset.data && <span className="vts-twin-head__type">{asset.data.type}</span>}
                  <div className="grow" />
                  <OperationalBadge status={live} />
                  <ModeBadge status={live} />
                  {t.deployment && (
                    <Link className="vts-tag" to={`${base}/engineering/package`} title="Deployed verified package">
                      {t.deployment.packageId}
                    </Link>
                  )}
                  {trust && <TrustBadge state={trust} label="Verification" />}
                  <Link
                    to={t.blueprintId ? `/studio/blueprints/${encodeURIComponent(t.blueprintId)}/v/${t.blueprintVersion ?? 1}` : `/studio?twin=${encodeURIComponent(t.id)}`}
                    className="vts-btn vts-btn--secondary vts-btn--sm"
                    title={t.blueprintId ? `Open ${t.blueprintId} v${t.blueprintVersion} (this twin's Blueprint) in Studio` : 'Open Studio for this twin'}
                  >
                    <PencilRuler size={14} aria-hidden="true" /> Open in Studio
                  </Link>
                </div>
                <TwinCrumbs base={base} twinName={t.name} />
              </header>
            );
          }}
        </QueryState>
        {twin.data && !twin.data.assetId ? (
          <div className="vts-page">
            <EmptyState title="This twin is not bound to an asset">Operational pages need the twin's asset; bind one in Studio.</EmptyState>
          </div>
        ) : workspace ? (
          <TwinScopeContext.Provider value={workspace}>
            <Outlet context={workspace} />
          </TwinScopeContext.Provider>
        ) : (
          <div className="vts-page">
            <QueryState query={asset} skeletonLines={6}>{() => null}</QueryState>
          </div>
        )}
      </div>
    </div>
  );
}
