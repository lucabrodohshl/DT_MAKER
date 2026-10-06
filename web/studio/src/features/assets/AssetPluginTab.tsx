/**
 * Hosts a domain plugin view for the asset's twin. The host owns the runtime
 * connection, ordering and resynchronisation; plugins receive ordered events and
 * the latest kernel state as props.
 */
import { useQueryClient } from '@tanstack/react-query';
import { useCallback, useEffect, useMemo, useRef } from 'react';
import { useParams } from 'react-router-dom';
import { EmptyState } from '@/design';
import { pluginsFor } from '@/plugins/registry';
import type { DomainPlugin, DomainPluginProps } from '@/plugins/types';
import type { AssetDetail, TwinDetail } from '@/api/types';
import { RuntimeStreamClient, runtimeApi, runtimeKeys, useRuntimeState } from '@/runtime/client';
import type { RuntimeStreamEvent } from '@/runtime/types';
import { useAssetContext } from './AssetLayout';

/** Shared subscription helper given to plugins. */
export function useRuntimeSubscribe(twinId: string | null, enabled: boolean): DomainPluginProps['subscribe'] {
  const clients = useRef(new Set<RuntimeStreamClient>());
  useEffect(() => {
    const set = clients.current;
    return () => {
      set.forEach((c) => c.stop());
      set.clear();
    };
  }, []);
  return useCallback(
    (topics: string[], onEvent: (e: RuntimeStreamEvent) => void, onGap?: (reason: string) => void) => {
      if (!twinId || !enabled || typeof EventSource === 'undefined') return () => undefined;
      const client = new RuntimeStreamClient(twinId, topics, onEvent, onGap ?? (() => undefined), () => undefined);
      clients.current.add(client);
      client.start();
      return () => {
        client.stop();
        clients.current.delete(client);
      };
    },
    [twinId, enabled],
  );
}

/** Hosts one domain plugin for a twin, live. Used by the asset tab and the twin overview. */
export function PluginHost({ plugin, twin, asset, runtimeConnected }: { plugin: DomainPlugin; twin: TwinDetail; asset: AssetDetail | null; runtimeConnected: boolean }) {
  const state = useRuntimeState(runtimeConnected ? twin.id : null);
  const qc = useQueryClient();
  const subscribe = useRuntimeSubscribe(twin.id, runtimeConnected);
  // Depend on the twin's id only: twin records are re-fetched often (their trust part carries an
  // evaluation time), and a new object must not tear down the runtime client or the subscriptions.
  const twinId = twin.id;
  const api = useMemo(() => runtimeApi(twinId), [twinId]);

  // Keep the kernel state current: refetch on state/decision events and after gaps.
  useEffect(() => {
    if (!runtimeConnected) return;
    return subscribe(
      ['state', 'decision'],
      () => void qc.invalidateQueries({ queryKey: runtimeKeys.state(twinId) }),
      () => void qc.invalidateQueries({ queryKey: runtimeKeys.state(twinId) }),
    );
  }, [twinId, runtimeConnected, subscribe, qc]);

  const Component = plugin.Component;
  return (
    <Component
      twin={twin}
      asset={asset}
      mode="live"
      state={state.data ?? null}
      runtime={api}
      subscribe={subscribe}
      replay={null}
      runtimeConnected={runtimeConnected && !state.isError}
    />
  );
}

export default function AssetPluginTab() {
  const { pluginId } = useParams();
  const { asset, twin, runtimeConnected } = useAssetContext();
  const plugin = pluginsFor(twin).find((p) => p.id === pluginId);
  if (!twin || !plugin) {
    return <EmptyState title="View not available">No domain view with this name applies to this asset's twin.</EmptyState>;
  }
  return <PluginHost plugin={plugin} twin={twin} asset={asset} runtimeConnected={runtimeConnected} />;
}
