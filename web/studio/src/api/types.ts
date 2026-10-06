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
  /** Blueprint the twin is an instance of (null for twins registered without one). */
  blueprintId?: string | null;
  /** Blueprint version the instance runs. */
  blueprintVersion?: number | null;
  /** Instance configuration (identity, placement, connectivity, target, asset map, channels). */
  instanceConfig?: Record<string, unknown>;
  /** Physical-twin simulator URL (ground truth, visualisation only). */
  worldUrl?: string | null;
  /** What the deployment supervisor reconciles to. */
  desiredState?: 'running' | 'stopped';
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
  /** Match quality (ranked first to last). */
  match?: 'exact' | 'prefix' | 'word' | 'substring';
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

// ------------------------------------------------------------------ blueprints (twin-blueprint/1)
/*
 * A Twin Blueprint is the reusable engineering definition of a type of twin; instances are
 * twins created from a published version. Every status below is computed by the backend
 * from stored evidence for the exact pinned inputs; the UI only renders it.
 */

export type BlueprintVersionState = 'draft' | 'published' | 'deprecated';
export type BlueprintSectionId =
  | 'identity'
  | 'structure'
  | 'world'
  | 'data'
  | 'connectivity'
  | 'presentation'
  | 'behavior'
  | 'assurance'
  | 'simulation'
  | 'scenarios';
export type FormalRole = 'ontology' | 'pt_interpretation' | 'dt_interpretation' | 'pt_model' | 'dt_model';
export type RuntimeMode = 'monitor' | 'cosimulation';

export interface BlueprintMeta {
  id: string;
  name: string;
  description: string;
  domain: string;
  icon: string;
  templateId: string | null;
  clonedFrom: string | null;
  createdAt: string;
  createdBy: string;
}

export interface BlueprintVersionSummary {
  blueprintId: string;
  version: number;
  /** "v2" or "v3-draft". */
  label: string;
  state: BlueprintVersionState;
  /** Optimistic-concurrency revision of a draft (incremented by every save). */
  revision: number;
  parent: number | null;
  documentSha256: string;
  /** role -> "artifact@version". */
  pins: Record<string, string>;
  packageId: string | null;
  bundleId: string | null;
  note: string;
  createdAt: string;
  createdBy: string;
  updatedAt: string;
  updatedBy: string;
  publishedAt: string | null;
}

export interface BlueprintListItem extends BlueprintMeta {
  versions: BlueprintVersionSummary[];
  draft: BlueprintVersionSummary | null;
  published: BlueprintVersionSummary | null;
  latest: BlueprintVersionSummary | null;
  updatedAt: string;
  instanceCount: number;
  draftErrors?: number;
  draftWarnings?: number;
}

export interface BlueprintDetail extends BlueprintMeta {
  versions: BlueprintVersionSummary[];
  instances: InstanceView[];
}

export interface BlueprintIdentity {
  name: string;
  description?: string;
  domain: string;
  icon?: string;
  tags?: string[];
  modelId: string;
  timeUnit: string;
  ticksPerUnit: number;
  runtimeMode: RuntimeMode;
  plugin?: string | null;
}

export interface AssetTypeProperty {
  key: string;
  label?: string;
  type?: string;
  default?: string;
  unit?: string;
}

export interface AssetTypeDef {
  id: string;
  name: string;
  category?: string;
  description?: string;
  properties: AssetTypeProperty[];
}

export interface BlueprintAssetDef {
  id: string;
  name: string;
  type: string;
  /** "instance": every instance gets its own; "context": shared site/estate asset. */
  scope: 'instance' | 'context';
  description?: string;
  properties?: Record<string, string>;
  tags?: string[];
  parent?: string;
}

export interface RelationshipDef {
  id: string;
  source: string;
  type: string;
  target: string;
}

export interface StructureSection {
  root: string;
  assetTypes: AssetTypeDef[];
  assets: BlueprintAssetDef[];
  relationships: RelationshipDef[];
}

export type WorldLayerRole = 'shared' | 'ground-truth' | 'knowledge' | 'event' | 'annotation' | 'background';
export type WorldMode = 'spatial' | 'topology' | 'diagram';
export type WorldObjectKind =
  | 'point'
  | 'label'
  | 'waypoint'
  | 'line'
  | 'polyline'
  | 'polygon'
  | 'region'
  | 'zone'
  | 'rect'
  | 'image'
  | 'node'
  | 'edge'
  | 'connector';

export interface WorldLayer {
  id: string;
  name: string;
  role: WorldLayerRole;
  visible: boolean;
  locked: boolean;
}

/** Integer world units. Rect-like kinds: {x, y, w, h} (top-left); points/labels: {x, y}; nodes: {x, y, w?, h?} (centre); lines: {points, width}; areas: {points} or {x, y, w, h}; links: {from, to, directed}. */
export interface WorldGeometry {
  x?: number;
  y?: number;
  w?: number;
  h?: number;
  points?: number[][];
  width?: number;
  from?: string;
  to?: string;
  directed?: boolean;
}

export interface WorldObject {
  id: string;
  layer: string;
  kind: WorldObjectKind;
  semanticType: string;
  name: string;
  geometry: WorldGeometry;
  properties: Record<string, string | number | boolean>;
  asset?: string | null;
  tags: string[];
}

export interface WorldDocument {
  format: 'twin-world/1';
  mode: WorldMode;
  unit: 'mm' | 'px';
  bounds: { x: number; y: number; w: number; h: number };
  grid?: { size: number; snap: boolean };
  layers: WorldLayer[];
  objects: WorldObject[];
}

export type DataType = 'real' | 'integer' | 'boolean' | 'string' | 'enum';

export interface StaticPropertyDef {
  id: string;
  label: string;
  type: DataType;
  asset?: string;
  perInstance?: boolean;
  value?: string;
}

export interface TelemetryDef {
  id: string;
  label: string;
  type: DataType;
  unit?: string;
  asset?: string;
  description?: string;
  expectedPeriodMs?: number;
  quality?: { maxAgeMs?: number };
  presentation?: { category?: string; precision?: number; chart?: string };
  ontologySymbol?: string;
  range?: { min?: string; max?: string };
  values?: string[];
}

export interface EventDef {
  id: string;
  label: string;
  asset?: string;
  description?: string;
  payload?: { key: string; type: DataType }[];
  /** Formal labels of this event in the PT and DT views ("label!"). */
  formal?: { pt?: string; dt?: string };
}

export interface CommandDef {
  id: string;
  label: string;
  asset?: string;
  description?: string;
  parameters?: { key: string; type: DataType; unit?: string }[];
  acknowledgement?: { event?: string; timeoutMs?: number };
  observedConsequence?: { event?: string; timeoutMs?: number };
}

export interface DataSection {
  properties: StaticPropertyDef[];
  telemetry: TelemetryDef[];
  events: EventDef[];
  commands: CommandDef[];
}

export type SourceKind = 'mqtt' | 'opcua' | 'rest' | 'simulator' | 'replay' | 'file';

export interface DataSourceDef {
  id: string;
  kind: SourceKind;
  name: string;
  config: Record<string, string | number | boolean>;
}

export interface BindingDef {
  id: string;
  target: { kind: 'telemetry' | 'event' | 'command'; id: string };
  source: string;
  select: { field?: string; path?: string; topic?: string; node?: string };
  unit?: { from?: string; scale?: string; offset?: string };
}

export interface ConnectivitySection {
  sources: DataSourceDef[];
  bindings: BindingDef[];
}

export interface StatePresentation {
  label?: string;
  tone?: Tone;
  summary?: string;
}

export interface PresentationSection {
  displayName: string;
  icon?: string;
  primaryView: string;
  plugin?: string | null;
  keyTelemetry: string[];
  importantAssets: string[];
  importantPropositions: string[];
  importantMonitors: string[];
  importantPredictions: string[];
  states: Record<string, StatePresentation>;
  events: Record<string, { label?: string }>;
  charts: { title: string; telemetry: string[] }[];
  units?: Record<string, string>;
  precision?: Record<string, number>;
}

export type RequirementCategory = 'safety' | 'mission' | 'performance' | 'timing' | 'operational';
export type MonitorKind = 'conformance' | 'property' | 'data_quality' | 'semantic';

export interface RequirementDef {
  id: string;
  title: string;
  category: RequirementCategory;
  severity: 'info' | 'warning' | 'critical';
  description?: string;
  monitors: string[];
  formal?: string;
}

export interface MonitorDef {
  id: string;
  kind: MonitorKind;
  name: string;
  severity: 'info' | 'warning' | 'critical';
  requirement?: string;
  property?: string;
  /** "all" or a list of Physical System View labels ("start!"). */
  events?: string | string[];
  unmatchedEvents?: 'record' | 'reject';
  field?: string;
  check?: string;
  maxAgeSeconds?: number;
  maxSilenceSeconds?: number;
  /** Integer or decimal string (canonical documents hold no floats). */
  min?: number | string;
  max?: number | string;
  condition?: string;
}

export interface AlertDef {
  id: string;
  monitor: string;
  on: 'violated' | 'finding' | 'inconclusive';
  severity: 'info' | 'warning' | 'critical';
  message: string;
}

export interface AssuranceSection {
  format?: string;
  requirements: RequirementDef[];
  monitors: MonitorDef[];
  alerts: AlertDef[];
}

export interface SimulationSection {
  kind: 'none' | 'mobile-robot' | 'event-script';
  [key: string]: unknown;
}

export type ScenarioStepKind = 'event' | 'delay' | 'observe' | 'world' | 'expect';

export interface ScenarioStep {
  id: string;
  kind: ScenarioStepKind;
  /** Logical time of the step (decimal string in model time units). */
  at?: string;
  delay?: string;
  label?: string;
  level?: 'pt' | 'dt';
  transition?: string;
  telemetry?: Record<string, string>;
  change?: Record<string, unknown>;
  observation?: Record<string, unknown>;
  expect?: Record<string, unknown>;
  expectRefused?: boolean;
  note?: string;
}

export interface ScenarioDef {
  id: string;
  name: string;
  description?: string;
  start: { kind: 'initial' | 'configurations'; configurations?: unknown[] };
  steps: ScenarioStep[];
}

export interface BlueprintDocument {
  format: 'twin-blueprint/1';
  identity: BlueprintIdentity;
  structure: StructureSection;
  world: WorldDocument;
  data: DataSection;
  connectivity: ConnectivitySection;
  presentation: PresentationSection;
  behavior: { pt?: { layout?: TaLayout | null }; dt?: { layout?: TaLayout | null } };
  assurance: AssuranceSection;
  simulation: SimulationSection;
  scenarios: ScenarioDef[];
}

export interface BlueprintVersionDetail extends BlueprintVersionSummary {
  document: BlueprintDocument;
  editable: boolean;
  /** Pinned formal artefact versions by role. */
  artifacts: Partial<Record<FormalRole, ArtifactVersion>>;
  blueprint: BlueprintMeta;
}

export interface SectionFinding {
  section: string;
  severity: 'error' | 'warning';
  code: string;
  message: string;
  target: string;
  path: string;
}

export interface SectionSaveResult {
  revision: number;
  documentSha256: string;
  updatedAt: string;
  section: string;
  findings: SectionFinding[];
}

export interface BlueprintValidation {
  valid: boolean;
  errors: number;
  warnings: number;
  sections: Record<string, { errors: number; warnings: number }>;
  findings: SectionFinding[];
}

export type SectionState = 'complete' | 'warnings' | 'errors' | 'empty';

export interface BlueprintSectionStatus {
  id: string;
  title: string;
  state: SectionState;
  summary: string;
  route: string;
  errors: number;
  warnings: number;
  detail: string;
}

export type GateState = 'pass' | 'fail' | 'not_run' | 'blocked' | 'error' | 'not_applicable';

export interface GateItem {
  id: string;
  title: string;
  state: GateState;
  detail: string;
  blocking: boolean;
  /** Blueprint-relative route that fixes the item. */
  fix: string;
  evidenceId: string | null;
}

export interface BlueprintStatus {
  version: BlueprintVersionSummary;
  sections: BlueprintSectionStatus[];
  completeness: {
    complete: number;
    total: number;
    next: { section: string; title: string; state: SectionState; route: string; detail: string }[];
  };
  readiness: { verdict: 'ready' | 'blocked'; readyToPackage: boolean; items: GateItem[]; blockers: string[] };
  alignment: { state: string; evidenceId: string | null; at?: string };
  findings: SectionFinding[];
}

// ---- canonical timed automata (twin-ta/1) and their layout (twin-ta-layout/1)
export interface TaAtom {
  clock: string;
  op: '<' | '<=' | '==' | '>=' | '>';
  bound: number | string;
  minus?: string;
}

export interface TaLocation {
  name: string;
  initial: boolean;
  invariant: TaAtom[];
  note: string;
}

export interface TaEdge {
  id: string;
  source: string;
  target: string;
  sync: { channel: string; direction: '!' | '?' } | null;
  guard: TaAtom[];
  resets: string[];
  note: string;
}

export interface TaModel {
  format: 'twin-ta/1';
  name: string;
  note: string;
  clocks: { name: string; note: string }[];
  constants: { name: string; value: number; note: string }[];
  channels: { name: string; note: string }[];
  locations: TaLocation[];
  edges: TaEdge[];
}

export interface TaPoint {
  x: number;
  y: number;
}

/** Diagram layout (twin-ta-layout/1): presentation only, integer coordinates, y downwards. */
export interface TaLayout {
  format?: 'twin-ta-layout/1';
  locations: Record<string, { x: number; y: number; label?: TaPoint }>;
  edges?: Record<string, { nails: TaPoint[]; label?: TaPoint }>;
}

export interface TaDiagnostic {
  severity: 'error' | 'warning' | 'note';
  code: string;
  message: string;
  hint?: string;
  /** What it concerns: kind "location" | "edge" | "clock" | "channel" | "constant" | "model" | "document", by name. */
  element?: { kind: string; name: string; part: string };
  range?: { line: number; column: number; endLine: number; endColumn: number };
}

export interface BlueprintModelView {
  role: 'pt_model' | 'dt_model';
  editable: boolean;
  revision: number;
  artifact: ArtifactVersion | null;
  model: TaModel | null;
  layout: TaLayout | null;
  diagnostics: TaDiagnostic[];
  semanticDigest: string | null;
  validation?: EvidenceRecord | null;
  contentFormat?: string;
}

export interface ModelSaveResult {
  revision: number;
  artifact: ArtifactVersion;
  diagnostics: TaDiagnostic[];
  semanticDigest: string | null;
}

export interface ModelImportResult extends Partial<ModelSaveResult> {
  imported: boolean;
  report: { format: string; diagnostics: TaDiagnostic[]; provenance?: Record<string, unknown>; layout?: unknown };
}

export interface SemanticsView {
  role: 'ontology' | 'pt_interpretation' | 'dt_interpretation';
  editable: boolean;
  revision: number;
  artifact: VersionDetail | null;
  pinned: string | null;
  coverage?: { key: string; kind: 'location' | 'event'; mapped: boolean }[];
  ontologyPinned?: string | null;
  validatedAgainstPinnedOntology?: boolean;
}

export interface CheckResult {
  check?: string;
  outcome?: Outcome;
  summary?: string;
  verdict?: string;
  id?: string;
  document?: Record<string, unknown>;
  results?: unknown[];
  [key: string]: unknown;
}

// ---- timing windows (kernel what-if)
export interface TimeValue {
  text: string;
  ticks: number;
}

export interface TimingInterval {
  earliest: TimeValue;
  latest: TimeValue | null;
  earliest_at: TimeValue;
  latest_at: TimeValue | null;
}

export interface TimingAlternative {
  enabled_now: boolean;
  guard: string;
  location: string;
  source: string;
  target: string;
  transition: string;
  resets: string[];
  window: TimingInterval | null;
  factors?: {
    atom: string;
    origin: string;
    never: boolean;
    depends_on_delay: boolean;
    min_delay: TimeValue | null;
    max_delay: TimeValue | null;
    value_now?: TimeValue;
  }[];
  [key: string]: unknown;
}

export interface Availability {
  label: string;
  /** "now": enabled at the current time; "later": after a delay; "blocked": never from here. */
  status: 'now' | 'later' | 'blocked';
  intervals: TimingInterval[];
  alternatives: TimingAlternative[];
}

export interface AvailabilityView {
  state: { time: TimeValue; configurations: { location: string; clocks: Record<string, TimeValue> }[] };
  propositions: { id: string; [key: string]: unknown }[];
  max_delay: TimeValue | null;
  max_delay_at: TimeValue | null;
  invariants: { location: string; invariant: string }[];
  availability: Availability[];
  unavailable: { label: string; from_locations: string[] }[];
}

export interface TimingStepResult {
  index: number;
  kind: 'event' | 'delay';
  status: 'ok' | 'invalid' | 'not_evaluated';
  requested?: { label?: string; transition?: string | null; delay?: TimeValue; at?: TimeValue };
  after?: AvailabilityView['state'];
  branches?: { transition: string; label: string; source: string; target: string }[];
  error?: { code: string; message: string };
  explanation?: Record<string, unknown>;
}

export interface TimingResult {
  start: { kind: string; state: AvailabilityView['state'] };
  steps: TimingStepResult[];
  first_invalid: number | null;
  final: AvailabilityView;
  irSha256: string;
  timeUnit: string;
  authority: string;
  note?: string;
}

export interface ScenarioStepResult {
  index: number;
  id: string;
  kind: string;
  generated: boolean;
  status: string;
  detail?: string;
  translated?: { pt: string; dt: string };
  expected?: Record<string, unknown>;
  actual?: string;
  at?: string;
  error?: { code: string; message: string };
  explanation?: Record<string, unknown>;
  after?: AvailabilityView['state'];
  telemetry?: Record<string, string>;
}

export interface ScenarioResult {
  id: string;
  name: string;
  outcome: 'pass' | 'fail' | 'error';
  passed: number;
  failed: number;
  refused: boolean;
  firstFailure: string;
  final: AvailabilityView['state'];
  steps: ScenarioStepResult[];
}

export interface ScenarioRun {
  outcome: 'pass' | 'fail';
  kind: 'tests';
  passed: number;
  failed: number;
  durationMs: number;
  evidenceId: string | null;
  results: ScenarioResult[];
}

export interface BlueprintImpact {
  against: number | null;
  version?: number;
  sections: { section: string; classification: 'formal' | 'deployment' | 'presentation' | 'verified-core' | 'tests'; consequences: string[]; from?: string; to?: string }[];
  formal?: { alignmentStale: boolean; irStale: boolean; packageStale: boolean; bundleStale: boolean };
  instances?: { id: string; name: string; version: number | null }[];
  summary: string;
}

export interface BundleView {
  id: string;
  blueprintId: string;
  version: number;
  packageId: string;
  bundleHash: string;
  createdAt: string;
  createdBy: string;
  intact?: boolean;
  filesIntact?: boolean;
  manifest?: {
    format: string;
    files: { path: string; role: string; sha256: string; size: number; verification: string }[];
    verificationScope: { formallyVerified: string[]; integrityOnly: string[]; note: string };
    [key: string]: unknown;
  };
}

export interface BlueprintPackageView {
  version: BlueprintVersionSummary;
  package: PackageDetail | null;
  bundle: BundleView | null;
  verifiedCoreInputs: { role: string; ref: string | null }[];
  deploymentContent: { section: string; sha256: string | null }[];
  verificationScope: { formallyVerified: string[]; integrityOnly: string[] };
  integrity?: PackageIntegrity;
}

export interface SupervisedProcess {
  name: string;
  pid: number;
  port: number;
  url: string;
  log: string;
  state: string;
  exitStatus: number;
  startedAt: string;
}

export interface SupervisorStatus {
  instance: string;
  kind?: 'instance' | 'preview';
  state: 'not_started' | 'starting' | 'running' | 'stopped' | 'failed';
  message?: string;
  startedAt?: string;
  runtimeUrl?: string | null;
  worldUrl?: string | null;
  processes: SupervisedProcess[];
}

export interface InstanceView extends TwinSummary {
  /** Process state from the deployment supervisor. */
  runtime: SupervisorStatus;
  latestPublishedVersion: number | null;
  /** A newer published version of the Blueprint exists. */
  upgradeAvailable: boolean;
}

export interface PreviewView {
  previewId: string;
  banner: 'STUDIO PREVIEW';
  mode?: RuntimeMode;
  simulator?: string;
  core?: Record<string, unknown>;
  runtime: SupervisorStatus;
  isolation?: string;
}

export interface BlueprintTemplate {
  id: string;
  name: string;
  description: string;
  domain: string;
  icon: string;
  order: number;
  includes: string[];
}

export interface PaletteTool {
  id: string;
  label: string;
  kind: WorldObjectKind;
  semanticType: string;
  icon?: string;
  layerRole?: WorldLayerRole;
  defaults?: { width?: number; properties?: Record<string, string | number | boolean>; w?: number; h?: number };
  description?: string;
}

export interface Palettes {
  format: string;
  palettes: Record<string, { label: string; tools: PaletteTool[] }>;
  domains: Record<string, string[]>;
}

export interface BindingTestResult {
  source: string;
  kind: SourceKind;
  /** ok | adapter_unavailable | unreachable | bad_payload | not_found | not_configured | type_mismatch */
  status: string;
  detail?: string;
  latencyMs?: number;
  httpStatus?: number;
  raw?: unknown;
  value?: unknown;
  availableFields?: string[];
  canonical?: unknown;
  typeCheck?: string;
  unit?: { source?: string; canonical?: string; scale?: string; offset?: string };
  timestamp?: string;
  quality?: string;
  samples?: unknown[];
}

export interface WorldRaster {
  cellMm: number;
  width: number;
  height: number;
  /** Cell code -> meaning ("#" wall, "." free, ...). */
  legend: Record<string, string>;
  groundTruth: string[];
  knowledge: string[];
  findings: { severity: string; code: string; message: string; object?: string; path?: string }[];
}
