/**
 * The trust summary of a twin: every badge is backed by a named evidence record
 * or a live check performed by the backend, and links to it.
 */
import { HelpCircle } from 'lucide-react';
import { Link } from 'react-router-dom';
import type { TrustItem, TwinDetail } from '@/api/types';
import { safeTrustState } from '@/api/schemas';
import { TimeStamp, TrustBadge } from '@/design';
import { EvidenceLink } from './links';

const ITEMS: { key: keyof TwinDetail['trust']; label: string; help: string }[] = [
  { key: 'alignment', label: 'Semantic alignment', help: 'PT/DT views aligned under the deployed domain knowledge (aligner, Z3).' },
  { key: 'ontologyRefinement', label: 'Ontology refinement', help: 'Deployed ontology version refines its predecessor (Def. 4).' },
  { key: 'compilation', label: 'DT → IR compilation', help: 'Strict compilation with translation validation against the aligner.' },
  { key: 'packageIntegrity', label: 'Package integrity', help: 'Every file hash and binding of the deployed package re-verified now.' },
  { key: 'runtimeCompatibility', label: 'Runtime compatibility', help: 'Package kernel compatibility matches this toolchain.' },
];

export function TrustRow({ label, item, help }: { label: string; item: TrustItem | undefined; help?: string }) {
  const state = safeTrustState(item?.state);
  return (
    <li className="row-between" style={{ alignItems: 'flex-start', padding: '8px 0' }}>
      <div className="stack-sm" style={{ gap: 2, minWidth: 0 }}>
        <span className="strong small">{label}</span>
        <span className="xsmall muted">{item?.detail ?? help}</span>
        {(item?.evidenceId || item?.at) && (
          <span className="xsmall subtle row-wrap">
            {item?.evidenceId && (
              <>
                Evidence <EvidenceLink id={item.evidenceId} kind={label.startsWith('Ontology') ? 'refinement' : undefined} />
              </>
            )}
            {item?.at && (
              <>
                · <TimeStamp value={item.at} kind="evidence" showKind />
              </>
            )}
          </span>
        )}
      </div>
      <TrustBadge state={state} />
    </li>
  );
}

export function TrustList({ twin }: { twin: TwinDetail }) {
  return (
    <ul className="vts-list" aria-label={`Trust indicators for ${twin.name}`}>
      {ITEMS.map((i) => (
        <TrustRow key={i.key} label={i.label} item={twin.trust?.[i.key]} help={i.help} />
      ))}
      <li className="xsmall subtle row" style={{ gap: 6 }}>
        <HelpCircle size={12} aria-hidden="true" />
        Badges reflect evidence recorded by the formal tools or checks run just now; none is assumed.{' '}
        <Link to="/studio/verification">Verification history</Link>
      </li>
    </ul>
  );
}
