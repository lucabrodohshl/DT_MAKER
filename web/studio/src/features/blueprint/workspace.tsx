/**
 * Workspace services shared by every Blueprint page: the bottom drawer (Problems, Output),
 * the check runner that records every check's result in the Output log, and the
 * guided / expert preference (progressive disclosure of advanced fields).
 */
import { useQueryClient } from '@tanstack/react-query';
import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';
import { ApiError } from '@/api/client';
import { blueprintApi, useInvalidateBlueprints } from '@/api/blueprints';
import type { CheckResult } from '@/api/types';

export type CheckId = 'formal' | 'alignment' | 'compile' | 'scenarios' | 'package' | 'refinement';

export interface OutputEntry {
  id: number;
  at: string;
  title: string;
  outcome: 'pass' | 'fail' | 'error' | 'info' | 'running';
  summary: string;
  detail?: unknown;
  evidenceId?: string | null;
}

interface WorkspaceUi {
  drawer: 'problems' | 'output' | null;
  setDrawer: (d: 'problems' | 'output' | null) => void;
  output: OutputEntry[];
  log: (e: Omit<OutputEntry, 'id' | 'at'>) => number;
  updateLog: (id: number, e: Partial<OutputEntry>) => void;
  /** Run one check through the API, logging it; resolves with the result (or rejects). */
  runCheck: (check: CheckId, body?: Record<string, unknown>) => Promise<CheckResult>;
  running: Set<CheckId>;
  expert: boolean;
  setExpert: (v: boolean) => void;
}

const Ctx = createContext<WorkspaceUi | null>(null);

export function useWorkspace(): WorkspaceUi {
  const c = useContext(Ctx);
  if (!c) throw new Error('useWorkspace outside a Blueprint workspace');
  return c;
}

const CHECK_TITLE: Record<CheckId, string> = {
  formal: 'Validate formal artefacts',
  alignment: 'Semantic alignment (aligner)',
  compile: 'Compile Digital Twin View',
  scenarios: 'Scenario regression tests',
  package: 'Build package and deployment bundle',
  refinement: 'Ontology refinement check',
};

export function checkTitle(c: CheckId): string {
  return CHECK_TITLE[c];
}

function readExpert(): boolean {
  try {
    return localStorage.getItem('vts.studio.expert') === '1';
  } catch {
    return false;
  }
}

export function WorkspaceProvider({ id, version, children }: { id: string; version: number; children: ReactNode }) {
  const [drawer, setDrawer] = useState<'problems' | 'output' | null>(null);
  const [output, setOutput] = useState<OutputEntry[]>([]);
  const [running, setRunning] = useState<Set<CheckId>>(new Set());
  const [expert, setExpertState] = useState(readExpert);
  const invalidate = useInvalidateBlueprints();
  const qc = useQueryClient();

  const log = useCallback((e: Omit<OutputEntry, 'id' | 'at'>) => {
    const id = Date.now() + Math.random();
    setOutput((o) => [{ ...e, id, at: new Date().toISOString() }, ...o].slice(0, 100));
    return id;
  }, []);
  const updateLog = useCallback((id: number, e: Partial<OutputEntry>) => {
    setOutput((o) => o.map((x) => (x.id === id ? { ...x, ...e } : x)));
  }, []);

  const runCheck = useCallback(
    async (check: CheckId, body: Record<string, unknown> = {}) => {
      setRunning((r) => new Set(r).add(check));
      const entry = log({ title: CHECK_TITLE[check], outcome: 'running', summary: 'Running…' });
      try {
        const r = await blueprintApi.check(id, version, check, body);
        const outcome = (r.outcome ?? 'info') as OutputEntry['outcome'];
        updateLog(entry, {
          outcome: outcome === 'pass' || outcome === 'fail' || outcome === 'error' ? outcome : 'info',
          summary: typeof r.summary === 'string' ? r.summary : outcome === 'pass' ? 'Passed.' : 'Finished.',
          detail: r,
          evidenceId: typeof r.id === 'string' ? r.id : typeof r.evidenceId === 'string' ? (r.evidenceId as string) : null,
        });
        return r;
      } catch (e) {
        const message = e instanceof ApiError ? e.message : e instanceof Error ? e.message : 'Check failed';
        updateLog(entry, {
          outcome: 'error',
          summary: message,
          detail: e instanceof ApiError ? { code: e.code, context: e.context } : undefined,
        });
        throw e;
      } finally {
        setRunning((r) => {
          const n = new Set(r);
          n.delete(check);
          return n;
        });
        void invalidate();
        void qc.invalidateQueries({ queryKey: ['blueprints'] });
      }
    },
    [id, version, log, updateLog, invalidate, qc],
  );

  const setExpert = useCallback((v: boolean) => {
    setExpertState(v);
    try {
      localStorage.setItem('vts.studio.expert', v ? '1' : '0');
    } catch {
      /* preference only */
    }
  }, []);

  const value = useMemo(
    () => ({ drawer, setDrawer, output, log, updateLog, runCheck, running, expert, setExpert }),
    [drawer, output, log, updateLog, runCheck, running, expert, setExpert],
  );
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}
