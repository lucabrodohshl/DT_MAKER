/**
 * Types of the Studio API (`/api/v1`, see api/studio.openapi.yaml).
 *
 * These describe *backend conclusions*. The UI never derives any of these values;
 * it only renders them. Trust-critical payloads are additionally validated at
 * runtime with Zod (schemas.ts) so a malformed response can never render as "pass".
 */

/** Normalised trust/evidence states shared by every trust badge. */
export type TrustState =
  | 'pass'
  | 'fail'
  | 'unknown'
  | 'not_checked'
  | 'stale'
  | 'invalidated'
  | 'check_running'
  | 'unavailable'
  | 'error'
  | 'blocked'
  | 'not_applicable';

export type ArtifactKind = 'ontology' | 'interpretation' | 'pt_model' | 'dt_model';
export type Lifecycle = 'draft' | 'validating' | 'verified' | 'published' | 'superseded' | 'rejected';
export type Outcome = 'pass' | 'fail' | 'unknown' | 'error';
export type EvidenceKind = 'validation' | 'refinement' | 'alignment' | 'compilation' | 'package';

export interface ApiErrorBody {
  error: { code: string; message: string; context: { key: string; value: string }[] };
}

export interface About {
  product: string;
  version: string;
  compilerVersion: string;
  kernelVersion: string;
  kernelCompat: string;
  irFormat: string;
  packageFormat: string;
  ledgerSchema: string;
  aligner: string;
  alignerDigest: string;
  ontologyServices: string;
  ontologyChecker: string;
  epoch: string;
}

// ------------------------------------------------------------------ assets & graph
export interface Asset {
  id: string;
  name: string;
  type: string;
  parentId: string | null;
  description: string;
  tags: string[];
  properties: Record<string, unknown>;
  twinId: string | null;
  childCount?: number;
}

export interface Relationship {
  id: number;
  sourceId: string;
  type: string;
  targetId: string;
  properties: Record<string, unknown>;
  direction?: 'incoming' | 'outgoing';
  other?: { id: string; name: string; type: string } | null;
}

export interface AssetDetail extends Asset {
  ancestors: { id: string; name: string; type: string }[];
  children: { id: string; name: string; type: string; twinId: string | null }[];
  relationships: Relationship[];
  twin: TwinDetail | null;
  twinInherited: boolean;
  twinAssetId?: string;
}

export interface Page<T> {
  items: T[];
  total: number;
  limit: number;
  offset: number;
}

export interface Neighborhood {
  focus: string;
  nodes: Asset[];
  edges: Relationship[];
  frontier: string[];
  truncated: boolean;
}

export interface GraphFacets {
  relationshipTypes: { type: string; count: number | null; hierarchy: boolean }[];
  assetTypes: { type: string; count: number }[];
  roots: { id: string; name: string; type: string }[];
}

// ------------------------------------------------------------------ telemetry
export type Freshness = 'fresh' | 'stale' | 'missing' | 'invalid';
export type Quality = 'good' | 'uncertain' | 'bad';

export interface Presentation {
  label?: string;
  category?: string;
  precision?: number;
  preferredView?: 'line' | 'state' | 'table';
  [key: string]: unknown;
}

export interface TelemetrySample {
  observedAt: string;
  observedMs: number;
  ingestedAt: string;
  quality: Quality;
  value: number | string | null;
}

export interface TelemetryChannel {
  id: string;
  assetId: string;
  name: string;
  valueType: 'number' | 'boolean' | 'category' | 'string';
  unit: string;
  ontologySymbol: string | null;
  source: string;
  expectedPeriodMs: number;
  presentation: Presentation;
  latest?: TelemetrySample | null;
  freshness?: Freshness;
  ageMs?: number | null;
}

export interface TelemetryChannels {
  assetId: string;
  channels: TelemetryChannel[];
  now: string;
}

export interface TelemetryBucket {
  startMs: number;
  endMs: number;
  count: number;
  min: number;
  max: number;
  avg: number;
  bad: number;
  uncertain: number;
}

export interface TelemetrySeries {
  channelId: string;
  channel: TelemetryChannel;
  from: string;
  to: string;
  totalSamples: number;
  downsampled: boolean;
  bucketMs?: number;
  samples?: TelemetrySample[];
  buckets?: TelemetryBucket[];
}

// ------------------------------------------------------------------ artifacts
export interface ArtifactVersion {
  artifactId: string;
  kind: ArtifactKind;
  version: number;
  ref: string;
  state: Lifecycle;
  contentSha256: string;
  refs: Record<string, string>;
  changeDescription: string;
  createdAt: string;
  createdBy: string;
  updatedAt: string;
  parentVersion: number | null;
  publishedAt: string | null;
  publishedBy: string | null;
  name?: string;
}

export interface ArtifactSummary {
  id: string;
  kind: ArtifactKind;
  name: string;
  description: string;
  createdAt: string;
  createdBy: string;
  versionCount: number;
  latest: ArtifactVersion | null;
  published: ArtifactVersion | null;
  open: ArtifactVersion | null;
}

export interface ArtifactDetail {
  id: string;
  kind: ArtifactKind;
  name: string;
  description: string;
  createdAt: string;
  createdBy: string;
  versions: ArtifactVersion[];
  interpretations: { ref: string; name: string; state: Lifecycle; ontologyRef: string }[];
  deployedIn: { twinId: string; twinName: string; role: string; ref: string; packageId: string; deploymentId: string }[];
}

export interface Span {
  line: number;
  column: number;
  length: number;
}

export interface SourceDiagnostic {
  code: string;
  severity: 'error' | 'warning' | 'note';
  message: string;
  span: Span;
}

export interface OntologyStructure {
  headerComment: string;
  sorts: { name: string; comment: string; span: Span }[];
  functions: { name: string; argSorts: string[]; returnSort: string; signature: string; comment: string; span: Span }[];
  relations: { name: string; argSorts: string[]; signature: string; comment: string; span: Span }[];
  axioms: { id: string; formula: string; comment: string; symbols: string[]; span: Span; formulaSpan: Span }[];
}

export interface InterpretationStructure {
  headerComment: string;
  entries: {
    key: string;
    isEvent: boolean;
    formula: string;
    comment: string;
    symbols: string[];
    span: Span;
    formulaSpan: Span;
  }[];
}

export interface EvidenceInput {
  role: string;
  ref: string;
  artifactId: string;
  version: number;
  sha256: string;
}

export interface EvidenceRecord {
  id: string;
  kind: EvidenceKind;
  outcome: Outcome;
  verdict: string;
  summary: string;
  checker: string;
  evidenceSha256: string;
  createdAt: string;
  createdBy: string;
  changeId: string | null;
  inputs: EvidenceInput[];
  document?: Record<string, unknown>;
  documentIntegrity?: 'pass' | 'fail' | 'unknown';
}

export interface VersionDetail extends ArtifactVersion {
  content: string;
  structure: OntologyStructure | InterpretationStructure | null;
  diagnostics: SourceDiagnostic[];
  validation: EvidenceRecord | null;
  validationRunning: boolean;
  ontologyRef?: string;
  parentSummary: ArtifactVersion | null;
}

export interface StructuralChange {
  kind: 'added' | 'removed' | 'modified' | 'renamed';
  element: string;
  name: string;
  previousName: string;
  before: string;
  after: string;
}

export interface DiffResult {
  kind: ArtifactKind;
  from: ArtifactVersion & { content: string };
  to: ArtifactVersion & { content: string };
  structural: { changes: StructuralChange[]; affectedSymbols?: string[] } | null;
  affectedInterpretations: { ref: string; name: string; state: Lifecycle; entries: string[] }[];
}

export interface SymbolInfo {
  ontologyRef: string;
  declaration: { kind: 'sort' | 'function' | 'relation'; name: string; signature: string; comment: string; span: Span };
  axioms: { id: string; formula: string; span: Span }[];
  interpretations: { ref: string; name: string; state: Lifecycle; entries: { key: string; isEvent: boolean; formula: string }[] }[];
  telemetryChannels: TelemetryChannel[];
}

export type Truth = 'true' | 'false' | 'unknown' | 'inconsistent_observation';

export interface EvaluationResult {
  interpretationRef: string;
  ontologyRef: string;
  observationsConsistent: 'true' | 'false' | 'unknown';
  entries: {
    key: string;
    isEvent: boolean;
    formula: string;
    truth: Truth;
    symbols: string[];
    unobserved: string[];
    solverNote: string;
  }[];
  observations: {
    symbol: string;
    value: string;
    unit?: string;
    channelId?: string;
    assetId?: string;
    observedAt?: string;
    ingestedAt?: string;
    quality?: Quality;
    freshness?: Freshness;
    source?: string;
  }[];
  warnings: string[];
  evaluatedAt: string;
  checker: string;
  ontologySha256: string;
  interpretationSha256: string;
}

// ------------------------------------------------------------------ refinement
export type ConditionStatus = 'holds' | 'violated' | 'unknown' | 'not_evaluated';
export type RefinementVerdict = 'valid_refinement' | 'not_a_refinement' | 'unknown' | 'check_failed';

export interface RefinementDocument {
  format: string;
  definition: string;
  verdict: RefinementVerdict;
  summary: string;
  conditions: { condition: string; title: string; status: ConditionStatus; details: string[] }[];
  obligations: {
    condition: string;
    subject: string;
    statement: string;
    status: ConditionStatus;
    counterModel: { symbol: string; value: string }[];
    note: string;
  }[];
  assumptions: string[];
  failureReasons: string[];
  checker: string;
  timeoutMs: number;
  hashes: Record<string, string>;
  base: { ontology: string; ptInterpretation: string | null; dtInterpretation: string | null };
  candidate: { ontology: string; ptInterpretation: string | null; dtInterpretation: string | null };
}

// ------------------------------------------------------------------ twins & release
export interface Binding {
  role: string;
  ref: string;
  artifactId: string;
  version: number;
  sha256: string;
}

export interface TrustItem {
  state: TrustState;
  verdict?: string;
  detail: string;
  evidenceId?: string;
  at?: string;
}

export interface Deployment {
  seq: number;
  id: string;
  twinId: string;
  packageId: string;
  previousPackageId: string | null;
  kind: 'deploy' | 'rollback';
  reason: string;
  deployedAt: string;
  deployedBy: string;
  current?: boolean;
  artifacts?: Record<string, string>;
  packageHash?: string;
  irSha256?: string;
}

export interface PackageRecord {
  id: string;
  twinId: string;
  packageHash: string;
  irSha256: string;
  modelVersion: string;
  bindings: Binding[];
  evidenceId: string | null;
  state: 'built' | 'released';
  createdAt: string;
  createdBy: string;
  releasedAt: string | null;
  changeId: string | null;
}

export interface PackageIntegrity {
  packageId: string;
  packageHash: string;
  integrity: 'pass' | 'fail';
  firstFailure: string;
  checks: { name: string; passed: boolean; detail: string }[];
  verifiedAt: string;
  verifier: string;
}

export interface PackageDetail extends PackageRecord {
  bindings: (Binding & { versionInfo?: ArtifactVersion })[];
  buildEvidence?: EvidenceRecord | null;
  integrity: PackageIntegrity | null;
  deployments: Deployment[];
}

export interface TwinPresentation {
  timeUnit?: string;
  states?: Record<string, { label?: string; tone?: Tone; summary?: string }>;
  events?: Record<string, { label?: string }>;
  keyTelemetry?: string[];
  plugin?: string;
  [key: string]: unknown;
}

export interface TwinSummary {
  id: string;
  name: string;
  assetId: string | null;
  description: string;
  modelId: string;
  ticksPerUnit: number;
  runtimeUrl: string | null;
  presentation: TwinPresentation;
  createdAt: string;
  deployment: Deployment | null;
}

export interface TwinDetail extends TwinSummary {
  package: PackageRecord | null;
  bindings: (Binding & { versionInfo: ArtifactVersion | null })[];
  trust: Record<'alignment' | 'ontologyRefinement' | 'packageIntegrity' | 'runtimeCompatibility' | 'compilation', TrustItem>;
  runtimeConnected?: boolean;
}

export interface Change {
  id: string;
  twinId: string;
  title: string;
  description: string;
  state: 'open' | 'released' | 'abandoned';
  artifacts: string[];
  createdAt: string;
  createdBy: string;
  closedAt: string | null;
  artifactVersions?: (ArtifactVersion & { deployedRef: string | null; role?: string })[];
  twin?: TwinSummary | null;
}

export interface PipelineStage {
  id: string;
  title: string;
  state: TrustState;
  mandatory: boolean;
  detail: string;
  evidence: string[];
  route?: 'alignment' | 'theorem3';
  packageId?: string | null;
}

export interface Pipeline {
  changeId: string;
  changeState?: Change['state'];
  stages: PipelineStage[];
  releaseReady: boolean;
  blocking?: string[];
  candidateBindings?: Binding[];
  deployedBindings?: Binding[];
  packageId?: string | null;
  computedAt: string;
}

export type ImpactClassification =
  | 'changed'
  | 'definitely_stale'
  | 'requires_verification'
  | 'preserved'
  | 'potentially_affected'
  | 'unaffected';

export interface ImpactReport {
  subject: ArtifactVersion;
  twins: { twinId: string; name: string; deployedRef: string; role: string; identicalToDeployed: boolean; structural: unknown }[];
  nodes: {
    id: string;
    type: string;
    label: string;
    classification: ImpactClassification;
    reason: string;
    ref?: string;
    evidenceId?: string | null;
    refinementEvidenceId?: string;
    entries?: string[];
    packageId?: string;
    deploymentId?: string;
    twinId?: string;
  }[];
  edges: { from: string; to: string; label: string }[];
  requiredActions: { stage: string; action: string }[];
  analyzedAt: string;
  legend: Record<ImpactClassification, string>;
}

export interface ApplicabilityReport {
  state: 'valid' | 'stale' | 'invalidated' | 'unknown';
  changed: { role: string; evidenceRef: string; evidenceSha256: string; currentRef: string; currentSha256: string }[];
  reasons: string[];
  evidenceId: string;
  context: string;
  evaluatedAt: string;
}

export interface RollbackPreview {
  current: PackageRecord;
  target: PackageRecord;
  roles: { role: string; current: string | null; target: string | null; same: boolean }[];
  targetIntegrity: PackageIntegrity | null;
  targetEvidence: EvidenceRecord | null;
  allowed: boolean;
}

// ------------------------------------------------------------------ audit, logs, search, overview
export interface AuditRecord {
  seq: number;
  at: string;
  actor: string;
  operation: string;
  outcome: string;
  subject: string;
  details: Record<string, unknown>;
  prevHash: string;
  hash: string;
}

export interface AuditVerification {
  valid: boolean;
  records: number;
  firstInvalidSeq: number | null;
  reason: string;
  headHash: string;
  verifiedAt: string;
}

export interface LogEntry {
  ts: string;
  level: 'debug' | 'info' | 'warn' | 'error';
  component: string;
  message: string;
  fields: Record<string, unknown>;
  correlationId: string | null;
  executionId: string | null;
  assetId: string | null;
}

export interface SearchHit {
  kind: string;
  id: string;
  title: string;
  subtitle: string;
  route: string;
}

export type Tone = 'ok' | 'warning' | 'critical' | 'info' | 'neutral' | 'formal';

export interface Overview {
  assets: { total: number; byType: { type: string; count: number }[] };
  telemetry: {
    channels: number;
    freshness: Record<Freshness, number>;
    attention: { channelId: string; assetId: string; label: string; freshness: Freshness; lastObservedAt: string | null }[];
  };
  twins: {
    id: string;
    name: string;
    assetId: string | null;
    deployment: Deployment | null;
    trust: TwinDetail['trust'];
    runtimeConnected: boolean;
  }[];
  runtime: { connectedTwins: number; totalTwins: number; note: string };
  verificationIssues: number;
  engineering: { openChanges: Change[]; openDrafts: number };
  recentEngineeringEvents: AuditRecord[];
  generatedAt: string;
}
