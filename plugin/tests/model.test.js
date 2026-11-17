const { test } = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const src = fs.readFileSync(path.join(__dirname, '..', 'Model.js'), 'utf8').replace(/^\.pragma library\s*\n/, '')
const { formatTime, parseLevels, lyricIndex, trackLabel } = new Function(src + '; return {formatTime, parseLevels, lyricIndex, trackLabel}')()

test('formats playback time', () => {
  assert.equal(formatTime(0), '0:00')
  assert.equal(formatTime(3723000), '1:02:03')
})
test('validates and clamps visualizer levels', () => {
  assert.equal(parseLevels([1, 2]), null)
  assert.deepEqual(parseLevels(Array(8).fill(150)), Array(8).fill(100))
  assert.equal(parseLevels(Array(8).fill('1')), null)
})
test('locates the current lyric', () => {
  assert.equal(lyricIndex([{ t: 0 }, { t: 5000 }], 4999), 0)
  assert.equal(lyricIndex([{ t: 1000 }], 0), -1)
})
test('leaves markup-shaped track text unchanged', () => {
  assert.equal(trackLabel({ title: '<b>title</b>', artists: ['artist'] }), '<b>title</b> · artist')
})
