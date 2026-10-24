// The Custom Edition maps importer (port/web/site/custom-maps-worker.js) on
// chosen folders made here: what of a custom_maps folder it copies, and the
// maps it leaves out. node --test "port/web/tests/*.test.js"

'use strict';

const test = require('node:test');
const assert = require('node:assert');
const custom = require('../site/custom-maps-worker.js');

// a map's first 0x800 bytes: a Custom Edition cache's (version 609), an Xbox
// cache's (5), or a resource map's (its kind)
function cacheHeader(version) {
  const bytes = new Uint8Array(0x800);
  bytes.set([0x64, 0x61, 0x65, 0x68], 0);
  new DataView(bytes.buffer).setUint32(4, version, true);
  bytes.set([0x74, 0x6F, 0x6F, 0x66], 0x7FC);
  return bytes;
}

function resourceHeader(kind) {
  const bytes = new Uint8Array(16);
  new DataView(bytes.buffer).setUint32(0, kind, true);
  return bytes;
}

// a chosen folder's file, as the browser gives it, of these bytes
function chosen(path, bytes = new Uint8Array(1)) {
  return {
    name: path.split('/').pop(),
    webkitRelativePath: path,
    size: bytes.length,
    slice: (start, end) => ({ arrayBuffer: async () => bytes.slice(start, end).buffer }),
  };
}

test('a custom_maps folder: its maps, pictures and descriptions, one level of subfolders', () => {
  const found = custom.customMapFiles([
    chosen('custom_maps/bitmaps.map'),
    chosen('custom_maps/deathisland.map'),
    chosen('custom_maps/deathisland.bmp'),
    chosen('custom_maps/Firefight/firefight-relic.map'),
    chosen('custom_maps/Firefight/firefight-relic.txt'),
    chosen('custom_maps/Firefight/old/a.map'),
    chosen('custom_maps/readme.pdf'),
    chosen('custom_maps/.DS_Store'),
    chosen('custom_maps/Firefight/.hidden.map'),
  ]);
  assert.deepStrictEqual(found.files.map((entry) => entry.parts.join('/')), [
    'bitmaps.map',
    'deathisland.map',
    'deathisland.bmp',
    'Firefight/firefight-relic.map',
    'Firefight/firefight-relic.txt',
  ]);
  assert.deepStrictEqual(found.skipped.map((entry) => entry.path), ['Firefight/old/a.map']);
});

test('one folder at a time, and not an empty one', () => {
  assert.throws(() => custom.customMapFiles([chosen('a/x.map'), chosen('b/y.map')]), /one custom_maps/);
  assert.throws(() => custom.customMapFiles([chosen('a/readme.pdf')]), /no maps/);
});

test('names that differ only in case: the first', () => {
  const found = custom.customMapFiles([chosen('c/Hugeass.map'), chosen('c/hugeass.map')]);
  assert.deepStrictEqual(found.files.map((entry) => entry.parts.join('/')), ['Hugeass.map']);
});

test('the maps the game takes: Custom Edition caches and the resource maps by their names', () => {
  assert.strictEqual(custom.mapProblem('deathisland.map', cacheHeader(609)), null);
  assert.match(custom.mapProblem('a10.map', cacheHeader(5)), /Xbox map/);
  assert.match(custom.mapProblem('x.map', cacheHeader(7)), /cache version is 7/);
  assert.match(custom.mapProblem('x.map', new Uint8Array(0x800)), /not a map/);
  assert.match(custom.mapProblem('x.map', cacheHeader(609).subarray(0, 100)), /too short/);
  assert.strictEqual(custom.mapProblem('bitmaps.map', resourceHeader(1)), null);
  assert.strictEqual(custom.mapProblem('Sounds.map', resourceHeader(2)), null);
  assert.strictEqual(custom.mapProblem('loc.map', resourceHeader(3)), null);
  assert.match(custom.mapProblem('loc.map', resourceHeader(1)), /not Custom Edition's loc\.map/);
});

test('the headers checked: what the game would refuse is left out', async () => {
  const found = custom.customMapFiles([
    chosen('c/bitmaps.map', resourceHeader(1)),
    chosen('c/hugeass.map', cacheHeader(609)),
    chosen('c/a10.map', cacheHeader(5)),
    chosen('c/Sub/sounds.map', resourceHeader(2)),
    chosen('c/hugeass.bmp'),
  ]);
  const kept = await custom.checkedFiles(found);
  assert.deepStrictEqual(kept.map((entry) => entry.parts.join('/')), ['bitmaps.map', 'hugeass.map', 'hugeass.bmp']);
  assert.deepStrictEqual(found.skipped.map((entry) => entry.path).sort(), ['Sub/sounds.map', 'a10.map']);
});

test('the resource maps among names', () => {
  assert.deepStrictEqual(custom.resourceMapsAmong(['Bitmaps.map', 'x.map', 'LOC.MAP']), ['bitmaps.map', 'loc.map']);
});
