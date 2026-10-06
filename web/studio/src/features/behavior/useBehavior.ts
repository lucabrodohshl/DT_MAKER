/**
 * Behaviour data for a twin: the model (IR) — from the runtime when connected,
 * otherwise from the deployed package — the live kernel state, and the recent
 * kernel steps (from the ledger), kept current by the runtime stream.
 */
import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useCallback } from 'react';
import { ApiError, api } from '@/api/client';
import type { TwinDetail } from '@/api/types';
import { runtimeApi, runtimeKeys, useRuntimeModel, useRuntimeState, useRuntimeStream } from '@/runtime/client';
import type { LedgerPage, TwinIr } from '@/runtime/types';

export function usePackageIr(packageId: string | null | undefined) {
  return useQuery({
    queryKey: ['packages', 'ir', packageId],
    queryFn: () => api.get<TwinIr & { irSha256: string }>(`/packages/${encodeURIComponent(packageId!)}/ir`),
    enabled: !!packageId,
    staleTime: Infinity,
  });
}

export function useRecentSteps(twinId: string | null, enabled: boolean) {
  return useQuery({
    queryKey: ['runtime', twinId, 'recent-steps'],
    queryFn: () => runtimeApi(twinId!).get<LedgerPage>('/runtime/ledger', { tail: 40, kind: 'step' }),
    enabled: !!twinId && enabled,
    retry: (n, e) => !(e instanceof ApiError && e.isRuntimeNotConnected) && n < 2,
    staleTime: 0,
  });
}

export function useBehavior(twin: TwinDetail | null, connected: boolean) {
  const qc = useQueryClient();
  const twinId = twin?.id ?? null;
  const state = useRuntimeState(connected ? twinId : null);
  const runtimeModel = useRuntimeModel(connected ? twinId : null);
  const packageIr = usePackageIr(!connected || runtimeModel.isError ? twin?.package?.id : null);
  const steps = useRecentSteps(twinId, connected);

  const refresh = useCallback(() => {
    if (!twinId) return;
    void qc.invalidateQueries({ queryKey: runtimeKeys.state(twinId) });
    void qc.invalidateQueries({ queryKey: ['runtime', twinId, 'recent-steps'] });
  }, [qc, twinId]);
  const streamStatus = useRuntimeStream(connected ? twinId : null, ['state', 'decision', 'ledger'], refresh, refresh);

  const ir = runtimeModel.data ?? packageIr.data ?? null;
  const irSource: 'runtime' | 'package' | null = runtimeModel.data ? 'runtime' : packageIr.data ? 'package' : null;
  const recentTransitions: string[] = [];
  const stepRecords = (steps.data?.records ?? []).filter((r) => r.body.kind === 'step');
  for (const r of [...stepRecords].reverse()) {
    for (const b of r.body.outcome?.branches ?? []) if (!recentTransitions.includes(b.transition)) recentTransitions.push(b.transition);
    if (recentTransitions.length >= 5) break;
  }
  return { state, ir, irSource, irQuery: runtimeModel.data ? runtimeModel : packageIr, steps, stepRecords, recentTransitions, streamStatus };
}
