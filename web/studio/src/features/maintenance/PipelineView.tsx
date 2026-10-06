/**
 * Release pipeline of a change. Every stage state is computed by the backend from
 * stored evidence for the exact candidate artefacts (never cached client-side);
 * stages that can be run trigger the authoritative tool.
 */
import { Check, CircleDashed, Loader2, Minus, Play, X } from 'lucide-react';
import type { Pipeline, PipelineStage } from '@/api/types';
import { safeTrustState } from '@/api/schemas';
import { Button, TrustBadge, trustPresentation } from '@/design';
import { EvidenceLink, PackageLink } from '@/features/common/links';

const RUNNABLE: Record<string, string> = {
  validate: 'Validate drafts',
  refinement: 'Check refinement',
  alignment: 'Run alignment',
  compile: 'Compile',
  package: 'Build package',
  verify: 'Verify package',
};

function Dot({ stage }: { stage: PipelineStage }) {
  const p = trustPresentation(safeTrustState(stage.state));
  const Icon = stage.state === 'pass' ? Check : stage.state === 'fail' || stage.state === 'error' ? X : stage.state === 'check_running' ? Loader2 : stage.state === 'not_applicable' ? Minus : CircleDashed;
  return (
    <span className={`vts-step__dot vts-step__dot--${p.tone === 'neutral' ? '' : p.tone}`} aria-hidden="true">
      <Icon size={13} className={stage.state === 'check_running' ? 'vts-spin' : undefined} />
    </span>
  );
}

export function PipelineView({
  pipeline,
  onRun,
  running,
  disabled,
}: {
  pipeline: Pipeline;
  onRun: (stage: string) => void;
  running: string | null;
  disabled?: boolean;
}) {
  return (
    <ol className="vts-steps" aria-label="Release pipeline">
      {pipeline.stages.map((s) => (
        <li key={s.id} className="vts-step">
          <Dot stage={s} />
          <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
            <span className="row-wrap">
              <strong className="small">{s.title}</strong>
              {s.mandatory ? <span className="vts-tag">mandatory</span> : <span className="vts-tag">advisory</span>}
              {s.route === 'theorem3' && <span className="vts-badge vts-badge--formal">via Theorem 3</span>}
            </span>
            <span className="small muted">{s.detail}</span>
            {(s.evidence.length > 0 || s.packageId) && (
              <span className="xsmall subtle row-wrap">
                {s.evidence.map((e) => <EvidenceLink key={e} id={e} kind={s.id === 'refinement' || (s.route === 'theorem3' && e === s.evidence[0]) ? 'refinement' : undefined} />)}
                {s.packageId && <PackageLink id={s.packageId} />}
              </span>
            )}
          </div>
          <div className="stack-sm" style={{ alignItems: 'flex-end' }}>
            <TrustBadge state={safeTrustState(s.state)} />
            {RUNNABLE[s.id] && s.state !== 'not_applicable' && (
              <Button size="sm" icon={<Play size={12} />} disabled={disabled || running !== null} loading={running === s.id} onClick={() => onRun(s.id)}>
                {s.state === 'not_checked' || s.state === 'blocked' ? RUNNABLE[s.id] : 'Re-run'}
              </Button>
            )}
          </div>
        </li>
      ))}
    </ol>
  );
}
