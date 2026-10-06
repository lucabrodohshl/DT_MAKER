/**
 * Twin scope: the selected twin and its asset, provided by the twin workspace
 * (/twins/:twinId/...). Every operational page reads its context from here when it is
 * rendered inside a twin; outside a twin (Studio, global asset pages) the scope is null.
 */
import { createContext, useContext } from 'react';
import type { AssetDetail, TwinDetail } from '@/api/types';

export interface TwinWorkspace {
  /** The selected twin (definition, deployment, bindings, trust). */
  twin: TwinDetail;
  /** The twin's root asset. Field names match AssetContext so asset pages work unchanged. */
  asset: AssetDetail;
  /** Whether a runtime is configured for the twin. */
  runtimeConnected: boolean;
  /** Route prefix of the workspace, e.g. "/twins/pump-p101-dt". */
  base: string;
}

export const TwinScopeContext = createContext<TwinWorkspace | null>(null);

/** The selected twin, or null outside a twin workspace. */
export function useTwinScope(): TwinWorkspace | null {
  return useContext(TwinScopeContext);
}

/** Route of a page of a twin's workspace. */
export function twinRoute(twinId: string, sub = ''): string {
  return `/twins/${encodeURIComponent(twinId)}${sub ? `/${sub.replace(/^\//, '')}` : ''}`;
}

// ------------------------------------------------------------------ recent twins (viewer convenience only)
const RECENT_KEY = 'vts.recentTwins';

export function recentTwins(): string[] {
  try {
    const v = JSON.parse(localStorage.getItem(RECENT_KEY) ?? '[]');
    return Array.isArray(v) ? v.filter((x): x is string => typeof x === 'string').slice(0, 6) : [];
  } catch {
    return [];
  }
}

export function rememberTwin(id: string): void {
  try {
    localStorage.setItem(RECENT_KEY, JSON.stringify([id, ...recentTwins().filter((x) => x !== id)].slice(0, 6)));
  } catch {
    /* storage unavailable: recents are a convenience only */
  }
}

export function lastTwin(): string | null {
  return recentTwins()[0] ?? null;
}
