/**
 * Server state of a twin model: GET/PUT …/model, the generated representations and lifting.
 * The server compiles on every save; the browser only shows what it returns.
 */
import { useQuery } from '@tanstack/react-query';
import { api } from '@/api/client';
import { blueprintPath } from '@/api/blueprints';
import type { GeneratedView, LiftResult, ModelDocumentView, ModelSaveResult, TwinModel } from './types';

export const modelKeys = {
  document: (id: string, v: number) => ['blueprints', 'model-document', id, v] as const,
  generated: (id: string, v: number, digest: string) => ['blueprints', 'generated', id, v, digest] as const,
};

export const useModelDocument = (id: string | undefined, v: number | undefined) =>
  useQuery({
    queryKey: modelKeys.document(id ?? '', v ?? 0),
    queryFn: () => api.get<ModelDocumentView>(`${blueprintPath(id!, v)}/model`),
    enabled: !!id && !!v,
    staleTime: Infinity,
  });

/** Generated representations, keyed by the semantic digest so they refresh exactly when the semantics change. */
export const useGenerated = (id: string, v: number, digest: string | undefined, enabled = true) =>
  useQuery({
    queryKey: modelKeys.generated(id, v, digest ?? ''),
    queryFn: () => api.get<GeneratedView>(`${blueprintPath(id, v)}/model/generated`),
    enabled: enabled && !!digest,
    staleTime: Infinity,
  });

export const modelApi = {
  save: (id: string, v: number, revision: number, model: TwinModel) => api.put<ModelSaveResult>(`${blueprintPath(id, v)}/model`, { revision, model }),
  lift: (id: string, v: number, body: Record<string, unknown> = {}) => api.post<LiftResult>(`${blueprintPath(id, v)}/model/lift`, body),
};
