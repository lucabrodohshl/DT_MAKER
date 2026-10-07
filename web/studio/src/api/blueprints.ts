/**
 * Blueprint Studio server state: queries and mutations over /api/v1/blueprints and
 * /api/v1/instances. Every status shown in Studio comes from these responses; nothing here
 * computes alignment, timing, validity or readiness.
 *
 * Drafts use optimistic concurrency: every save sends the revision it was based on and the
 * server refuses a stale one (409), so two editors never overwrite each other silently.
 */
import { keepPreviousData, useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { api } from './client';
import type {
  BindingTestResult,
  BlueprintDetail,
  BlueprintImpact,
  BlueprintListItem,
  BlueprintModelView,
  BlueprintPackageView,
  BlueprintSectionId,
  BlueprintStatus,
  BlueprintTemplate,
  BlueprintValidation,
  BlueprintVersionDetail,
  BlueprintVersionSummary,
  CheckResult,
  InstanceView,
  ModelImportResult,
  ModelSaveResult,
  Palettes,
  PreviewView,
  ScenarioRun,
  SectionSaveResult,
  SemanticsView,
  SupervisorStatus,
  TaLayout,
  TaModel,
  TimingResult,
  WorldRaster,
} from './types';

const enc = encodeURIComponent;
export const blueprintPath = (id: string, v?: number) => `/blueprints/${enc(id)}${v !== undefined ? `/versions/${v}` : ''}`;

export const bpKeys = {
  root: ['blueprints'] as const,
  list: ['blueprints', 'list'] as const,
  templates: ['blueprints', 'templates'] as const,
  palettes: ['blueprints', 'palettes'] as const,
  detail: (id: string) => ['blueprints', 'detail', id] as const,
  version: (id: string, v: number) => ['blueprints', 'version', id, v] as const,
  status: (id: string, v: number) => ['blueprints', 'status', id, v] as const,
  validation: (id: string, v: number) => ['blueprints', 'validation', id, v] as const,
  model: (id: string, v: number, role: string) => ['blueprints', 'model', id, v, role] as const,
  semantics: (id: string, v: number, role: string) => ['blueprints', 'semantics', id, v, role] as const,
  pkg: (id: string, v: number) => ['blueprints', 'package', id, v] as const,
  impact: (id: string, v: number, against?: number) => ['blueprints', 'impact', id, v, against ?? 'parent'] as const,
  raster: (id: string, v: number, sha: string) => ['blueprints', 'raster', id, v, sha] as const,
  preview: (id: string, v: number) => ['blueprints', 'preview', id, v] as const,
  instances: (bp?: string) => ['instances', 'list', bp ?? 'all'] as const,
  instance: (id: string) => ['instances', 'detail', id] as const,
  supervisor: ['supervisor'] as const,
};

// ------------------------------------------------------------------ queries
export const useBlueprints = () =>
  useQuery({ queryKey: bpKeys.list, queryFn: () => api.get<BlueprintListItem[]>('/blueprints'), staleTime: 0 });
export const useBlueprint = (id: string | undefined) =>
  useQuery({ queryKey: bpKeys.detail(id ?? ''), queryFn: () => api.get<BlueprintDetail>(blueprintPath(id!)), enabled: !!id, staleTime: 0 });
export const useBlueprintVersion = (id: string | undefined, v: number | undefined) =>
  useQuery({
    queryKey: bpKeys.version(id ?? '', v ?? 0),
    queryFn: () => api.get<BlueprintVersionDetail>(blueprintPath(id!, v)),
    enabled: !!id && !!v,
    staleTime: 0,
  });
export const useBlueprintStatus = (id: string | undefined, v: number | undefined) =>
  useQuery({
    queryKey: bpKeys.status(id ?? '', v ?? 0),
    queryFn: () => api.get<BlueprintStatus>(`${blueprintPath(id!, v)}/status`),
    enabled: !!id && !!v,
    staleTime: 0,
    placeholderData: keepPreviousData,
  });
export const useBlueprintValidation = (id: string | undefined, v: number | undefined) =>
  useQuery({
    queryKey: bpKeys.validation(id ?? '', v ?? 0),
    queryFn: () => api.get<BlueprintValidation>(`${blueprintPath(id!, v)}/validation`),
    enabled: !!id && !!v,
    staleTime: 0,
    placeholderData: keepPreviousData,
  });
export const useBlueprintModel = (id: string | undefined, v: number | undefined, role: 'pt' | 'dt') =>
  useQuery({
    queryKey: bpKeys.model(id ?? '', v ?? 0, role),
    queryFn: () => api.get<BlueprintModelView>(`${blueprintPath(id!, v)}/models/${role}`),
    enabled: !!id && !!v,
    staleTime: 0,
  });
export const useBlueprintSemantics = (id: string | undefined, v: number | undefined, role: 'ontology' | 'pt_interpretation' | 'dt_interpretation') =>
  useQuery({
    queryKey: bpKeys.semantics(id ?? '', v ?? 0, role),
    queryFn: () => api.get<SemanticsView>(`${blueprintPath(id!, v)}/semantics/${role}`),
    enabled: !!id && !!v,
    staleTime: 0,
  });
export const useBlueprintPackage = (id: string | undefined, v: number | undefined) =>
  useQuery({
    queryKey: bpKeys.pkg(id ?? '', v ?? 0),
    queryFn: () => api.get<BlueprintPackageView>(`${blueprintPath(id!, v)}/package`),
    enabled: !!id && !!v,
    staleTime: 0,
  });
export const useBlueprintImpact = (id: string | undefined, v: number | undefined, against?: number) =>
  useQuery({
    queryKey: bpKeys.impact(id ?? '', v ?? 0, against),
    queryFn: () => api.get<BlueprintImpact>(`${blueprintPath(id!, v)}/impact`, { against }),
    enabled: !!id && !!v,
    staleTime: 0,
  });
/** Rasterised simulator grids; keyed by the document hash so it refreshes after every save. */
export const useWorldRaster = (id: string | undefined, v: number | undefined, documentSha: string | undefined, enabled = true) =>
  useQuery({
    queryKey: bpKeys.raster(id ?? '', v ?? 0, documentSha ?? ''),
    queryFn: () => api.get<WorldRaster>(`${blueprintPath(id!, v)}/world/raster`),
    enabled: enabled && !!id && !!v,
    placeholderData: keepPreviousData,
    retry: false,
  });
export const usePreview = (id: string | undefined, v: number | undefined, poll: boolean) =>
  useQuery({
    queryKey: bpKeys.preview(id ?? '', v ?? 0),
    queryFn: () => api.get<PreviewView>(`${blueprintPath(id!, v)}/preview`),
    enabled: !!id && !!v,
    refetchInterval: poll ? 3000 : false,
    staleTime: 0,
  });
export const useBlueprintTemplates = () =>
  useQuery({ queryKey: bpKeys.templates, queryFn: () => api.get<BlueprintTemplate[]>('/blueprints/templates'), staleTime: 60_000 });
export const usePalettes = () =>
  useQuery({ queryKey: bpKeys.palettes, queryFn: () => api.get<Palettes>('/blueprints/palettes'), staleTime: 60_000 });
export const useInstances = (blueprint?: string) =>
  useQuery({ queryKey: bpKeys.instances(blueprint), queryFn: () => api.get<InstanceView[]>('/instances', { blueprint }), staleTime: 0, refetchInterval: 5000 });
export const useInstance = (id: string | undefined) =>
  useQuery({ queryKey: bpKeys.instance(id ?? ''), queryFn: () => api.get<InstanceView>(`/instances/${enc(id!)}`), enabled: !!id, staleTime: 0 });
export const useSupervisor = () =>
  useQuery({
    queryKey: bpKeys.supervisor,
    queryFn: () => api.get<{ available: boolean; binDir: string; instances: SupervisorStatus[] }>('/supervisor'),
    refetchInterval: 5000,
  });

// ------------------------------------------------------------------ commands
export const blueprintApi = {
  create: (body: Record<string, unknown>) =>
    api.post<BlueprintVersionSummary & { blueprint: BlueprintListItem; imports: { role: string; filename: string; imported: boolean; diagnostics?: unknown[] }[] }>('/blueprints', body),
  updateMeta: (id: string, body: Record<string, unknown>) => api.put<BlueprintListItem>(blueprintPath(id), body),
  createDraft: (id: string, from: number, note: string) => api.post<BlueprintVersionSummary>(`${blueprintPath(id, from)}/drafts`, { note }),
  saveSection: (id: string, v: number, section: BlueprintSectionId, revision: number, content: unknown) =>
    api.put<SectionSaveResult>(`${blueprintPath(id, v)}/sections/${section}`, { revision, content }),
  saveModel: (id: string, v: number, role: 'pt' | 'dt', revision: number, model: TaModel, layout: TaLayout | null) =>
    api.put<ModelSaveResult>(`${blueprintPath(id, v)}/models/${role}`, { revision, model, layout }),
  importModel: (id: string, v: number, role: 'pt' | 'dt', revision: number, filename: string, content: string) =>
    api.post<ModelImportResult>(`${blueprintPath(id, v)}/models/${role}/import`, { revision, filename, content }),
  saveSemantics: (id: string, v: number, role: string, revision: number, body: { content?: string; ref?: string }) =>
    api.put<SemanticsView & { revision: number }>(`${blueprintPath(id, v)}/semantics/${role}`, { revision, ...body }),
  check: (id: string, v: number, check: 'formal' | 'alignment' | 'compile' | 'scenarios' | 'package' | 'refinement', body: Record<string, unknown> = {}) =>
    api.post<CheckResult>(`${blueprintPath(id, v)}/checks/${check}`, body),
  timing: (id: string, v: number, request: Record<string, unknown>) => api.post<TimingResult>(`${blueprintPath(id, v)}/timing`, request),
  runScenario: (id: string, v: number, scenario: string) => api.post<ScenarioRun>(`${blueprintPath(id, v)}/scenarios/${enc(scenario)}/run`),
  publish: (id: string, v: number) => api.post<BlueprintVersionSummary>(`${blueprintPath(id, v)}/publish`),
  exportBundle: (id: string, v: number) => api.get<Record<string, unknown>>(`${blueprintPath(id, v)}/export`),
  testBinding: (id: string, v: number, body: Record<string, unknown>) => api.post<BindingTestResult>(`${blueprintPath(id, v)}/bindings/test`, body),
  startPreview: (id: string, v: number, body: Record<string, unknown> = {}) => api.post<PreviewView>(`${blueprintPath(id, v)}/preview`, body),
  stopPreview: (id: string, v: number) => api.del<PreviewView>(`${blueprintPath(id, v)}/preview`),
  createInstance: (body: Record<string, unknown>) => api.post<InstanceView>('/instances', body),
  deployInstance: (id: string, body: Record<string, unknown> = {}) =>
    api.post<{ instance: string; version: number; runtime: SupervisorStatus; operate: string }>(`/instances/${enc(id)}/deploy`, body),
  controlInstance: (id: string, action: 'start' | 'stop') => api.post<InstanceView>(`/instances/${enc(id)}/${action}`),
  uploadBlob: (mime: string, data: string) => api.post<{ sha256: string; url: string; mime: string; size: number }>('/blobs', { mime, data }),
};

/** Invalidate everything a Blueprint operation can change (queries refetch only when shown). */
export function useInvalidateBlueprints() {
  const qc = useQueryClient();
  return () =>
    Promise.all(
      ['blueprints', 'instances', 'supervisor', 'twins', 'artifacts', 'evidence', 'packages', 'deployments', 'audit', 'search'].map((k) =>
        qc.invalidateQueries({ queryKey: [k] }),
      ),
    );
}

/** A Blueprint mutation that refreshes Blueprint, instance and engineering state when it settles. */
export function useBlueprintMutation<TVars, TResult>(fn: (vars: TVars) => Promise<TResult>) {
  const invalidate = useInvalidateBlueprints();
  return useMutation({ mutationFn: fn, onSettled: () => invalidate() });
}

/** Route of a Blueprint version page ("" = overview). */
export function blueprintRoute(id: string, v: number, sub = ''): string {
  return `/studio/blueprints/${enc(id)}/v/${v}${sub ? `/${sub.replace(/^\//, '')}` : ''}`;
}
