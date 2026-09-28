// Web GUI message counter (#1173): updateCharsLeft() must count UTF-8 bytes,
// because send_message() caps ":" / ":{call}" + text at 150 BYTES. Counting
// characters let a Polish or emoji-heavy message pass the counter and then be
// cut short on the node.
//
// The JS lives as a C string inside src/web_functions/web_functions.cpp; this
// test lifts that println() line out of the source, unescapes it and runs it
// against a two-element stub DOM. No node, no jsdom needed.
//
//   node --test tools/tests/test_webgui_charsleft.mjs

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const SRC = new URL('../../src/web_functions/web_functions.cpp', import.meta.url);

function loadCounter() {
  const line = readFileSync(SRC, 'utf8')
    .split('\n')
    .find((l) => l.includes('web_client.println(') && l.includes('function updateCharsLeft()'));
  assert.ok(line, 'updateCharsLeft() println not found in web_functions.cpp');
  const lit = line.slice(line.indexOf('println(') + 'println('.length, line.lastIndexOf(');'));
  const js = JSON.parse(lit); // a C string literal with only \" and \n escapes is valid JSON
  const els = {
    sendcall: { value: '' },
    messagetext: { value: '' },
    indicator_charsleft: { innerHTML: '' },
  };
  const document = { getElementById: (id) => els[id] };
  const run = new Function('document', `${js}; return updateCharsLeft;`)(document);
  return (call, text) => {
    els.sendcall.value = call;
    els.messagetext.value = text;
    run();
    return { left: Number(els.indicator_charsleft.innerHTML), text: els.messagetext.value };
  };
}

const bytes = (s) => new TextEncoder().encode(s).length;

test('ASCII counts one per character', () => {
  const count = loadCounter();
  assert.equal(count('', 'hello').left, 144);
});

test('Polish letters count two bytes each', () => {
  const count = loadCounter();
  assert.equal(count('', 'ąęś').left, 149 - 6);
});

test('emoji counts four bytes', () => {
  const count = loadCounter();
  assert.equal(count('', '\u{1F600}').left, 145);
});

test('destination call and its braces are charged', () => {
  const count = loadCounter();
  // ":{DK5EN-1}" + text <= 150  ->  149 - (7 + 2)
  assert.equal(count('DK5EN-1', '').left, 140);
});

test('over-long text is cut to the byte limit, between characters', () => {
  const count = loadCounter();
  const r = count('', 'ą'.repeat(100)); // 200 bytes
  assert.equal(r.text, 'ą'.repeat(74)); // 148 bytes; a 75th would need 150
  assert.equal(bytes(r.text), 148);
  assert.equal(r.left, 1);
});

test('cut never splits an emoji surrogate pair', () => {
  const count = loadCounter();
  const r = count('', 'a' + '\u{1F600}'.repeat(40)); // 1 + 160 bytes
  assert.equal(r.text, 'a' + '\u{1F600}'.repeat(37)); // 149 bytes
  assert.equal(r.left, 0);
  assert.ok(r.text.isWellFormed());
});

test('the result always fits what send_message() sends', () => {
  const count = loadCounter();
  for (const [call, text] of [
    ['', 'x'.repeat(300)],
    ['9', 'ż'.repeat(200)],
    ['DK5EN-99', 'ä\u{1F937}‍♂️'.repeat(30)],
  ]) {
    const r = count(call, text);
    const wire = call ? `:{${call}}${r.text}` : `:${r.text}`;
    assert.ok(bytes(wire) <= 150, `${bytes(wire)} bytes for call "${call}"`);
    assert.ok(r.left >= 0);
  }
});
