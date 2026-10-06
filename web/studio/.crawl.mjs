import { chromium } from '@playwright/test';
const b = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_CHROMIUM });
const p = await b.newPage({ viewport: { width: 1440, height: 900 } });
let errs = [];
p.on('pageerror', e => errs.push('PAGEERROR ' + e.message.slice(0, 160)));
p.on('console', m => m.type() === 'error' && errs.push('CONSOLE ' + m.text().split('\n')[0].slice(0, 160)));
const subs = ['', 'assets', 'knowledge', 'operations/live', 'operations/telemetry', 'operations/events', 'behavior', 'behavior/model', 'behavior/facts', 'behavior/conformance', 'predict/what-if', 'predict/predictions', 'predict/planning', 'audit', 'audit/provenance', 'audit/replay', 'audit/ledger', 'engineering', 'engineering/models', 'engineering/ontology', 'engineering/interpretations', 'engineering/verification', 'engineering/package', 'engineering/deployment', 'maintenance', 'maintenance/impact', 'maintenance/versions', 'maintenance/readiness', 'maintenance/rollback', 'admin/data-sources', 'admin/runtime', 'admin/storage', 'admin/logs'];
const urls = [];
for (const t of ['pump-p101-dt', 'indoor-drone-dt']) for (const s of subs) urls.push(`/twins/${t}/${s}`);
for (const s of ['', '/changes', '/impact', '/history', '/models', '/ontologies', '/interpretations', '/verification', '/packages', '/deployments', '/audit']) urls.push('/studio' + s);
urls.push('/twins', '/about', '/admin/logs');
for (const u of urls) {
  errs = [];
  await p.goto('http://127.0.0.1:8080' + u); await p.waitForTimeout(1800);
  const text = await p.locator('body').innerText();
  const crash = text.includes('This page failed to render');
  const bad = [...text.matchAll(/(Could not load this information|Page not found|Something went wrong)[^\n]*\n?[^\n]*/g)].map(m => m[0].replace(/\n/g, ' | ')).slice(0, 2);
  if (crash || bad.length || errs.length) console.log((crash ? 'CRASH ' : 'ISSUE ') + u + '\n   ' + [...bad, ...errs.slice(0, 3)].join('\n   '));
}
console.log('crawled', urls.length);
await b.close();
