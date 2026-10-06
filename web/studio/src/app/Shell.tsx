/**
 * Application shell: navigation (information architecture), top bar (global search,
 * live status, disclosure and theme), and the routed page.
 */
import {
  ChevronDown,
  CircleHelp,
  Moon,
  Pause,
  Play,
  Search,
  Sun,
} from 'lucide-react';
import * as DropdownMenu from '@radix-ui/react-dropdown-menu';
import { useEffect, useState } from 'react';
import { Link, NavLink, Outlet, useLocation } from 'react-router-dom';
import clsx from 'clsx';
import { getActor, setActor } from '@/api/client';
import { lastTwin, twinRoute } from '@/app/twinScope';
import { Button, Segmented } from '@/design';
import { useLive } from '@/live/LiveProvider';
import { useDisclosure } from './disclosure';
import { SearchPalette } from './SearchPalette';

/** OPERATE | STUDIO: the two modes of the product (§ "Studio is a mode"). */
function ModeSwitch() {
  const location = useLocation();
  const studio = location.pathname.startsWith('/studio');
  const twinMatch = location.pathname.match(/^\/twins\/([^/]+)/);
  const current = twinMatch ? decodeURIComponent(twinMatch[1]!) : null;
  const back = lastTwin();
  return (
    <nav className="vts-mode" aria-label="Mode">
      <Link to={!studio ? location.pathname : back ? twinRoute(back) : '/twins'} className={clsx('vts-mode__opt', !studio && 'active')} aria-current={!studio ? 'page' : undefined}>Operate</Link>
      <Link to={current ? `/studio?twin=${encodeURIComponent(current)}` : '/studio'} className={clsx('vts-mode__opt', studio && 'active')} aria-current={studio ? 'page' : undefined}>Studio</Link>
    </nav>
  );
}

function LiveIndicator() {
  const live = useLive();
  const label =
    live.paused
      ? `Paused · ${live.pendingWhilePaused} update${live.pendingWhilePaused === 1 ? '' : 's'} waiting`
      : live.connection === 'open'
        ? 'Live'
        : live.connection === 'connecting'
          ? 'Connecting…'
          : live.connection === 'reconnecting'
            ? 'Reconnecting…'
            : 'Offline';
  const tone = live.paused ? 'warning' : live.connection === 'open' ? 'ok' : live.connection === 'closed' ? 'neutral' : 'warning';
  return (
    <div className="row" aria-live="polite">
      <span className={`vts-badge vts-badge--${tone}`} title={live.lastEventAt ? `Last update ${live.lastEventAt}` : undefined}>
        <span className="vts-dot" style={{ background: 'currentColor' }} aria-hidden="true" />
        {label}
      </span>
      <Button
        size="sm"
        variant="ghost"
        iconOnly
        icon={live.paused ? <Play size={14} /> : <Pause size={14} />}
        onClick={() => (live.paused ? live.resume() : live.pause())}
      >
        {live.paused ? 'Resume live updates (jump to now)' : 'Pause live updates'}
      </Button>
    </div>
  );
}

function UserMenu() {
  const { theme, setTheme } = useDisclosure();
  const [actor, setActorState] = useState(getActor());
  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <Button size="sm" variant="ghost" icon={theme === 'dark' ? <Moon size={14} /> : <Sun size={14} />}>
          <span className="truncate" style={{ maxWidth: 120 }}>{actor}</span>
          <ChevronDown size={12} />
        </Button>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content className="vts-menu" align="end" sideOffset={6}>
          <DropdownMenu.Label className="xsmall subtle" style={{ padding: '6px 10px' }}>
            Name recorded in the engineering audit (not a login)
          </DropdownMenu.Label>
          <DropdownMenu.Item
            className="vts-menu__item"
            onSelect={() => {
              const n = window.prompt('Your name for audit records', actor);
              if (n && n.trim()) {
                setActor(n.trim().slice(0, 64));
                setActorState(n.trim().slice(0, 64));
              }
            }}
          >
            Change name…
          </DropdownMenu.Item>
          <DropdownMenu.Separator className="vts-divider" />
          <DropdownMenu.Label className="xsmall subtle" style={{ padding: '6px 10px' }}>Theme</DropdownMenu.Label>
          {(['system', 'light', 'dark'] as const).map((t) => (
            <DropdownMenu.CheckboxItem key={t} className="vts-menu__item" checked={theme === t} onCheckedChange={() => setTheme(t)}>
              {t === 'system' ? 'Match system' : t === 'light' ? 'Light' : 'Dark'}
              {theme === t ? ' ✓' : ''}
            </DropdownMenu.CheckboxItem>
          ))}
          <DropdownMenu.Separator className="vts-divider" />
          <DropdownMenu.Item className="vts-menu__item" asChild>
            <NavLink to="/about">About & component versions</NavLink>
          </DropdownMenu.Item>
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
  );
}

export function Shell() {
  const { disclosure, setDisclosure } = useDisclosure();
  const [searchOpen, setSearchOpen] = useState(false);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && !e.shiftKey && !e.altKey && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        setSearchOpen(true);
      }
      if (e.key === '/' && !(e.target instanceof HTMLInputElement) && !(e.target instanceof HTMLTextAreaElement) &&
          !(e.target as HTMLElement)?.isContentEditable) {
        e.preventDefault();
        setSearchOpen(true);
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  return (
    <div className="vts-app">
      <a href="#main" className="skip-link">
        Skip to content
      </a>
      <header className="vts-topbar">
        <Link to="/twins" className="vts-brand" aria-label="Verified Twin Studio — your twins">
          <svg width="22" height="22" viewBox="0 0 32 32" aria-hidden="true">
            <rect width="32" height="32" rx="7" fill="var(--accent)" />
            <path d="M8 10h7v12H8zM17 10h7v5h-7zM17 17h7v5h-7z" fill="#fff" />
          </svg>
          <span>
            Verified Twin <strong>Studio</strong>
          </span>
        </Link>
        <ModeSwitch />
        <button type="button" className="vts-search-trigger" onClick={() => setSearchOpen(true)}>
          <Search size={15} aria-hidden="true" />
          <span className="grow">Search twins, assets, symbols, versions…</span>
          <kbd className="vts-kbd">⌘K</kbd>
        </button>
        <div className="grow" />
        <LiveIndicator />
        <a className="vts-btn vts-btn--ghost vts-btn--sm" href="/docs/" target="_blank" rel="noopener" title="Product manual, tutorial and screenshot tour (opens in a new tab)">
          <CircleHelp size={15} aria-hidden="true" /> Help
        </a>
        <Segmented
          label="Detail level"
          value={disclosure}
          onChange={setDisclosure}
          options={[
            { id: 'operations', label: 'Operator' },
            { id: 'engineering', label: 'Engineer' },
          ]}
        />
        <UserMenu />
      </header>
      <main id="main" className="vts-content" tabIndex={-1}>
        <Outlet />
      </main>
      <SearchPalette open={searchOpen} onOpenChange={setSearchOpen} />
    </div>
  );
}
