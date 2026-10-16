/** The model's composition tree and reusable type library. */
import { ChevronRight, Plus } from 'lucide-react';
import { useState } from 'react';
import { addType, effective, toTypeName, typeByName } from './ops';
import { useModel } from './store';
import type { AssetType } from './types';

function Parts({ type, depth, visited }: { type: AssetType; depth: number; visited: Set<string> }) {
  const m = useModel();
  if (depth > 8 || visited.has(type.name)) return null;
  const next = new Set(visited).add(type.name);
  return <>{effective(m.doc, type, 'parts').map((part) => {
    const child = typeByName(m.doc, part.type);
    return <li key={`${type.name}:${part.id}`}>
      <button type="button" style={{ '--depth': depth } as React.CSSProperties} aria-selected={m.selection === part.id}
        onClick={() => { m.setScope(type.name); m.setView('structure'); m.select(part.id); }}
        onDoubleClick={() => { if (child) { m.setScope(child.name); m.select(child.id); } }}
        title={`Select ${part.name}; double-click to edit ${part.type}`}>
        <ChevronRight size={12} aria-hidden="true" className="ms-tree__ext" />
        <span className="truncate">{part.label ?? part.name}{part.external ? ' ⇢' : ''}</span>
        <span className="ms-tree__type">{part.type}</span>
      </button>
      {child && <ul className="ms-tree"><Parts type={child} depth={depth + 1} visited={next} /></ul>}
    </li>;
  })}</>;
}

export function Explorer() {
  const m = useModel();
  const root = typeByName(m.doc, m.doc.root);
  const [error, setError] = useState<string | null>(null);
  const newType = () => {
    const name = window.prompt('New reusable asset type', 'Pump');
    if (!name?.trim()) return;
    setError(m.apply((d) => addType(d, toTypeName(name))));
  };
  return <>
    <section className="ms-section">
      <div className="ms-section__head">Twin design</div>
      <ul className="ms-tree"><li><button type="button" aria-selected={m.selection === null} onClick={() => m.select(null)}><span className="ms-tree__ext">◇</span><span className="truncate">{m.doc.label ?? m.doc.name}</span></button></li></ul>
    </section>
    <section className="ms-section">
      <div className="ms-section__head">System <span>{root ? effective(m.doc, root, 'parts').length : 0} assets</span></div>
      <ul className="ms-tree">
        {root && <li>
          <button type="button" aria-selected={m.selection === root.id} onClick={() => { m.setScope(root.name); m.setView('structure'); m.select(root.id); }}>
            <span className="ms-tree__ext">◆</span><span className="truncate">{root.label ?? root.name}</span>
          </button>
          <ul className="ms-tree"><Parts type={root} depth={1} visited={new Set()} /></ul>
        </li>}
      </ul>
    </section>
    <section className="ms-section">
      <div className="ms-section__head">Asset types <button type="button" disabled={!m.editable} onClick={newType} title="New asset type" aria-label="New asset type"><Plus size={14} /></button></div>
      {error && <div className="ms-err" style={{ padding: '0 16px' }}>{error}</div>}
      <ul className="ms-tree">
        {m.doc.types.filter((t) => t.name !== m.doc.root).map((t) => <li key={t.id}>
          <button type="button" aria-selected={m.selection === t.id} onClick={() => { m.setScope(t.name); m.setView('structure'); m.select(t.id); }}>
            <span className="ms-tree__ext">◇</span><span className="truncate">{t.label ?? t.name}</span>
            <span className="ms-tree__type">{t.behavior?.states.length ? `${t.behavior.states.length} states` : ''}</span>
          </button>
        </li>)}
      </ul>
    </section>
  </>;
}
