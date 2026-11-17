'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const flow = JSON.parse(fs.readFileSync(path.join(root, 'node-red/flows.json'), 'utf8'));
const schema = fs.readFileSync(path.join(root, 'database/schema.sql'), 'utf8');
const byId = new Map(flow.map(n => [n.id, n]));
assert.equal(byId.size, flow.length);
for (const n of flow) {
    for (const branch of n.wires || []) {
        for (const target of branch) assert(byId.has(target), 'Missing target: ' + target);
    }
    for (const key of ['z', 'broker', 'server', 'group', 'tab']) {
        if (n[key] && n.type !== 'mqtt-broker') assert(byId.has(n[key]), 'Missing config ' + n[key]);
    }
}
const allSources = flow.filter(n => n.type === 'mqtt in');
const sources = allSources.filter(n => byId.get(n.wires[0][0])?.type === 'function');
const statusSources = allSources.filter(n => byId.get(n.wires[0][0])?.type === 'ui_text');
assert.equal(sources.length, 17);
assert.equal(allSources.length, 31);
assert.equal(new Set(allSources.map(n => n.topic)).size, 31);
assert.equal(statusSources.length, 14);
assert.deepEqual(statusSources.map(n => n.topic).sort(),
    ['esp32/status','esp32/geiger/status','esp32/bmp280/status','esp32/lcd/status',
     'esp32/output/state','esp32/alarm','esp32/aht/status','esp32/voltage/status',
     'esp32/gps/status','esp32/ntp/status','esp32/mag/status','esp32/lora/status',
     'esp32/hall/status','esp32/inttemp/status'].sort());
assert.equal(flow.filter(n => n.type === 'ui_chart').length, 16);
assert.equal(flow.filter(n => n.type === 'ui_text').length, 14);
const tables = new Set();
const db = flow.find(n => n.type === 'Stackhero-MySQL');
let checks = 0;
for (const source of sources) {
    assert.equal(source.wires[0].length, 1);
    const fn = byId.get(source.wires[0][0]);
    assert.equal(fn.type, 'function');
    assert.equal(fn.outputs, 2);
    assert.deepEqual(fn.wires[0], [db.id]);
    const run = new Function('msg', 'node', 'Buffer', fn.func);
    const feed = payload => run({payload, topic: source.topic}, {warn() {}}, Buffer);
    const timestamp = source.topic === 'esp32/timestamp';
    for (const raw of timestamp
        ? ['2026-01-01T00:00:00Z', 1767225600000, Buffer.from('2026-01-01T00:00:00+00:00')]
        : [42.5, '42.5', Buffer.from('-42.5'), '0', '1e-2']) {
        const [sql, display] = feed(raw);
        assert.equal(display.topic, source.topic);
        assert.equal(display.payload, sql.payload.value);
        assert.match(sql.payload.time, /^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}$/);
        const [, table, column] = sql.topic.match(/^INSERT INTO (\w+) \(`time`, `(\w+)`\) VALUES \(:time, :value\)$/);
        const tableDDL = schema.split('CREATE TABLE IF NOT EXISTS `' + table + '` (')[1];
        assert(tableDDL && tableDDL.split(') ENGINE=InnoDB')[0].includes('`' + column + '` '));
        tables.add(table);
        checks++;
    }
    for (const bad of timestamp
        ? [null, '', 'nonsense', '2026-01-01T00:00:00', Infinity]
        : [null, '', '   ', '12abc', false, {}, [], Infinity, NaN, '1; DROP TABLE x', '0x10']) {
        assert.equal(feed(bad), null, source.topic + ' rejected malformed input');
        checks++;
    }
}
assert.equal(tables.size, 17);
assert.equal(flow.find(n => n.type === 'mqtt-broker').broker, 'localhost');
assert.equal(flow.find(n => n.type === 'Stackhero-MySQL-Server').host, 'localhost');
console.log('PASS: ' + checks + ' payload cases; 17 telemetry topics/tables; 16 charts; 14 status indicators; valid flow wiring');
