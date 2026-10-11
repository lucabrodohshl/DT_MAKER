/**
 * Operator-defined missions: place inspection targets (and optionally move the home pad) on the
 * facility map, then restart the physical twin with that mission. The world simulator is the
 * authority: it checks every cell against the real building and refuses walls, obstacles,
 * closed doors, duplicates and the home pad, with the reason shown here.
 */
import { Plus, RotateCcw, Trash2 } from 'lucide-react';
import { useState } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import { Button, Callout, Dialog, Segmented } from '@/design';
import type { DomainPluginProps } from '@/plugins/types';
import { MapCanvas } from './MapCanvas';
import { cellAt, type Grid, type MissionJson } from './model';

interface Draft {
  id: string;
  name: string;
  cell: [number, number];
}

const MAX_TARGETS = 12;

function errorText(e: unknown): string {
  if (!(e instanceof Error)) return String(e);
  const ctx = (e as { context?: { key: string; value: string }[] }).context ?? [];
  const extra = ctx.filter((c) => c.key === 'target' || c.key === 'cell').map((c) => `${c.key} ${c.value}`);
  return extra.length ? `${e.message} (${extra.join(', ')})` : e.message;
}

export function NewMissionDialog({
  open,
  onOpenChange,
  grid,
  mission,
  runtime,
  twinId,
}: {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  grid: Grid | null;
  mission: MissionJson | null;
  runtime: DomainPluginProps['runtime'];
  twinId: string;
}) {
  const qc = useQueryClient();
  const [targets, setTargets] = useState<Draft[]>([]);
  const [home, setHome] = useState<[number, number] | null>(null);
  const [tool, setTool] = useState<'target' | 'home'>('target');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [seeded, setSeeded] = useState(false);

  // Start from the current mission each time the dialog opens.
  if (open && !seeded) {
    setSeeded(true);
    setTargets((mission?.targets ?? []).map((t) => ({ id: t.id, name: t.name, cell: t.cell })));
    setHome(mission?.home ?? null);
    setError(null);
  }
  if (!open && seeded) setSeeded(false);

  const nextId = () => {
    for (let i = 1; ; i++) if (!targets.some((t) => t.id === `T${i}`)) return `T${i}`;
  };
  const onCell = (x: number, y: number) => {
    setError(null);
    if (grid && cellAt(grid, x, y) === '#') {
      setError(`Cell ${x},${y} is a wall.`);
      return;
    }
    if (tool === 'home') {
      setHome([x, y]);
      return;
    }
    const existing = targets.find((t) => t.cell[0] === x && t.cell[1] === y);
    if (existing) {
      setTargets(targets.filter((t) => t !== existing));
      return;
    }
    if (targets.length >= MAX_TARGETS) {
      setError(`A mission has at most ${MAX_TARGETS} targets.`);
      return;
    }
    const id = nextId();
    setTargets([...targets, { id, name: `Inspection point ${id}`, cell: [x, y] }]);
  };

  const finish = async (path: string, body: unknown) => {
    setBusy(true);
    setError(null);
    try {
      await runtime.post(path, body);
      await runtime.post('/simulation/reset', {});
      await qc.invalidateQueries({ queryKey: ['drone', twinId] });
      onOpenChange(false);
    } catch (e) {
      setError(errorText(e));
    } finally {
      setBusy(false);
    }
  };
  const submit = () =>
    void finish('/scenario/mission', {
      targets: targets.map((t) => ({ id: t.id, name: t.name.trim(), cell: t.cell })),
      ...(home ? { home } : {}),
    });

  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      wide
      title="New mission"
      description="Click the map to place inspection targets (click a target again to remove it). Applying restarts the physical twin and starts a new execution."
      footer={
        <div className="row-between" style={{ width: '100%' }}>
          <Button icon={<RotateCcw size={14} />} onClick={() => void finish('/scenario/mission/default', {})} loading={busy}>
            Restore default mission
          </Button>
          <div className="row-wrap">
            <Button onClick={() => onOpenChange(false)}>Cancel</Button>
            <Button variant="primary" onClick={submit} loading={busy} disabled={targets.length === 0 || targets.some((t) => !t.name.trim())}>
              Apply and restart
            </Button>
          </div>
        </div>
      }
    >
      <div className="stack">
        <Segmented
          label="Map tool"
          value={tool}
          onChange={(v) => setTool(v as 'target' | 'home')}
          options={[
            { id: 'target', label: 'Place targets' },
            { id: 'home', label: 'Move home pad' },
          ]}
        />
        <MapCanvas
          grid={grid}
          overlay={{ targets: targets.map((t) => ({ ...t, status: 'pending' })), home }}
          label="Facility map: click to place inspection targets"
          onCellClick={onCell}
        />
        {error && (
          <Callout tone="critical" title="Mission not accepted">
            {error}
          </Callout>
        )}
        {targets.length === 0 ? (
          <p className="small muted">
            <Plus size={12} /> No targets yet: click a free cell on the map.
          </p>
        ) : (
          <ol className="drone-mission-targets stack-sm">
            {targets.map((t, i) => (
              <li key={t.id} className="row-wrap">
                <span className="strong">{t.id}</span>
                <span className="small muted">cell {t.cell[0]},{t.cell[1]}</span>
                <input
                  className="vts-input"
                  aria-label={`Name of target ${t.id}`}
                  value={t.name}
                  onChange={(e) => setTargets(targets.map((u, j) => (j === i ? { ...u, name: e.target.value } : u)))}
                  style={{ flex: 1, minWidth: 200 }}
                />
                <Button variant="ghost" iconOnly icon={<Trash2 size={14} />} onClick={() => setTargets(targets.filter((_, j) => j !== i))}>
                  Remove {t.id}
                </Button>
              </li>
            ))}
          </ol>
        )}
        <p className="small muted">
          The map is the twin's facility plan. The simulator checks every cell against the real building; a target on an obstacle the
          plan does not show is refused with the reason.
        </p>
      </div>
    </Dialog>
  );
}
