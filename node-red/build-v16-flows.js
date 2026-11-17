'use strict';
// Deterministic extension of the existing exported flow. Run from any directory.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const file = path.join(__dirname, 'flows.json');
const prefix = 'a160';
const flow = JSON.parse(fs.readFileSync(file, 'utf8')).filter(n => !n.id.startsWith(prefix) && !n.id.startsWith('v160'));
const broker = flow.find(n => n.type === 'mqtt-broker').id;
const server = flow.find(n => n.type === 'Stackhero-MySQL-Server').id;
const templates = Object.fromEntries(['mqtt in','function','ui_chart','ui_text','ui_tab','ui_group','tab','Stackhero-MySQL','debug'].map(t => [t, flow.find(n => n.type === t)]));
const id = name => prefix + crypto.createHash('sha256').update(name).digest('hex').slice(0,12);
const add = (name, type, overrides) => {
    const node = {...templates[type], ...overrides, id:id(name), type};
    flow.push(node);
    return node.id;
};
const tab = (name, label, order) => {
    const z = add(name+'edit', 'tab', {label, info:'V1.6 — additional MQTT observations'});
    const ui = add(name+'ui', 'ui_tab', {name:label, order, icon:'show_chart'});
    const group = add(name+'group', 'ui_group', {name:label, tab:ui, order:1, width:'12'});
    return {z, group};
};
const mqtt = (name, z, topic, fn, y) => add(name,'mqtt in',{z, name:topic,topic,broker,qos:'0',datatype:'auto-detect',x:160,y,wires:[[fn]]});
const chart = (name, z, group, label, order, y) => add(name,'ui_chart',{z,group,label,order,width:6,height:4,nodata:'A aguardar leitura válida',removeOlder:24,removeOlderUnit:'3600',x:800,y,wires:[[]]});
const readout = (name,z,group,label,order,y) => add(name,'ui_text',{z,group,label,name:label,order,width:6,height:1,format:'{{msg.payload}}',x:800,y,wires:[]});
const database = (name,z,y) => {
    const debug = add(name+'result','debug',{z,name:'MySQL result',active:false,x:800,y:y+40,wires:[]});
    return add(name,'Stackhero-MySQL',{z,server,name:'Persist validated observations',x:600,y,wires:[[debug]]});
};
const parseObject = `let p = msg.payload;
try { if (Buffer.isBuffer(p)) p = p.toString('utf8'); if (typeof p === 'string') p = JSON.parse(p); }
catch (_) { node.warn('Invalid JSON'); return null; }
if (!p || typeof p !== 'object' || Array.isArray(p)) return null;`;
const time = `const time = new Date().toISOString().slice(0,23).replace('T',' ');`;
const radiation = tab('rad','Radiation — extended',3);
const radFields = [
    ['cpm','CPM (15 s)','chart'],['cpm60','CPM (rolling 1 min)','chart'],
    ['cpm300','CPM (rolling 5 min)','chart'],['cph_est','CPH estimate (not measured)','chart'],
    ['cph_rolling','CPH rolling (60 min, normalized)','chart'],['total','Total pulses since boot','text'],
    ['sigma_cpm','CPM statistical sigma (1 min)','chart'],['count','Raw counts per interval','chart'],
    ['elapsed_ms','Interval duration (ms)','text'],['estimate_ms','CPH estimate integration (ms)','text'],
    ['dose_rate_est','Estimated µSv/h — UNCALIBRATED','chart'],['dose_total_est','Estimated µSv since boot — UNCALIBRATED','text']
];
const radDb = database('raddb', radiation.z, 100);
const radColumns = ['count','elapsed_ms','total','cpm','cpm60','cpm300','cph_est','estimate_ms','cph_rolling','window60_ms','window300_ms','window3600_ms','sigma_cpm','dose_rate_est','dose_total_est','dose_ratio'];
const radQuery = 'INSERT INTO ESP32GEIGERSTATS (`time`, '+radColumns.map(c=>'`'+c+'`').join(', ')+') VALUES (:time, '+radColumns.map(c=>':'+c).join(', ')+')';
const radFn = add('radfn','function',{z:radiation.z,name:'Validate coherent count snapshot',outputs:radFields.length+1,x:400,y:400,
    func:parseObject+`\nconst nonneg = x => typeof x === 'number' && Number.isFinite(x) && x >= 0;
const whole = x => Number.isSafeInteger(x) && x >= 0;
if (!whole(p.count) || p.count > 4294967295 || !whole(p.elapsed_ms) || p.elapsed_ms < 15000 || p.elapsed_ms > 4294967295 || !whole(p.total) || p.total < p.count) return null;
if (!nonneg(p.cpm) || Math.abs(p.cpm - 60000*p.count/p.elapsed_ms) > 0.001 || !nonneg(p.cph_est)) return null;
for (const [value, duration, target] of [['cpm60','window60_ms',60000],['cpm300','window300_ms',300000],['cph_rolling','window3600_ms',3600000]]) {
    if (!whole(p[duration])) return null;
    if (p[value] === null ? p[duration] !== 0 : !nonneg(p[value]) || p[duration] < target || p[duration] > target+15000) return null;
}
if (p.estimate_ms !== (p.cpm60 === null ? p.elapsed_ms : p.window60_ms) || Math.abs(p.cph_est - 60*(p.cpm60 === null ? p.cpm : p.cpm60)) > 0.032) return null;
for(const k of ['sigma_cpm','dose_rate_est','dose_total_est']) if(p[k] !== null && !nonneg(p[k])) return null;
if (p.cpm60 === null && (p.sigma_cpm !== null || p.dose_rate_est !== null)) return null;
if (p.dose_ratio === null) { if (p.dose_rate_est !== null || p.dose_total_est !== null) return null; }
else if (!nonneg(p.dose_ratio) || p.dose_ratio <= 0 || !nonneg(p.dose_total_est) || (p.cpm60 !== null && !nonneg(p.dose_rate_est))) return null;
${time}
const data = {time}; for(const k of ${JSON.stringify(radColumns)}) data[k]=p[k];
const sql = {topic:${JSON.stringify(radQuery)},payload:data};
return [sql, ...${JSON.stringify(radFields.map(f=>f[0]))}.map(k=>p[k]===null ? null : ({topic:k,payload:p[k]}))];`,
    wires:[[radDb], ...radFields.map(([k,l,t],i)=>[t==='chart'?chart('rad'+k,radiation.z,radiation.group,l,i+1,150+i*50):readout('rad'+k,radiation.z,radiation.group,l,i+1,150+i*50)])]});
mqtt('radin',radiation.z,'esp32/geiger/stats',radFn,400);

const device = tab('dev','ESP32 diagnostics',4);
const deviceFields = [['rssi_dbm','Wi-Fi RSSI (dBm)'],['heap_bytes','Free heap (bytes)'],['uptime_s','Uptime (s)']];
const devDb = database('devdb',device.z,100);
const devFn = add('devfn','function',{z:device.z,name:'Validate device telemetry',outputs:4,x:400,y:200,
    func:parseObject+`\nif (!Number.isInteger(p.rssi_dbm) || p.rssi_dbm < -127 || p.rssi_dbm > 0 || !Number.isSafeInteger(p.heap_bytes) || p.heap_bytes < 0 || !Number.isSafeInteger(p.uptime_s) || p.uptime_s < 0) return null;
${time}
return [{topic:'INSERT INTO ESP32DEVICE (\`time\`, rssi_dbm, heap_bytes, uptime_s) VALUES (:time, :rssi_dbm, :heap_bytes, :uptime_s)',payload:{time,rssi_dbm:p.rssi_dbm,heap_bytes:p.heap_bytes,uptime_s:p.uptime_s}}, ...['rssi_dbm','heap_bytes','uptime_s'].map(k=>({topic:k,payload:p[k]}))];`,
    wires:[[devDb],...deviceFields.map(([k,l],i)=>[chart('dev'+k,device.z,device.group,l,i+1,160+i*60)])]});
mqtt('devin',device.z,'esp32/device/stats',devFn,200);

const extra = tab('ext','Environmental and GPS extended',5);
const gps = add('gpsgroup','ui_group',{name:'GPS quality',tab:id('extui'),order:2,width:'12'});
const mag = add('maggroup','ui_group',{name:'Magnetometer',tab:id('extui'),order:3,width:'12'});
const metrics = [
    ['dewpoint','Dew point (°C)',extra.group,-200,60,false],
    ['gps/altitude','GPS altitude (m)',gps,-12000,100000,false],
    ['gps/hdop','GPS HDOP (dimensionless)',gps,0,1000,false],
    ['gps/satellites','GPS satellites',gps,0,255,true],
    ['mag/magnitude','Magnetic field magnitude (µT)',mag,0,2000,false]
];
const extraDb = database('extdb',extra.z,100);
for (let i=0;i<metrics.length;i++) {
    const [metric,label,group,min,max,integer] = metrics[i];
    const key = 'ext'+i, y = 180+i*70;
    const plot = chart(key+'chart',extra.z,group,label,i+1,y);
    const fn = add(key+'fn','function',{z:extra.z,name:'Validate '+metric,outputs:2,x:430,y,wires:[[extraDb],[plot]],
        func:`let raw = Buffer.isBuffer(msg.payload) ? msg.payload.toString('utf8') : msg.payload;
if (typeof raw !== 'number' && (typeof raw !== 'string' || !/^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?$/.test(raw.trim()))) return null;
const value = Number(raw);
if (!Number.isFinite(value) || value < ${min} || value > ${max} || (${integer} && !Number.isInteger(value))) return null;
${time}
return [{topic:'INSERT INTO ESP32EXTRA (\`time\`, metric, value) VALUES (:time, :metric, :value)',payload:{time,metric:${JSON.stringify(metric)},value}},{topic:${JSON.stringify(metric)},payload:value}];`});
    mqtt(key+'in',extra.z,'esp32/'+metric,fn,y);
}
// Keep all existing wiring and data contracts, but explain previously blank charts.
for(const n of flow.filter(n=>n.type==='ui_chart' && !n.id.startsWith(prefix))) {
    if (/Counts per hour/i.test(n.label)) n.nodata='Primeira amostra após 60 min; ver CPH estimado em Radiation — extended';
    else if (/Latitude|Longitude/i.test(n.label)) n.nodata='A aguardar fix GPS válido (ver GPS status)';
    else if (/Magnetic Field|Heading/i.test(n.label)) n.nodata='A aguardar HMC5883L ativo e leitura válida (ver MAG status)';
}
fs.writeFileSync(file,JSON.stringify(flow,null,2)+'\n');
console.log('Generated V1.6 extended flow; retained '+flow.filter(n=>!n.id.startsWith(prefix)).length+' original nodes');
