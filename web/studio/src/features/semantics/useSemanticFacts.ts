/**
 * Meaning of the current observations: the backend evaluates the deployed DT
 * interpretation against the latest symbol-bound telemetry of the twin's asset
 * (three-valued, Z3). The UI only displays the result.
 */
import { useQuery } from '@tanstack/react-query';
import { evaluateInterpretation } from '@/api/queries';
import type { TwinDetail } from '@/api/types';

export function dtInterpretationRef(twin: TwinDetail | null | undefined): string | null {
  return twin?.bindings?.find((b) => b.role === 'dt_interpretation')?.ref ?? null;
}

export function useSemanticFacts(twin: TwinDetail | null | undefined, opts: { refetchMs?: number } = {}) {
  const ref = dtInterpretationRef(twin);
  const assetId = twin?.assetId ?? null;
  return useQuery({
    queryKey: ['semantics', 'facts', ref, assetId],
    queryFn: () => evaluateInterpretation(ref!, { assetId: assetId! }),
    enabled: !!ref && !!assetId,
    refetchInterval: opts.refetchMs ?? 15_000,
    staleTime: 0,
  });
}
