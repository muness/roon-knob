// Check controller/channel routing and browser messaging without a serial device.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'web/assets/site.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'web/flash.html'), 'utf8');
const ids = [...html.matchAll(/class="flash-card" id="([^"]+)"/g)].map(match => match[1]);
const names = {
  dial: 'HiPhi Dial', frame: 'HiPhi Frame', rlcd: 'HiPhi Slate', joy: 'HiPhi Joy',
  tough: 'HiPhi Tough', m5dial: 'HiPhi Dial Lab', sticks3: 'HiPhi Twist',
  stopwatch: 'HiPhi Remote', stackchan: 'Kizz', knobaux: 'Dial auxiliary parking image'
};
function run({search = '', filter = 'all', chooser = false, missing = false,
  agent = 'desktop', serial = true, secure = true, platform = '', touches = 0} = {}) {
  const cards = (missing ? [] : ids).map(id => ({id, remove() { this.removed = true; }}));
  const heading = {textContent: 'Match the model before flashing.'};
  const subject = {dataset: {subjectB64: Buffer.from('Fix “Dial” → room 🎵').toString('base64')}};
  const status = {};
  const notice = {dataset: {}, querySelector() { return status; }};
  const links = ['/stable/', '/beta/', '/alpha/', '/'].map(href => ({
    href, getAttribute() { return href; }, setAttribute(key, value) { this[key] = value; }
  }));
  const list = {
    querySelectorAll() { return cards; },
    querySelector() { return cards.find(card => !card.removed); },
    append(node) { this.emptyNotice = node; }
  };
  const document = {
    body: {dataset: {targetFilter: filter, channel: 'alpha'}},
    getElementById() { return subject; },
    querySelectorAll(selector) {
      if (selector === '[data-channel-link], [data-preserve-target]') return links;
      if (selector === '[data-browser-requirement]') return [notice];
      return [];
    },
    querySelector(selector) {
      if (selector === '.flash-list') return chooser ? null : list;
      if (selector === 'h1') return heading;
      if (selector === '[data-channel-link="alpha"]') return links[2];
      return null;
    },
    createElement() { return {}; }
  };
  vm.runInNewContext(source, {document, location: {search, href: `https://firmware.hiphi.audio/alpha/${search}`},
    navigator: {...(serial ? {serial: {}} : {}), userAgent: agent, platform, maxTouchPoints: touches},
    window: {isSecureContext: secure}, URL, URLSearchParams, TextDecoder, Uint8Array, atob});
  return {cards: cards.filter(card => !card.removed).map(card => card.id), heading, links, list, notice, status, subject};
}
for (const [target, name] of Object.entries(names)) {
  assert(ids.includes(`${target}-card`), `${target} needs a real template card`);
  const result = run({search: `?target=${target}`});
  assert.deepEqual(result.cards, target === 'dial' ? ['dial-card', 'knobaux-card'] : [`${target}-card`]);
  assert.equal(result.heading.textContent, `Flash ${name}`);
  assert(result.links.every(link => new URL(link.href).searchParams.get('target') === target));
}
assert.equal(run({search: '?target=frame', filter: 'dial'}).heading.textContent, 'Flash HiPhi Frame');
assert.deepEqual(run({filter: 'dial'}).cards, ['dial-card', 'knobaux-card']);
for (const invalid of ['constructor', '__proto__', '<img src=x>', 'unknown']) {
  assert.deepEqual(run({search: `?target=${encodeURIComponent(invalid)}`}).cards, ids);
}
assert.match(run({search: '?target=joy', missing: true}).list.emptyNotice.textContent, /not listed/);
assert.equal(run({search: '?target=rlcd', chooser: true}).heading.textContent, 'Choose a release for HiPhi Slate.');
assert.equal(run().links[2]['aria-current'], 'page');
assert.equal(run().subject.textContent, 'Fix “Dial” → room 🎵');
assert.equal(run().notice.dataset.browserState, 'supported');
for (const options of [{agent: 'iPhone'}, {agent: 'Android'}, {platform: 'MacIntel', touches: 5}, {secure: false}, {serial: false}]) {
  const result = run(options);
  assert.equal(result.notice.dataset.browserState, 'unsupported');
  assert(result.status.textContent.length > 30);
}
console.log('Firmware site checks passed: all target cards, channel preservation, invalid targets, empty state, UTF-8 preview, browser requirements.');

const redirectTemplate = fs.readFileSync(path.join(root, 'web/redirect.html'), 'utf8');
const legacyAliases = {'dial-lab': 'm5dial', slate: 'rlcd', twist: 'sticks3', remote: 'stopwatch', kizz: 'stackchan', 'dial-aux': 'knobaux'};
for (const [route, destination] of [
  ['https://firmware.hiphi.audio/flash/alpha/', '../../alpha/'],
  ['https://firmware.hiphi.audio/flash/beta/', '../../beta/'],
  ['https://firmware.hiphi.audio/flash/stable/', '../../stable/'],
  ['https://firmware.hiphi.audio/flash/', '../'],
  ['https://firmware.hiphi.audio/flash.html', './stable/'],
  ['https://firmware.hiphi.audio/alpha/flash.html', './']
]) {
  const script = redirectTemplate.replaceAll('{{REDIRECT_URL}}', destination).match(/<script>([\s\S]*?)<\/script>/)[1];
  for (const [alias, target] of Object.entries({...Object.fromEntries(Object.keys(names).map(key => [key, key])), ...legacyAliases})) {
    let content;
    const location = {href: route + '#' + alias, search: '', hash: '#' + alias, replace(url) { this.destination = url; }};
    const meta = {setAttribute(key, value) { content = value; }};
    vm.runInNewContext(script, {location, URL, URLSearchParams, document: {querySelector() { return meta; }}});
    assert.equal(location.destination, new URL(destination + '?target=' + target, route).href);
    assert.equal(content, '0;url=' + location.destination);
  }
}
console.log('Legacy firmware redirects preserve every target and alias through center/channel routes.');
