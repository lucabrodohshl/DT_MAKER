/**
 * Redirects that keep every earlier URL working after the reorganisation around twins:
 * global operational pages resolve to the selected twin's workspace, engineering and
 * maintenance editing resolves to Studio, and asset pages resolve to the asset's twin.
 */
import type { ReactNode } from 'react';
import { Navigate, Outlet, useLocation, useParams, useSearchParams } from 'react-router-dom';
import { useAsset, useTwins } from '@/api/queries';
import { QueryState } from '@/design';
import { lastTwin, twinRoute } from './twinScope';

const keepQuery = (params: URLSearchParams, drop: string[] = []) => {
  const p = new URLSearchParams(params);
  for (const d of drop) p.delete(d);
  const s = p.toString();
  return s ? `?${s}` : '';
};

/** A global page that is now per twin: /operations/live?twin=X → /twins/X/operations/live. */
export function ToTwin({ sub }: { sub: string }) {
  const [params] = useSearchParams();
  const twins = useTwins();
  const explicit = params.get('twin');
  if (explicit) return <Navigate to={`${twinRoute(explicit, sub)}${keepQuery(params, ['twin'])}`} replace />;
  const remembered = lastTwin();
  if (remembered) return <Navigate to={`${twinRoute(remembered, sub)}${keepQuery(params)}`} replace />;
  if (twins.isLoading) return null;
  const first = twins.data?.[0];
  return <Navigate to={first ? `${twinRoute(first.id, sub)}${keepQuery(params)}` : '/twins'} replace />;
}

/** /engineering/... and /maintenance/... → /studio/... (same remainder and query). */
export function ToStudio({ from, to = '/studio' }: { from: string; to?: string }) {
  const location = useLocation();
  const rest = location.pathname.slice(from.length);
  return <Navigate to={`${to}${rest}${location.search}`} replace />;
}

/** /audit/executions/:twinId/:session[/replay] → the twin's audit. */
export function ToTwinExecution({ replay }: { replay?: boolean }) {
  const { twinId = '', session = '' } = useParams();
  const location = useLocation();
  return <Navigate to={`${twinRoute(twinId, `audit/executions/${encodeURIComponent(session)}${replay ? '/replay' : ''}`)}${location.search}`} replace />;
}

/** Asset pages: an asset that belongs to a twin opens inside that twin's workspace. */
export function AssetGate({ children }: { children?: ReactNode }) {
  const { assetId = '' } = useParams();
  const location = useLocation();
  const q = useAsset(assetId);
  return (
    <QueryState query={q} skeletonLines={4}>
      {(a) => {
        if (a.twin) {
          const rest = location.pathname.slice(`/assets/${encodeURIComponent(assetId)}`.length);
          return <Navigate to={`${twinRoute(a.twin.id, `assets/${encodeURIComponent(assetId)}`)}${rest}${location.search}`} replace />;
        }
        return children ?? <Outlet />;
      }}
    </QueryState>
  );
}

/** /graph?focus=A → the twin's knowledge graph when A belongs to a twin. */
export function GraphGate({ children }: { children: ReactNode }) {
  const [params] = useSearchParams();
  const focus = params.get('focus') ?? undefined;
  const q = useAsset(focus);
  if (!focus) return <>{children}</>;
  if (q.isLoading) return null;
  if (q.data?.twin) return <Navigate to={`${twinRoute(q.data.twin.id, 'knowledge')}?focus=${encodeURIComponent(focus)}`} replace />;
  return <>{children}</>;
}
