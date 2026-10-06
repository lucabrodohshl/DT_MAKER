/**
 * STUDIO mode: the authoring environment. Separate from operating twins: definitions are
 * created and changed here (artefact versions, change workspaces, verification, packages),
 * and "Back to Operate" returns to the twin the user came from.
 */
import { ArrowLeft, BookOpen, ClipboardCheck, FileClock, FileCode2, GitCompare, GitPullRequest, Home, Network, Package, Rocket, ShieldCheck, Workflow } from 'lucide-react';
import clsx from 'clsx';
import { Link, NavLink, Outlet, useSearchParams } from 'react-router-dom';
import { lastTwin, twinRoute } from '@/app/twinScope';
import '../twins/twins.css';

const NAV = [
  { group: '', items: [{ to: '/studio', label: 'Studio home', icon: Home, end: true }] },
  { group: 'Change', items: [
    { to: '/studio/changes', label: 'Changes', icon: GitPullRequest },
    { to: '/studio/impact', label: 'Impact analysis', icon: Network },
    { to: '/studio/history', label: 'Version history', icon: GitCompare },
  ] },
  { group: 'Definitions', items: [
    { to: '/studio/models', label: 'Models (PT / DT)', icon: Workflow },
    { to: '/studio/ontologies', label: 'Ontologies', icon: BookOpen },
    { to: '/studio/interpretations', label: 'Interpretations', icon: FileCode2 },
  ] },
  { group: 'Assurance & release', items: [
    { to: '/studio/verification', label: 'Verification', icon: ShieldCheck },
    { to: '/studio/packages', label: 'Packages', icon: Package },
    { to: '/studio/deployments', label: 'Deployments', icon: Rocket },
    { to: '/studio/audit', label: 'Engineering audit', icon: FileClock },
  ] },
];

export default function StudioLayout() {
  const [params] = useSearchParams();
  const back = params.get('twin') ?? lastTwin();
  return (
    <div className="vts-twin-ws">
      <aside className="vts-twin-side" aria-label="Studio navigation">
        <Link to={back ? twinRoute(back) : '/twins'} className="vts-twin-side__toggle" style={{ textDecoration: 'none' }}>
          <ArrowLeft size={16} aria-hidden="true" /> <span>Back to Operate</span>
        </Link>
        <nav className="vts-twin-nav" aria-label="Studio">
          {NAV.map((g) => (
            <div key={g.group || 'top'}>
              {g.group && <div className="vts-studio-nav__heading">{g.group}</div>}
              {g.items.map((i) => (
                <NavLink key={i.to} to={i.to} end={'end' in i ? i.end : false} className={({ isActive }) => clsx('vts-twin-nav__group', isActive && 'active')}>
                  <i.icon size={16} aria-hidden="true" />
                  <span>{i.label}</span>
                </NavLink>
              ))}
            </div>
          ))}
        </nav>
        <p className="xsmall subtle" style={{ margin: 'auto 8px 8px', display: 'flex', gap: 6 }}>
          <ClipboardCheck size={14} aria-hidden="true" style={{ flex: 'none' }} /> Studio changes definitions. Deployed versions stay immutable; every change is a new, verified version.
        </p>
      </aside>
      <div className="vts-twin-main">
        <Outlet />
      </div>
    </div>
  );
}
