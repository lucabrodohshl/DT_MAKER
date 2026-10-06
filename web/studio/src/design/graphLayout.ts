/**
 * Layered (dagre) layout that never throws. Pure presentation: positions only.
 *
 * dagre cannot route self-loops or edges to nodes it has not been given a size for
 * ("Not possible to find intersection inside of the rectangle"). Such edges are left
 * out of the layout (React Flow still draws them), and if dagre fails anyway the
 * nodes fall back to a grid instead of taking the page down.
 */
import Dagre from '@dagrejs/dagre';

type Graph = InstanceType<typeof Dagre.graphlib.Graph>;
interface Box { x: number; y: number; width: number; height: number }

export function safeLayout(g: Graph): void {
  const box = (id: string) => g.node(id) as unknown as Box | undefined;
  for (const e of g.edges()) {
    if (e.v === e.w || !box(e.v)?.width || !box(e.w)?.width) g.removeEdge(e);
  }
  for (const id of g.nodes()) {
    if (!box(id)?.width) g.removeNode(id);
  }
  try {
    Dagre.layout(g);
  } catch {
    g.nodes().forEach((id, i) => {
      const n = box(id)!;
      n.x = (i % 6) * (n.width + 40) + n.width / 2;
      n.y = Math.floor(i / 6) * (n.height + 60) + n.height / 2;
    });
  }
}
