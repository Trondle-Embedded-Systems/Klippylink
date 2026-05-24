'use strict';

// ── Constants ────────────────────────────────────────────────────────────────

const EFFECTS = [
  { id: 0, label: 'Static',       color: '#4a90d9' },
  { id: 1, label: 'Flash',        color: '#e8a838' },
  { id: 2, label: 'Rainbow',      color: '#a56ef5' },
  { id: 3, label: 'Sweep/Chase',  color: '#3ccfa0' },
];

const POWER_LABELS = [
  '−20 dBm', '−16 dBm', '−12 dBm', '−8 dBm',
  '−4 dBm',  '0 dBm',  '+4 dBm',  '+9 dBm',
];

// ── State ────────────────────────────────────────────────────────────────────

const state = {
  nodes:        [],
  selectedNode: null,
  activeTab:    'zones',
  nodeZones:    {},   // nodeName → LedZone[]
  nodeRadio:    {},   // nodeName → RadioSettings
  drag: {
    active:     false,
    stripIndex: 0,
    start:      -1,
    end:        -1,
  },
};

// ── API helpers ──────────────────────────────────────────────────────────────

async function apiFetch(path, opts = {}) {
  const res = await fetch(path, {
    headers: { 'Content-Type': 'application/json' },
    ...opts,
  });
  if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
  return res.json();
}

const api = {
  nodes:      ()           => apiFetch('/api/nodes'),
  zones:      (n)          => apiFetch(`/api/nodes/${n}/leds/zones`),
  setZone:    (n, body)    => apiFetch(`/api/nodes/${n}/leds/zones`, { method: 'POST', body: JSON.stringify(body) }),
  clearZone:  (n, zid)     => apiFetch(`/api/nodes/${n}/leds/zones/${zid}`, { method: 'DELETE' }),
  getRadio:   (n)          => apiFetch(`/api/nodes/${n}/radio`),
  setRadio:   (n, body)    => apiFetch(`/api/nodes/${n}/radio`, { method: 'PUT', body: JSON.stringify(body) }),
};

// ── Polling ──────────────────────────────────────────────────────────────────

async function pollNodes() {
  try {
    const nodes = await api.nodes();
    state.nodes = nodes;
    renderNodeList();
    setConnStatus(true);

    if (state.selectedNode) {
      const still = nodes.find(n => n.name === state.selectedNode);
      if (!still || !still.connected) deselectNode();
      else await refreshNodeData(state.selectedNode);
    }
  } catch {
    setConnStatus(false);
  }
}

async function refreshNodeData(name) {
  const [zones, radio] = await Promise.all([
    api.zones(name).catch(() => state.nodeZones[name] || []),
    api.getRadio(name).catch(() => state.nodeRadio[name] || defaultRadio()),
  ]);
  state.nodeZones[name] = zones;
  state.nodeRadio[name] = radio;
  renderActiveTab();
}

function defaultRadio() {
  return { power: 4, channel: 80, conn_interval_ms: 20, telemetry_enabled: true };
}

// ── Connection status ─────────────────────────────────────────────────────────

function setConnStatus(ok) {
  const dot   = document.getElementById('conn-dot');
  const label = document.getElementById('conn-label');
  dot.className   = 'status-dot ' + (ok ? 'ok' : 'err');
  label.textContent = ok ? 'Connected' : 'Disconnected';
}

// ── Node list ────────────────────────────────────────────────────────────────

function renderNodeList() {
  const ul = document.getElementById('node-list');
  const connected = state.nodes.filter(n => n.connected);

  if (!connected.length) {
    ul.innerHTML = '<li class="node-item node-empty">No nodes connected</li>';
    return;
  }

  ul.innerHTML = connected.map(n => `
    <li class="node-item ${n.name === state.selectedNode ? 'active' : ''}"
        data-name="${n.name}" onclick="selectNode('${n.name}')">
      <span class="node-dot on"></span>
      <span class="node-name" title="${n.name}">${n.name}</span>
    </li>
  `).join('');
}

function selectNode(name) {
  state.selectedNode = name;
  renderNodeList();
  document.getElementById('no-node-msg').hidden  = true;
  document.getElementById('node-content').hidden = false;
  refreshNodeData(name);
}

function deselectNode() {
  state.selectedNode = null;
  document.getElementById('no-node-msg').hidden  = false;
  document.getElementById('node-content').hidden = true;
  renderNodeList();
}

// ── Tabs ─────────────────────────────────────────────────────────────────────

document.querySelectorAll('.tab').forEach(btn => {
  btn.addEventListener('click', () => {
    state.activeTab = btn.dataset.tab;
    document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t === btn));
    document.querySelectorAll('.tab-panel').forEach(p =>
      p.classList.toggle('active', p.id === `panel-${state.activeTab}`));
    renderActiveTab();
  });
});

function renderActiveTab() {
  switch (state.activeTab) {
    case 'zones':   renderZonesTab();   break;
    case 'effects': renderEffectsTab(); break;
    case 'radio':   renderRadioTab();   break;
  }
}

// ── LED Zones tab ─────────────────────────────────────────────────────────────

function renderZonesTab() {
  const name = state.selectedNode;
  if (!name) return;

  const node  = state.nodes.find(n => n.name === name);
  const zones = state.nodeZones[name] || [];
  const strips = node?.capabilities?.neopixels || [];

  renderStrips(strips, zones);
  renderZoneList(zones);
  renderEffectsTab();
}

function renderStrips(strips, zones) {
  const container = document.getElementById('strips-container');

  if (!strips.length) {
    container.innerHTML = '<p class="muted">No LED strips reported by this node. Connect the node and wait for device info.</p>';
    return;
  }

  container.innerHTML = strips.map((strip, si) => `
    <div class="strip-block">
      <div class="strip-name">${strip.name} &mdash; ${strip.count} LEDs (pin ${strip.pin})</div>
      <div class="led-chain" id="chain-${si}">${buildLedHtml(strip.count, si, zones)}</div>
      <div class="strip-hint">Click and drag to select LEDs and create a zone</div>
    </div>
  `).join('');

  strips.forEach((_, si) => attachChainEvents(si, strips[si].count));
}

function buildLedHtml(count, stripIndex, zones) {
  const ledColors = new Array(count).fill(null);
  zones.filter(z => z.strip === stripIndex).forEach(z => {
    for (let i = z.start; i < z.start + z.count && i < count; i++) {
      ledColors[i] = { hex: rgbToHex(z.color), zone: z };
    }
  });

  return Array.from({ length: count }, (_, i) => {
    const lc = ledColors[i];
    const style = lc ? `background:${lc.hex};` : '';
    const cls   = lc ? 'led in-zone' : 'led';
    return `
      <div class="${cls}" data-index="${i}" data-strip="${stripIndex}"
           style="${style}" title="">
        <span class="led-index-tip">${i}</span>
      </div>`;
  }).join('');
}

function rgbToHex([r, g, b]) {
  return '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
}

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff];
}

// ── Drag-to-select on LED chain ───────────────────────────────────────────────

function attachChainEvents(stripIndex, count) {
  const chain = document.getElementById(`chain-${stripIndex}`);
  if (!chain) return;

  function ledAt(el) {
    const led = el.closest('.led');
    return led ? parseInt(led.dataset.index) : -1;
  }

  chain.addEventListener('mousedown', e => {
    const idx = ledAt(e.target);
    if (idx < 0) return;
    state.drag = { active: true, stripIndex, start: idx, end: idx };
    e.preventDefault();
    updateSelectionHighlight();
  });

  chain.addEventListener('mousemove', e => {
    if (!state.drag.active || state.drag.stripIndex !== stripIndex) return;
    const idx = ledAt(e.target);
    if (idx >= 0 && idx !== state.drag.end) {
      state.drag.end = idx;
      updateSelectionHighlight();
    }
  });

  chain.addEventListener('mouseup', e => {
    if (!state.drag.active || state.drag.stripIndex !== stripIndex) return;
    state.drag.active = false;
    const lo = Math.min(state.drag.start, state.drag.end);
    const hi = Math.max(state.drag.start, state.drag.end);
    clearSelectionHighlight();
    openZoneDialog(null, stripIndex, lo, hi - lo + 1);
  });
}

document.addEventListener('mouseup', () => {
  if (state.drag.active) {
    state.drag.active = false;
    clearSelectionHighlight();
  }
});

function updateSelectionHighlight() {
  const si  = state.drag.stripIndex;
  const lo  = Math.min(state.drag.start, state.drag.end);
  const hi  = Math.max(state.drag.start, state.drag.end);
  const chain = document.getElementById(`chain-${si}`);
  if (!chain) return;
  chain.querySelectorAll('.led').forEach(led => {
    const idx = parseInt(led.dataset.index);
    led.classList.toggle('selecting', idx >= lo && idx <= hi);
  });
}

function clearSelectionHighlight() {
  document.querySelectorAll('.led.selecting').forEach(l => l.classList.remove('selecting'));
}

// ── Zone list ─────────────────────────────────────────────────────────────────

function renderZoneList(zones) {
  const container = document.getElementById('zone-list');
  if (!zones.length) {
    container.innerHTML = '<p class="muted zone-empty">No zones configured yet. Drag across the LEDs above to create one.</p>';
    return;
  }

  container.innerHTML = zones.map(z => {
    const eff    = EFFECTS.find(e => e.id === z.effect) || EFFECTS[0];
    const hex    = rgbToHex(z.color);
    const meta   = `Strip ${z.strip} · LEDs ${z.start}–${z.start + z.count - 1} · ${eff.label}`;
    return `
      <div class="zone-card">
        <div class="zone-swatch" style="background:${hex};"></div>
        <div class="zone-info">
          <div class="zone-info-name">${z.name || `zone_${z.zone_id}`}</div>
          <div class="zone-info-meta">${meta}</div>
        </div>
        <div class="zone-actions">
          <button class="btn btn-ghost btn-sm" onclick="editZone(${z.zone_id})">Edit</button>
          <button class="btn btn-ghost btn-sm" onclick="applyZone(${z.zone_id})">Apply</button>
        </div>
      </div>`;
  }).join('');
}

// ── Zone dialog ───────────────────────────────────────────────────────────────

function openZoneDialog(existingZone, stripIndex, start, count) {
  const dlg = document.getElementById('zone-dialog');

  if (existingZone) {
    document.getElementById('dialog-title').textContent = 'Edit Zone';
    document.getElementById('z-zone-id').value   = existingZone.zone_id;
    document.getElementById('z-name').value      = existingZone.name;
    document.getElementById('z-color1').value    = rgbToHex(existingZone.color);
    document.getElementById('z-color2').value    = rgbToHex(existingZone.color2);
    document.getElementById('z-effect').value    = existingZone.effect;
    document.getElementById('z-brightness').value = existingZone.brightness;
    document.getElementById('z-speed').value     = existingZone.speed;
    document.getElementById('z-strip').value     = existingZone.strip;
    document.getElementById('z-start').value     = existingZone.start;
    document.getElementById('z-count').value     = existingZone.count;
    document.getElementById('btn-zone-delete').hidden = false;
  } else {
    document.getElementById('dialog-title').textContent = `New Zone — LEDs ${start}–${start + count - 1}`;
    const zones = state.nodeZones[state.selectedNode] || [];
    const nextId = zones.length ? Math.max(...zones.map(z => z.zone_id)) + 1 : 0;
    document.getElementById('z-zone-id').value    = nextId;
    document.getElementById('z-name').value       = '';
    document.getElementById('z-color1').value     = '#00bfff';
    document.getElementById('z-color2').value     = '#000000';
    document.getElementById('z-effect').value     = '0';
    document.getElementById('z-brightness').value = '255';
    document.getElementById('z-speed').value      = '128';
    document.getElementById('z-strip').value      = stripIndex;
    document.getElementById('z-start').value      = start;
    document.getElementById('z-count').value      = count;
    document.getElementById('btn-zone-delete').hidden = true;
  }

  syncDialogSliders();
  dlg.showModal();
}

function syncDialogSliders() {
  document.getElementById('z-brightness-val').textContent =
    document.getElementById('z-brightness').value;
  document.getElementById('z-speed-val').textContent =
    document.getElementById('z-speed').value;
}

document.getElementById('z-brightness').addEventListener('input', syncDialogSliders);
document.getElementById('z-speed').addEventListener('input', syncDialogSliders);

document.getElementById('btn-zone-cancel').addEventListener('click', () => {
  document.getElementById('zone-dialog').close();
});

document.getElementById('btn-zone-save').addEventListener('click', async () => {
  const name = state.selectedNode;
  if (!name) return;

  const zone = {
    zone_id:    parseInt(document.getElementById('z-zone-id').value),
    strip:      parseInt(document.getElementById('z-strip').value),
    start:      parseInt(document.getElementById('z-start').value),
    count:      parseInt(document.getElementById('z-count').value),
    effect:     parseInt(document.getElementById('z-effect').value),
    color:      hexToRgb(document.getElementById('z-color1').value),
    color2:     hexToRgb(document.getElementById('z-color2').value),
    brightness: parseInt(document.getElementById('z-brightness').value),
    speed:      parseInt(document.getElementById('z-speed').value),
    name:       document.getElementById('z-name').value.trim(),
  };

  try {
    await api.setZone(name, zone);
    document.getElementById('zone-dialog').close();
    await refreshNodeData(name);
  } catch (err) {
    alert('Failed to save zone: ' + err.message);
  }
});

document.getElementById('btn-zone-delete').addEventListener('click', async () => {
  const name   = state.selectedNode;
  const zoneId = parseInt(document.getElementById('z-zone-id').value);
  if (!name || !confirm('Delete this zone?')) return;
  try {
    await api.clearZone(name, zoneId);
    document.getElementById('zone-dialog').close();
    await refreshNodeData(name);
  } catch (err) {
    alert('Failed to delete zone: ' + err.message);
  }
});

// Global edit/apply helpers (called from innerHTML onclick)
window.editZone = (zoneId) => {
  const zones = state.nodeZones[state.selectedNode] || [];
  const zone  = zones.find(z => z.zone_id === zoneId);
  if (zone) openZoneDialog(zone, zone.strip, zone.start, zone.count);
};

window.applyZone = async (zoneId) => {
  const name  = state.selectedNode;
  const zones = state.nodeZones[name] || [];
  const zone  = zones.find(z => z.zone_id === zoneId);
  if (!zone) return;
  try {
    await api.setZone(name, zone);
  } catch (err) {
    alert('Failed to apply zone: ' + err.message);
  }
};

document.getElementById('btn-apply-all').addEventListener('click', async () => {
  const name  = state.selectedNode;
  const zones = state.nodeZones[name] || [];
  try {
    await Promise.all(zones.map(z => api.setZone(name, z)));
  } catch (err) {
    alert('Failed to apply zones: ' + err.message);
  }
});

// ── Effects tab ───────────────────────────────────────────────────────────────

function renderEffectsTab() {
  const name  = state.selectedNode;
  const zones = name ? (state.nodeZones[name] || []) : [];

  const emptyEl    = document.getElementById('timeline-empty');
  const timelineEl = document.getElementById('timeline');

  if (!zones.length) {
    emptyEl.hidden    = false;
    timelineEl.hidden = true;
    return;
  }

  emptyEl.hidden    = true;
  timelineEl.hidden = false;

  timelineEl.innerHTML = zones.map(z => {
    const eff   = EFFECTS.find(e => e.id === z.effect) || EFFECTS[0];
    const hex   = rgbToHex(z.color);
    return `
      <div class="timeline-track">
        <div class="track-label" title="${z.name || `zone_${z.zone_id}`}">
          ${z.name || `zone_${z.zone_id}`}
        </div>
        <div class="track-body">
          <button class="effect-chip"
                  style="background:${eff.color}22; color:${eff.color}; border:1px solid ${eff.color}44;"
                  onclick="editZoneEffect(${z.zone_id})">
            <span class="effect-dot" style="background:${hex};"></span>
            ${eff.label}
            ${z.speed !== 128 ? `<span style="opacity:.6">· spd ${z.speed}</span>` : ''}
          </button>
          <button class="effect-chip-add" onclick="editZoneEffect(${z.zone_id})">+ Edit effect</button>
        </div>
      </div>`;
  }).join('');
}

window.editZoneEffect = (zoneId) => {
  const zones = state.nodeZones[state.selectedNode] || [];
  const zone  = zones.find(z => z.zone_id === zoneId);
  if (zone) openZoneDialog(zone, zone.strip, zone.start, zone.count);
};

// ── Radio tab ─────────────────────────────────────────────────────────────────

function renderRadioTab() {
  const name  = state.selectedNode;
  const radio = (name && state.nodeRadio[name]) || defaultRadio();

  document.getElementById('radio-power').value    = radio.power;
  document.getElementById('radio-channel').value  = radio.channel;
  document.getElementById('radio-interval').value = radio.conn_interval_ms;
  document.getElementById('radio-telemetry').checked = radio.telemetry_enabled;
  syncPowerLabel();
}

function syncPowerLabel() {
  const val = parseInt(document.getElementById('radio-power').value);
  document.getElementById('radio-power-val').textContent = POWER_LABELS[val] || '';
}

document.getElementById('radio-power').addEventListener('input', syncPowerLabel);

document.getElementById('btn-radio-save').addEventListener('click', async () => {
  const name = state.selectedNode;
  if (!name) return;

  const body = {
    power:             parseInt(document.getElementById('radio-power').value),
    channel:           parseInt(document.getElementById('radio-channel').value),
    conn_interval_ms:  parseInt(document.getElementById('radio-interval').value),
    telemetry_enabled: document.getElementById('radio-telemetry').checked,
  };

  try {
    await api.setRadio(name, body);
    state.nodeRadio[name] = body;
    const s = document.getElementById('radio-save-status');
    s.textContent = 'Settings applied';
    s.classList.add('visible');
    setTimeout(() => s.classList.remove('visible'), 2500);
  } catch (err) {
    alert('Failed to apply radio settings: ' + err.message);
  }
});

// ── Boot ──────────────────────────────────────────────────────────────────────

pollNodes();
setInterval(pollNodes, 3000);
