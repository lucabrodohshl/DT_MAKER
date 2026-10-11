#!/usr/bin/env node
/**
 * Screenshots of the "Connect a real twin" tutorial (docs/studio/tutorial-connect-real-twin.md),
 * taken while the tutorial is carried out on a running Studio (STUDIO_URL, default
 * http://127.0.0.1:8080) on which the centrifugal-pump Blueprint is published and no instance
 * pump-p102-dt exists yet:
 *   1. the New instance dialog: data from the real system, REST and MQTT endpoints, the plant
 *      names of examples/real-twin/pump-p102.connection.json;
 *   2. Deploy now → the Plant connection page;
 *   3. the recorded PLC log (examples/real-twin/plc-p102.jsonl) replayed through the REST
 *      endpoint, exactly as the tutorial's curl command does;
 *   4. a deviation published through the MQTT endpoint (a minimal MQTT 3.1.1 client below),
 *      as the tutorial's mosquitto_pub command does.
 *
 *   STUDIO_URL=http://127.0.0.1:18480 PLAYWRIGHT_CHROMIUM=<chromium> node scripts/screenshots/real-twin.mjs
 */
import { createRequire } from 'node:module';
import { mkdirSync, readFileSync } from 'node:fs';
import net from 'node:net';
import { fileURLToPath } from 'node:url';

const require = createRequire(new URL('../../web/studio/package.json', import.meta.url));
const { chromium } = require('playwright');

const STUDIO = process.env.STUDIO_URL ?? 'http://127.0.0.1:8080';
const OUT = fileURLToPath(new URL('../../docs/screenshots/real-twin/', import.meta.url));
const EXAMPLE = fileURLToPath(new URL('../../examples/real-twin/', import.meta.url));
const INSTANCE = 'pump-p102-dt';
mkdirSync(OUT, { recursive: true });

const plan = JSON.parse(readFileSync(`${EXAMPLE}pump-p102.connection.json`, 'utf8'));

async function settle(page, ms = 1500) {
  await page.waitForLoadState('networkidle').catch(() => undefined);
  await page.waitForTimeout(ms);
}

async function shot(page, path, name, waitText) {
  if (path) await page.goto(`${STUDIO}${path}`, { waitUntil: 'domcontentloaded' });
  if (waitText) await page.getByText(waitText).first().waitFor({ timeout: 60_000 }).catch(() => undefined);
  await settle(page);
  await page.screenshot({ path: `${OUT}${name}` });
  console.log(`captured real-twin/${name}`);
}

/** Publish one message with QoS 1 to an MQTT 3.1.1 server and wait for its PUBACK. */
function mqttPublish({ host, port, username, password, topic, payload }) {
  const str = (s) => {
    const b = Buffer.from(s, 'utf8');
    return Buffer.concat([Buffer.from([b.length >> 8, b.length & 255]), b]);
  };
  const packet = (type, flags, body) => {
    const len = [];
    let n = body.length;
    do {
      let d = n % 128;
      n = Math.floor(n / 128);
      if (n > 0) d |= 128;
      len.push(d);
    } while (n > 0);
    return Buffer.concat([Buffer.from([(type << 4) | flags, ...len]), body]);
  };
  const connect = packet(1, 0, Buffer.concat([str('MQTT'), Buffer.from([4, 0xc2, 0, 30]), str('plc-p102-gateway'), str(username), str(password)]));
  const publish = packet(3, 2, Buffer.concat([str(topic), Buffer.from([0, 1]), Buffer.from(payload, 'utf8')]));
  return new Promise((resolve, reject) => {
    const s = net.connect(port, host);
    let got = Buffer.alloc(0);
    s.on('connect', () => s.write(connect));
    s.on('data', (d) => {
      got = Buffer.concat([got, d]);
      if (got.length >= 4 && got[0] === 0x20) {
        if (got[3] !== 0) return reject(new Error(`CONNACK return code ${got[3]}`));
        if (got.length === 4) s.write(publish);
      }
      if (got.length >= 8 && got[4] === 0x40) {
        s.end(Buffer.from([0xe0, 0]));
        resolve();
      }
    });
    s.on('error', reject);
    setTimeout(() => reject(new Error('MQTT timeout')), 10_000);
  });
}

const browser = await chromium.launch(process.env.PLAYWRIGHT_CHROMIUM ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM } : {});
const context = await browser.newContext({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1, colorScheme: 'light', locale: 'en-US', timezoneId: 'UTC' });
const page = await context.newPage();
try {
  await shot(page, '/studio/blueprints/centrifugal-pump/v/1/build/data', '01-blueprint-data-sources.png', 'Telemetry');

  // Step 2: the New instance dialog with the plant connection and the plant names.
  await page.goto(`${STUDIO}/studio/blueprints/centrifugal-pump/v/1/release/instances?new=1`, { waitUntil: 'domcontentloaded' });
  await page.getByText('Data comes from').waitFor({ timeout: 60_000 });
  await page.getByPlaceholder('pump-p102-dt').fill(INSTANCE);
  await page.getByPlaceholder('Pump P-102').fill('Pump P-102');
  await page.getByLabel('The real system (plant connection)').check();
  await page.getByLabel('Timestamp field (optional)').fill(plan.timestamp.field);
  await page.getByText('Plant names').click();
  for (const [id, field] of Object.entries(plan.names.telemetry)) await page.getByLabel(`Plant field of ${id}`).fill(field);
  for (const [id, codes] of Object.entries(plan.names.events)) await page.getByLabel(`Plant codes of ${id}`).fill(codes.join(', '));
  await page.getByText('Data comes from').scrollIntoViewIfNeeded();
  await shot(page, null, '02-new-instance-plant-connection.png');
  await page.getByRole('button', { name: 'Create instance' }).click();
  await shot(page, null, '03-instance-created.png', 'Access token');

  // Step 3: Deploy now opens the Plant connection page.
  await page.getByRole('button', { name: 'Deploy now' }).click();
  await page.waitForURL(`**/twins/${INSTANCE}/connection`, { timeout: 60_000 });
  await shot(page, null, '04-plant-connection-page.png', 'WAITING FOR DATA');

  // Step 4: the tutorial's commands, in its order: one message over REST, one over MQTT, then the
  // recorded PLC log through the REST endpoint.
  const conn = await (await fetch(`${STUDIO}/api/v1/instances/${INSTANCE}/connection`)).json();
  const restUrl = conn.rest.url.replace(/^http:\/\/[^:/]+/, 'http://127.0.0.1');
  const one = await fetch(restUrl, { method: 'POST', headers: { authorization: `Bearer ${conn.token}`, 'content-type': 'application/json' }, body: '{"TT101_degC": 40.1, "running": false}' });
  if (one.status !== 200) throw new Error(`one message: ${one.status} ${await one.text()}`);
  await mqttPublish({ host: '127.0.0.1', port: conn.mqtt.port, username: INSTANCE, password: conn.token, topic: conn.mqtt.topic, payload: '{"TT101_degC": 40.3, "running": false}' });
  const replay = await fetch(restUrl, {
    method: 'POST',
    headers: { authorization: `Bearer ${conn.token}` },
    body: readFileSync(`${EXAMPLE}plc-p102.jsonl`, 'utf8'),
  });
  const replayed = await replay.json();
  if (replay.status !== 200 || replayed.summary.eventsAccepted !== 7) throw new Error(`replay: ${replay.status} ${JSON.stringify(replayed.summary)}`);
  console.log('replayed:', JSON.stringify(replayed.summary));
  await shot(page, `/twins/${INSTANCE}/connection`, '05-plant-connection-receiving.png', 'RECEIVING DATA');
  await shot(page, `/twins/${INSTANCE}/operations/telemetry`, '06-live-telemetry.png');
  await shot(page, `/twins/${INSTANCE}/behavior/monitors`, '07-monitors.png');
  await shot(page, `/twins/${INSTANCE}/audit/ledger`, '08-ledger.png');

  // A deviation through the MQTT endpoint: cooling reported done while the pump is stopped.
  await mqttPublish({ host: '127.0.0.1', port: conn.mqtt.port, username: INSTANCE, password: conn.token, topic: conn.mqtt.topic, payload: '{"event": "AUX_COOLING_DONE"}' });
  await page.waitForTimeout(1000);
  const after = await (await fetch(`${STUDIO}/api/v1/instances/${INSTANCE}/connection`)).json();
  if (after.status?.lastEvent?.accepted !== false) throw new Error(`expected a rejection, got ${JSON.stringify(after.status?.lastEvent)}`);
  await shot(page, `/twins/${INSTANCE}/behavior/conformance`, '09-conformance-deviation.png');
  await shot(page, `/twins/${INSTANCE}/behavior/monitors`, '10-monitors-after-deviation.png');
} finally {
  await browser.close();
}
