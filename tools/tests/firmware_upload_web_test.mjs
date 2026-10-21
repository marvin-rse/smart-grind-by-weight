// Exercise the production firmware upload helpers from the embedded page.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const page = readFileSync(new URL('../../src/network/device_page.h', import.meta.url), 'utf8');
const script = page.split('<script>')[1].split('</script>')[0];
const helpers = script.slice(script.indexOf('const boardMarker='), script.indexOf('async function installFirmwareBlob('));

// The page must never contain the patterns it searches firmware images for.
for (const pattern of ['SMARTGRIND-BOARD:V', 'touch-amoled-164/', 'touch-amoled-164-v2/']) {
  assert.equal(page.includes(pattern), false, `page contains ${pattern}`);
}

function makeContext(statusReplies, log = {prepares: 0, steps: []}) {
  let clock = 0;
  const elements = new Map();
  const context = vm.createContext({
    $: id => { if (!elements.has(id)) elements.set(id, {textContent: ''}); return elements.get(id); },
    manualOta: {busy: false, message: ''},
    toast: () => {},
    performance: {now: () => clock},
    setTimeout: (resolve, ms) => { clock += ms; resolve(); },
    api: async url => {
      if (url.includes('/ota/prepare')) { log.prepares++; return {preparing: true}; }
      const reply = statusReplies.shift();
      if (!reply) throw new TypeError('Failed to fetch');
      return reply;
    },
    TextDecoder, Set,
  });
  vm.runInContext(helpers, context);
  return context;
}

const status = (uptime, build, active = false) => ({system: {uptime_ms: uptime}, firmware: {build}, ota: {active}});

// Lost HTTP reply, then the grinder comes back freshly booted with a new build.
let context = makeContext([null, null, status(2500, 6)]);
let result = await context.waitForRestart(5);
assert.deepEqual({...result}, {restarted: true, build: 6, changed: true});

// Same build after restart (re-installing the same file) is still a restart.
context = makeContext([status(1200, 5)]);
result = await context.waitForRestart(5);
assert.equal(result.restarted, true);
assert.equal(result.changed, false);

// Interrupted upload: the grinder stays up and no update is active.
context = makeContext([status(600000, 5), status(601000, 5), status(602000, 5), status(603000, 5), status(604000, 5)]);
result = await context.waitForRestart(5);
assert.deepEqual({...result}, {restarted: false, build: 5});

// No answer at all for 90 s.
context = makeContext([]);
result = await context.waitForRestart(5);
assert.deepEqual({...result}, {restarted: false, timeout: true});

// Board detection on synthetic images.
const blob = text => new Blob([Buffer.from(text, 'latin1')]);
context = makeContext([]);
context.Blob = Blob;
assert.equal(await context.detectFirmwareBoard(blob('\xe9 .pio/libdeps/waveshare-esp32s3-touch-amoled-164/x')), 'v1');
assert.equal(await context.detectFirmwareBoard(blob('\xe9 .pio/libdeps/waveshare-esp32s3-touch-amoled-164-v2/x')), 'v2');
assert.equal(await context.detectFirmwareBoard(blob('\xe9 touch-amoled-164/ SMARTGRIND-BOARD:V2\0')), 'v2');
assert.equal(await context.detectFirmwareBoard(blob('SMARTGRIND-BOARD:V1 SMARTGRIND-BOARD:V2')), '');
assert.equal(await context.detectFirmwareBoard(blob('\xe9 no markers here')), '');

// Preparing: Bluetooth off -> ready directly.
const prep = (uptime, ota) => ({system: {uptime_ms: uptime}, firmware: {build: 6},
  ota: {active: false, preparing: false, restarting: false, ready: false, ...ota}});
let log = {prepares: 0, steps: []};
context = makeContext([prep(400000, {preparing: true}), prep(400500, {ready: true})], log);
await context.prepareForUpdate(step => log.steps.push(step));
assert.equal(log.prepares, 1);

// Bluetooth on: the grinder restarts without Bluetooth, comes back idle, is prepared again.
log = {prepares: 0, steps: []};
context = makeContext([prep(400000, {preparing: true, restarting: true}), null, null,
  prep(3000, {}), prep(3500, {preparing: true}), prep(4000, {ready: true})], log);
await context.prepareForUpdate(step => log.steps.push(step));
assert.equal(log.prepares, 2);
assert.ok(log.steps.some(step => /without Bluetooth/.test(step)));
assert.ok(log.steps.some(step => /restarted/.test(step)));

// A restart noticed only through uptime (status never showed "restarting").
log = {prepares: 0, steps: []};
context = makeContext([prep(400000, {preparing: true}), prep(2000, {}), prep(2500, {ready: true})], log);
await context.prepareForUpdate(() => {});
assert.equal(log.prepares, 2);

// Restarting a second time is reported instead of looping.
context = makeContext([prep(400000, {restarting: true, preparing: true}), prep(1000, {}),
  prep(1500, {restarting: true, preparing: true}), prep(900, {})]);
await assert.rejects(context.prepareForUpdate(() => {}), /restarted again/);

// Not enough memory: preparation ends without becoming ready.
context = makeContext([prep(400000, {preparing: true}), ...Array.from({length: 10}, (_, i) => prep(401000 + i * 500, {}))]);
await assert.rejects(context.prepareForUpdate(() => {}), /free enough memory/);

// No answer at all for 90 s.
context = makeContext([]);
await assert.rejects(context.prepareForUpdate(() => {}), /did not get ready/);

console.log('Firmware upload web tests passed.');
