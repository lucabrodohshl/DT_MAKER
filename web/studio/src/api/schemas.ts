/**
 * Runtime validation of trust-critical payloads.
 *
 * A trust badge may only show PASS when the backend says so in a well-formed
 * response. If a payload does not match (schema drift, proxy error page, partial
 * response), the parse fails and the UI shows UNKNOWN with the parse problem,
 * never a default "pass".
 */
import { z } from 'zod';
import type { TrustState } from './types';

export const trustStateSchema = z.enum([
  'pass',
  'fail',
  'unknown',
  'not_checked',
  'stale',
  'invalidated',
  'check_running',
  'unavailable',
  'error',
  'blocked',
  'not_applicable',
]);

export const trustItemSchema = z.object({
  state: trustStateSchema,
  verdict: z.string().optional(),
  detail: z.string(),
  evidenceId: z.string().optional(),
  at: z.string().optional(),
});

export const twinTrustSchema = z.object({
  alignment: trustItemSchema,
  ontologyRefinement: trustItemSchema,
  packageIntegrity: trustItemSchema,
  runtimeCompatibility: trustItemSchema,
  compilation: trustItemSchema,
});

export const refinementDocumentSchema = z.object({
  format: z.literal('twin-refinement-evidence/1'),
  verdict: z.enum(['valid_refinement', 'not_a_refinement', 'unknown', 'check_failed']),
  summary: z.string(),
  conditions: z.array(
    z.object({
      condition: z.string(),
      title: z.string(),
      status: z.enum(['holds', 'violated', 'unknown', 'not_evaluated']),
      details: z.array(z.string()),
    }),
  ),
  obligations: z.array(
    z.object({
      condition: z.string(),
      subject: z.string(),
      statement: z.string(),
      status: z.enum(['holds', 'violated', 'unknown', 'not_evaluated']),
      counterModel: z.array(z.object({ symbol: z.string(), value: z.string() })),
      note: z.string(),
    }),
  ),
  assumptions: z.array(z.string()),
  failureReasons: z.array(z.string()),
  checker: z.string(),
  hashes: z.record(z.string(), z.string()),
});

export const pipelineSchema = z.object({
  changeId: z.string(),
  releaseReady: z.boolean(),
  stages: z.array(
    z.object({
      id: z.string(),
      title: z.string(),
      state: trustStateSchema,
      mandatory: z.boolean(),
      detail: z.string(),
      evidence: z.array(z.string()),
    }).passthrough(),
  ),
}).passthrough();

export const auditVerificationSchema = z.object({
  valid: z.boolean(),
  records: z.number(),
  firstInvalidSeq: z.number().nullable(),
  reason: z.string(),
  headHash: z.string(),
  verifiedAt: z.string(),
});

export const packageIntegritySchema = z.object({
  packageId: z.string(),
  integrity: z.enum(['pass', 'fail']),
  firstFailure: z.string(),
  checks: z.array(z.object({ name: z.string(), passed: z.boolean(), detail: z.string() })),
  verifiedAt: z.string(),
}).passthrough();

/** Parse a trust state; anything unexpected becomes 'unknown'. */
export function safeTrustState(value: unknown): TrustState {
  const r = trustStateSchema.safeParse(value);
  return r.success ? r.data : 'unknown';
}

/** Validate and return, or throw an Error explaining the mismatch (rendered as an error state). */
export function validated<T>(schema: z.ZodType<T>, value: unknown, what: string): T {
  const r = schema.safeParse(value);
  if (!r.success) {
    throw new Error(`The ${what} response did not match the expected format; trust status cannot be shown. (${r.error.issues[0]?.message ?? 'invalid'})`);
  }
  return r.data;
}
