/**
 * Release → Deployment: where each instance of this Blueprint runs. The deployment supervisor of
 * this Studio host starts an instance's runtime (and its simulator or event feed) on the
 * version's released package and deployment bundle, keeps it to its desired state and stops it.
 * DEPLOYED means the supervisor runs it; whether it is CONFORMANT is shown in Operate, from the
 * runtime's own conformance monitoring.
 */
import { ExternalLink, Play, Rocket, RotateCcw, Server, Square } from 'lucide-react';
import { useState } from 'react';
import { Link } from 'react-router-dom';
import { blueprintApi, blueprintRoute, useBlueprint, useBlueprintMutation, useInstances, useSupervisor } from '@/api/blueprints';
import type { InstanceView } from '@/api/types';
import { Button, Callout, Dialog, EmptyState, ErrorBlock, KeyValue, Skeleton, StatusBadge, TimeStamp } from '@/design';
import { useEditor } from '../editor';
import { EdPage, Pane } from '../ui';
import { RuntimeBadge } from './InstancesPage';

function InstanceCard({ i }: { i: InstanceView }) {
  const e = useEditor();
  const bp = useBlueprint(e.id);
  const deploy = useBlueprintMutation((x: { version?: number; reason?: string }) => blueprintApi.deployInstance(i.id, { ...(x.version ? { version: x.version } : {}), ...(x.reason ? { reason: x.reason } : {}) }));
  const control = useBlueprintMutation((a: 'start' | 'stop') => blueprintApi.controlInstance(i.id, a));
  const [rollback, setRollback] = useState(false);
  const [target, setTarget] = useState<number | null>(null);
  const rt = i.runtime;
  const running = rt?.state === 'running';
  const published = (bp.data?.versions ?? []).filter((v) => v.state === 'published').sort((a, b) => b.version - a.version);
  const others = published.filter((v) => v.version !== i.blueprintVersion);
  const busy = deploy.isPending || control.isPending;
  return (
    <Pane
      title={
        <span className="row">
          <Server size={14} aria-hidden="true" /> {i.name} <span className="mono xsmall subtle">{i.id}</span>
        </span>
      }
      actions={<RuntimeBadge i={i} />}
    >
      <div className="stack-sm">
        <KeyValue
          compact
          items={[
            ['Version', <span key="v">v{i.blueprintVersion}{i.upgradeAvailable ? <StatusBadge tone="info" label={` v${i.latestPublishedVersion} available`} /> : null}</span>],
            ['Desired state', i.desiredState === 'running' ? 'running (restarted by the supervisor)' : 'stopped'],
            ['Runtime', rt?.runtimeUrl ? <span key="r" className="mono xsmall">{rt.runtimeUrl}</span> : '—'],
            ...(rt?.worldUrl ? ([['Simulator', <span key="w" className="mono xsmall">{rt.worldUrl}</span>]] as [string, React.ReactNode][]) : []),
            ['Started', rt?.startedAt ? <TimeStamp key="t" value={rt.startedAt} relative /> : '—'],
            ['Target', String(((i.instanceConfig?.target as { kind?: string } | undefined)?.kind ?? 'local') === 'local' ? 'Local supervisor' : (i.instanceConfig?.target as { kind?: string }).kind)],
          ]}
        />
        {(rt?.processes ?? []).length > 0 && (
          <table className="vts-table">
            <caption className="sr-only">Processes of {i.name}</caption>
            <thead>
              <tr>
                <th scope="col">Process</th>
                <th scope="col">PID</th>
                <th scope="col">Port</th>
                <th scope="col">State</th>
              </tr>
            </thead>
            <tbody>
              {rt!.processes.map((p) => (
                <tr key={p.name}>
                  <td className="mono small">{p.name}</td>
                  <td className="mono xsmall">{p.pid}</td>
                  <td className="mono xsmall">{p.port || '—'}</td>
                  <td>
                    <StatusBadge tone={p.state === 'running' ? 'ok' : p.state === 'exited' && p.exitStatus === 0 ? 'neutral' : p.state === 'exited' ? 'critical' : 'info'} label={p.state === 'exited' ? `exited (${p.exitStatus})` : p.state} />
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
        {rt?.state === 'failed' && rt.message && <Callout tone="critical" title="The deployment failed">{rt.message}</Callout>}
        <div className="row-wrap">
          {!running ? (
            <Button size="sm" variant="primary" icon={<Rocket size={13} />} loading={deploy.isPending} disabled={busy} onClick={() => deploy.mutate({})}>
              Deploy v{i.blueprintVersion}
            </Button>
          ) : (
            <Button size="sm" variant="danger" icon={<Square size={13} />} loading={control.isPending && control.variables === 'stop'} disabled={busy} onClick={() => control.mutate('stop')}>
              Stop
            </Button>
          )}
          {!running && i.desiredState !== 'running' && rt?.state !== 'not_started' && (
            <Button size="sm" icon={<Play size={13} />} loading={control.isPending && control.variables === 'start'} disabled={busy} onClick={() => control.mutate('start')}>
              Start
            </Button>
          )}
          {i.upgradeAvailable && (
            <Button size="sm" icon={<Rocket size={13} />} disabled={busy} onClick={() => deploy.mutate({ version: i.latestPublishedVersion ?? undefined, reason: 'upgrade' })}>
              Upgrade to v{i.latestPublishedVersion}
            </Button>
          )}
          {others.length > 0 && (
            <Button size="sm" variant="ghost" icon={<RotateCcw size={13} />} disabled={busy} onClick={() => setRollback(true)}>
              Deploy another version…
            </Button>
          )}
          <span className="grow" />
          <Link to={`/twins/${encodeURIComponent(i.id)}`} className="vts-btn vts-btn--secondary vts-btn--sm">
            <ExternalLink size={13} aria-hidden="true" /> Open in Operate
          </Link>
        </div>
        {(deploy.isError || control.isError) && <ErrorBlock error={deploy.error ?? control.error} compact />}
      </div>
      <Dialog
        open={rollback}
        onOpenChange={setRollback}
        title={`Deploy another version of ${i.name}`}
        description="The running version is stopped and the chosen published version deployed; its package must be usable by this instance."
        footer={
          <>
            <Button onClick={() => setRollback(false)}>Cancel</Button>
            <Button
              variant="primary"
              disabled={target === null}
              loading={deploy.isPending}
              onClick={() =>
                deploy.mutate(
                  { version: target ?? undefined, reason: target !== null && target < (i.blueprintVersion ?? 0) ? 'rollback' : 'upgrade' },
                  { onSuccess: () => setRollback(false) },
                )
              }
            >
              Deploy v{target ?? '…'}
            </Button>
          </>
        }
      >
        <select className="vts-select" value={target ?? ''} onChange={(x) => setTarget(Number(x.target.value))} aria-label="Version">
          <option value="">Choose a published version…</option>
          {others.map((v) => (
            <option key={v.version} value={v.version}>
              v{v.version}
              {v.version < (i.blueprintVersion ?? 0) ? ' (rollback)' : ' (upgrade)'}
              {v.note ? ` — ${v.note}` : ''}
            </option>
          ))}
        </select>
      </Dialog>
    </Pane>
  );
}

export default function DeploymentPage() {
  const e = useEditor();
  const q = useInstances(e.id);
  const sup = useSupervisor();
  return (
    <EdPage
      title="Deployment"
      wide
      description="Where each instance runs, and its runtime processes."
      guide={
        <>
          Deploying starts an instance&apos;s runtime on its version&apos;s released package and bundle, under this Studio&apos;s deployment supervisor (restarted with Studio, stopped cleanly). <strong>DEPLOYED</strong> means it runs;
          conformance of its behaviour is monitored live in Operate.
        </>
      }
    >
      <div className="row-wrap">
        {sup.data ? (
          <StatusBadge tone={sup.data.available ? 'ok' : 'critical'} label={sup.data.available ? 'Deployment supervisor available' : 'Supervisor unavailable'} title={sup.data.binDir} />
        ) : (
          <StatusBadge tone="neutral" label="Supervisor…" />
        )}
        {sup.data && !sup.data.available && <span className="xsmall muted">Start Studio with --bin-dir pointing at the runtime binaries to deploy from here.</span>}
      </div>
      {q.isPending ? (
        <Skeleton lines={4} />
      ) : q.isError ? (
        <ErrorBlock error={q.error} onRetry={() => void q.refetch()} />
      ) : q.data.length === 0 ? (
        <EmptyState title="Nothing to deploy yet" action={<Link to={blueprintRoute(e.id, e.version, 'release/instances')} className="vts-btn vts-btn--primary">Create an instance</Link>}>
          Deploy instances, not Blueprints: create one per real asset first.
        </EmptyState>
      ) : (
        <div className="stack">
          {q.data.map((i) => (
            <InstanceCard key={i.id} i={i} />
          ))}
        </div>
      )}
    </EdPage>
  );
}
