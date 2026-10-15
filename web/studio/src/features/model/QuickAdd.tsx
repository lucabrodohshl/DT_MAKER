/**
 * Floating pickers of the canvases: Quick add (an asset of an existing type, or a new type with
 * one asset), the relationship picker (vocabulary filtered by the allowed end types, or a new
 * relationship type) and the trigger picker of a new transition.
 */
import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { createPortal } from 'react-dom';
import { addPart, addRelationship, addRelationshipType, addTypeAndPart, effective, moveNodes, toTypeName, typeAt, typeByName } from './ops';
import { useModel } from './store';
import type { Trigger } from './types';

interface Option {
  key: string;
  label: ReactNode;
  meta?: string;
  group?: string;
  run: () => void;
}

function Picker({ at, placeholder, options, onClose, initial = '' }: { at: { x: number; y: number }; placeholder: string; options: (q: string) => Option[]; onClose: () => void; initial?: string }) {
  const [q, setQ] = useState(initial);
  const [i, setI] = useState(0);
  const ref = useRef<HTMLDivElement>(null);
  const list = useMemo(() => options(q), [options, q]);
  useEffect(() => {
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) onClose();
    };
    window.addEventListener('mousedown', onDown);
    return () => window.removeEventListener('mousedown', onDown);
  }, [onClose]);
  const left = Math.min(at.x, window.innerWidth - 320);
  const top = Math.min(at.y, window.innerHeight - 360);
  return createPortal(
    <div className="ms-pop" ref={ref} style={{ position: 'fixed', left, top }} role="dialog" aria-label={placeholder}>
      <input
        autoFocus
        value={q}
        placeholder={placeholder}
        onChange={(e) => { setQ(e.target.value); setI(0); }}
        onKeyDown={(e) => {
          if (e.key === 'Escape') onClose();
          else if (e.key === 'ArrowDown') {
            e.preventDefault();
            setI((x) => Math.min(x + 1, list.length - 1));
          } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            setI((x) => Math.max(x - 1, 0));
          } else if (e.key === 'Enter' && list[i]) {
            e.preventDefault();
            list[i].run();
          }
        }}
        role="combobox"
        aria-expanded="true"
        aria-controls="ms-pop-list"
      />
      <ul id="ms-pop-list" role="listbox">
        {list.length === 0 && <li aria-disabled="true" className="ms-note">Nothing matches.</li>}
        {list.map((o, k) => {
          const head = o.group && o.group !== list[k - 1]?.group ? <div className="ms-pop__group">{o.group}</div> : null;
          return (
            <div key={o.key}>
              {head}
              <li role="option" aria-selected={k === i} onMouseEnter={() => setI(k)} onMouseDown={(e) => { e.preventDefault(); o.run(); }}>
                {o.label}
                {o.meta && <small>{o.meta}</small>}
              </li>
            </div>
          );
        })}
      </ul>
    </div>,
    document.body,
  );
}

export function QuickAdd({ at, container, onClose, onPlaced }: { at: { x: number; y: number; flow: { x: number; y: number } }; container: string; onClose: () => void; onPlaced?: (id: string) => void }) {
  const m = useModel();
  const place = (make: (d: typeof m.doc) => { doc: typeof m.doc; label: string; select?: string }) => {
    const err = m.apply((d) => {
      const e = make(d);
      if (!e.select) return e;
      const moved = moveNodes(e.doc, 'structure', container, { [e.select]: { x: Math.round(at.flow.x), y: Math.round(at.flow.y) } });
      return { doc: moved.doc, label: e.label, select: e.select };
    });
    if (err) window.alert(err);
    onClose();
    void onPlaced;
  };
  const options = (q: string): Option[] => {
    const text = q.trim().toLowerCase();
    const types = m.doc.types.filter((t) => t.name !== container && (!text || t.name.toLowerCase().includes(text) || (t.label ?? '').toLowerCase().includes(text)));
    const out: Option[] = types.map((t) => ({
      key: `t:${t.name}`,
      label: <>Add <strong>{t.label ?? t.name}</strong></>,
      meta: t.name,
      group: 'Asset of an existing type',
      run: () => place((d) => addPart(d, container, t.name)),
    }));
    if (q.trim() && !m.doc.types.some((t) => t.name === toTypeName(q))) {
      out.push({
        key: 'new',
        label: <>New type <strong>{toTypeName(q)}</strong></>,
        meta: 'type + asset',
        group: 'New',
        run: () => place((d) => addTypeAndPart(d, container, q)),
      });
    }
    return out;
  };
  return <Picker at={at} placeholder="Add an asset: type a name, e.g. Pump" options={options} onClose={onClose} />;
}

export function RelationPicker({ at, container, from, to, onPick, onClose }: { at: { x: number; y: number }; container: string; from: string; to: string; onPick: (type: string) => void; onClose: () => void }) {
  const m = useModel();
  const scope = typeByName(m.doc, container)!;
  const fromType = typeAt(m.doc, scope, from);
  const toType = typeAt(m.doc, scope, to);
  const isA = (t: typeof fromType, names: string[] | undefined) => {
    if (!names || names.length === 0) return true;
    let cur = t;
    const seen = new Set<string>();
    while (cur && !seen.has(cur.name)) {
      if (names.includes(cur.name)) return true;
      seen.add(cur.name);
      cur = cur.extends ? typeByName(m.doc, cur.extends) : undefined;
    }
    return false;
  };
  const options = (q: string): Option[] => {
    const text = q.trim().toLowerCase();
    const vocab = (m.doc.relationshipTypes ?? []).filter((r) => isA(fromType, r.from) && isA(toType, r.to) && (!text || r.name.toLowerCase().includes(text)));
    const out: Option[] = vocab.map((r) => ({ key: r.name, label: <>{from} <strong>{r.label ?? r.name}</strong> {to}</>, meta: r.name, run: () => onPick(r.name) }));
    if (q.trim() && !(m.doc.relationshipTypes ?? []).some((r) => r.name === q.trim())) {
      out.push({
        key: 'new',
        label: <>New relationship <strong>{q.trim()}</strong></>,
        group: 'New',
        run: () => {
          const err = m.apply((d) => {
            const a = addRelationshipType(d, q);
            const name = a.doc.relationshipTypes![a.doc.relationshipTypes!.length - 1]!.name;
            const b = addRelationship(a.doc, container, name, from, to);
            return { doc: b.doc, label: `${a.label}; ${b.label}`, select: b.select };
          });
          if (err) window.alert(err);
          onClose();
        },
      });
    }
    return out;
  };
  return <Picker at={at} placeholder={`${from} … ${to}: which relationship?`} options={options} onClose={onClose} />;
}

export function TriggerPicker({ at, type, onPick, onClose }: { at: { x: number; y: number }; type: string; onPick: (t: Trigger) => void; onClose: () => void }) {
  const m = useModel();
  const t = typeByName(m.doc, type);
  const options = (q: string): Option[] => {
    if (!t) return [];
    const text = q.trim().toLowerCase();
    const match = (s: string) => !text || s.toLowerCase().includes(text);
    const out: Option[] = [];
    for (const e of effective(m.doc, t, 'events')) if (match(e.name)) out.push({ key: `e:${e.name}`, label: <>▸ {e.name}</>, meta: e.origin, group: 'Event', run: () => onPick({ event: e.name }) });
    for (const p of effective(m.doc, t, 'parts')) {
      const pt = typeByName(m.doc, p.type);
      if (!pt) continue;
      for (const e of effective(m.doc, pt, 'events')) {
        const path = `${p.name}.${e.name}`;
        if (match(path)) out.push({ key: `e:${path}`, label: <>▸ {path}</>, meta: e.origin, group: 'Event of a part', run: () => onPick({ event: path }) });
      }
    }
    for (const a of effective(m.doc, t, 'actions')) if (match(a.name)) out.push({ key: `a:${a.name}`, label: <>◦ {a.name}()</>, group: 'Action', run: () => onPick({ action: a.name }) });
    const seconds = text.match(/^(?:after\s*)?(\d+)\s*s?$/);
    out.push({
      key: 'timeout',
      label: <>⧗ after {seconds ? seconds[1] : '…'} s</>,
      meta: 'timeout',
      group: 'Time',
      run: () => {
        const v = seconds ? Number(seconds[1]) : Number(window.prompt('Leave the state after how many seconds?', '10'));
        if (Number.isFinite(v) && v >= 0) onPick({ timeout: Math.round(v) });
      },
    });
    if (match('internal') || match('τ')) out.push({ key: 'tau', label: <>τ internal step</>, meta: 'advanced', group: 'Time', run: () => onPick({ internal: true }) });
    if (text && /^[A-Za-z][A-Za-z0-9_ ]*$/.test(q.trim()) && !effective(m.doc, t, 'events').some((e) => e.name === q.trim())) {
      out.push({ key: 'new-event', label: <>New event <strong>{q.trim()}</strong></>, group: 'New', run: () => onPick({ event: `__new__:${q.trim()}` }) });
      out.push({ key: 'new-action', label: <>New action <strong>{q.trim()}()</strong></>, group: 'New', run: () => onPick({ action: `__new__:${q.trim()}` }) });
    }
    return out;
  };
  return <Picker at={at} placeholder="What makes it happen? event, action, after 10 s" options={options} onClose={onClose} />;
}
