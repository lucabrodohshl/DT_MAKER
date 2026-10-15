/**
 * Operations on the twin model: every edit the Studio makes is one of these pure functions
 * (document in, document out), named so the undo history and the audit can say what happened.
 * Renames carry their references along (paths, triggers, expressions, requirements, layout) in
 * one step. Nothing here decides validity: the server's compiler does.
 */
import type {
  Action,
  AssetType,
  Behavior,
  Link,
  ModelEvent,
  Part,
  Property,
  Relationship,
  State,
  Transition,
  Trigger,
  TwinModel,
} from './types';

export interface Edit {
  doc: TwinModel;
  label: string;
  /** Element to select after the edit (by id). */
  select?: string;
}

// ------------------------------------------------------------------ ids and names

let counter = 0;
/** A short element id, unique in practice (random) and readable in the JSON. */
export function newId(prefix: string): string {
  const bytes = new Uint8Array(4);
  try {
    crypto.getRandomValues(bytes);
  } catch {
    bytes[0] = counter++ & 255;
    bytes[1] = Date.now() & 255;
  }
  return `${prefix}_${Array.from(bytes, (b) => b.toString(36).padStart(2, '0')).join('').slice(0, 7)}`;
}

const KEYWORDS = new Set(['and', 'or', 'not', 'true', 'false', 'self']);

/** True for names the language accepts. */
export function isName(text: string): boolean {
  return /^[A-Za-z][A-Za-z0-9_]*$/.test(text) && !text.includes('__') && !KEYWORDS.has(text);
}

/** An identifier from free text ("Pump P-01" -> "Pump_P_01"), or a fallback. */
export function toName(text: string, fallback = 'item'): string {
  let s = text.trim().replace(/[^A-Za-z0-9_]+/g, '_').replace(/_+/g, '_').replace(/^_|_$/g, '');
  if (!s || !/^[A-Za-z]/.test(s)) s = `${fallback}${s ? `_${s}` : ''}`;
  if (KEYWORDS.has(s)) s = `${s}_`;
  return s;
}

/** A type name from free text ("storage tank" -> "StorageTank"). */
export function toTypeName(text: string): string {
  const parts = text.trim().split(/[^A-Za-z0-9]+/).filter(Boolean);
  const s = parts.map((p) => p.charAt(0).toUpperCase() + p.slice(1)).join('');
  return isName(s) ? s : toName(s || 'Type', 'Type');
}

/** `base`, else `base_2`, `base_3`… not in `taken`. */
export function uniqueName(base: string, taken: Iterable<string>): string {
  const set = new Set(taken);
  if (!set.has(base)) return base;
  for (let i = 2; ; i++) {
    const n = `${base}_${i}`;
    if (!set.has(n)) return n;
  }
}

// ------------------------------------------------------------------ lookup

export const typeByName = (doc: TwinModel, name: string | undefined) => doc.types.find((t) => t.name === name);
export const rootType = (doc: TwinModel) => typeByName(doc, doc.root);

/** Member names of a type, including inherited ones (properties, events, actions, parts). */
export function memberNames(doc: TwinModel, type: AssetType): string[] {
  const out: string[] = [];
  const seen = new Set<string>();
  let t: AssetType | undefined = type;
  while (t && !seen.has(t.name)) {
    seen.add(t.name);
    for (const list of [t.properties, t.events, t.actions, t.parts]) for (const m of list ?? []) out.push(m.name);
    t = t.extends ? typeByName(doc, t.extends) : undefined;
  }
  return out;
}

/** Inherited members first, like the server's effective type. */
export function effective<K extends 'properties' | 'events' | 'actions' | 'parts'>(doc: TwinModel, type: AssetType, key: K): NonNullable<AssetType[K]> {
  const chain: AssetType[] = [];
  const seen = new Set<string>();
  let t: AssetType | undefined = type;
  while (t && !seen.has(t.name)) {
    seen.add(t.name);
    chain.unshift(t);
    t = t.extends ? typeByName(doc, t.extends) : undefined;
  }
  return chain.flatMap((x) => (x[key] ?? []) as unknown[]) as NonNullable<AssetType[K]>;
}

export type ElementKind = 'type' | 'part' | 'property' | 'event' | 'action' | 'relationship' | 'link' | 'assumption' | 'state' | 'group' | 'transition' | 'timer';

export interface Located {
  kind: ElementKind;
  type: AssetType;
  element: AssetType | Part | Property | ModelEvent | Action | Relationship | Link | State | Transition | { id: string; name: string };
}

/** Find any element by id. */
export function locate(doc: TwinModel, id: string | null | undefined): Located | null {
  if (!id) return null;
  for (const t of doc.types) {
    if (t.id === id) return { kind: 'type', type: t, element: t };
    const lists: [ElementKind, { id: string }[] | undefined][] = [
      ['part', t.parts],
      ['property', t.properties],
      ['event', t.events],
      ['action', t.actions],
      ['relationship', t.relationships],
      ['link', t.links],
      ['assumption', t.assumptions],
      ['state', t.behavior?.states],
      ['group', t.behavior?.groups],
      ['transition', t.behavior?.transitions],
      ['timer', t.behavior?.timers],
    ];
    for (const [kind, list] of lists) {
      const e = list?.find((x) => x.id === id);
      if (e) return { kind, type: t, element: e as Located['element'] };
    }
  }
  return null;
}

/** The type of the asset at a dotted part path inside `scope` ("" or "self" = the scope). */
export function typeAt(doc: TwinModel, scope: AssetType, path: string): AssetType | undefined {
  if (!path || path === 'self') return scope;
  let cur: AssetType | undefined = scope;
  for (const seg of path.split('.')) {
    if (!cur) return undefined;
    const p: Part | undefined = effective(doc, cur, 'parts').find((x) => x.name === seg);
    cur = p ? typeByName(doc, p.type) : undefined;
  }
  return cur;
}

/** Every type that contains a part of type `name` (directly). */
export function containersOf(doc: TwinModel, name: string): { container: AssetType; part: Part }[] {
  const out: { container: AssetType; part: Part }[] = [];
  for (const t of doc.types) for (const p of t.parts ?? []) if (p.type === name) out.push({ container: t, part: p });
  return out;
}

// ------------------------------------------------------------------ editing core

function edit(doc: TwinModel, label: string, fn: (d: TwinModel) => string | void): Edit {
  const d = structuredClone(doc);
  const select = fn(d) ?? undefined;
  return { doc: d, label, select };
}

function mustType(d: TwinModel, name: string): AssetType {
  const t = typeByName(d, name);
  if (!t) throw new Error(`no type ${name}`);
  return t;
}

/** Replace path references that start with `old` (as a whole identifier) by `next` in an expression. */
export function renameInExpression(text: string | undefined, old: string, next: string, mode: 'bare' | 'prefix'): string | undefined {
  if (!text) return text;
  if (text.startsWith('smt:')) {
    // Raw SMT uses generated symbols; leave it to the user (the compiler reports unknown symbols).
    return text;
  }
  const esc = old.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  if (mode === 'prefix') {
    return text.replace(new RegExp(`(^|[^A-Za-z0-9_.])${esc}\\.`, 'g'), `$1${next}.`);
  }
  // A bare identifier not preceded by '.' and not followed by '.' (a member of the scope itself).
  return text.replace(new RegExp(`(^|[^A-Za-z0-9_.])${esc}(?![A-Za-z0-9_.])`, 'g'), `$1${next}`);
}

/** Rewrite a member path `[prefix.]name`. */
function renamePath(path: string, old: string, next: string, mode: 'bare' | 'prefix'): string {
  if (mode === 'bare') return path === old ? next : path;
  return path === old ? next : path.startsWith(`${old}.`) ? `${next}${path.slice(old.length)}` : path;
}

function eachExpression(t: AssetType, fn: (s: string | undefined) => string | undefined) {
  for (const e of t.events ?? []) {
    e.when = fn(e.when);
    e.rearm = fn(e.rearm);
    e.meaning = fn(e.meaning);
  }
  for (const a of t.actions ?? []) a.meaning = fn(a.meaning);
  for (const a of t.assumptions ?? []) a.expr = fn(a.expr) ?? a.expr;
  for (const s of t.behavior?.states ?? []) s.meaning = fn(s.meaning);
  for (const g of t.behavior?.groups ?? []) g.meaning = fn(g.meaning);
}

function clean<T extends object>(o: T): T {
  for (const [k, v] of Object.entries(o)) if (v === undefined || v === '') delete (o as Record<string, unknown>)[k];
  return o;
}

// ------------------------------------------------------------------ types and parts

export function addType(doc: TwinModel, wanted: string): Edit {
  const name = uniqueName(toTypeName(wanted), doc.types.map((t) => t.name));
  return edit(doc, `Add type ${name}`, (d) => {
    const t: AssetType = { id: newId('t'), name, label: wanted.trim() && wanted.trim() !== name ? wanted.trim() : undefined };
    d.types.push(clean(t));
    return t.id;
  });
}

/** Add an asset (a part) of `typeName` to `container`, named after the type unless a name is given. */
export function addPart(doc: TwinModel, container: string, typeName: string, wanted?: string, external = false): Edit {
  const c = typeByName(doc, container);
  const base = toName(wanted ?? `${typeName.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase()}_01`, 'asset');
  const name = uniqueName(base, c ? memberNames(doc, c) : []);
  return edit(doc, `Add ${name} : ${typeName}`, (d) => {
    const t = mustType(d, container);
    const p: Part = clean({ id: newId('pt'), name, type: typeName, external: external || undefined });
    t.parts = [...(t.parts ?? []), p];
    return p.id;
  });
}

/** Quick add: a new type and one asset of it in `container`. */
export function addTypeAndPart(doc: TwinModel, container: string, wanted: string): Edit {
  const a = addType(doc, wanted);
  const typeName = a.doc.types[a.doc.types.length - 1]!.name;
  const b = addPart(a.doc, container, typeName, wanted);
  return { doc: b.doc, label: `Add ${typeName}`, select: b.select };
}

export function renameType(doc: TwinModel, old: string, wanted: string): Edit {
  const next = toTypeName(wanted);
  return edit(doc, `Rename type ${old} to ${next}`, (d) => {
    if (typeByName(d, next) && next !== old) throw new Error(`a type named ${next} already exists`);
    const t = mustType(d, old);
    t.name = next;
    if (d.root === old) d.root = next;
    for (const x of d.types) {
      if (x.extends === old) x.extends = next;
      for (const p of x.parts ?? []) if (p.type === old) p.type = next;
    }
    for (const r of d.relationshipTypes ?? []) {
      r.from = r.from?.map((n) => (n === old ? next : n));
      r.to = r.to?.map((n) => (n === old ? next : n));
    }
    for (const view of ['structure', 'behavior'] as const) {
      const l = d.layout?.[view];
      if (l && l[old]) {
        l[next] = l[old]!;
        delete l[old];
      }
    }
    return t.id;
  });
}

/** Rename a part of `container`, with every path that goes through it. */
export function renamePart(doc: TwinModel, container: string, old: string, wanted: string): Edit {
  const next = toName(wanted, 'asset');
  return edit(doc, `Rename ${old} to ${next}`, (d) => {
    const t = mustType(d, container);
    if (memberNames(d, t).includes(next) && next !== old) throw new Error(`${container} already has a member named ${next}`);
    const p = (t.parts ?? []).find((x) => x.name === old);
    if (!p) throw new Error(`no part ${old}`);
    p.name = next;
    for (const r of t.relationships ?? []) {
      r.from = renamePath(r.from, old, next, 'prefix');
      r.to = renamePath(r.to, old, next, 'prefix');
    }
    for (const l of t.links ?? []) {
      l.from = renamePath(l.from, old, next, 'prefix');
      l.to = renamePath(l.to, old, next, 'prefix');
    }
    for (const tr of t.behavior?.transitions ?? []) {
      const trig = tr.trigger as Record<string, unknown>;
      for (const k of ['event', 'action'] as const) if (typeof trig[k] === 'string') trig[k] = renamePath(trig[k] as string, old, next, 'prefix');
    }
    eachExpression(t, (s) => renameInExpression(s, old, next, 'prefix'));
    if (container === d.root) {
      for (const q of d.requirements ?? []) if (q.state) q.state = renamePath(q.state, old, next, 'prefix');
    }
    const l = d.layout?.structure?.[container];
    if (l && l[p.id] === undefined && l[old]) {
      l[p.id] = l[old]!;
      delete l[old];
    }
    return p.id;
  });
}

/** Rename a property, event or action of `typeName`, with every reference in it and in its containers. */
export function renameMember(doc: TwinModel, typeName: string, kind: 'property' | 'event' | 'action', old: string, wanted: string): Edit {
  const next = toName(wanted, kind);
  return edit(doc, `Rename ${kind} ${old} to ${next}`, (d) => {
    const t = mustType(d, typeName);
    if (memberNames(d, t).includes(next) && next !== old) throw new Error(`${typeName} already has a member named ${next}`);
    const list = (kind === 'property' ? t.properties : kind === 'event' ? t.events : t.actions) ?? [];
    const m = list.find((x) => x.name === old);
    if (!m) throw new Error(`no ${kind} ${old}`);
    m.name = next;
    // Inside the type: bare references.
    if (kind === 'property') {
      eachExpression(t, (s) => renameInExpression(s, old, next, 'bare'));
      for (const p of d.types.flatMap((x) => x.parts ?? [])) {
        if (p.type === typeName && p.values && old in p.values) {
          p.values[next] = p.values[old]!;
          delete p.values[old];
        }
      }
    }
    for (const tr of t.behavior?.transitions ?? []) {
      const trig = tr.trigger as Record<string, unknown>;
      if (kind !== 'property' && trig[kind] === old) trig[kind] = next;
    }
    // In containers: `part.old` (one level; deeper paths go through the intermediate type).
    for (const { container, part } of containersOf(d, typeName)) {
      const from = `${part.name}.${old}`;
      const to = `${part.name}.${next}`;
      if (kind === 'property') eachExpression(container, (s) => renameInExpression(s, from, to, 'bare'));
      for (const tr of container.behavior?.transitions ?? []) {
        const trig = tr.trigger as Record<string, unknown>;
        if (kind !== 'property' && trig[kind] === from) trig[kind] = to;
      }
      for (const l of container.links ?? []) {
        if (l.from === from) l.from = to;
        if (l.to === from) l.to = to;
      }
    }
    return m.id;
  });
}

export function renameState(doc: TwinModel, typeName: string, old: string, wanted: string): Edit {
  const next = toName(wanted, 'State');
  return edit(doc, `Rename state ${old} to ${next}`, (d) => {
    const t = mustType(d, typeName);
    const b = t.behavior;
    if (!b) throw new Error('no behaviour');
    if (b.states.some((s) => s.name === next) && next !== old) throw new Error(`a state named ${next} already exists`);
    const s = b.states.find((x) => x.name === old);
    if (!s) throw new Error(`no state ${old}`);
    s.name = next;
    for (const g of b.groups ?? []) g.states = g.states.map((x) => (x === old ? next : x));
    for (const tr of b.transitions) {
      tr.from = tr.from.map((x) => (x === old ? next : x));
      if (tr.to === old) tr.to = next;
    }
    for (const q of d.requirements ?? []) {
      if (!q.state) continue;
      if (typeName === d.root && q.state === old) q.state = next;
      for (const { part } of containersOf(d, typeName)) if (q.state === `${part.name}.${old}`) q.state = `${part.name}.${next}`;
    }
    return s.id;
  });
}

/** What refers to an element (for the delete confirmation). */
export function dependents(doc: TwinModel, id: string): string[] {
  const loc = locate(doc, id);
  if (!loc) return [];
  const out: string[] = [];
  const t = loc.type;
  if (loc.kind === 'type') {
    for (const x of doc.types) {
      if (x.extends === t.name) out.push(`${x.name} extends it`);
      for (const p of x.parts ?? []) if (p.type === t.name) out.push(`${x.name}.${p.name} is a ${t.name}`);
    }
    if (doc.root === t.name) out.push('it is the system itself');
  } else if (loc.kind === 'part') {
    const n = (loc.element as Part).name;
    for (const r of t.relationships ?? []) if (r.from.split('.')[0] === n || r.to.split('.')[0] === n) out.push(`relationship ${r.from} ${r.type} ${r.to}`);
    for (const l of t.links ?? []) if (l.from.split('.')[0] === n || l.to.split('.')[0] === n) out.push(`link ${l.from} → ${l.to}`);
  } else if (loc.kind === 'state') {
    const n = (loc.element as State).name;
    for (const tr of t.behavior?.transitions ?? []) if (tr.from.includes(n) || tr.to === n) out.push(`transition ${tr.from.join(', ')} → ${tr.to}`);
  } else if (loc.kind === 'event' || loc.kind === 'action') {
    const n = (loc.element as ModelEvent).name;
    for (const tr of t.behavior?.transitions ?? []) {
      const trig = tr.trigger as Record<string, unknown>;
      if (trig.event === n || trig.action === n) out.push(`transition ${tr.from.join(', ')} → ${tr.to}`);
    }
    for (const { container, part } of containersOf(doc, t.name)) {
      for (const l of container.links ?? []) if (l.from === `${part.name}.${n}` || l.to === `${part.name}.${n}`) out.push(`link ${l.from} → ${l.to}`);
    }
  }
  return out;
}

/** Delete an element; structural references to it (relationships, links, transitions) go with it. */
export function remove(doc: TwinModel, id: string): Edit {
  const loc = locate(doc, id);
  if (!loc) return { doc, label: 'Nothing to delete' };
  const name = (loc.element as { name?: string }).name ?? loc.kind;
  return edit(doc, `Delete ${loc.kind} ${name}`, (d) => {
    const at = locate(d, id)!;
    const t = at.type;
    const drop = <T extends { id: string }>(list: T[] | undefined) => list?.filter((x) => x.id !== id);
    switch (at.kind) {
      case 'type':
        if (d.root === t.name) throw new Error('the system type cannot be deleted');
        d.types = d.types.filter((x) => x.id !== id);
        break;
      case 'part': {
        const n = (at.element as Part).name;
        t.parts = drop(t.parts);
        t.relationships = t.relationships?.filter((r) => r.from.split('.')[0] !== n && r.to.split('.')[0] !== n);
        t.links = t.links?.filter((l) => l.from.split('.')[0] !== n && l.to.split('.')[0] !== n);
        break;
      }
      case 'property': t.properties = drop(t.properties); break;
      case 'event': t.events = drop(t.events); break;
      case 'action': t.actions = drop(t.actions); break;
      case 'relationship': t.relationships = drop(t.relationships); break;
      case 'link': t.links = drop(t.links); break;
      case 'assumption': t.assumptions = drop(t.assumptions); break;
      case 'state': {
        const b = t.behavior!;
        const n = (at.element as State).name;
        b.states = b.states.filter((x) => x.id !== id);
        b.transitions = b.transitions
          .map((tr) => ({ ...tr, from: tr.from.filter((f) => f !== n) }))
          .filter((tr) => tr.to !== n && tr.from.length > 0);
        for (const g of b.groups ?? []) g.states = g.states.filter((x) => x !== n);
        if (b.states.length > 0 && !b.states.some((s) => s.initial)) b.states[0]!.initial = true;
        break;
      }
      case 'group': t.behavior!.groups = drop(t.behavior!.groups); break;
      case 'transition': t.behavior!.transitions = t.behavior!.transitions.filter((x) => x.id !== id); break;
      case 'timer': t.behavior!.timers = drop(t.behavior!.timers); break;
    }
  });
}

// ------------------------------------------------------------------ members

/**
 * One-line property declaration, e.g.
 *   `bearing_temp: real degC measured -20..150 every 1`
 *   `temp_limit: real degC = 90`
 *   `serial: string per-twin`
 *   `mode: enum(OFF, AUTO, MANUAL)`
 * The kind defaults to measured, or fixed when a value is given.
 */
export function parseDeclaration(line: string): { property?: Omit<Property, 'id'>; error?: string } {
  const m = line.trim().match(/^([A-Za-z][A-Za-z0-9_]*)\s*(?::\s*(.*))?$/);
  if (!m) return { error: 'Start with a name, e.g. temperature: real degC' };
  const name = m[1]!;
  let rest = (m[2] ?? 'real').trim();
  const p: Omit<Property, 'id'> = { name, kind: 'measured', type: 'real' };
  const enumMatch = rest.match(/^enum\s*\(([^)]*)\)\s*(.*)$/);
  if (enumMatch) {
    p.type = 'enum';
    p.values = enumMatch[1]!.split(',').map((x) => x.trim()).filter(Boolean);
    rest = enumMatch[2]!;
  }
  const eq = rest.indexOf('=');
  if (eq >= 0) {
    p.value = rest.slice(eq + 1).trim();
    p.kind = 'fixed';
    rest = rest.slice(0, eq).trim();
  }
  const tokens = rest.split(/\s+/).filter(Boolean);
  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i]!;
    if (['real', 'integer', 'boolean', 'string'].includes(tok)) p.type = tok as Property['type'];
    else if (tok === 'bool') p.type = 'boolean';
    else if (tok === 'int') p.type = 'integer';
    else if (['measured', 'fixed', 'per-twin'].includes(tok)) p.kind = tok as Property['kind'];
    else if (/^-?\d+(\.\d+)?\.\.-?\d+(\.\d+)?$/.test(tok)) {
      const [lo, hi] = tok.split('..');
      p.range = { min: lo, max: hi };
    } else if (tok === 'every' && tokens[i + 1]) {
      p.period = tokens[++i]!.replace(/s$/, '');
    } else if (!p.unit && tok !== 'enum') p.unit = tok;
    else return { error: `Did not understand '${tok}'` };
  }
  if (p.kind === 'measured' && p.type !== 'string' && !p.period) p.period = '1';
  return { property: clean(p) };
}

export function addProperty(doc: TwinModel, typeName: string, decl: Omit<Property, 'id'>): Edit {
  return edit(doc, `Add property ${decl.name}`, (d) => {
    const t = mustType(d, typeName);
    const name = uniqueName(decl.name, memberNames(d, t));
    const p = clean({ ...decl, id: newId('p'), name }) as Property;
    t.properties = [...(t.properties ?? []), p];
    return p.id;
  });
}

export function updateProperty(doc: TwinModel, typeName: string, id: string, patch: Partial<Property>): Edit {
  return edit(doc, 'Edit property', (d) => {
    const t = mustType(d, typeName);
    t.properties = (t.properties ?? []).map((p) => (p.id === id ? (clean({ ...p, ...patch }) as Property) : p));
    return id;
  });
}

export function addEvent(doc: TwinModel, typeName: string, wanted: string, origin: ModelEvent['origin'] = 'reported', when?: string): Edit {
  return edit(doc, `Add event ${wanted}`, (d) => {
    const t = mustType(d, typeName);
    const name = uniqueName(toName(wanted, 'event'), memberNames(d, t));
    const e: ModelEvent = clean({ id: newId('e'), name, origin, when: origin === 'condition' ? when ?? '' : undefined });
    if (origin === 'condition') e.when = when ?? '';
    t.events = [...(t.events ?? []), e];
    return e.id;
  });
}

export function updateEvent(doc: TwinModel, typeName: string, id: string, patch: Partial<ModelEvent>): Edit {
  return edit(doc, 'Edit event', (d) => {
    const t = mustType(d, typeName);
    t.events = (t.events ?? []).map((e) => {
      if (e.id !== id) return e;
      const next = { ...e, ...patch } as ModelEvent;
      if (next.origin === 'reported') {
        delete next.when;
        delete next.rearm;
      } else {
        delete next.meaning;
        next.when = next.when ?? '';
      }
      return clean(next);
    });
    return id;
  });
}

export function addAction(doc: TwinModel, typeName: string, wanted: string): Edit {
  return edit(doc, `Add action ${wanted}`, (d) => {
    const t = mustType(d, typeName);
    const name = uniqueName(toName(wanted, 'action'), memberNames(d, t));
    const a: Action = { id: newId('a'), name };
    t.actions = [...(t.actions ?? []), a];
    return a.id;
  });
}

export function updateAction(doc: TwinModel, typeName: string, id: string, patch: Partial<Action>): Edit {
  return edit(doc, 'Edit action', (d) => {
    const t = mustType(d, typeName);
    t.actions = (t.actions ?? []).map((a) => (a.id === id ? (clean({ ...a, ...patch }) as Action) : a));
    return id;
  });
}

export function updatePart(doc: TwinModel, typeName: string, id: string, patch: Partial<Part>): Edit {
  return edit(doc, 'Edit asset', (d) => {
    const t = mustType(d, typeName);
    t.parts = (t.parts ?? []).map((p) => (p.id === id ? (clean({ ...p, ...patch }) as Part) : p));
    return id;
  });
}

export function updateType(doc: TwinModel, typeName: string, patch: Partial<AssetType>): Edit {
  return edit(doc, `Edit type ${typeName}`, (d) => {
    const t = mustType(d, typeName);
    Object.assign(t, patch);
    clean(t);
    return t.id;
  });
}

export function addRelationship(doc: TwinModel, container: string, type: string, from: string, to: string): Edit {
  return edit(doc, `Relate ${from} ${type} ${to}`, (d) => {
    const t = mustType(d, container);
    const r: Relationship = { id: newId('r'), type, from, to };
    t.relationships = [...(t.relationships ?? []), r];
    return r.id;
  });
}

export function updateRelationship(doc: TwinModel, container: string, id: string, patch: Partial<Relationship>): Edit {
  return edit(doc, 'Edit relationship', (d) => {
    const t = mustType(d, container);
    t.relationships = (t.relationships ?? []).map((r) => (r.id === id ? { ...r, ...patch } : r));
    return id;
  });
}

export function addLink(doc: TwinModel, container: string, from: string, to: string): Edit {
  return edit(doc, `Link ${from} → ${to}`, (d) => {
    const t = mustType(d, container);
    const l: Link = { id: newId('l'), from, to };
    t.links = [...(t.links ?? []), l];
    return l.id;
  });
}

export function addRelationshipType(doc: TwinModel, wanted: string): Edit {
  const name = uniqueName(toName(wanted, 'relates').replace(/^[A-Z]/, (c) => c.toLowerCase()), (doc.relationshipTypes ?? []).map((r) => r.name));
  return edit(doc, `Add relationship type ${name}`, (d) => {
    const r = { id: newId('rt'), name };
    d.relationshipTypes = [...(d.relationshipTypes ?? []), r];
    return r.id;
  });
}

// ------------------------------------------------------------------ behaviour

function ensure(t: AssetType): Behavior {
  if (!t.behavior) t.behavior = { timers: [{ id: newId('x'), name: 't', description: 'time in the current state' }], states: [], transitions: [] };
  if (!t.behavior.timers || t.behavior.timers.length === 0) t.behavior.timers = [{ id: newId('x'), name: 't', description: 'time in the current state' }];
  return t.behavior;
}

export function addState(doc: TwinModel, typeName: string, wanted = 'State', at?: { x: number; y: number }): Edit {
  return edit(doc, `Add state ${wanted}`, (d) => {
    const t = mustType(d, typeName);
    const b = ensure(t);
    const name = uniqueName(toName(wanted, 'State'), b.states.map((s) => s.name));
    const s: State = { id: newId('s'), name };
    if (b.states.length === 0) s.initial = true;
    b.states.push(s);
    if (at) {
      d.layout = d.layout ?? {};
      d.layout.behavior = d.layout.behavior ?? {};
      d.layout.behavior[typeName] = { ...(d.layout.behavior[typeName] ?? {}), [s.id]: at };
    }
    return s.id;
  });
}

export function updateState(doc: TwinModel, typeName: string, id: string, patch: Partial<State>): Edit {
  return edit(doc, 'Edit state', (d) => {
    const b = mustType(d, typeName).behavior!;
    b.states = b.states.map((s) => {
      if (patch.initial && s.id !== id) {
        const rest = { ...s };
        delete rest.initial;
        return rest;
      }
      return s.id === id ? (clean({ ...s, ...patch }) as State) : s;
    });
    return id;
  });
}

export function addTransition(doc: TwinModel, typeName: string, from: string, to: string, trigger: Trigger): Edit {
  return edit(doc, `Add transition ${from} → ${to}`, (d) => {
    const b = ensure(mustType(d, typeName));
    const tr: Transition = { id: newId('tr'), from: [from], to, trigger };
    b.transitions.push(tr);
    return tr.id;
  });
}

export function updateTransition(doc: TwinModel, typeName: string, id: string, patch: Partial<Transition>): Edit {
  return edit(doc, 'Edit transition', (d) => {
    const b = mustType(d, typeName).behavior!;
    b.transitions = b.transitions.map((t) => (t.id === id ? (clean({ ...t, ...patch }) as Transition) : t));
    return id;
  });
}

// ------------------------------------------------------------------ layout (non-semantic)

export function moveNodes(doc: TwinModel, view: 'structure' | 'behavior', key: string, positions: Record<string, { x: number; y: number }>): Edit {
  return edit(doc, 'Move', (d) => {
    d.layout = d.layout ?? {};
    const l = (d.layout[view] = d.layout[view] ?? {});
    l[key] = { ...(l[key] ?? {}), ...positions };
  });
}

// ------------------------------------------------------------------ describing triggers

export function triggerText(t: Trigger, unit = 's'): string {
  if ('event' in t) return `▸ ${t.event}`;
  if ('action' in t) return `◦ ${t.action}()`;
  if ('timeout' in t) return `⧗ after ${t.timeout} ${unit}`;
  return 'τ internal';
}

export function windowText(w: { timer: string; op: string; bound: number | string }[] | undefined): string {
  if (!w || w.length === 0) return '';
  return w.map((a) => `${a.timer} ${a.op.replace('>=', '≥').replace('<=', '≤')} ${a.bound}`).join(', ');
}
