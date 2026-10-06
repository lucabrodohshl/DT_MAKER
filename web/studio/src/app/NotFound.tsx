import { Compass } from 'lucide-react';
import { Link, isRouteErrorResponse, useRouteError } from 'react-router-dom';
import { EmptyState } from '@/design';

export function NotFound() {
  const error = useRouteError();
  const crashed = error && !(isRouteErrorResponse(error) && error.status === 404);
  return (
    <div className="vts-page">
      <EmptyState
        icon={<Compass size={32} />}
        title={crashed ? 'This page failed to render' : 'Page not found'}
        action={<Link to="/">Go to the overview</Link>}
      >
        {crashed ? (
          <p>An unexpected error occurred in the interface. Reload the page; if it persists, the system log has details.</p>
        ) : (
          <p>The address does not match any page. It may refer to an entity that does not exist.</p>
        )}
      </EmptyState>
    </div>
  );
}
