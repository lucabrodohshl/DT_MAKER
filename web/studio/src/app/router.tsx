/**
 * Information architecture: choose a twin → inspect / operate it → open Studio to change it.
 *
 *   /twins                                        Your twins (landing page)
 *   /twins/{twinId}                               Twin overview
 *   /twins/{twinId}/assets[/{assetId}/...]        assets of the twin
 *   /twins/{twinId}/knowledge                     knowledge graph around the twin
 *   /twins/{twinId}/operations/{live|telemetry|events}
 *   /twins/{twinId}/behavior[/model|/facts|/conformance]
 *   /twins/{twinId}/predict/{what-if|predictions|planning}
 *   /twins/{twinId}/audit[/provenance|/replay|/ledger|/executions/{session}[/replay]]
 *   /twins/{twinId}/engineering[/models|/ontology|/interpretations|/verification|/package|/deployment]
 *   /twins/{twinId}/maintenance[/impact|/versions|/readiness|/rollback]
 *   /twins/{twinId}/admin/{data-sources|runtime|storage|logs}
 *   /studio[/changes/{id}|/refinement/{id}|/impact|/history|/{models|ontologies|interpretations}/{id}[/versions/{v}]
 *           |/verification[/{id}]|/packages[/{id}]|/deployments|/audit]
 *
 * Earlier URLs (/operations/..., /behavior, /engineering/..., /maintenance/..., /audit/...,
 * /assets/{id}) redirect to their place in this structure.
 */
import { lazy, Suspense, type ComponentType, type ReactNode } from 'react';
import { createBrowserRouter, Navigate, type RouteObject } from 'react-router-dom';
import { Skeleton } from '@/design';
import { Shell } from './Shell';
import { NotFound } from './NotFound';
import { ArtifactRedirect } from '@/features/common/links';
import { AssetGate, GraphGate, ToStudio, ToTwin, ToTwinExecution } from './redirects';

type Loader = () => Promise<{ default: ComponentType<Record<string, unknown>> }>;

function page(loader: Loader, props: Record<string, unknown> = {}) {
  const C = lazy(loader);
  return (
    <Suspense
      fallback={
        <div className="vts-page" role="status" aria-label="Loading page">
          <Skeleton lines={5} />
        </div>
      }
    >
      <C {...props} />
    </Suspense>
  );
}

/** Lazy loader of a named export. */
const named = (load: () => Promise<Record<string, unknown>>, name: string): Loader => () =>
  load().then((m) => ({ default: m[name] as ComponentType<Record<string, unknown>> }));
const twinPages = () => import('@/features/twins/TwinPages');

const P: Record<string, Loader> = {
  library: () => import('@/features/twins/TwinLibraryPage') as never,
  twin: () => import('@/features/twins/TwinLayout') as never,
  twinOverview: () => import('@/features/twins/TwinOverviewPage') as never,
  twinAssets: () => import('@/features/twins/TwinAssetsPage') as never,
  twinLive: named(twinPages, 'TwinLivePage'),
  twinEngineering: named(twinPages, 'TwinEngineeringPage'),
  twinOntology: named(twinPages, 'TwinOntologyPage'),
  twinInterpretations: named(twinPages, 'TwinInterpretationsPage'),
  twinVerification: named(twinPages, 'TwinVerificationPage'),
  twinPackage: named(twinPages, 'TwinPackagePage'),
  twinReadiness: named(twinPages, 'TwinReadinessPage'),
  twinReplay: named(twinPages, 'TwinReplayPage'),
  twinData: named(twinPages, 'TwinDataSourcesPage'),
  twinRuntime: named(twinPages, 'TwinRuntimeHealthPage'),
  twinStorage: named(twinPages, 'TwinStoragePage'),
  studio: () => import('@/features/studio/StudioLayout') as never,
  studioHome: () => import('@/features/studio/StudioHomePage') as never,
  whatIf: () => import('@/features/predict/WhatIfPage') as never,
  models: () => import('@/features/twins/TwinModelsPage') as never,
  asset: () => import('@/features/assets/AssetLayout'),
  assetOverview: () => import('@/features/assets/AssetOverviewTab'),
  assetTelemetry: () => import('@/features/telemetry/AssetTelemetryTab'),
  assetBehavior: () => import('@/features/behavior/AssetBehaviorTab'),
  assetPredict: () => import('@/features/predict/AssetPredictTab'),
  assetHistory: () => import('@/features/audit/AssetHistoryTab'),
  assetRelationships: () => import('@/features/assets/AssetRelationshipsTab'),
  assetPlugin: () => import('@/features/assets/AssetPluginTab'),
  graph: () => import('@/features/graph/KnowledgeGraphPage'),
  events: () => import('@/features/operations/EventsPage'),
  audit: () => import('@/features/audit/AuditTimelinePage'),
  execution: () => import('@/features/audit/ExecutionPage'),
  replay: () => import('@/features/audit/ReplayPage'),
  ledger: () => import('@/features/audit/LedgerPage'),
  provenance: () => import('@/features/audit/ProvenancePage'),
  engAudit: () => import('@/features/audit/EngineeringAuditPage'),
  artifactList: () => import('@/features/engineering/ArtifactListPage'),
  artifact: () => import('@/features/engineering/ArtifactPage'),
  version: () => import('@/features/engineering/VersionPage'),
  evidenceList: () => import('@/features/engineering/EvidenceListPage'),
  evidence: () => import('@/features/engineering/EvidencePage'),
  packages: () => import('@/features/engineering/PackagesPage'),
  pkg: () => import('@/features/engineering/PackagePage'),
  deployments: () => import('@/features/engineering/DeploymentsPage'),
  changes: () => import('@/features/maintenance/ChangesPage'),
  change: () => import('@/features/maintenance/ChangePage'),
  refinement: () => import('@/features/maintenance/RefinementPage'),
  impact: () => import('@/features/maintenance/ImpactPage'),
  history: () => import('@/features/maintenance/HistoryPage'),
  rollback: () => import('@/features/maintenance/RollbackPage'),
  logs: () => import('@/features/admin/LogsPage'),
  about: () => import('@/features/admin/AboutPage'),
};

const r = (path: string, element: ReactNode, children?: RouteObject[]): RouteObject => ({ path, element, children });

const assetTabs: RouteObject[] = [
  { index: true, element: page(P.assetOverview!) },
  r('telemetry', page(P.assetTelemetry!)),
  r('behavior', page(P.assetBehavior!)),
  r('predict', page(P.assetPredict!)),
  r('history', page(P.assetHistory!)),
  r('relationships', page(P.assetRelationships!)),
  r('view/:pluginId', page(P.assetPlugin!)),
];

const twinChildren: RouteObject[] = [
  { index: true, element: page(P.twinOverview!) },
  r('assets', page(P.twinAssets!)),
  r('assets/:assetId', page(P.asset!), assetTabs),
  r('knowledge', page(P.graph!)),
  r('operations', <Navigate to="live" replace />),
  r('operations/live', page(P.twinLive!)),
  r('operations/telemetry', page(P.assetTelemetry!)),
  r('operations/events', page(P.events!)),
  r('behavior', page(P.assetBehavior!, { view: 'state' })),
  r('behavior/model', page(P.models!, { behaviourOnly: true })),
  r('behavior/facts', page(P.assetBehavior!, { view: 'facts' })),
  r('behavior/conformance', page(P.assetBehavior!, { view: 'conformance' })),
  r('predict', <Navigate to="what-if" replace />),
  r('predict/what-if', page(P.whatIf!)),
  r('predict/predictions', page(P.assetPredict!, { view: 'future' })),
  r('predict/planning', page(P.assetPredict!, { view: 'planning' })),
  r('audit', page(P.audit!)),
  r('audit/provenance', page(P.provenance!)),
  r('audit/replay', page(P.twinReplay!)),
  r('audit/ledger', page(P.ledger!)),
  r('audit/executions/:session', page(P.execution!)),
  r('audit/executions/:session/replay', page(P.replay!)),
  r('engineering', page(P.twinEngineering!)),
  r('engineering/models', page(P.models!)),
  r('engineering/ontology', page(P.twinOntology!)),
  r('engineering/interpretations', page(P.twinInterpretations!)),
  r('engineering/verification', page(P.twinVerification!)),
  r('engineering/package', page(P.twinPackage!)),
  r('engineering/deployment', page(P.deployments!)),
  r('maintenance', page(P.changes!)),
  r('maintenance/impact', page(P.impact!)),
  r('maintenance/versions', page(P.history!)),
  r('maintenance/readiness', page(P.twinReadiness!)),
  r('maintenance/rollback', page(P.rollback!)),
  r('admin', <Navigate to="data-sources" replace />),
  r('admin/data-sources', page(P.twinData!)),
  r('admin/runtime', page(P.twinRuntime!)),
  r('admin/storage', page(P.twinStorage!)),
  r('admin/logs', page(P.logs!)),
  r('*', <NotFound />),
];

const studioChildren: RouteObject[] = [
  { index: true, element: page(P.studioHome!) },
  r('changes', page(P.changes!)),
  r('changes/:changeId', page(P.change!)),
  r('refinement/:evidenceId', page(P.refinement!)),
  r('impact', page(P.impact!)),
  r('history', page(P.history!)),
  ...(['models', 'ontologies', 'interpretations'] as const).flatMap((k) => [
    r(k, page(P.artifactList!)),
    r(`${k}/:artifactId`, page(P.artifact!)),
    r(`${k}/:artifactId/versions/:version`, page(P.version!)),
  ]),
  r('artifacts/:artifactId', <ArtifactRedirect />),
  r('artifacts/:artifactId/versions/:version', <ArtifactRedirect />),
  r('verification', page(P.evidenceList!)),
  r('alignment', page(P.evidenceList!)),
  r('verification/:evidenceId', page(P.evidence!)),
  r('packages', page(P.packages!)),
  r('packages/:packageId', page(P.pkg!)),
  r('deployments', page(P.deployments!)),
  r('audit', page(P.engAudit!)),
  r('*', <NotFound />),
];

export const routes: RouteObject[] = [
  {
    path: '/',
    element: <Shell />,
    errorElement: <NotFound />,
    children: [
      { index: true, element: <Navigate to="/twins" replace /> },
      r('twins', page(P.library!)),
      r('twins/:twinId', page(P.twin!), twinChildren),
      r('studio', page(P.studio!), studioChildren),

      // Outside any twin: assets that belong to no twin, the estate-wide graph, platform pages.
      r('assets/:assetId', <AssetGate>{page(P.asset!)}</AssetGate>, assetTabs),
      r('graph', <GraphGate>{page(P.graph!)}</GraphGate>),
      r('admin/logs', page(P.logs!)),
      r('about', page(P.about!)),

      // Earlier URLs.
      r('assets', <Navigate to="/twins" replace />),
      r('operations', <ToTwin sub="operations/live" />),
      r('operations/live', <ToTwin sub="operations/live" />),
      r('operations/telemetry', <ToTwin sub="operations/telemetry" />),
      r('operations/events', <ToTwin sub="operations/events" />),
      r('behavior', <ToTwin sub="behavior" />),
      r('predict', <ToTwin sub="predict/what-if" />),
      r('audit', <ToTwin sub="audit" />),
      r('audit/ledger', <ToTwin sub="audit/ledger" />),
      r('audit/provenance', <ToTwin sub="audit/provenance" />),
      r('audit/engineering', <Navigate to="/studio/audit" replace />),
      r('audit/executions/:twinId/:session', <ToTwinExecution />),
      r('audit/executions/:twinId/:session/replay', <ToTwinExecution replay />),
      r('engineering', <Navigate to="/studio" replace />),
      r('engineering/*', <ToStudio from="/engineering" />),
      r('maintenance', <Navigate to="/studio/changes" replace />),
      r('maintenance/rollback', <ToTwin sub="maintenance/rollback" />),
      r('maintenance/*', <ToStudio from="/maintenance" />),
      r('*', <NotFound />),
    ],
  },
];

export const createAppRouter = () => createBrowserRouter(routes);
