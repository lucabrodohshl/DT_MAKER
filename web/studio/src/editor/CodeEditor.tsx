/**
 * Source editor (CodeMirror 6) for ontologies, interpretations and models.
 *
 * - Highlighting for the aligner formats / XML.
 * - Diagnostics from the backend's strict parser and validation, shown as lint
 *   markers at their exact line/column (no client-side parsing of formulas).
 * - Autocomplete of ontology symbols and SMT-LIB operators.
 * - Go to definition: F12 or Cmd/Ctrl-click on a symbol (positions from the
 *   backend's structural reading).
 * - Search (Cmd/Ctrl-F), undo/redo, read-only mode.
 *
 * The editor is uncontrolled: live data never replaces the user's text. The
 * parent decides when to load new content via `resetKey`.
 */
import { autocompletion, type CompletionContext, type CompletionResult } from '@codemirror/autocomplete';
import { defaultKeymap, history, historyKeymap, indentWithTab } from '@codemirror/commands';
import { bracketMatching } from '@codemirror/language';
import { linter, lintGutter, setDiagnostics, type Diagnostic } from '@codemirror/lint';
import { highlightSelectionMatches, search, searchKeymap } from '@codemirror/search';
import { EditorState, type Extension } from '@codemirror/state';
import { EditorView, highlightActiveLine, highlightActiveLineGutter, keymap, lineNumbers } from '@codemirror/view';
import { useEffect, useRef } from 'react';
import type { SourceDiagnostic, Span } from '@/api/types';
import { ontologyLanguage, studioHighlight, xmlLanguage } from './languages';

export interface SymbolEntry {
  name: string;
  kind: 'sort' | 'function' | 'relation' | 'axiom' | 'entry';
  detail?: string;
  span: Span;
}

const SMT_OPS = ['and', 'or', 'not', '=>', 'ite', 'distinct', 'forall', 'exists', 'let', 'true', 'false', 'div', 'mod', 'abs'];

export function spanToRange(doc: EditorState['doc'], span: Span): { from: number; to: number } {
  const lineNo = Math.min(Math.max(span.line || 1, 1), doc.lines);
  const line = doc.line(lineNo);
  if (!span.line) return { from: 0, to: Math.min(doc.length, line.to) };
  const from = Math.min(line.from + Math.max(span.column - 1, 0), line.to);
  const to = span.length ? Math.min(from + span.length, line.to) : line.to;
  return { from, to: Math.max(to, from) };
}

function toCmDiagnostics(doc: EditorState['doc'], diags: SourceDiagnostic[]): Diagnostic[] {
  return diags.map((d) => {
    const { from, to } = spanToRange(doc, d.span);
    return {
      from,
      to: to === from ? Math.min(from + 1, doc.length) : to,
      severity: d.severity === 'error' ? 'error' : d.severity === 'warning' ? 'warning' : 'info',
      message: `${d.code}: ${d.message}`,
      source: 'Studio validation',
    };
  });
}

export interface CodeEditorProps {
  /** Initial text; the editor reloads it only when `resetKey` changes. */
  value: string;
  resetKey: string;
  onChange?: (text: string) => void;
  readOnly?: boolean;
  language: 'ontology' | 'interpretation' | 'xml' | 'json' | 'text';
  diagnostics?: SourceDiagnostic[];
  symbols?: SymbolEntry[];
  onGoToSymbol?: (name: string) => void;
  /** Request to reveal and select a span (e.g. from the structure panel). */
  reveal?: { span: Span; nonce: number } | null;
  ariaLabel: string;
  height?: number | string;
}

export function CodeEditor({
  value,
  resetKey,
  onChange,
  readOnly,
  language,
  diagnostics = [],
  symbols = [],
  onGoToSymbol,
  reveal,
  ariaLabel,
  height = 520,
}: CodeEditorProps) {
  const host = useRef<HTMLDivElement>(null);
  const view = useRef<EditorView | null>(null);
  const symbolsRef = useRef(symbols);
  const onChangeRef = useRef(onChange);
  const onGoRef = useRef(onGoToSymbol);
  useEffect(() => {
    symbolsRef.current = symbols;
    onChangeRef.current = onChange;
    onGoRef.current = onGoToSymbol;
  });

  useEffect(() => {
    if (!host.current) return;
    const complete = (ctx: CompletionContext): CompletionResult | null => {
      const word = ctx.matchBefore(/[\w.!]+/);
      if (!word || (word.from === word.to && !ctx.explicit)) return null;
      return {
        from: word.from,
        options: [
          ...symbolsRef.current
            .filter((s) => s.kind !== 'axiom')
            .map((s) => ({ label: s.name, type: s.kind === 'sort' ? 'type' : s.kind === 'relation' ? 'function' : 'variable', detail: s.detail })),
          ...SMT_OPS.map((o) => ({ label: o, type: 'keyword' })),
        ],
      };
    };
    const goTo = (v: EditorView, pos: number) => {
      const w = v.state.wordAt(pos);
      if (!w) return false;
      const name = v.state.sliceDoc(w.from, w.to);
      const target = symbolsRef.current.find((s) => s.name === name);
      if (target) {
        const r = spanToRange(v.state.doc, target.span);
        v.dispatch({ selection: { anchor: r.from, head: r.to }, scrollIntoView: true });
        onGoRef.current?.(name);
        return true;
      }
      return false;
    };
    const extensions: Extension[] = [
      lineNumbers(),
      highlightActiveLineGutter(),
      highlightActiveLine(),
      history(),
      bracketMatching(),
      search({ top: true }),
      highlightSelectionMatches(),
      lintGutter(),
      linter(null),
      language === 'xml' ? xmlLanguage : language === 'json' || language === 'text' ? [] : ontologyLanguage,
      studioHighlight,
      autocompletion({ override: [complete] }),
      keymap.of([
        ...defaultKeymap,
        ...historyKeymap,
        ...searchKeymap,
        indentWithTab,
        { key: 'F12', run: (v) => goTo(v, v.state.selection.main.head) },
      ]),
      EditorView.domEventHandlers({
        mousedown: (e, v) => {
          if (!(e.metaKey || e.ctrlKey)) return false;
          const pos = v.posAtCoords({ x: e.clientX, y: e.clientY });
          return pos !== null && goTo(v, pos);
        },
      }),
      EditorState.readOnly.of(!!readOnly),
      EditorView.editable.of(!readOnly),
      EditorView.contentAttributes.of({ 'aria-label': ariaLabel }),
      EditorView.updateListener.of((u) => {
        if (u.docChanged) onChangeRef.current?.(u.state.doc.toString());
      }),
      EditorView.theme({
        '&': { height: typeof height === 'number' ? `${height}px` : height, fontSize: '13px', backgroundColor: 'var(--surface)', color: 'var(--text)' },
        '.cm-scroller': { fontFamily: 'var(--font-mono)', lineHeight: '1.55' },
        '.cm-gutters': { backgroundColor: 'var(--bg)', color: 'var(--text-subtle)', borderRight: '1px solid var(--divider)' },
        '.cm-activeLine': { backgroundColor: 'var(--surface-hover)' },
        '.cm-activeLineGutter': { backgroundColor: 'var(--surface-hover)' },
        '&.cm-focused': { outline: '2px solid var(--focus)' },
        '.cm-panels': { backgroundColor: 'var(--bg)', color: 'var(--text)' },
        '.cm-tooltip': { backgroundColor: 'var(--surface-raised)', border: '1px solid var(--border)' },
      }),
    ];
    const v = new EditorView({ state: EditorState.create({ doc: value, extensions }), parent: host.current });
    view.current = v;
    return () => {
      v.destroy();
      view.current = null;
    };
    // Recreate only when the document identity, language or mode changes — never on live updates.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [resetKey, language, readOnly]);

  useEffect(() => {
    const v = view.current;
    if (!v) return;
    v.dispatch(setDiagnostics(v.state, toCmDiagnostics(v.state.doc, diagnostics)));
  }, [diagnostics, resetKey]);

  useEffect(() => {
    const v = view.current;
    if (!v || !reveal) return;
    const r = spanToRange(v.state.doc, reveal.span);
    v.dispatch({ selection: { anchor: r.from, head: r.to }, scrollIntoView: true });
    v.focus();
  }, [reveal]);

  return <div ref={host} style={{ border: '1px solid var(--border)', borderRadius: 'var(--radius)', overflow: 'hidden' }} />;
}
