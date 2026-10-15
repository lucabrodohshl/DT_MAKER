/**
 * The twin modelling language (twin-model/1) as TypeScript types, mirroring
 * include/twin/model/document.hpp. The browser edits this document; every conclusion about it
 * (validity, generated artefacts, alignment) comes from the server's compiler.
 */

export type PropertyKind = 'measured' | 'fixed' | 'per-twin';
export type DataType = 'real' | 'integer' | 'boolean' | 'enum' | 'string';
export type Unexpected = 'nonconformance' | 'ignore';
export type Tone = 'normal' | 'warning' | 'critical';
export type Op = '<' | '<=' | '==' | '>=' | '>';

export interface Property {
  id: string;
  name: string;
  label?: string;
  description?: string;
  kind: PropertyKind;
  type: DataType;
  unit?: string;
  values?: string[];
  value?: string;
  range?: { min?: string; max?: string };
  period?: string;
  sort?: string;
}

export interface ModelEvent {
  id: string;
  name: string;
  label?: string;
  description?: string;
  origin: 'reported' | 'condition';
  when?: string;
  rearm?: string;
  meaning?: string;
  unexpected?: Unexpected;
  uninterpreted?: boolean;
  direction?: '!' | '?';
}

export interface Action {
  id: string;
  name: string;
  label?: string;
  description?: string;
  meaning?: string;
  unexpected?: Unexpected;
  requestable?: boolean;
  direction?: '!' | '?';
}

export interface Part {
  id: string;
  name: string;
  label?: string;
  description?: string;
  type: string;
  external?: boolean;
  values?: Record<string, string>;
}

export interface Relationship {
  id: string;
  type: string;
  from: string;
  to: string;
}

export interface Link {
  id: string;
  from: string;
  to: string;
}

export interface Assumption {
  id: string;
  name: string;
  expr: string;
  description?: string;
}

export interface TimeAtom {
  timer: string;
  op: Op;
  bound: number | string;
  minus?: string;
}

export interface Timer {
  id: string;
  name: string;
  restart?: 'on-state-change' | 'explicit';
  description?: string;
}

export interface Constant {
  id: string;
  name: string;
  value: number;
  description?: string;
}

export interface State {
  id: string;
  name: string;
  label?: string;
  description?: string;
  initial?: boolean;
  meaning?: string;
  limits?: TimeAtom[];
  tone?: Tone;
}

export interface Group {
  id: string;
  name: string;
  label?: string;
  states: string[];
  meaning?: string;
}

export type Trigger =
  | { event: string; timer?: string }
  | { action: string; timer?: string }
  | { timeout: number | string; timer?: string }
  | { internal: true; timer?: string };

export interface Transition {
  id: string;
  from: string[];
  to: string;
  trigger: Trigger;
  window?: TimeAtom[];
  restart?: string[];
  description?: string;
}

export interface Behavior {
  timers?: Timer[];
  constants?: Constant[];
  states: State[];
  groups?: Group[];
  transitions: Transition[];
}

export interface AssetType {
  id: string;
  name: string;
  label?: string;
  description?: string;
  extends?: string;
  values?: Record<string, string>;
  properties?: Property[];
  events?: ModelEvent[];
  actions?: Action[];
  parts?: Part[];
  relationships?: Relationship[];
  links?: Link[];
  assumptions?: Assumption[];
  behavior?: Behavior;
}

export interface RelationshipType {
  id: string;
  name: string;
  label?: string;
  inverse?: string;
  from?: string[];
  to?: string[];
  symmetric?: boolean;
  description?: string;
}

export interface Requirement {
  id: string;
  name: string;
  title?: string;
  kind: 'never' | 'property';
  state?: string;
  property?: string;
  severity: 'info' | 'warning' | 'critical';
  description?: string;
}

/** Non-semantic editor layout: node positions per type canvas and per behaviour. */
export interface ModelLayout {
  structure?: Record<string, Record<string, { x: number; y: number }>>;
  behavior?: Record<string, Record<string, { x: number; y: number }>>;
}

export interface TwinModel {
  format: 'twin-model/1';
  name: string;
  label?: string;
  description?: string;
  root: string;
  time?: { unit: string; ticksPerUnit: number };
  iriBase?: string;
  allowNondeterminism?: boolean;
  types: AssetType[];
  relationshipTypes?: RelationshipType[];
  reference?: unknown;
  requirements?: Requirement[];
  declarations?: string[];
  layout?: ModelLayout;
}

export interface ModelDiagnostic {
  severity: 'error' | 'warning' | 'info';
  code: string;
  message: string;
  hint?: string;
  path: string;
  element?: string;
}

export interface ModelSummary {
  compiler: string;
  digest: string;
  assets: number;
  machines: number;
  locations: number;
  edges: number;
  clocks: number;
  channels: number;
  symbols: number;
  axioms: number;
  schemaTriples: number;
  graphTriples: number;
}

/** GET …/model */
export interface ModelDocumentView {
  editable: boolean;
  revision: number;
  model: TwinModel | null;
  diagnostics?: ModelDiagnostic[];
  digest?: string;
  summary?: ModelSummary;
}

/** PUT …/model */
export interface ModelSaveResult {
  revision: number;
  documentSha256: string;
  diagnostics: ModelDiagnostic[];
  digest: string;
  derived: boolean;
  summary?: ModelSummary;
}

export interface TraceEntry {
  kind: string;
  name: string;
  sources: { id: string; rule: string }[];
}

export interface CanonicalTa {
  format: string;
  name: string;
  clocks: { name: string; note?: string }[];
  constants: { name: string; value: number }[];
  channels: { name: string }[];
  locations: { name: string; initial: boolean; invariant: { clock: string; op: string; bound: number | string; minus?: string }[]; note?: string }[];
  edges: {
    id: string;
    source: string;
    target: string;
    sync: { channel: string; direction: '!' | '?' } | null;
    guard: { clock: string; op: string; bound: number | string; minus?: string }[];
    resets: string[];
    note?: string;
  }[];
}

/** GET …/model/generated */
export interface GeneratedView {
  diagnostics: ModelDiagnostic[];
  digest: string;
  artifacts: {
    automaton: CanonicalTa | null;
    automatonXml: string;
    ontology: string;
    dtInterpretation: string;
    reference?: CanonicalTa;
    referenceXml?: string;
    ptInterpretation?: string;
    schemaTurtle: string;
    graphTurtle: string;
    jsonld: { '@context': Record<string, string>; schema: { '@graph': JsonLdNode[] }; graph: { '@graph': JsonLdNode[] } };
    sections: Record<string, unknown>;
    trace: TraceEntry[];
    summary: ModelSummary;
  } | null;
}

export type JsonLdNode = { '@id': string; '@type'?: string[] } & Record<string, unknown>;

/** POST …/model/lift */
export interface LiftResult {
  diagnostics: ModelDiagnostic[];
  model: TwinModel | null;
  roundTrip?: boolean;
  difference?: string;
  compileDiagnostics?: ModelDiagnostic[];
}
