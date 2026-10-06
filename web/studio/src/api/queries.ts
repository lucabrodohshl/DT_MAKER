/**
 * Server-state hooks (TanStack Query). One place defines every query key, so live
 * events can invalidate precisely (see live/useLiveInvalidation.ts).
 *
 * Trust-bearing queries (twins, pipelines, evidence status, package integrity,
 * audit verification) use `staleTime: 0`: they are re-validated whenever they are
 * shown, so a cache can never make stale verification status appear current.
 */
import { keepPreviousData, useMutation, useQuery, useQueryClient, type QueryKey } from '@tanstack/react-query';
import { api } from './client';
import { auditVerificationSchema, packageIntegritySchema, pipelineSchema, twinTrustSchema, validated } from './schemas';
import type {
  About,
  ApplicabilityReport,
  ArtifactDetail,
  ArtifactKind,
  ArtifactSummary,
  ArtifactVersion,
  AssetDetail,
  Asset,
  AuditRecord,
  AuditVerification,
  Change,
  DiffResult,
  Deployment,
  EvaluationResult,
  EvidenceRecord,
  GraphFacets,
  ImpactReport,
  LogEntry,
  Neighborhood,
  Overview,
  PackageDetail,
  PackageIntegrity,
  PackageRecord,
  Page,
  Pipeline,
  RollbackPreview,
  SearchHit,
  SymbolInfo,
  TelemetryChannels,
  TelemetrySeries,
  TwinDetail,
  TwinSummary,
  VersionDetail,
} from './types';

export const keys = {
  about: ['about'] as const,
  overview: ['overview'] as const,
  search: (q: string) => ['search', q] as const,
  assets: (params: Record<string, unknown>) => ['assets', 'list', params] as const,
  asset: (id: string) => ['assets', 'detail', id] as const,
  neighborhood: (id: string, depth: number, types: string[]) => ['assets', 'neighborhood', id, depth, types] as const,
  facets: ['graph', 'facets'] as const,
  channels: (assetId: string) => ['telemetry', 'channels', assetId] as const,
  series: (channel: string, from: number, to: number, points: number) => ['telemetry', 'series', channel, from, to, points] as const,
  twins: ['twins'] as const,
  twin: (id: string) => ['twins', id] as const,
  artifacts: (kind?: ArtifactKind) => ['artifacts', 'list', kind ?? 'all'] as const,
  artifact: (id: string) => ['artifacts', 'detail', id] as const,
  version: (id: string, v: number) => ['artifacts', 'version', id, v] as const,
  diff: (from: string, to: string) => ['artifacts', 'diff', from, to] as const,
  symbol: (ref: string, name: string) => ['artifacts', 'symbol', ref, name] as const,
  impact: (ref: string) => ['impact', ref] as const,
  evidenceList: (params: Record<string, unknown>) => ['evidence', 'list', params] as const,
  evidence: (id: string) => ['evidence', 'detail', id] as const,
  evidenceStatus: (id: string, ctx: string) => ['evidence', 'status', id, ctx] as const,
  changes: (state?: string) => ['changes', 'list', state ?? 'all'] as const,
  change: (id: string) => ['changes', 'detail', id] as const,
  pipeline: (id: string) => ['changes', 'pipeline', id] as const,
  packages: (twin?: string) => ['packages', 'list', twin ?? 'all'] as const,
  package: (id: string) => ['packages', 'detail', id] as const,
  packageIntegrity: (id: string) => ['packages', 'integrity', id] as const,
  deployments: (twin?: string) => ['deployments', twin ?? 'all'] as const,
  rollbackPreview: (twin: string, pkg: string) => ['deployments', 'rollback-preview', twin, pkg] as const,
  audit: (params: Record<string, unknown>) => ['audit', 'list', params] as const,
  logs: (params: Record<string, unknown>) => ['logs', params] as const,
} satisfies Record<string, QueryKey | ((...args: never[]) => QueryKey)>;

// ------------------------------------------------------------------ general
export const useAbout = () => useQuery({ queryKey: keys.about, queryFn: () => api.get<About>('/about'), staleTime: 60_000 });
export const useOverview = () => useQuery({ queryKey: keys.overview, queryFn: () => api.get<Overview>('/overview'), staleTime: 0 });
export const useSearch = (q: string) =>
  useQuery({
    queryKey: keys.search(q),
    queryFn: ({ signal }) => api.get<{ query: string; hits: SearchHit[] }>('/search', { q, limit: 30 }, signal),
    enabled: q.trim().length >= 2,
    placeholderData: keepPreviousData,
    staleTime: 10_000,
  });

// ------------------------------------------------------------------ assets
export const useAssets = (params: { type?: string; parent?: string; q?: string; limit?: number; offset?: number }) =>
  useQuery({
    queryKey: keys.assets(params),
    queryFn: ({ signal }) => api.get<Page<Asset>>('/assets', params, signal),
    placeholderData: keepPreviousData,
  });
export const useAsset = (id: string | undefined) =>
  useQuery({ queryKey: keys.asset(id ?? ''), queryFn: () => api.get<AssetDetail>(`/assets/${encodeURIComponent(id!)}`), enabled: !!id });
export const useNeighborhood = (id: string | undefined, depth: number, types: string[]) =>
  useQuery({
    queryKey: keys.neighborhood(id ?? '', depth, types),
    queryFn: () => api.get<Neighborhood>(`/assets/${encodeURIComponent(id!)}/neighborhood`, { depth, types: types.join(','), max: 120 }),
    enabled: !!id,
    placeholderData: keepPreviousData,
  });
export const useGraphFacets = () => useQuery({ queryKey: keys.facets, queryFn: () => api.get<GraphFacets>('/graph/facets') });

// ------------------------------------------------------------------ telemetry
export const useChannels = (assetId: string | undefined, opts?: { refetchMs?: number | false }) =>
  useQuery({
    queryKey: keys.channels(assetId ?? ''),
    queryFn: () => api.get<TelemetryChannels>(`/assets/${encodeURIComponent(assetId!)}/telemetry`, { descendants: true }),
    enabled: !!assetId,
    refetchInterval: opts?.refetchMs ?? false,
  });
export const useSeries = (channel: string | undefined, from: number, to: number, points = 800) =>
  useQuery({
    queryKey: keys.series(channel ?? '', from, to, points),
    queryFn: ({ signal }) =>
      api.get<TelemetrySeries>(`/telemetry/${encodeURIComponent(channel!)}/series`, { from, to, maxPoints: points }, signal),
    enabled: !!channel,
    placeholderData: keepPreviousData,
  });

// ------------------------------------------------------------------ twins
export const useTwins = () => useQuery({ queryKey: keys.twins, queryFn: () => api.get<TwinSummary[]>('/twins'), staleTime: 0 });
export const useTwin = (id: string | undefined | null) =>
  useQuery({
    queryKey: keys.twin(id ?? ''),
    queryFn: async () => {
      const t = await api.get<TwinDetail>(`/twins/${encodeURIComponent(id!)}`);
      validated(twinTrustSchema, t.trust, 'twin trust');
      return t;
    },
    enabled: !!id,
    staleTime: 0,
  });

// ------------------------------------------------------------------ artifacts
export const useArtifacts = (kind?: ArtifactKind) =>
  useQuery({ queryKey: keys.artifacts(kind), queryFn: () => api.get<ArtifactSummary[]>('/artifacts', { kind }) });
export const useArtifact = (id: string | undefined) =>
  useQuery({ queryKey: keys.artifact(id ?? ''), queryFn: () => api.get<ArtifactDetail>(`/artifacts/${encodeURIComponent(id!)}`), enabled: !!id });
export const useVersion = (id: string | undefined, v: number | undefined) =>
  useQuery({
    queryKey: keys.version(id ?? '', v ?? 0),
    queryFn: () => api.get<VersionDetail>(`/artifacts/${encodeURIComponent(id!)}/versions/${v}`),
    enabled: !!id && !!v,
    staleTime: 0,
  });
export const useDiff = (from: string | undefined, to: string | undefined) =>
  useQuery({ queryKey: keys.diff(from ?? '', to ?? ''), queryFn: () => api.get<DiffResult>('/diff', { from, to }), enabled: !!from && !!to });
export const useSymbol = (ref: string | undefined, name: string | undefined) =>
  useQuery({
    queryKey: keys.symbol(ref ?? '', name ?? ''),
    queryFn: () => {
      const [id, v] = ref!.split('@');
      return api.get<SymbolInfo>(`/artifacts/${encodeURIComponent(id!)}/versions/${v}/symbols/${encodeURIComponent(name!)}`);
    },
    enabled: !!ref && !!name,
  });
export const useImpact = (ref: string | undefined) =>
  useQuery({
    queryKey: keys.impact(ref ?? ''),
    queryFn: () => {
      const [id, v] = ref!.split('@');
      return api.get<ImpactReport>(`/artifacts/${encodeURIComponent(id!)}/versions/${v}/impact`);
    },
    enabled: !!ref,
    staleTime: 0,
  });

// ------------------------------------------------------------------ evidence
export const useEvidenceList = (params: { kind?: string; artifact?: string; version?: number; change?: string; limit?: number; offset?: number }) =>
  useQuery({
    queryKey: keys.evidenceList(params),
    queryFn: () => api.get<Page<EvidenceRecord>>('/evidence', params),
    placeholderData: keepPreviousData,
    staleTime: 0,
  });
export const useEvidence = (id: string | undefined) =>
  useQuery({ queryKey: keys.evidence(id ?? ''), queryFn: () => api.get<EvidenceRecord>(`/evidence/${encodeURIComponent(id!)}`), enabled: !!id });
export const useEvidenceStatus = (id: string | undefined, ctx: { twin?: string; change?: string }) =>
  useQuery({
    queryKey: keys.evidenceStatus(id ?? '', ctx.change ? `change:${ctx.change}` : `twin:${ctx.twin}`),
    queryFn: () => api.get<ApplicabilityReport>(`/evidence/${encodeURIComponent(id!)}/status`, ctx),
    enabled: !!id && (!!ctx.twin || !!ctx.change),
    staleTime: 0,
  });

// ------------------------------------------------------------------ changes & release
export const useChanges = (state?: string) =>
  useQuery({ queryKey: keys.changes(state), queryFn: () => api.get<Change[]>('/changes', { state }), staleTime: 0 });
export const useChange = (id: string | undefined) =>
  useQuery({ queryKey: keys.change(id ?? ''), queryFn: () => api.get<Change>(`/changes/${encodeURIComponent(id!)}`), enabled: !!id, staleTime: 0 });
export const usePipeline = (id: string | undefined) =>
  useQuery({
    queryKey: keys.pipeline(id ?? ''),
    queryFn: async () => {
      const p = await api.get<Pipeline>(`/changes/${encodeURIComponent(id!)}/pipeline`);
      validated(pipelineSchema, p, 'release pipeline');
      return p;
    },
    enabled: !!id,
    staleTime: 0,
  });

export const usePackages = (twin?: string) =>
  useQuery({ queryKey: keys.packages(twin), queryFn: () => api.get<PackageRecord[]>('/packages', { twin }) });
export const usePackage = (id: string | undefined) =>
  useQuery({ queryKey: keys.package(id ?? ''), queryFn: () => api.get<PackageDetail>(`/packages/${encodeURIComponent(id!)}`), enabled: !!id, staleTime: 0 });
export const useDeployments = (twin?: string) =>
  useQuery({ queryKey: keys.deployments(twin), queryFn: () => api.get<Deployment[]>('/deployments', { twin }), staleTime: 0 });
export const useRollbackPreview = (twin: string | undefined, pkg: string | undefined) =>
  useQuery({
    queryKey: keys.rollbackPreview(twin ?? '', pkg ?? ''),
    queryFn: () => api.get<RollbackPreview>('/deployments/rollback-preview', { twin, package: pkg }),
    enabled: !!twin && !!pkg,
    staleTime: 0,
  });

// ------------------------------------------------------------------ audit & logs
export const useAudit = (params: { operation?: string; subject?: string; actor?: string; limit?: number; offset?: number }) =>
  useQuery({ queryKey: keys.audit(params), queryFn: () => api.get<Page<AuditRecord>>('/audit', params), placeholderData: keepPreviousData });
export const useLogs = (params: Record<string, string | number | undefined>, live: boolean) =>
  useQuery({
    queryKey: keys.logs(params),
    queryFn: () => api.get<{ items: LogEntry[] }>('/logs', params),
    refetchInterval: live ? 5000 : false,
    placeholderData: keepPreviousData,
  });

// ------------------------------------------------------------------ mutations
/** Invalidate everything that depends on engineering state (cheap: queries refetch only when shown). */
export function useInvalidateEngineering() {
  const qc = useQueryClient();
  return () =>
    Promise.all(
      ['artifacts', 'evidence', 'changes', 'packages', 'deployments', 'twins', 'impact', 'audit', 'overview'].map((k) =>
        qc.invalidateQueries({ queryKey: [k] }),
      ),
    );
}

/** Generic mutation that invalidates engineering state on settle. */
export function useEngineeringMutation<TVars, TResult>(fn: (vars: TVars) => Promise<TResult>) {
  const invalidate = useInvalidateEngineering();
  return useMutation({ mutationFn: fn, onSettled: () => invalidate() });
}

export const verifyAudit = () =>
  api.post<AuditVerification>('/audit/verify').then((v) => validated(auditVerificationSchema, v, 'audit verification'));
export const verifyPackage = (id: string) =>
  api.post<PackageIntegrity>(`/packages/${encodeURIComponent(id)}/verify`).then((v) => validated(packageIntegritySchema, v, 'package integrity') as unknown as PackageIntegrity);
export const evaluateInterpretation = (ref: string, body: { observations?: Record<string, string>; assetId?: string; keys?: string[] }) => {
  const [id, v] = ref.split('@');
  return api.post<EvaluationResult>(`/artifacts/${encodeURIComponent(id!)}/versions/${v}/evaluate`, body);
};
export const versionPath = (ref: string) => {
  const [id, v] = ref.split('@');
  return `/artifacts/${encodeURIComponent(id!)}/versions/${v}`;
};
export type { ArtifactVersion };
