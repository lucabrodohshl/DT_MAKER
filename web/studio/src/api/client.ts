/**
 * Typed HTTP client for the Studio API.
 *
 * - All requests go to `/api/v1` on the same origin (Vite proxies it in development).
 * - Errors become `ApiError` with the backend's stable code, a message suitable for
 *   operators and structured context for engineers (never a stack trace).
 * - `X-Twin-Actor` identifies the user in engineering audit records. It is not
 *   authentication (see docs/studio/security.md).
 */
import type { ApiErrorBody } from './types';

export const API_BASE = '/api/v1';

/** A failed API call with the backend's structured error. */
export class ApiError extends Error {
  readonly status: number;
  readonly code: string;
  readonly context: { key: string; value: string }[];
  readonly requestId: string | null;

  constructor(status: number, code: string, message: string, context: { key: string; value: string }[] = [], requestId: string | null = null) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
    this.context = context;
    this.requestId = requestId;
  }

  /** The runtime for a twin is not connected (a normal state, not a failure). */
  get isRuntimeNotConnected(): boolean {
    return this.code === 'runtime_not_connected';
  }

  /** The backend refused because of a lifecycle/state rule (HTTP 409). */
  get isConflict(): boolean {
    return this.status === 409;
  }

  /** Infrastructure could not be reached. */
  get isUnavailable(): boolean {
    return this.status === 503 || this.status === 0;
  }
}

const ACTOR_KEY = 'vts.actor';

/** The display name used for audit attribution (a per-browser preference). */
export function getActor(): string {
  try {
    return localStorage.getItem(ACTOR_KEY) || 'studio-user';
  } catch {
    return 'studio-user';
  }
}

export function setActor(name: string): void {
  try {
    localStorage.setItem(ACTOR_KEY, name);
  } catch {
    // Preference only; ignore storage failures.
  }
}

function isErrorBody(value: unknown): value is ApiErrorBody {
  return (
    typeof value === 'object' &&
    value !== null &&
    'error' in value &&
    typeof (value as ApiErrorBody).error?.message === 'string'
  );
}

export interface RequestOptions {
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE';
  body?: unknown;
  query?: Record<string, string | number | boolean | undefined | null>;
  signal?: AbortSignal;
}

export function buildUrl(path: string, query?: RequestOptions['query']): string {
  const params = new URLSearchParams();
  if (query) {
    for (const [k, v] of Object.entries(query)) {
      if (v !== undefined && v !== null && v !== '') params.set(k, String(v));
    }
  }
  const qs = params.toString();
  return `${API_BASE}${path}${qs ? `?${qs}` : ''}`;
}

/** Perform a request and parse JSON; throws ApiError on any non-2xx or network failure. */
export async function request<T>(path: string, options: RequestOptions = {}): Promise<T> {
  const { method = 'GET', body, query, signal } = options;
  let response: Response;
  try {
    response = await fetch(buildUrl(path, query), {
      method,
      signal,
      headers: {
        Accept: 'application/json',
        'X-Twin-Actor': getActor(),
        ...(body !== undefined ? { 'Content-Type': 'application/json' } : {}),
      },
      body: body !== undefined ? JSON.stringify(body) : method === 'POST' ? '{}' : undefined,
    });
  } catch (e) {
    if (e instanceof DOMException && e.name === 'AbortError') throw e;
    throw new ApiError(0, 'network_error', 'Studio server unreachable. Check that twin-studio is running.');
  }
  const requestId = response.headers.get('X-Request-Id');
  const text = await response.text();
  let parsed: unknown = undefined;
  if (text) {
    try {
      parsed = JSON.parse(text);
    } catch {
      if (!response.ok) {
        throw new ApiError(response.status, 'bad_response', `Unexpected response (${response.status}).`, [], requestId);
      }
      throw new ApiError(response.status, 'bad_response', 'The server returned a response that is not JSON.', [], requestId);
    }
  }
  if (!response.ok) {
    if (isErrorBody(parsed)) {
      throw new ApiError(response.status, parsed.error.code, parsed.error.message, parsed.error.context ?? [], requestId);
    }
    throw new ApiError(response.status, 'http_error', `Request failed (${response.status}).`, [], requestId);
  }
  return parsed as T;
}

export const api = {
  get: <T>(path: string, query?: RequestOptions['query'], signal?: AbortSignal) => request<T>(path, { query, signal }),
  post: <T>(path: string, body?: unknown) => request<T>(path, { method: 'POST', body }),
  put: <T>(path: string, body?: unknown) => request<T>(path, { method: 'PUT', body }),
  del: <T>(path: string) => request<T>(path, { method: 'DELETE' }),
};
