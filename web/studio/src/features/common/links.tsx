/**
 * Cross-navigation links between entities (asset ↔ telemetry ↔ behaviour ↔
 * ontology ↔ interpretation ↔ evidence ↔ package ↔ audit). Avoiding silos is a
 * product requirement, so every identifier rendered in the UI is a link.
 */
import { ChevronRight } from 'lucide-react';
import { Link, Navigate, useParams } from 'react-router-dom';
import type { ReactNode } from 'react';
import { useArtifact } from '@/api/queries';
import type { ArtifactKind } from '@/api/types';
import { QueryState } from '@/design';

export function artifactSection(kind: ArtifactKind): string {
  return kind === 'ontology' ? 'ontologies' : kind === 'interpretation' ? 'interpretations' : 'models';
}

export function versionRoute(kind: ArtifactKind, ref: string): string {
  const [id, v] = ref.split('@');
  return `/studio/${artifactSection(kind)}/${encodeURIComponent(id!)}/versions/${v}`;
}

/** Route that does not need the artefact kind; resolves it and redirects. */
export function genericVersionRoute(ref: string): string {
  const [id, v] = ref.split('@');
  return `/studio/artifacts/${encodeURIComponent(id!)}${v ? `/versions/${v}` : ''}`;
}

export function ArtifactRedirect() {
  const { artifactId, version } = useParams();
  const q = useArtifact(artifactId);
  return (
    <div className="vts-page">
      <QueryState query={q}>
        {(a) => (
          <Navigate
            replace
            to={`/studio/${artifactSection(a.kind)}/${encodeURIComponent(a.id)}${version ? `/versions/${version}` : ''}`}
          />
        )}
      </QueryState>
    </div>
  );
}

export function RefLink({ refId, kind, children }: { refId: string; kind?: ArtifactKind; children?: ReactNode }) {
  return (
    <Link to={kind ? versionRoute(kind, refId) : genericVersionRoute(refId)} className="mono small">
      {children ?? refId}
    </Link>
  );
}

export function EvidenceLink({ id, kind, children }: { id: string; kind?: string; children?: ReactNode }) {
  const to = kind === 'refinement' ? `/studio/refinement/${id}` : `/studio/verification/${id}`;
  return (
    <Link to={to} className="mono small">
      {children ?? id}
    </Link>
  );
}

export function AssetLink({ id, children }: { id: string; children?: ReactNode }) {
  return <Link to={`/assets/${encodeURIComponent(id)}`}>{children ?? id}</Link>;
}

export function PackageLink({ id }: { id: string }) {
  return (
    <Link to={`/studio/packages/${encodeURIComponent(id)}`} className="mono small">
      {id}
    </Link>
  );
}

export function ChangeLink({ id, children }: { id: string; children?: ReactNode }) {
  return <Link to={`/studio/changes/${encodeURIComponent(id)}`}>{children ?? id}</Link>;
}

/** Breadcrumb trail (context: which asset / twin / version is in scope). */
export function Crumbs({ items }: { items: { label: ReactNode; to?: string }[] }) {
  return (
    <nav aria-label="Breadcrumb">
      <ol className="row-wrap" style={{ listStyle: 'none', margin: 0, padding: 0, gap: 4 }}>
        {items.map((c, i) => (
          <li key={i} className="row" style={{ gap: 4 }}>
            {c.to ? <Link to={c.to}>{c.label}</Link> : <span aria-current="page">{c.label}</span>}
            {i < items.length - 1 && <ChevronRight size={12} aria-hidden="true" />}
          </li>
        ))}
      </ol>
    </nav>
  );
}

/** Link to the product manual served by twin-studio at /docs/ (opens in a new tab). */
export function LearnMore({ page, children = 'Learn more' }: { page: string; children?: ReactNode }) {
  return (
    <a className="small" href={`/docs/${page}`} target="_blank" rel="noopener" title="Open the manual (new tab)">
      {children} ↗
    </a>
  );
}
