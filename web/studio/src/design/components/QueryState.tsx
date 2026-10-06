/**
 * Uniform loading / empty / error / unavailable rendering for server state.
 * No view ever renders a blank panel or placeholder values that look real.
 */
import { AlertOctagon, PlugZap, RefreshCw, ServerOff } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import type { UseQueryResult } from '@tanstack/react-query';
import { ApiError } from '@/api/client';
import { useDisclosure } from '@/app/disclosure';
import { Button, EmptyState, Skeleton } from './Layout';

export function ErrorBlock({ error, onRetry, compact }: { error: unknown; onRetry?: () => void; compact?: boolean }) {
  const { engineering } = useDisclosure();
  const [details, setDetails] = useState(false);
  if (error instanceof ApiError && error.isRuntimeNotConnected) {
    return <RuntimeUnavailable message={error.message} compact={compact} />;
  }
  const api = error instanceof ApiError ? error : null;
  const message = api?.message ?? (error instanceof Error ? error.message : 'Unexpected error.');
  const unreachable = api?.status === 0;
  return (
    <EmptyState
      compact={compact}
      icon={unreachable ? <ServerOff size={28} /> : <AlertOctagon size={28} />}
      title={unreachable ? 'Studio server unreachable' : 'Could not load this information'}
      action={
        <div className="row-wrap" style={{ justifyContent: 'center' }}>
          {onRetry && (
            <Button size="sm" icon={<RefreshCw size={13} />} onClick={onRetry}>
              Retry
            </Button>
          )}
          {engineering && api && (api.context.length > 0 || api.requestId) && (
            <Button size="sm" variant="ghost" onClick={() => setDetails((d) => !d)} aria-expanded={details}>
              {details ? 'Hide details' : 'Technical details'}
            </Button>
          )}
        </div>
      }
    >
      <p>{message}</p>
      {details && api && (
        <pre className="vts-code" style={{ textAlign: 'left', marginTop: 8 }}>
          {[`code: ${api.code}`, `http: ${api.status}`, ...(api.requestId ? [`request: ${api.requestId}`] : []),
            ...api.context.map((c) => `${c.key}: ${c.value}`)].join('\n')}
        </pre>
      )}
    </EmptyState>
  );
}

/** The twin's runtime is not connected: an expected state with a clear explanation. */
export function RuntimeUnavailable({ message, compact, what }: { message?: string; compact?: boolean; what?: string }) {
  return (
    <EmptyState compact={compact} icon={<PlugZap size={28} />} title="Runtime not connected">
      <p>
        {what ? `${what} ` : ''}
        Live behavioural state, executions, predictions and ledgers are produced only by the twin's verified runtime. No
        runtime is connected for this twin, so nothing is shown rather than an estimate.
      </p>
      {message && <p className="small subtle" style={{ marginTop: 6 }}>{message}</p>}
    </EmptyState>
  );
}

/**
 * Render a query: skeleton while loading, error block (with retry) on failure,
 * `empty` when `isEmpty(data)`, otherwise `children(data)`.
 */
export function QueryState<T>({
  query,
  children,
  empty,
  isEmpty,
  skeletonLines = 3,
  compact,
}: {
  query: UseQueryResult<T>;
  children: (data: T) => ReactNode;
  empty?: ReactNode;
  isEmpty?: (data: T) => boolean;
  skeletonLines?: number;
  compact?: boolean;
}) {
  if (query.isPending) {
    return (
      <div role="status" aria-label="Loading" style={{ padding: compact ? 8 : 16 }}>
        <Skeleton lines={skeletonLines} />
      </div>
    );
  }
  if (query.isError) return <ErrorBlock error={query.error} onRetry={() => void query.refetch()} compact={compact} />;
  const data = query.data as T;
  if (isEmpty?.(data)) return <>{empty ?? <EmptyState compact title="Nothing to show" />}</>;
  return <>{children(data)}</>;
}
