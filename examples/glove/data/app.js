let wsHost = location.hostname;
let ws;
let modalContext = 'capture';
let modalSymbol = null;

if (wsHost == 'localhost') wsHost = '192.168.3.141';

const state = {
  connected: false,
  mode: 0,
  threshold: 1,
  outputs: [],
  inputs: [],
  capacitance: new Array(16).fill(0),
  alphabet: [],
  gestures: [],
  chat: [],
  typing: null,
  typingBuffer: '',
  playback: 'idle',
  capture: { active: false, symbol: null, steps: [] },
};

const chatWindow = document.getElementById('chat-window');
const statusPill = document.getElementById('status-pill');
const modeLabel = document.getElementById('mode-label');
const thresholdLabel = document.getElementById('threshold-label');
const statusMode = document.getElementById('status-mode');
const statusBuffer = document.getElementById('status-buffer');
const statusPlayback = document.getElementById('status-playback');
const inputGrid = document.getElementById('input-grid');
const outputGrid = document.getElementById('output-grid');
const inputGridDebug = document.getElementById('input-grid-debug');
const outputGridDebug = document.getElementById('output-grid-debug');
const alphabetGrid = document.getElementById('alphabet-grid');
const playGrid = document.getElementById('play-grid');
const gestureSeq = document.getElementById('gesture-seq');
const gestureCandidates = document.getElementById('gesture-candidates');
const logBox = document.getElementById('log');
const modal = document.getElementById('modal');
const modalTitle = document.getElementById('modal-title');
const modalSubtitle = document.getElementById('modal-subtitle');
const modalSteps = document.getElementById('modal-steps');
const modalSave = document.getElementById('modal-save');
const modalCancel = document.getElementById('modal-cancel');
const modalClose = document.getElementById('modal-close');

function initGrids() {
  [inputGrid, outputGrid, inputGridDebug, outputGridDebug].forEach(grid => {
    if (!grid) return;
    const isInputGrid = grid === inputGrid || grid === inputGridDebug;
    grid.innerHTML = '';
    for (let i = 0; i < 16; i++) {
      const cell = document.createElement('div');
      cell.className = 'io-cell';
      cell.innerHTML = `<span class="pin-number">${i}</span>`;
      cell.dataset.pin = i;
      if (grid === outputGrid || grid === outputGridDebug) {
        cell.onclick = () => toggleOutput(i);
        cell.style.cursor = 'pointer';
      }
      if (isInputGrid) {
        const bar = document.createElement('div');
        bar.className = 'capacitance-bar';
        cell.appendChild(bar);
      }
      grid.appendChild(cell);
    }
  });
}

function updateCapacitanceBars() {
  [inputGrid, inputGridDebug].forEach(grid => {
    if (!grid) return;
    Array.from(grid.children).forEach((cell, idx) => {
      const bar = cell.querySelector('.capacitance-bar');
      if (bar) {
        const value = state.capacitance[idx] || 0;
        const threshold = state.threshold;
        const heightPercent = Math.min(100, (value / (threshold * 2)) * 100);
        bar.style.height = `${heightPercent}%`;
      }
    });
  });
}

function setTab(tab) {
  document.querySelectorAll('.tab').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.tab === tab);
  });
  document.querySelectorAll('.panel').forEach(p => {
    p.classList.toggle('active', p.id === `tab-${tab}`);
  });
}

function appendChat({ from, text, typing }) {
  if (typing) {
    const existing = document.getElementById('typing-bubble');
    if (existing) existing.remove();
    const bubble = document.createElement('div');
    bubble.className = `bubble ${from}`;
    bubble.id = 'typing-bubble';
    bubble.innerHTML = `<div class="typing-dots"><span></span><span></span><span></span></div>`;
    chatWindow.appendChild(bubble);
  } else {
    const bubble = document.createElement('div');
    bubble.className = `bubble ${from}`;
    bubble.textContent = text;
    if (document.getElementById('typing-bubble')) {
      document.getElementById('typing-bubble').remove();
    }
    chatWindow.appendChild(bubble);
  }
  chatWindow.scrollTop = chatWindow.scrollHeight;
}

function toggleOutput(pin) {
  const isActive = state.outputs.includes(pin);
  if (isActive) {
    sendCmd('set_output', { mask: 0 });
    pushLog(`Output ${pin} deactivated`);
  } else {
    const mask = 1 << pin;
    sendCmd('set_output', { mask });
    pushLog(`Output ${pin} activated (mask: ${mask})`);
  }
}

function updateIOGrid(gridEl, activePins) {
  if (!gridEl) return;
  Array.from(gridEl.children).forEach((cell, idx) => {
    cell.classList.toggle('active', activePins.includes(idx));
  });
}

function renderAlphabet() {
  const letters = state.alphabet.length ? state.alphabet : defaultAlphabet();
  alphabetGrid.innerHTML = '';
  letters.forEach(letter => {
    const btn = document.createElement('div');
    btn.className = 'letter-btn';
    btn.textContent = letter;
    btn.onclick = () => openCapture(letter);
    alphabetGrid.appendChild(btn);
  });
  const add = document.createElement('div');
  add.className = 'letter-btn add';
  add.textContent = '+';
  add.onclick = () => {
    const sym = prompt('What character would you like to add? (a single letter or symbol)');
    if (sym && sym.length === 1) {
      openCapture(sym);
    }
  };
  alphabetGrid.appendChild(add);

  // same grid reused for quick play
  playGrid.innerHTML = '';
  letters.forEach(letter => {
    const btn = document.createElement('div');
    btn.className = 'letter-btn';
    btn.textContent = letter;
    btn.onclick = () => openPreview(letter);
    playGrid.appendChild(btn);
  });
}

function defaultAlphabet() {
  const base = [];
  for (let i = 0; i < 26; i++) base.push(String.fromCharCode(97 + i));
  base.push(' ');
  base.push('W');
  return base;
}

function openCapture(symbol) {
  modalContext = 'capture';
  modalSymbol = symbol;
  state.capture = { active: true, symbol, steps: [] };
  modal.classList.remove('hidden');
  modalTitle.textContent = 'Record gesture';
  modalSubtitle.textContent = `Letter: ${symbol}`;
  modalSave.textContent = 'Save';
  modalCancel.textContent = 'Cancel';
  modalSteps.innerHTML = '';
  sendCmd('start_capture', { symbol });
}

function openPreview(symbol) {
  modalContext = 'preview';
  modalSymbol = symbol;
  modal.classList.remove('hidden');
  modalTitle.textContent = 'Current configuration';
  modalSubtitle.textContent = `Letter: ${symbol}`;
  modalSave.textContent = 'Play';
  modalCancel.textContent = 'Close';
  const gesture = (state.gestures || []).find(g => g.symbol === symbol);
  renderCaptureSteps(gesture ? gesture.steps || [] : []);
}

function closeModal() {
  modal.classList.add('hidden');
  modalSymbol = null;
  state.capture = { active: false, symbol: null, steps: [] };
  modalSteps.innerHTML = '';
  if (modalContext === 'capture') {
    sendCmd('cancel_capture');
  }
}

modalCancel.onclick = closeModal;
modalClose.onclick = closeModal;
modalSave.onclick = () => {
  if (modalContext === 'capture') {
    sendCmd('save_capture');
    closeModal();
    setTimeout(() => sendCmd('request_alphabet'), 150);
  } else {
    sendCmd('play_letter', { symbol: modalSymbol });
    closeModal();
  }
};

function renderCaptureSteps(steps) {
  modalSteps.innerHTML = '';
  steps.forEach((step, idx) => {
    const mask = typeof step === 'object' ? step.mask : step;
    const chip = document.createElement('div');
    chip.className = 'step-chip';
    chip.textContent = `#${idx+1}: ${mask.toString(2).padStart(16, '0')}`;
    modalSteps.appendChild(chip);
  });
}

function sanitize(text) {
  return text
    .toLowerCase()
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/[^a-z w]/g, '')
    .trim();
}

function sendCmd(cmd, payload = {}) {
  if (!state.connected) return;
  ws.send(JSON.stringify({ cmd, ...payload }));
}

function connectWS() {
  ws = new WebSocket(`ws://${wsHost}:81`);
  ws.onopen = () => {
    state.connected = true;
    statusPill.textContent = 'Connected';
    statusPill.classList.remove('disconnected');
    statusPill.classList.add('connected');
    sendCmd('request_status');
    sendCmd('request_alphabet');
  };
  ws.onclose = () => {
    state.connected = false;
    statusPill.textContent = 'Disconnected';
    statusPill.classList.add('disconnected');
    statusPill.classList.remove('connected');
    setTimeout(connectWS, 1500);
  };
  ws.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      handleMessage(msg);
    } catch (e) {
      console.error(e);
    }
  };
}

function handleMessage(msg) {
  switch (msg.type) {
    case 'status':
      state.mode = msg.mode ?? state.mode;
      state.threshold = msg.threshold ?? state.threshold;
      modeLabel.textContent = state.mode;
      statusMode.textContent = state.mode;
      thresholdLabel.textContent = state.threshold;
      statusPlayback.textContent = msg.playback_active ? 'playing' : 'idle';
      statusBuffer.textContent = msg.buffer || '-';
      break;
    case 'message':
      appendChat({ from: msg.direction === 'inbound' ? 'me' : 'device', text: msg.text });
      pushLog(`Message ${msg.direction}: ${msg.text}`);
      statusBuffer.textContent = '-';
      break;
    case 'typing':
      statusBuffer.textContent = msg.buffer || '-';
      if (msg.state === 'typing') {
        appendChat({ from: 'device', typing: true });
      } else if (msg.state === 'done' || msg.state === 'cancel') {
        const dot = document.getElementById('typing-bubble');
        if (dot) dot.remove();
      }
      break;
    case 'symbol':
      pushLog(`Symbol detected: ${msg.value}`);
      break;
    case 'input':
      state.inputs = msg.pins || [];
      if (msg.capacitance) state.capacitance = msg.capacitance;
      updateIOGrid(inputGrid, state.inputs);
      updateIOGrid(inputGridDebug, state.inputs);
      updateCapacitanceBars();
      document.getElementById('input-state').textContent = `Active inputs: ${state.inputs.join(', ') || 'none'}`;
      break;
    case 'input_idle':
      state.inputs = [];
      state.capacitance = new Array(16).fill(0);
      updateIOGrid(inputGrid, []);
      updateIOGrid(inputGridDebug, []);
      updateCapacitanceBars();
      document.getElementById('input-state').textContent = 'No input detected';
      break;
    case 'output':
      state.outputs = msg.pins || [];
      updateIOGrid(outputGrid, msg.pins || []);
      updateIOGrid(outputGridDebug, msg.pins || []);
      break;
    case 'alphabet':
      state.gestures = msg.gestures || [];
      state.alphabet = state.gestures.map(g => g.symbol);
      renderAlphabet();
      break;
    case 'gesture_state':
      renderGestureState(msg);
      break;
    case 'capture':
      if (modalContext === 'capture') {
        state.capture.steps = msg.steps || [];
        renderCaptureSteps(state.capture.steps);
      }
      break;
    case 'log':
      pushLog(msg.msg);
      break;
  }
}

function renderGestureState(msg) {
  gestureSeq.innerHTML = '';
  (msg.sequence || []).forEach((mask, idx) => {
    const chip = document.createElement('div');
    chip.className = 'step-chip';
    chip.textContent = `#${idx+1}: ${mask.toString(2).padStart(16, '0')}`;
    gestureSeq.appendChild(chip);
  });
  gestureCandidates.innerHTML = '';
  (msg.candidates || []).forEach(c => {
    const badge = document.createElement('div');
    badge.className = 'badge';
    badge.textContent = c;
    gestureCandidates.appendChild(badge);
  });
  if (msg.expired) pushLog('Gesture not recognized: vibrating "w"');
}

function pushLog(text) {
  const line = document.createElement('div');
  line.className = 'log-line';
  line.textContent = `[${new Date().toLocaleTimeString()}] ${text}`;
  logBox.appendChild(line);
  logBox.scrollTop = logBox.scrollHeight;
}

// UI bindings
document.querySelectorAll('.tab').forEach(btn => {
  btn.onclick = () => setTab(btn.dataset.tab);
});

document.getElementById('chat-send').onclick = () => {
  const input = document.getElementById('chat-input');
  const clean = sanitize(input.value);
  if (!clean) return;
  appendChat({ from: 'me', typing: true });
  sendCmd('send_message', { text: clean });
  input.value = '';
};
document.getElementById('chat-input').addEventListener('keydown', (e) => {
  if (e.key === 'Enter') document.getElementById('chat-send').click();
});

document.getElementById('threshold-slider').addEventListener('input', (e) => {
  thresholdLabel.textContent = e.target.value;
  sendCmd('set_threshold', { value: Number(e.target.value) });
});

document.getElementById('timing-apply').onclick = () => {
  sendCmd('set_timing', {
    on: Number(document.getElementById('timing-on').value),
    off: Number(document.getElementById('timing-off').value),
    gap: Number(document.getElementById('timing-gap').value),
  });
};

document.getElementById('anim-apply').onclick = () => {
  sendCmd('set_animation', {
    name: document.getElementById('anim-select').value,
    color: parseInt(document.getElementById('anim-color').value.slice(1), 16),
    speed: Number(document.getElementById('anim-speed').value),
  });
};

document.getElementById('refresh-grids').onclick = () => sendCmd('request_status');
document.getElementById('request-alphabet').onclick = () => sendCmd('request_alphabet');
document.getElementById('download-config').onclick = () => {
  const payload = { gestures: state.gestures || [] };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'glove-gestures.json';
  a.click();
  URL.revokeObjectURL(url);
};
document.getElementById('upload-config').onclick = () => document.getElementById('config-file').click();
document.getElementById('config-file').addEventListener('change', (e) => {
  const file = e.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const data = JSON.parse(reader.result);
      if (data.gestures) sendCmd('upload_config', { gestures: data.gestures });
    } catch (err) {
      alert('Invalid file');
    }
  };
  reader.readAsText(file);
});

initGrids();
renderAlphabet();
connectWS();
