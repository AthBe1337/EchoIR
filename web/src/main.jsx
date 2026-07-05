import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import {
  Activity,
  BookOpen,
  Cpu,
  Download,
  Fan,
  Lightbulb,
  Play,
  Power,
  Radio,
  RefreshCw,
  RotateCcw,
  Save,
  Send,
  SlidersHorizontal,
  Thermometer,
  Zap
} from 'lucide-react';
import './styles.css';

const baudRates = ['9600', '19200', '38400', '57600', '115200'];
const profiles = ['high', 'basic'];
const powerModes = ['on', 'off'];
const modes = ['auto', 'cool', 'dry', 'fan', 'heat'];
const fans = ['auto', 'low', 'mid', 'high'];

const brandOptions = [
  ['gree1', '格力 1'],
  ['gree2', '格力 2'],
  ['gree3', '格力 3'],
  ['midea1', '美的 1'],
  ['midea2', '美的 2'],
  ['haier1', '海尔 1'],
  ['haier2', '海尔 2'],
  ['haier3', '海尔 3'],
  ['haier4', '海尔 4'],
  ['tcl1', 'TCL'],
  ['aux1', '奥克斯'],
  ['mi1', '小米'],
  ['hisense1', '海信 1'],
  ['hisense2', '海信 2'],
  ['hisense3', '海信 3'],
  ['chigo1', '志高'],
  ['chiq1', '长虹'],
  ['daikin1', '大金 1'],
  ['daikin2', '大金 2'],
  ['panasonic1', '松下'],
  ['mitsubishi1', '三菱']
];

const optionRange = (from, to, label = (value) => String(value)) =>
  Array.from({ length: to - from + 1 }, (_item, index) => {
    const value = from + index;
    return { value, label: label(value) };
  });

const binaryOptions = [
  { value: 0, label: '关闭' },
  { value: 1, label: '开启' }
];
const powerOptions = [
  { value: 0, label: '关机' },
  { value: 1, label: '开机' }
];
const modeOptions = [
  { value: 0, label: '自动' },
  { value: 1, label: '制冷' },
  { value: 2, label: '除湿' },
  { value: 3, label: '送风' },
  { value: 4, label: '制热' }
];
const shortModeOptions = modeOptions.slice(0, 4);
const fanOptions = [
  { value: 0, label: '自动' },
  { value: 1, label: '低风' },
  { value: 2, label: '中风' },
  { value: 3, label: '高风' }
];
const fan5Options = [
  { value: 0, label: '自动' },
  { value: 1, label: '静音' },
  { value: 2, label: '低风' },
  { value: 3, label: '中风' },
  { value: 4, label: '高风' }
];
const fan6Options = [
  { value: 0, label: '自动' },
  { value: 1, label: '低风' },
  { value: 2, label: '中低' },
  { value: 3, label: '中风' },
  { value: 4, label: '中高' },
  { value: 5, label: '高风' }
];
const fan7Options = [
  { value: 0, label: '自动' },
  { value: 1, label: '静音' },
  { value: 2, label: '低风' },
  { value: 3, label: '中低' },
  { value: 4, label: '中风' },
  { value: 5, label: '中高' },
  { value: 6, label: '高风' }
];
const temp16Options = optionRange(0, 14, (value) => `${value + 16} C`);
const temp17Options = optionRange(0, 13, (value) => `${value + 17} C`);
const rawTempOptions = optionRange(16, 30, (value) => `${value} C`);
const timer24Options = [{ value: 0, label: '关闭' }, ...optionRange(1, 24, (value) => `${value} 小时`)];
const timer48Options = [{ value: 0, label: '关闭' }, ...optionRange(1, 48, (value) => `${value} 档`)];
const swing3Options = [
  { value: 0, label: '关闭' },
  { value: 1, label: '摆风 1' },
  { value: 2, label: '摆风 2' }
];
const swing6Options = [
  { value: 0, label: '自动' },
  { value: 1, label: '位置 1' },
  { value: 2, label: '位置 2' },
  { value: 3, label: '位置 3' },
  { value: 4, label: '位置 4' },
  { value: 5, label: '位置 5' }
];
const level4Options = optionRange(0, 3, (value) => `档位 ${value}`);
const level8Options = optionRange(0, 7, (value) => `档位 ${value}`);
const level16Options = optionRange(0, 16, (value) => `档位 ${value}`);

const field = (offset, label, options, defaultValue = 0) => ({ offset, label, options, defaultValue });
const greeCommon = [
  field(32, '模式', modeOptions, 1),
  field(33, '电源', powerOptions, 1),
  field(34, '风量', fanOptions, 0),
  field(35, '扫风', binaryOptions, 0),
  field(36, '睡眠', binaryOptions, 0),
  field(37, '温度', temp16Options, 8),
  field(38, '定时', timer24Options, 0),
  field(39, '换气', binaryOptions, 0),
  field(40, '灯光', binaryOptions, 1),
  field(41, '强劲', binaryOptions, 0),
  field(42, '静音', binaryOptions, 0),
  field(43, '节能', binaryOptions, 0),
  field(44, '上下扫风', binaryOptions, 0),
  field(45, '辅热/灯光', binaryOptions, 0),
  field(46, '风向', level4Options, 0)
];

const officialFieldSchemas = {
  gree1: greeCommon,
  gree2: greeCommon,
  midea1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp17Options, 7),
    field(35, '风量', fanOptions, 0),
    field(36, '扫风索引', level16Options, 0),
    field(37, '定时索引', optionRange(0, 7, (value) => (value === 0 ? '关闭' : `档位 ${value}`)), 0)
  ],
  midea2: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp17Options, 7),
    field(35, '风量', fanOptions, 0),
    field(36, '扫风索引', level16Options, 0)
  ],
  gree3: [
    field(32, '模式', modeOptions, 1),
    field(33, '电源', powerOptions, 1),
    field(34, '风量', fanOptions, 0),
    field(35, '扫风', binaryOptions, 0),
    field(36, '温度', temp16Options, 8),
    field(37, '换气', binaryOptions, 0),
    field(38, '灯光', binaryOptions, 1),
    field(39, '睡眠', level4Options, 0),
    field(40, '定时', timer24Options, 0)
  ],
  tcl1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 8),
    field(35, '风量', fanOptions, 0),
    field(36, '睡眠', binaryOptions, 1),
    field(37, '左右扫风', binaryOptions, 0),
    field(38, '上下扫风', binaryOptions, 0),
    field(39, '强劲', binaryOptions, 0),
    field(40, '显示', binaryOptions, 0),
    field(41, '节能', binaryOptions, 0),
    field(42, '静音', binaryOptions, 0),
    field(43, '关机定时', timer24Options, 0),
    field(44, '开机定时', timer24Options, 0)
  ],
  aux1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 8),
    field(35, '风量', fanOptions, 0),
    field(36, '扫风', binaryOptions, 0),
    field(37, '健康', binaryOptions, 0),
    field(38, '灯光', binaryOptions, 0),
    field(39, '定时模式', level4Options, 0),
    field(40, '定时', timer24Options, 0),
    field(41, '睡眠', binaryOptions, 0),
    field(42, '强劲', binaryOptions, 0),
    field(43, '节能', binaryOptions, 0),
    field(44, '显示', binaryOptions, 0),
    field(45, '辅热', binaryOptions, 0),
    field(46, '清洁', binaryOptions, 0)
  ],
  haier1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', rawTempOptions, 16),
    field(35, '风量', fanOptions, 0),
    field(36, '扫风', binaryOptions, 0),
    field(37, '温度细分 A', level8Options, 0),
    field(38, '温度细分 B', level8Options, 0),
    field(39, '健康', binaryOptions, 0),
    field(40, '睡眠', binaryOptions, 0),
    field(41, '强劲', binaryOptions, 0),
    field(42, '定时', timer48Options, 0),
    field(43, '半小时', binaryOptions, 0)
  ],
  mi1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', shortModeOptions, 0),
    field(34, '温度编码', optionRange(0, 27, (value) => `编码 ${value}`), 16),
    field(35, '风量', [{ value: 0, label: '自动' }, { value: 1, label: '低风' }, { value: 2, label: '高风' }], 0),
    field(36, '扫风', binaryOptions, 0),
    field(37, '显示', binaryOptions, 0),
    field(39, '定时开', binaryOptions, 0),
    field(40, '睡眠', binaryOptions, 0),
    field(41, '定时关', binaryOptions, 1)
  ],
  chigo1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 0),
    field(34, '温度', rawTempOptions, 16),
    field(35, '风量', fanOptions, 0),
    field(36, '健康', binaryOptions, 0),
    field(37, '扫风', swing3Options, 0),
    field(38, '睡眠', binaryOptions, 0),
    field(39, '定时高位', level4Options, 0),
    field(40, '定时', timer24Options, 0)
  ],
  daikin1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 8),
    field(35, '风量', fan7Options, 0),
    field(36, '扫风', binaryOptions, 0)
  ],
  chiq1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', [{ value: 0, label: '自动' }, { value: 1, label: '制冷' }, { value: 2, label: '除湿' }], 0),
    field(34, '温度', temp16Options, 8),
    field(35, '风量', fanOptions, 0),
    field(36, '睡眠', binaryOptions, 0),
    field(37, '扫风', binaryOptions, 0),
    field(38, '灯光', binaryOptions, 0),
    field(39, '定时', timer24Options, 0),
    field(40, '定时高位', level4Options, 0),
    field(41, '定时模式', level4Options, 0),
    field(42, '显示', binaryOptions, 0),
    field(43, '强劲', binaryOptions, 0),
    field(44, '清洁', binaryOptions, 0)
  ],
  panasonic: [
    field(32, '模式', shortModeOptions, 1),
    field(33, '电源', powerOptions, 1),
    field(34, '风量', fanOptions, 0),
    field(35, '温度', temp16Options, 8),
    field(36, '扫风', swing6Options, 0),
    field(37, '静音', binaryOptions, 0)
  ],
  mitsubishi1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 8),
    field(35, '风量', fanOptions, 0),
    field(36, '强劲', binaryOptions, 0),
    field(37, '风门', swing6Options, 0),
    field(38, '扫风', swing6Options, 2)
  ],
  hisense2: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 6),
    field(35, '风量', fan5Options, 0),
    field(36, '强劲', binaryOptions, 0),
    field(37, '扫风', binaryOptions, 0),
    field(38, '定时', timer24Options, 0),
    field(39, '睡眠', binaryOptions, 0)
  ],
  hisense3: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 6),
    field(35, '风量', fanOptions, 0),
    field(36, '强劲', binaryOptions, 0),
    field(37, '扫风', binaryOptions, 0),
    field(38, '灯光', binaryOptions, 0),
    field(39, '定时', timer24Options, 0),
    field(40, '睡眠', binaryOptions, 0),
    field(41, '节能', binaryOptions, 0)
  ],
  hisense1: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 6),
    field(35, '风量', fan6Options, 0),
    field(36, '定时', timer24Options, 0),
    field(37, '扫风', binaryOptions, 0),
    field(38, '温度模式', binaryOptions, 0),
    field(39, '灯光', binaryOptions, 0),
    field(40, '定时模式', binaryOptions, 0)
  ],
  haier2: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '风量', fanOptions, 0),
    field(35, '温度', temp16Options, 8),
    field(36, '扫风', binaryOptions, 0),
    field(37, '扫风模式', binaryOptions, 0),
    field(38, '睡眠', binaryOptions, 0),
    field(39, '强劲', binaryOptions, 0),
    field(40, '定时', timer48Options, 0),
    field(41, '静音', binaryOptions, 0),
    field(42, '健康', binaryOptions, 0),
    field(43, '显示', binaryOptions, 0)
  ],
  haier3: [
    field(32, '模式', modeOptions, 1),
    field(33, '温度低位', level16Options, 1),
    field(34, '风量', fanOptions, 0),
    field(35, '温度高位', level16Options, 8),
    field(36, '扫风', binaryOptions, 0),
    field(37, '电源', powerOptions, 1),
    field(38, '定时低位', timer24Options, 0),
    field(39, '定时高位', timer24Options, 0)
  ],
  haier4: [
    field(32, '模式', modeOptions, 1),
    field(33, '电源', powerOptions, 1),
    field(34, '风量', fanOptions, 0),
    field(35, '温度', temp16Options, 8),
    field(36, '扫风', binaryOptions, 0),
    field(37, '温度细分 A', level8Options, 0),
    field(38, '温度细分 B', level8Options, 0),
    field(39, '强劲', binaryOptions, 0),
    field(40, '睡眠', binaryOptions, 0),
    field(41, '静音', binaryOptions, 0),
    field(42, '健康', binaryOptions, 0),
    field(43, '定时', timer48Options, 0)
  ],
  daikin2: [
    field(32, '电源', powerOptions, 1),
    field(33, '模式', modeOptions, 1),
    field(34, '温度', temp16Options, 6),
    field(35, '风量', fan7Options, 0),
    field(36, '强劲', binaryOptions, 0),
    field(37, '扫风', binaryOptions, 0),
    field(38, '定时', timer24Options, 0),
    field(39, '静音/强劲', binaryOptions, 0),
    field(41, '节能', binaryOptions, 0)
  ]
};

function fieldsForProtocol(protocol) {
  return officialFieldSchemas[protocol] || officialFieldSchemas.gree3;
}

function defaultFieldValues(protocol) {
  return Object.fromEntries(fieldsForProtocol(protocol).map((item) => [item.offset, item.defaultValue]));
}

function App() {
  const [tab, setTab] = useState('ac');
  const [connection, setConnection] = useState({
    port: '/dev/ttyUSB0',
    baud: '115200',
    address: 'FF',
    profile: 'high'
  });
  const [protocols, setProtocols] = useState([]);
  const [detectedPorts, setDetectedPorts] = useState([]);
  const [codes, setCodes] = useState([]);
  const [busy, setBusy] = useState('');
  const [result, setResult] = useState({
    title: 'Ready',
    ok: true,
    stdout: '',
    stderr: '',
    command: ''
  });

  const api = async (path, body, title) => {
    setBusy(title);
    try {
      const response = await fetch(path, {
        method: body ? 'POST' : 'GET',
        headers: body ? { 'Content-Type': 'application/json' } : undefined,
        body: body ? JSON.stringify(body) : undefined
      });
      const payload = await response.json();
      const commandResult = payload.result || {};
      setResult({
        title,
        ok: payload.ok,
        stdout: commandResult.stdout || payload.error || '',
        stderr: commandResult.stderr || '',
        command: commandResult.command || ''
      });
      if (!response.ok || !payload.ok) {
        throw new Error(payload.error || 'request failed');
      }
      return payload;
    } finally {
      setBusy('');
    }
  };

  const refreshCodes = async () => {
    const payload = await api('/api/codes', null, 'Refresh codes');
    setCodes(payload.codes || []);
  };

  useEffect(() => {
    let cancelled = false;
    async function load() {
      try {
        const response = await fetch('/api/health');
        const payload = await response.json();
        if (!cancelled && payload.ok) {
          setProtocols(payload.protocols || []);
          setDetectedPorts(payload.detectedPorts || []);
          if (payload.serialPort) {
            setConnection((current) => ({ ...current, port: payload.serialPort }));
          }
          setResult((current) => ({
            ...current,
            title: 'API connected',
            stdout: `CLI: ${payload.cliPath}\nPort: ${payload.serialPort || connection.port}`
          }));
        }
        const codesResponse = await fetch('/api/codes');
        const codesPayload = await codesResponse.json();
        if (!cancelled && codesPayload.ok) {
          setCodes(codesPayload.codes || []);
        }
      } catch (error) {
        if (!cancelled) {
          setResult({ title: 'API offline', ok: false, stdout: error.message, stderr: '', command: '' });
        }
      }
    }
    load();
    return () => {
      cancelled = true;
    };
  }, []);

  const payloadWithConnection = (payload = {}) => ({ ...connection, ...payload });

  const tabs = [
    ['device', Cpu, '设备'],
    ['ac', Thermometer, '万能空调'],
    ['official', SlidersHorizontal, '官方空调'],
    ['learn', BookOpen, '学习码库']
  ];

  return (
    <main className="app-shell">
      <header className="topbar">
        <div>
          <p className="eyebrow">EchoIR</p>
          <h1>红外控制台</h1>
        </div>
        <div className={result.ok ? 'status-pill ok' : 'status-pill error'}>
          <Activity size={16} />
          <span>{busy || result.title}</span>
        </div>
      </header>

      <ConnectionPanel connection={connection} setConnection={setConnection} detectedPorts={detectedPorts} />

      <nav className="tabbar">
        {tabs.map(([id, Icon, label]) => (
          <button key={id} className={tab === id ? 'tab active' : 'tab'} onClick={() => setTab(id)}>
            <Icon size={18} />
            <span>{label}</span>
          </button>
        ))}
      </nav>

      <section className="workspace">
        {tab === 'device' && (
          <DevicePanel api={api} payloadWithConnection={payloadWithConnection} busy={busy} />
        )}
        {tab === 'ac' && (
          <UniversalAcPanel api={api} payloadWithConnection={payloadWithConnection} busy={busy} />
        )}
        {tab === 'official' && (
          <OfficialAcPanel
            api={api}
            payloadWithConnection={payloadWithConnection}
            protocols={protocols}
            busy={busy}
          />
        )}
        {tab === 'learn' && (
          <LearnPanel
            api={api}
            payloadWithConnection={payloadWithConnection}
            refreshCodes={refreshCodes}
            codes={codes}
            busy={busy}
          />
        )}
        <OutputPanel result={result} />
      </section>
    </main>
  );
}

function ConnectionPanel({ connection, setConnection, detectedPorts }) {
  const update = (key, value) => setConnection((current) => ({ ...current, [key]: value }));
  return (
    <section className="connection-strip">
      <Field label="端口">
        <input
          list="serial-port-options"
          value={connection.port}
          onChange={(event) => update('port', event.target.value)}
        />
        <datalist id="serial-port-options">
          {detectedPorts.map((port) => <option value={port} key={port} />)}
        </datalist>
      </Field>
      <Field label="波特率">
        <select value={connection.baud} onChange={(event) => update('baud', event.target.value)}>
          {baudRates.map((rate) => <option key={rate}>{rate}</option>)}
        </select>
      </Field>
      <Field label="地址">
        <input value={connection.address} onChange={(event) => update('address', event.target.value)} />
      </Field>
      <Field label="版本">
        <select value={connection.profile} onChange={(event) => update('profile', event.target.value)}>
          {profiles.map((profile) => <option key={profile}>{profile}</option>)}
        </select>
      </Field>
    </section>
  );
}

function DevicePanel({ api, payloadWithConnection, busy }) {
  return (
    <section className="panel tool-panel">
      <PanelTitle icon={Cpu} title="设备" />
      <div className="button-row">
        <ActionButton icon={RefreshCw} busy={busy === 'Device info'} onClick={() => api('/api/device/info', payloadWithConnection(), 'Device info')}>
          读取信息
        </ActionButton>
        <ActionButton icon={RotateCcw} variant="ghost" busy={busy === 'Reset device'} onClick={() => api('/api/device/reset', payloadWithConnection(), 'Reset device')}>
          复位
        </ActionButton>
      </div>
    </section>
  );
}

function UniversalAcPanel({ api, payloadWithConnection, busy }) {
  const [form, setForm] = useState({
    brand: 'gree1',
    power: 'on',
    mode: 'cool',
    temp: 24,
    fan: 'auto'
  });
  const update = (key, value) => setForm((current) => ({ ...current, [key]: value }));
  const payload = () => payloadWithConnection(form);

  return (
    <section className="panel tool-panel">
      <PanelTitle icon={Thermometer} title="万能空调" />
      <div className="form-grid">
        <Field label="品牌">
          <select value={form.brand} onChange={(event) => update('brand', event.target.value)}>
            {brandOptions.map(([value, label]) => <option key={value} value={value}>{label}</option>)}
          </select>
        </Field>
        <Field label="电源">
          <Segmented value={form.power} options={powerModes} onChange={(value) => update('power', value)} />
        </Field>
        <Field label="模式">
          <select value={form.mode} onChange={(event) => update('mode', event.target.value)}>
            {modes.map((mode) => <option key={mode}>{mode}</option>)}
          </select>
        </Field>
        <Field label="温度">
          <Stepper value={form.temp} min={16} max={30} onChange={(value) => update('temp', value)} />
        </Field>
        <Field label="风量">
          <select value={form.fan} onChange={(event) => update('fan', event.target.value)}>
            {fans.map((fan) => <option key={fan}>{fan}</option>)}
          </select>
        </Field>
      </div>
      <div className="button-row">
        <ActionButton icon={Zap} variant="ghost" busy={busy === 'AC dry run'} onClick={() => api('/api/ac/dry-run', payload(), 'AC dry run')}>
          生成
        </ActionButton>
        <ActionButton icon={Send} busy={busy === 'AC send'} onClick={() => api('/api/ac/send', payload(), 'AC send')}>
          发射
        </ActionButton>
      </div>
    </section>
  );
}

function OfficialAcPanel({ api, payloadWithConnection, protocols, busy }) {
  const [protocol, setProtocol] = useState('gree3');
  const [fieldValues, setFieldValues] = useState(() => defaultFieldValues('gree3'));
  const schema = fieldsForProtocol(protocol);
  const fieldsText = useMemo(() => (
    schema.map((item) => `${item.offset}=${fieldValues[item.offset] ?? item.defaultValue}`).join(',')
  ), [schema, fieldValues]);

  const updateProtocol = (value) => {
    setProtocol(value);
    setFieldValues(defaultFieldValues(value));
  };
  const updateField = (offset, value) => {
    setFieldValues((current) => ({ ...current, [offset]: Number(value) }));
  };
  const payload = () => {
    return payloadWithConnection({ protocol, fields: fieldsText });
  };

  const protocolList = protocols.length ? protocols : ['gree3', 'midea1', 'midea2'];

  return (
    <section className="panel tool-panel">
      <PanelTitle icon={SlidersHorizontal} title="官方空调" />
      <div className="form-grid">
        <Field label="协议">
          <select value={protocol} onChange={(event) => updateProtocol(event.target.value)}>
            {protocolList.map((protocol) => <option key={protocol}>{protocol}</option>)}
          </select>
        </Field>
        {schema.map((item) => (
          <Field label={`${item.label} (${item.offset})`} key={`${protocol}-${item.offset}`}>
            <select
              value={fieldValues[item.offset] ?? item.defaultValue}
              onChange={(event) => updateField(item.offset, event.target.value)}
            >
              {item.options.map((option) => (
                <option value={option.value} key={option.value}>{option.label}</option>
              ))}
            </select>
          </Field>
        ))}
        <Field label="字段" wide>
          <input value={fieldsText} readOnly />
        </Field>
      </div>
      <div className="button-row">
        <ActionButton icon={Zap} variant="ghost" busy={busy === 'Official dry run'} onClick={() => api('/api/ac-official/dry-run', payload(), 'Official dry run')}>
          生成
        </ActionButton>
        <ActionButton icon={Send} busy={busy === 'Official send'} onClick={() => api('/api/ac-official/send', payload(), 'Official send')}>
          发射
        </ActionButton>
      </div>
    </section>
  );
}

function LearnPanel({ api, payloadWithConnection, refreshCodes, codes, busy }) {
  const [external, setExternal] = useState({ name: 'tv_power', timeoutMs: 15000 });
  const [internal, setInternal] = useState({ slot: 0, timeoutMs: 15000 });
  const updateExternal = (key, value) => setExternal((current) => ({ ...current, [key]: value }));
  const updateInternal = (key, value) => setInternal((current) => ({ ...current, [key]: value }));

  const learnExternal = async () => {
    await api('/api/external/learn', payloadWithConnection(external), 'Learn external');
    await refreshCodes();
  };

  return (
    <section className="panel tool-panel">
      <PanelTitle icon={BookOpen} title="学习码库" />
      <div className="dual-grid">
        <div className="subtool">
          <h3><Radio size={17} /> 外部码</h3>
          <Field label="名称">
            <input value={external.name} onChange={(event) => updateExternal('name', event.target.value)} />
          </Field>
          <Field label="超时">
            <Stepper value={external.timeoutMs} min={1000} max={120000} step={1000} onChange={(value) => updateExternal('timeoutMs', value)} />
          </Field>
          <div className="button-row compact">
            <ActionButton icon={Save} busy={busy === 'Learn external'} onClick={learnExternal}>学习</ActionButton>
            <ActionButton icon={RefreshCw} variant="ghost" busy={busy === 'Refresh codes'} onClick={refreshCodes}>刷新</ActionButton>
          </div>
        </div>
        <div className="subtool">
          <h3><Download size={17} /> 内部槽</h3>
          <Field label="槽位">
            <Stepper value={internal.slot} min={0} max={95} onChange={(value) => updateInternal('slot', value)} />
          </Field>
          <Field label="超时">
            <Stepper value={internal.timeoutMs} min={1000} max={120000} step={1000} onChange={(value) => updateInternal('timeoutMs', value)} />
          </Field>
          <div className="button-row compact">
            <ActionButton icon={Save} busy={busy === 'Learn internal'} onClick={() => api('/api/internal/learn', payloadWithConnection(internal), 'Learn internal')}>学习</ActionButton>
            <ActionButton icon={Play} variant="ghost" busy={busy === 'Send internal'} onClick={() => api('/api/internal/send', payloadWithConnection(internal), 'Send internal')}>发射</ActionButton>
          </div>
        </div>
      </div>
      <CodeList api={api} payloadWithConnection={payloadWithConnection} codes={codes} busy={busy} />
    </section>
  );
}

function CodeList({ api, payloadWithConnection, codes, busy }) {
  return (
    <div className="code-list">
      {codes.length === 0 && <div className="empty-state">No saved codes</div>}
      {codes.map((file) => (
        <div className="code-row" key={file}>
          <span>{file}</span>
          <div>
            <IconButton title="dump" icon={BookOpen} disabled={busy === 'Dump code'} onClick={() => api('/api/external/dump', { file }, 'Dump code')} />
            <IconButton title="send" icon={Send} disabled={busy === 'Send external'} onClick={() => api('/api/external/send', payloadWithConnection({ file }), 'Send external')} />
          </div>
        </div>
      ))}
    </div>
  );
}

function OutputPanel({ result }) {
  const content = result.stdout || result.stderr || 'No output';
  return (
    <aside className="panel output-panel">
      <PanelTitle icon={Lightbulb} title="输出" />
      <pre className={result.ok ? '' : 'error-text'}>{content}</pre>
      {result.stderr && <pre className="error-text">{result.stderr}</pre>}
    </aside>
  );
}

function PanelTitle({ icon: Icon, title }) {
  return (
    <div className="panel-title">
      <Icon size={20} />
      <h2>{title}</h2>
    </div>
  );
}

function Field({ label, children, wide = false }) {
  return (
    <label className={wide ? 'field wide' : 'field'}>
      <span>{label}</span>
      {children}
    </label>
  );
}

function Segmented({ value, options, onChange }) {
  return (
    <div className="segmented">
      {options.map((option) => (
        <button key={option} type="button" className={value === option ? 'selected' : ''} onClick={() => onChange(option)}>
          {option}
        </button>
      ))}
    </div>
  );
}

function Stepper({ value, min, max, step = 1, onChange }) {
  const numeric = Number(value);
  const next = (delta) => {
    const candidate = Math.min(max, Math.max(min, numeric + delta));
    onChange(candidate);
  };
  return (
    <div className="stepper">
      <button type="button" onClick={() => next(-step)}>-</button>
      <input
        type="number"
        value={value}
        min={min}
        max={max}
        step={step}
        onChange={(event) => onChange(Number(event.target.value))}
      />
      <button type="button" onClick={() => next(step)}>+</button>
    </div>
  );
}

function ActionButton({ icon: Icon, children, onClick, busy, variant = 'primary' }) {
  return (
    <button type="button" className={`action-button ${variant}`} onClick={onClick} disabled={busy}>
      <Icon size={18} />
      <span>{busy ? '处理中' : children}</span>
    </button>
  );
}

function IconButton({ icon: Icon, title, onClick, disabled }) {
  return (
    <button type="button" className="icon-button" title={title} aria-label={title} onClick={onClick} disabled={disabled}>
      <Icon size={17} />
    </button>
  );
}

createRoot(document.getElementById('root')).render(<App />);
