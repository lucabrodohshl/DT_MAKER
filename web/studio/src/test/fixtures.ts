/** Test fixtures shaped like real twin-studio responses (used only by tests). */
import type { EvidenceRecord, RefinementDocument, VersionDetail } from '@/api/types';

export const ontologyText = 'sort Temperature\nfun t : Temperature\naxiom a : (> t 0)\n';

export function version(overrides: Partial<VersionDetail> = {}): VersionDetail {
  return {
    artifactId: 'process-pump',
    kind: 'ontology',
    version: 3,
    ref: 'process-pump@3',
    state: 'draft',
    contentSha256: 'a'.repeat(64),
    refs: {},
    changeDescription: 'rev C',
    createdAt: '2026-10-04T10:00:00.000Z',
    createdBy: 'alice',
    updatedAt: '2026-10-04T10:00:00.000Z',
    parentVersion: 1,
    publishedAt: null,
    publishedBy: null,
    name: 'Process pump domain',
    content: ontologyText,
    structure: {
      headerComment: '',
      sorts: [{ name: 'Temperature', comment: '', span: { line: 1, column: 6, length: 11 } }],
      functions: [{ name: 't', argSorts: [], returnSort: 'Temperature', signature: 'Temperature', comment: '', span: { line: 2, column: 5, length: 1 } }],
      relations: [],
      axioms: [{ id: 'a', formula: '(> t 0)', comment: '', symbols: ['t'], span: { line: 3, column: 7, length: 1 }, formulaSpan: { line: 3, column: 11, length: 7 } }],
    },
    diagnostics: [],
    validation: null,
    validationRunning: false,
    parentSummary: null,
    ...overrides,
  };
}

export function refinementEvidence(doc: Partial<RefinementDocument>): EvidenceRecord {
  const document: RefinementDocument = {
    format: 'twin-refinement-evidence/1',
    definition: 'Def. 4',
    verdict: 'valid_refinement',
    summary: 'summary',
    conditions: [
      { condition: 'a', title: 'Signature inclusion', status: 'holds', details: [] },
      { condition: 'b', title: 'Axiom entailment', status: 'holds', details: [] },
      { condition: 'c.P', title: 'PT interpretation preserved', status: 'not_evaluated', details: [] },
      { condition: 'c.D', title: 'DT interpretation preserved', status: 'not_evaluated', details: [] },
    ],
    obligations: [],
    assumptions: ['Definition 4'],
    failureReasons: [],
    checker: 'z3 4.x; SemPTDTAlignmentICSE; twin-ontology/1',
    timeoutMs: 10000,
    hashes: { baseOntology: 'b'.repeat(64), candidateOntology: 'c'.repeat(64) },
    base: { ontology: 'process-pump@1', ptInterpretation: null, dtInterpretation: null },
    candidate: { ontology: 'process-pump@3', ptInterpretation: null, dtInterpretation: null },
    ...doc,
  };
  return {
    id: 'EV-0042',
    kind: 'refinement',
    outcome: document.verdict === 'valid_refinement' ? 'pass' : document.verdict === 'not_a_refinement' ? 'fail' : document.verdict === 'unknown' ? 'unknown' : 'error',
    verdict: document.verdict,
    summary: document.summary,
    checker: document.checker,
    evidenceSha256: 'd'.repeat(64),
    createdAt: '2026-10-04T10:00:00.000Z',
    createdBy: 'alice',
    changeId: null,
    inputs: [],
    document: document as unknown as Record<string, unknown>,
  };
}
