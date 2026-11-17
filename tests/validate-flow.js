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
const legacyTab = flow.find(n => n.type === 'tab' && n.label === 'ESP32 radiation and sensors').id;
const allSources = flow.filter(n => n.type === 'mqtt in' && n.z === legacyTab);
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
assert.equal(flow.filter(n => n.type === 'ui_chart' && n.z === legacyTab).length, 16);
assert.equal(flow.filter(n => n.type === 'ui_text' && n.z === legacyTab).length, 14);
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
assert.equal(new Set(flow.filter(n=>n.type==='mqtt in').map(n=>n.topic)).size, 38);
const checkNew = (topic,payload) => {
    const source = flow.find(n=>n.type==='mqtt in' && n.topic===topic);
    const fn = byId.get(source.wires[0][0]);
    assert.equal(fn.wires.length,fn.outputs);
    const run = new Function('msg','node','Buffer',fn.func);
    return run({payload,topic},{warn(){}},Buffer);
};
const sample = {count:5,elapsed_ms:15000,total:5,cpm:20,cpm60:null,cpm300:null,cph_est:1200,estimate_ms:15000,cph_rolling:null,window60_ms:0,window300_ms:0,window3600_ms:0,sigma_cpm:null,dose_rate_est:null,dose_total_est:null,dose_ratio:null};
const geigerTopic='esp32/geiger/stats';
const out = checkNew(geigerTopic,JSON.stringify(sample));
assert.equal(out[0].payload.count,5);
assert.equal(out[0].payload.cpm60,null);
assert.equal(out[2],null);
assert(out[0].topic.includes('ESP32GEIGERSTATS'));
const ready = {...sample,total:1200,cpm60:20,cpm300:20,cph_rolling:1200,window60_ms:60000,window300_ms:300000,window3600_ms:3600000,estimate_ms:60000,sigma_cpm:4.472,dose_rate_est:0.0664,dose_total_est:0.0664,dose_ratio:0.00332};
assert(checkNew(geigerTopic,ready));
for(const bad of ['bad',null,{}, {...sample,count:-1}, {...sample,cpm:100}, {...sample,cpm60:0}, {...sample,estimate_ms:0}, {...sample,sigma_cpm:0}, {...sample,dose_rate_est:0.1}]) assert.equal(checkNew(geigerTopic,bad),null);
assert(checkNew('esp32/device/stats',{rssi_dbm:-60,heap_bytes:90000,uptime_s:100}));
assert.equal(checkNew('esp32/device/stats',{rssi_dbm:10,heap_bytes:90000,uptime_s:100}),null);
for(const [metric,value,bad] of [['dewpoint',13.85,1000],['gps/altitude',-5,1e9],['gps/hdop',1.2,-1],['gps/satellites',8,8.5],['mag/magnitude',100,-1]]) {
    const result=checkNew('esp32/'+metric,String(value));
    assert.equal(result[0].payload.metric,metric);
    assert.equal(result[1].payload,value);
    assert.equal(checkNew('esp32/'+metric,bad),null);
    assert.equal(checkNew('esp32/'+metric,'bad'),null);
}
for(const table of ['ESP32GEIGERSTATS','ESP32DEVICE','ESP32EXTRA']) assert(schema.includes('CREATE TABLE IF NOT EXISTS `'+table+'`'));
assert.equal(flow.filter(n=>n.type==='ui_tab').length,4);
console.log('PASS: ' + checks + ' payload cases; 17 telemetry topics/tables; 16 charts; 14 status indicators; valid flow wiring');
console.log('PASS: 7 new MQTT topics, 3 database tables and 3 dashboards; invalid, missing and warm-up samples rejected/suppressed');
