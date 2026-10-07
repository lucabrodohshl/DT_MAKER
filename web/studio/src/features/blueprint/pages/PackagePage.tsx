/**
 * Release → Package: the release readiness gate, then the two release artefacts, kept apart on
 * purpose — the VERIFIED CORE PACKAGE (the pinned formal artefacts, the compiled Twin IR and
 * the evidence that covers them: formally verified) and the DEPLOYMENT BUNDLE (structure, world,
 * data contract, connectivity, presentation, scenarios, simulator inputs: versioned and
 * hash-protected, not formally verified) — the impact of this version against the previous one,
 * and publishing. The gate, the build and publishing are the backend's; publishing is refused
 * while any blocking item does not pass.
 */
import { Download, FileArchive, Lock, PackageCheck, Rocket, ShieldCheck } from 'lucide-react';
import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { blueprintApi, blueprintRoute, useBlueprintImpact, useBlueprintMutation, useBlueprintPackage, useBlueprintStatus } from '@/api/blueprints';
import { Button, Callout, Dialog, ErrorBlock, HashChip, KeyValue, Skeleton, StatusBadge, TimeStamp, downloadText } from '@/design';
import { RefLink } from '@/features/common/links';
import { RunCheckButton } from '../BlueprintWorkspace';
import { useEditor } from '../editor';
import { ReleaseVerdictBadge } from '../status';
import { EdPage, Pane } from '../ui';
import { GateList } from './OverviewPage';

const CLASS_TONE: Record<string, 'formal' | 'info' | 'neutral' | 'warning'> = { formal: 'formal', 'verified-core': 'formal', deployment: 'info', presentation: 'neutral', tests: 'warning' };

function ImpactPanel() {
  const e = useEditor();
  const q = useBlueprintImpact(e.id, e.version);
  if (q.isPending) return <Skeleton lines={3} />;
  if (q.isError) return <ErrorBlock error={q.error} compact />;
  const im = q.data;
  if (!im.against) return <p className="small muted">{im.summary}</p>;
  return (
    <div className="stack-sm">
      <p className="small" style={{ margin: 0 }}>
        Compared with v{im.against}: {im.summary}
      </p>
      {im.formal && (
        <div className="row-wrap">
          {im.formal.alignmentStale && <StatusBadge tone="warning" label="Alignment evidence must be re-run" />}
          {im.formal.irStale && <StatusBadge tone="warning" label="Twin IR must be recompiled" />}
          {im.formal.packageStale && <StatusBadge tone="warning" label="Verified Core Package must be rebuilt" />}
          {im.formal.bundleStale && <StatusBadge tone="info" label="Deployment Bundle must be rebuilt" />}
        </div>
      )}
      {im.sections.length > 0 && (
        <table className="vts-table">
          <caption className="sr-only">Changed sections and their consequences</caption>
          <thead>
            <tr>
              <th scope="col">Changed</th>
              <th scope="col">Affects</th>
              <th scope="col">Consequence</th>
            </tr>
          </thead>
          <tbody>
            {im.sections.map((s, i) => (
              <tr key={`${s.section}-${i}`}>
                <td className="small">{s.section}</td>
                <td>
                  <StatusBadge tone={CLASS_TONE[s.classification] ?? 'neutral'} label={s.classification === 'verified-core' || s.classification === 'formal' ? 'Verified core' : s.classification === 'deployment' ? 'Deployment bundle' : s.classification === 'tests' ? 'Tests' : 'Presentation only'} />
                </td>
                <td className="xsmall muted">{s.consequences.join(' ')}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
      {(im.instances ?? []).length > 0 && (
        <p className="xsmall subtle" style={{ margin: 0 }}>
          Instances on earlier versions: {(im.instances ?? []).map((i) => `${i.name} (v${i.version ?? '?'})`).join(', ')} — they keep running their version until upgraded.
        </p>
      )}
    </div>
  );
}

export default function PackagePage() {
  const e = useEditor();
  const navigate = useNavigate();
  const status = useBlueprintStatus(e.id, e.version);
  const pkg = useBlueprintPackage(e.id, e.version);
  const [confirm, setConfirm] = useState(false);
  const publish = useBlueprintMutation(() => blueprintApi.publish(e.id, e.version));
  const base = blueprintRoute(e.id, e.version);
  const readiness = status.data?.readiness;
  const p = pkg.data?.package ?? null;
  const b = pkg.data?.bundle ?? null;
  const scope = pkg.data?.verificationScope;
  const published = e.detail.state === 'published';
  return (
    <EdPage
      title="Package & release"
      wide
      description="The release gate, the Verified Core Package and the Deployment Bundle of this version."
      actions={
        <>
          {e.editable && <RunCheckButton check="package" label={p ? 'Rebuild package & bundle' : 'Build package & bundle'} />}
          {b && (
            <Button
              size="sm"
              variant="ghost"
              icon={<Download size={14} />}
              onClick={async () => {
                const x = await blueprintApi.exportBundle(e.id, e.version);
                downloadText(`${e.id}-v${e.version}-bundle.json`, 'application/json', JSON.stringify(x, null, 2));
              }}
            >
              Export
            </Button>
          )}
          {e.editable && (
            <Button size="sm" variant="primary" icon={<Rocket size={14} />} disabled={readiness?.verdict !== 'ready'} onClick={() => setConfirm(true)} title={readiness?.verdict !== 'ready' ? 'The release gate must pass first' : undefined}>
              Publish v{e.version}
            </Button>
          )}
        </>
      }
      guide={
        <>
          Only the <strong>Verified Core Package</strong> is covered by the formal evidence (alignment, compilation, validation). The <strong>Deployment Bundle</strong> carries everything else an instance needs; it is versioned and
          hash-protected, not formally verified. Build both when the gate passes everything else, then publish: published versions are immutable and can be instantiated.
        </>
      }
    >
      {status.isPending ? (
        <Skeleton lines={5} />
      ) : status.isError ? (
        <ErrorBlock error={status.error} onRetry={() => void status.refetch()} />
      ) : (
        <Pane
          title={
            <span className="row">
              <ShieldCheck size={14} aria-hidden="true" /> Release readiness
            </span>
          }
          actions={<ReleaseVerdictBadge verdict={readiness?.verdict} size="lg" />}
        >
          <div className="stack-sm">
            {published ? (
              <Callout tone="ok" title={`v${e.version} is published`}>
                Published <TimeStamp value={e.detail.publishedAt ?? e.detail.updatedAt} relative />. It never changes; create instances from it in{' '}
                <Link to={`${base}/release/instances`}>Release → Instances</Link>.
              </Callout>
            ) : readiness?.verdict === 'ready' ? (
              <Callout tone="ok" title="READY TO RELEASE">
                Every blocking item passes for exactly these inputs. Publishing makes v{e.version} immutable.
              </Callout>
            ) : (
              <Callout tone="warning" title={`RELEASE BLOCKED — ${readiness?.blockers.length ?? 0} blocker(s)`}>
                {readiness?.readyToPackage ? 'Everything but the package passes: build the package and bundle.' : 'Fix the items below; each links to the editor or check that resolves it.'}
              </Callout>
            )}
            {readiness && <GateList items={readiness.items} base={base} />}
          </div>
        </Pane>
      )}
      {pkg.isError && <ErrorBlock error={pkg.error} />}
      <div className="vts-ed-split vts-ed-split--two" style={{ gridTemplateColumns: 'minmax(0, 1fr) minmax(0, 1fr)' }}>
        <Pane
          title={
            <span className="row">
              <PackageCheck size={14} aria-hidden="true" /> Verified Core Package
            </span>
          }
          actions={<StatusBadge tone="formal" icon={ShieldCheck} label="Formally verified scope" />}
        >
          <div className="stack-sm">
            {p ? (
              <>
                <KeyValue
                  compact
                  items={[
                    ['Package', <Link key="p" to={`/studio/packages/${encodeURIComponent(p.id)}`} className="mono small">{p.id}</Link>],
                    ['Package hash', <HashChip key="h" value={p.packageHash} label="package hash" />],
                    ['Twin IR', <HashChip key="i" value={p.irSha256} label="IR sha256" />],
                    ['Model version', <span key="m" className="mono small">{p.modelVersion}</span>],
                    ['State', <StatusBadge key="s" tone={p.state === 'released' ? 'ok' : 'info'} icon={p.state === 'released' ? Lock : undefined} label={p.state === 'released' ? 'RELEASED' : 'BUILT'} />],
                    ['Built', <TimeStamp key="t" value={p.createdAt} relative />],
                    ['Build evidence', p.evidenceId ? <Link key="e" to={`/studio/verification/${encodeURIComponent(p.evidenceId)}`} className="mono xsmall">{p.evidenceId}</Link> : '—'],
                  ]}
                />
                {pkg.data?.integrity && (
                  <div className="stack-sm">
                    <StatusBadge tone={pkg.data.integrity.integrity === 'pass' ? 'ok' : 'critical'} label={pkg.data.integrity.integrity === 'pass' ? `Integrity verified (${pkg.data.integrity.checks.length} checks)` : `Integrity FAILED: ${pkg.data.integrity.firstFailure}`} />
                    <details className="xsmall">
                      <summary>Integrity checks</summary>
                      <ul className="vts-findings">
                        {pkg.data.integrity.checks.map((c) => (
                          <li key={c.name}>
                            <span style={{ color: c.passed ? 'var(--ok)' : 'var(--crit)' }}>{c.passed ? '✓' : '✗'}</span>
                            <span>
                              {c.name} <span className="subtle">{c.detail}</span>
                            </span>
                          </li>
                        ))}
                      </ul>
                    </details>
                  </div>
                )}
              </>
            ) : (
              <p className="small muted" style={{ margin: 0 }}>
                Not built yet. {readiness?.readyToPackage ? 'The gate allows building it now.' : 'It can be built once every other gate item passes.'}
              </p>
            )}
            <table className="vts-table">
              <caption className="small" style={{ textAlign: 'left' }}>
                Pinned formal inputs
              </caption>
              <tbody>
                {(pkg.data?.verifiedCoreInputs ?? []).map((i) => (
                  <tr key={i.role}>
                    <td className="small">{i.role.replace(/_/g, ' ')}</td>
                    <td>{i.ref ? <RefLink refId={i.ref} /> : <span style={{ color: 'var(--crit)' }}>not defined</span>}</td>
                  </tr>
                ))}
              </tbody>
            </table>
            {scope && (
              <ul className="xsmall muted" style={{ margin: 0, paddingLeft: 16 }}>
                {scope.formallyVerified.map((x) => (
                  <li key={x}>{x}</li>
                ))}
              </ul>
            )}
          </div>
        </Pane>
        <Pane
          title={
            <span className="row">
              <FileArchive size={14} aria-hidden="true" /> Deployment Bundle
            </span>
          }
          actions={<StatusBadge tone="info" icon={Lock} label="Integrity-protected, not formally verified" />}
        >
          <div className="stack-sm">
            {b ? (
              <>
                <KeyValue
                  compact
                  items={[
                    ['Bundle', <span key="b" className="mono small">{b.id}</span>],
                    ['Bundle hash', <HashChip key="h" value={b.bundleHash} label="bundle hash" />],
                    ['Integrity', <StatusBadge key="i" tone={b.intact === false || b.filesIntact === false ? 'critical' : 'ok'} label={b.intact === false || b.filesIntact === false ? 'MODIFIED — does not match its hash' : 'Intact'} />],
                    ['Verified core', <span key="c" className="mono xsmall">{b.packageId}</span>],
                    ['Built', <TimeStamp key="t" value={b.createdAt} relative />],
                  ]}
                />
                {b.manifest && (
                  <table className="vts-table">
                    <caption className="small" style={{ textAlign: 'left' }}>
                      Files
                    </caption>
                    <thead>
                      <tr>
                        <th scope="col">File</th>
                        <th scope="col">Role</th>
                        <th scope="col">sha256</th>
                        <th scope="col">Size</th>
                      </tr>
                    </thead>
                    <tbody>
                      {b.manifest.files.map((f) => (
                        <tr key={f.path}>
                          <td className="mono xsmall">{f.path}</td>
                          <td className="xsmall">{f.role.replace(/_/g, ' ')}</td>
                          <td>
                            <HashChip value={f.sha256} length={10} label={`${f.path} sha256`} />
                          </td>
                          <td className="xsmall num">{(f.size / 1024).toFixed(1)} KiB</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                )}
              </>
            ) : (
              <p className="small muted" style={{ margin: 0 }}>
                Built together with the Verified Core Package.
              </p>
            )}
            <table className="vts-table">
              <caption className="small" style={{ textAlign: 'left' }}>
                Bundle content of this version
              </caption>
              <tbody>
                {(pkg.data?.deploymentContent ?? []).map((c) => (
                  <tr key={c.section}>
                    <td className="small">{c.section}</td>
                    <td>
                      <HashChip value={c.sha256} length={10} label={`${c.section} sha256`} />
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            {scope && (
              <ul className="xsmall muted" style={{ margin: 0, paddingLeft: 16 }}>
                {scope.integrityOnly.map((x) => (
                  <li key={x}>{x}</li>
                ))}
              </ul>
            )}
          </div>
        </Pane>
      </div>
      <Pane title="Impact of this version">
        <ImpactPanel />
      </Pane>
      <Dialog
        open={confirm}
        onOpenChange={setConfirm}
        title={`Publish ${e.doc.identity.name} v${e.version}?`}
        description="Publishing is permanent."
        footer={
          <>
            <Button onClick={() => setConfirm(false)}>Cancel</Button>
            <Button
              variant="primary"
              icon={<Rocket size={14} />}
              loading={publish.isPending}
              onClick={() =>
                publish.mutate(undefined, {
                  onSuccess: () => {
                    setConfirm(false);
                    navigate(blueprintRoute(e.id, e.version, 'release/instances'));
                  },
                })
              }
            >
              Publish
            </Button>
          </>
        }
      >
        <div className="stack-sm small">
          <ul style={{ margin: 0, paddingLeft: 16 }}>
            <li>v{e.version} becomes read-only; every later change goes into a new draft.</li>
            <li>The pinned formal artefact drafts are published with it, and the package is marked released.</li>
            <li>Instances can be created from it, and instances of earlier versions can be upgraded to it.</li>
          </ul>
          {publish.isError && <ErrorBlock error={publish.error} compact />}
        </div>
      </Dialog>
    </EdPage>
  );
}
