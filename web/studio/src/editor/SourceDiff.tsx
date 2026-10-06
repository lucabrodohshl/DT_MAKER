/** Side-by-side source diff (read-only) using @codemirror/merge. */
import { MergeView } from '@codemirror/merge';
import { EditorState } from '@codemirror/state';
import { EditorView, lineNumbers } from '@codemirror/view';
import { useEffect, useRef } from 'react';
import { ontologyLanguage, studioHighlight, xmlLanguage } from './languages';

export function SourceDiff({ a, b, language, labelA, labelB }: { a: string; b: string; language: 'ontology' | 'xml'; labelA: string; labelB: string }) {
  const host = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!host.current) return;
    const ext = [
      lineNumbers(),
      language === 'xml' ? xmlLanguage : ontologyLanguage,
      studioHighlight,
      EditorState.readOnly.of(true),
      EditorView.editable.of(false),
      EditorView.theme({
        '&': { fontSize: '12.5px', backgroundColor: 'var(--surface)', color: 'var(--text)' },
        '.cm-scroller': { fontFamily: 'var(--font-mono)' },
        '.cm-gutters': { backgroundColor: 'var(--bg)', color: 'var(--text-subtle)' },
      }),
    ];
    const mv = new MergeView({
      a: { doc: a, extensions: [...ext, EditorView.contentAttributes.of({ 'aria-label': labelA })] },
      b: { doc: b, extensions: [...ext, EditorView.contentAttributes.of({ 'aria-label': labelB })] },
      parent: host.current,
      collapseUnchanged: { margin: 3, minSize: 6 },
      gutter: true,
    });
    return () => mv.destroy();
  }, [a, b, language, labelA, labelB]);
  return (
    <div className="stack-sm">
      <div className="row-between xsmall subtle">
        <span>{labelA}</span>
        <span>{labelB}</span>
      </div>
      <div ref={host} style={{ border: '1px solid var(--border)', borderRadius: 'var(--radius)', maxHeight: 640, overflow: 'auto' }} />
    </div>
  );
}
