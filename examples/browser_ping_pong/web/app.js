/**
 * Browser Aether Ping/Pong UI wiring.
 * Loads the Emscripten Module and libsodium; does NOT implement Aether protocol.
 */

const STAT_KEYS = [
  'sent',
  'pong_received',
  'timed_out',
  'duplicate',
  'out_of_order',
  'rtt_latest_ms',
  'rtt_min_ms',
  'rtt_avg_ms',
  'rtt_p50_ms',
  'rtt_p90_ms',
  'rtt_p95_ms',
  'rtt_p99_ms',
  'rtt_max_ms',
  'transport',
  'reconnect_count',
];

function qs(id) {
  return document.getElementById(id);
}

function selectedRadio(name) {
  const el = document.querySelector(`input[name="${name}"]:checked`);
  return el ? el.value : '';
}

function profileName() {
  const mode = selectedRadio('profile');
  if (mode === 'custom') {
    return (qs('profile-custom').value || 'custom').trim() || 'custom';
  }
  return mode || 'A';
}

function applyProfileFromUrl() {
  const params = new URLSearchParams(window.location.search);
  const profile = params.get('profile');
  if (!profile) {
    return;
  }
  if (profile === 'A' || profile === 'B') {
    const radio = document.querySelector(`input[name="profile"][value="${profile}"]`);
    if (radio) {
      radio.checked = true;
    }
    return;
  }
  const custom = document.querySelector('input[name="profile"][value="custom"]');
  if (custom) {
    custom.checked = true;
    qs('profile-custom').disabled = false;
    qs('profile-custom').value = profile;
  }
}

function applyTransportFromUrl() {
  const params = new URLSearchParams(window.location.search);
  const transport = params.get('transport');
  if (!transport) {
    return;
  }
  const radio = document.querySelector(
    `input[name="transport"][value="${transport.toUpperCase()}"]`,
  );
  if (radio) {
    radio.checked = true;
  }
  const gateway = params.get('gateway');
  if (gateway) {
    qs('gateway').value = gateway;
  }
}

function setBootError(message) {
  const el = qs('boot-error');
  el.hidden = !message;
  el.textContent = message || '';
}

function utf8FromPtr(Module, ptr) {
  if (!ptr) {
    return '';
  }
  return Module.UTF8ToString(ptr);
}

function refreshUi(api, Module) {
  const state = utf8FromPtr(Module, api.getState());
  const stateEl = qs('state');
  stateEl.textContent = state;
  const short = state.split(':')[0].trim();
  stateEl.dataset.state = short;

  const uid = utf8FromPtr(Module, api.getUid());
  qs('my-uid').value = uid;

  let stats = {};
  try {
    const raw = utf8FromPtr(Module, api.getStats());
    stats = raw ? JSON.parse(raw) : {};
  } catch (e) {
    stats = {};
  }
  for (const key of STAT_KEYS) {
    const dd = document.querySelector(`[data-stat="${key}"]`);
    if (!dd) {
      continue;
    }
    const value = stats[key];
    dd.textContent = value === undefined || value === null ? '' : String(value);
  }
}

async function loadSodium() {
  // Script-tag UMD path: libsodium core then wrappers (sets window.sodium).
  if (typeof window !== 'undefined' && window.sodium && window.sodium.ready) {
    await window.sodium.ready;
    return window.sodium;
  }

  async function loadScript(src) {
    await new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = src;
      s.async = false;
      s.onload = () => resolve();
      s.onerror = () => reject(new Error('failed to load ' + src));
      document.head.appendChild(s);
    });
  }

  try {
    await loadScript('./vendor/libsodium/dist/modules/libsodium.js');
    await loadScript('./vendor/libsodium/libsodium-wrappers.js');
    if (window.sodium && window.sodium.ready) {
      await window.sodium.ready;
      return window.sodium;
    }
  } catch (e) {
    console.warn('UMD sodium load failed', e);
  }

  console.warn('libsodium wrappers not found; Module.aeSodium unset');
  return null;
}

async function createModule(sodium) {
  const factory = (await import('./aether_browser_ping_pong.js')).default;
  return factory({
    aeSodium: sodium || undefined,
    locateFile(path) {
      return path;
    },
  });
}

function bindControls(api, Module) {
  document.querySelectorAll('input[name="profile"]').forEach((el) => {
    el.addEventListener('change', () => {
      qs('profile-custom').disabled = selectedRadio('profile') !== 'custom';
    });
  });

  qs('copy-uid').addEventListener('click', async () => {
    const uid = qs('my-uid').value;
    if (!uid) {
      return;
    }
    try {
      await navigator.clipboard.writeText(uid);
    } catch (e) {
      qs('my-uid').select();
      document.execCommand('copy');
    }
  });

  qs('connect').addEventListener('click', () => {
    api.setRemoteUid(qs('remote-uid').value.trim());
    api.connect();
    refreshUi(api, Module);
  });

  qs('send-ping').addEventListener('click', () => {
    api.sendPing();
    refreshUi(api, Module);
  });

  qs('start-periodic').addEventListener('click', () => {
    const interval = Number(qs('interval-ms').value) || 200;
    api.startPeriodic(interval);
  });

  qs('stop').addEventListener('click', () => {
    api.stop();
  });

  qs('clear-profile').addEventListener('click', () => {
    api.clearProfile();
    refreshUi(api, Module);
  });
}

function makeApi(Module) {
  const cwrap = Module.cwrap.bind(Module);
  return {
    configure: cwrap('aether_bpp_configure', null, ['string', 'string', 'string']),
    start: cwrap('aether_bpp_start', null, []),
    setRemoteUid: cwrap('aether_bpp_set_remote_uid', null, ['string']),
    connect: cwrap('aether_bpp_connect', null, []),
    sendPing: cwrap('aether_bpp_send_ping', null, []),
    startPeriodic: cwrap('aether_bpp_start_periodic', null, ['number']),
    stop: cwrap('aether_bpp_stop', null, []),
    clearProfile: cwrap('aether_bpp_clear_profile', null, []),
    getUid: cwrap('aether_bpp_get_uid', 'number', []),
    getState: cwrap('aether_bpp_get_state', 'number', []),
    getStats: cwrap('aether_bpp_get_stats_json', 'number', []),
    getProfile: cwrap('aether_bpp_get_profile', 'number', []),
  };
}

async function boot() {
  applyProfileFromUrl();
  applyTransportFromUrl();
  qs('profile-custom').disabled = selectedRadio('profile') !== 'custom';

  try {
    const sodium = await loadSodium();
    const Module = await createModule(sodium);
    const api = makeApi(Module);

    const transport = selectedRadio('transport') || 'WS';
    const gateway = qs('gateway').value.trim();
    const profile = profileName();
    api.configure(profile, gateway, transport);
    api.start();

    window.__AETHER_TEST__ = {
      getUid: () => utf8FromPtr(Module, api.getUid()),
      getState: () => utf8FromPtr(Module, api.getState()),
      getStats: () => {
        try {
          return JSON.parse(utf8FromPtr(Module, api.getStats()) || '{}');
        } catch (e) {
          return {};
        }
      },
      getProfile: () => utf8FromPtr(Module, api.getProfile()),
      setRemoteUid: (uid) => api.setRemoteUid(uid),
      connect: () => api.connect(),
      sendPing: () => api.sendPing(),
      startPeriodic: (ms) => api.startPeriodic(ms),
      stop: () => api.stop(),
      clearProfile: () => api.clearProfile(),
      configure: (p, g, t) => api.configure(p, g, t),
      start: () => api.start(),
    };

    bindControls(api, Module);
    setInterval(() => refreshUi(api, Module), 250);
    refreshUi(api, Module);
  } catch (e) {
    console.error(e);
    setBootError(String(e && e.message ? e.message : e));
  }
}

boot();
