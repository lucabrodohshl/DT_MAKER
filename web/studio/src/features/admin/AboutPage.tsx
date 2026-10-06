/** About: product and component versions, registered domain plugins, trust model notes. */
import { useAbout } from '@/api/queries';
import { HashChip, KeyValue, PageHeader, Panel, QueryState } from '@/design';
import { allPlugins } from '@/plugins/registry';

export default function AboutPage() {
  const about = useAbout();
  const plugins = allPlugins();
  return (
    <div className="vts-page">
      <PageHeader eyebrow="About" title="Verified Twin Studio" meta={<span>Digital Twin operations and engineering console for verified behavioural twins</span>} />
      <div className="grid-2">
        <Panel title="Components">
          <QueryState query={about}>
            {(a) => (
              <KeyValue
                items={[
                  ['Studio', a.version],
                  ['Twin compiler', a.compilerVersion],
                  ['Semantic kernel', `${a.kernelVersion} (${a.kernelCompat})`],
                  ['Twin IR format', a.irFormat],
                  ['Package format', a.packageFormat],
                  ['Ledger schema', a.ledgerSchema],
                  ['Semantic aligner', <span key="a">{a.aligner} <HashChip value={a.alignerDigest} label="aligner source digest" /></span>],
                  ['Ontology services', a.ontologyServices],
                  ['Ontology checker', a.ontologyChecker],
                  ['Server instance', <span key="e" className="mono small">{a.epoch}</span>],
                ]}
              />
            )}
          </QueryState>
        </Panel>
        <div className="stack">
          <Panel title="Domain plugins">
            {plugins.length === 0 ? (
              <p className="small muted">No domain plugin is installed; all generic views are available.</p>
            ) : (
              <ul className="vts-list">{plugins.map((p) => <li key={p.id}><strong>{p.title}</strong> <span className="mono xsmall">{p.id}</span><div className="small muted">{p.description}</div></li>)}</ul>
            )}
          </Panel>
          <Panel title="Where conclusions come from">
            <ul className="small" style={{ margin: 0, paddingLeft: 16 }}>
              <li>Behavioural state, admissible transitions, predictions, conformance and ledgers: the twin's verified runtime (semantic kernel).</li>
              <li>Validation, refinement (Def. 4), alignment preservation (Theorem 3) and meaning of observations: the formal ontology services (aligner + Z3).</li>
              <li>Alignment: the SemPTDTAlignmentICSE aligner. Compilation and packages: the twin toolchain.</li>
              <li>This interface renders those results; it never computes them.</li>
              <li>The name in the top bar only attributes audit records. It is not authentication.</li>
            </ul>
          </Panel>
        </div>
      </div>
    </div>
  );
}
