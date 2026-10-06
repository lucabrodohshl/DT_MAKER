import { Link } from 'react-router-dom';
import { EmptyState, Panel } from '@/design';
import { useAssetContext } from './AssetLayout';

/** Typed relationships (incoming and outgoing) and containment, as an accessible table. */
export default function AssetRelationshipsTab() {
  const { asset } = useAssetContext();
  return (
    <div className="grid-2">
      <Panel title="Typed relationships" subtitle="Operational relationships between asset instances" flush>
        {asset.relationships.length === 0 ? (
          <EmptyState compact title="No typed relationships" />
        ) : (
          <table className="vts-table">
            <caption className="sr-only">Relationships of {asset.name}</caption>
            <thead>
              <tr>
                <th scope="col">From</th>
                <th scope="col">Relationship</th>
                <th scope="col">To</th>
              </tr>
            </thead>
            <tbody>
              {asset.relationships.map((r) => (
                <tr key={r.id}>
                  <td>{r.sourceId === asset.id ? <strong>{asset.name}</strong> : <Link to={`/assets/${encodeURIComponent(r.sourceId)}`}>{r.other?.name ?? r.sourceId}</Link>}</td>
                  <td><span className="vts-tag">{r.type}</span></td>
                  <td>{r.targetId === asset.id ? <strong>{asset.name}</strong> : <Link to={`/assets/${encodeURIComponent(r.targetId)}`}>{r.other?.name ?? r.targetId}</Link>}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </Panel>
      <Panel title="Containment" subtitle="Parent and components">
        <div className="stack">
          <div>
            <span className="vts-label">Part of</span>
            <div className="small">
              {asset.ancestors.length === 0 ? (
                <span className="muted">Top-level asset</span>
              ) : (
                asset.ancestors.map((a, i) => (
                  <span key={a.id}>
                    {i > 0 && ' › '}
                    <Link to={`/assets/${encodeURIComponent(a.id)}`}>{a.name}</Link>
                  </span>
                ))
              )}
            </div>
          </div>
          <div>
            <span className="vts-label">Components ({asset.children.length})</span>
            {asset.children.length === 0 ? (
              <p className="small muted">None</p>
            ) : (
              <ul className="vts-list">
                {asset.children.map((c) => (
                  <li key={c.id} className="row-between small">
                    <Link to={`/assets/${encodeURIComponent(c.id)}`}>{c.name}</Link>
                    <span className="vts-tag">{c.type}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      </Panel>
    </div>
  );
}
