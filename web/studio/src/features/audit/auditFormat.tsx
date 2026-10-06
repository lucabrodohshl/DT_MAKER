/** Human-readable rendering of engineering-audit records (operation codes come from the backend). */
import { Link } from 'react-router-dom';
import type { AuditRecord } from '@/api/types';
import { StatusBadge, humanize } from '@/design';
import { genericVersionRoute } from '@/features/common/links';

const OPERATION_TEXT: Record<string, string> = {
  'artifact.create': 'Artefact imported',
  'artifact.draft.create': 'Draft created',
  'artifact.draft.save': 'Draft saved',
  'artifact.validate': 'Validation run',
  'artifact.publish': 'Version published',
  'artifact.reject': 'Version rejected',
  'refinement.run': 'Refinement check run',
  'alignment.run': 'Semantic alignment run',
  'model.compile': 'DT view compiled',
  'package.build': 'Package built',
  'package.verify': 'Package verified',
  'package.release': 'Package released',
  'change.create': 'Change opened',
  'change.release': 'Change released',
  'change.abandon': 'Change abandoned',
  'deployment.deploy': 'Deployment',
  'deployment.rollback': 'Rollback',
};

export function operationText(op: string): string {
  return OPERATION_TEXT[op] ?? humanize(op.replace(/\./g, ' '));
}

export function outcomeTone(outcome: string): 'ok' | 'critical' | 'warning' | 'neutral' {
  if (outcome === 'success' || outcome === 'pass') return 'ok';
  if (outcome === 'fail' || outcome === 'error' || outcome === 'failure') return 'critical';
  if (outcome === 'unknown') return 'warning';
  return 'neutral';
}

/** Route for the subject of an audit record, when it names an entity. */
export function subjectRoute(subject: string): string | null {
  if (/^CHG-\d+$/.test(subject)) return `/studio/changes/${subject}`;
  if (/^PKG-\d+$/.test(subject)) return `/studio/packages/${subject}`;
  if (/^[a-z0-9._-]+@\d+$/.test(subject)) return genericVersionRoute(subject);
  return null;
}

export function AuditOperation({ record }: { record: AuditRecord }) {
  const route = subjectRoute(record.subject);
  return (
    <span className="row-wrap small" style={{ minWidth: 0 }}>
      <span className="strong">{operationText(record.operation)}</span>
      {route ? (
        <Link to={route} className="mono">
          {record.subject}
        </Link>
      ) : (
        <span className="mono">{record.subject}</span>
      )}
      <StatusBadge tone={outcomeTone(record.outcome)} label={humanize(record.outcome)} />
      <span className="xsmall subtle">by {record.actor}</span>
    </span>
  );
}
