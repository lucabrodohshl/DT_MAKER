/**
 * Asset page frame: identity, context (breadcrumbs), the bound twin and tabs.
 * Child tabs read the loaded asset via useAssetContext().
 */
import { Network, PlugZap, Workflow } from 'lucide-react';
import { Link, Outlet, useOutletContext, useParams } from 'react-router-dom';
import { useAsset } from '@/api/queries';
import type { AssetDetail, TwinDetail } from '@/api/types';
import { useRuntimeState } from '@/runtime/client';
import { NavTabs, PageHeader, QueryState, StatusBadge, TONE_ICON, toneOf } from '@/design';
import { Crumbs } from '@/features/common/links';
import { pluginsFor } from '@/plugins/registry';
import { useTwinScope } from '@/app/twinScope';

export interface AssetContext {
  asset: AssetDetail;
  twin: TwinDetail | null;
  runtimeConnected: boolean;
}

export const useAssetContext = () => useOutletContext<AssetContext>();

function TwinStatus({ twin, connected }: { twin: TwinDetail; connected: boolean }) {
  const state = useRuntimeState(connected ? twin.id : null);
  if (!connected) {
    return <StatusBadge tone="neutral" icon={PlugZap} label="Runtime not connected" title="Live behavioural state requires the twin's runtime" />;
  }
  if (state.isError) return <StatusBadge tone="warning" icon={PlugZap} label="Runtime unreachable" />;
  const loc = state.data?.configurations[0]?.location;
  if (!loc) return <span className="subtle small">Loading state…</span>;
  const p = twin.presentation.states?.[loc];
  const tone = toneOf(p?.tone);
  return <StatusBadge tone={tone} icon={TONE_ICON[tone]} label={`Mode: ${p?.label ?? loc}`} size="lg" />;
}

export default function AssetLayout() {
  const { assetId } = useParams();
  const scope = useTwinScope();
  const q = useAsset(assetId);
  return (
    <div className="vts-page">
      <QueryState query={q} skeletonLines={6}>
        {(asset) => {
          const twin = asset.twin;
          const connected = !!twin?.runtimeConnected || !!twin?.runtimeUrl;
          const base = scope ? `${scope.base}/assets/${encodeURIComponent(asset.id)}` : `/assets/${encodeURIComponent(asset.id)}`;
          const plugins = pluginsFor(twin);
          return (
            <div className="stack">
              <PageHeader
                eyebrow={<Crumbs items={[{ label: 'Assets', to: '/assets' }, ...asset.ancestors.map((a) => ({ label: a.name, to: `/assets/${encodeURIComponent(a.id)}` })), { label: asset.name }]} />}
                title={asset.name}
                meta={
                  <>
                    <span className="vts-tag">{asset.type}</span>
                    <span className="mono xsmall">{asset.id}</span>
                    {twin && (
                      <span className="row">
                        <Workflow size={14} aria-hidden="true" />
                        {asset.twinInherited ? 'Part of twin' : 'Twin'} <strong>{twin.name}</strong>
                      </span>
                    )}
                  </>
                }
                actions={
                  <>
                    {twin && <TwinStatus twin={twin} connected={connected} />}
                    <Link className="vts-btn vts-btn--sm" to={scope ? `${scope.base}/knowledge?focus=${encodeURIComponent(asset.id)}` : `/graph?focus=${encodeURIComponent(asset.id)}`}>
                      <Network size={14} /> Show in graph
                    </Link>
                  </>
                }
              />
              <NavTabs
                label="Asset views"
                tabs={[
                  { to: base, label: 'Overview', end: true },
                  { to: `${base}/telemetry`, label: 'Telemetry' },
                  ...(twin
                    ? [
                        { to: `${base}/behavior`, label: 'Behaviour' },
                        { to: `${base}/predict`, label: 'Predict' },
                        ...plugins.map((p) => ({ to: `${base}/view/${p.id}`, label: p.title })),
                        { to: `${base}/history`, label: 'History & audit' },
                      ]
                    : []),
                  { to: `${base}/relationships`, label: 'Relationships' },
                ]}
              />
              <Outlet context={{ asset, twin, runtimeConnected: connected } satisfies AssetContext} />
            </div>
          );
        }}
      </QueryState>
    </div>
  );
}
