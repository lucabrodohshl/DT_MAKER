/** /studio/blueprints/:bpId → the version to work on: the open draft, else the latest version. */
import { Navigate, useParams } from 'react-router-dom';
import { blueprintRoute, useBlueprint } from '@/api/blueprints';
import { QueryState } from '@/design';

export default function BlueprintRedirect() {
  const { bpId } = useParams();
  const q = useBlueprint(bpId);
  return (
    <div className="vts-page">
      <QueryState query={q}>
        {(b) => {
          const target = b.versions.find((v) => v.state === 'draft') ?? b.versions[0];
          return target ? <Navigate replace to={blueprintRoute(b.id, target.version)} /> : <p>This Blueprint has no version.</p>;
        }}
      </QueryState>
    </div>
  );
}
