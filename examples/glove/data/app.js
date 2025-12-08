let wsHost = location.hostname;
let ws;
let modalContext = 'capture';
let modalSymbol = null;

if (wsHost == 'localhost') wsHost = '192.168.3.141';

const state = {
  connected: false,
  mode: 0,
  threshold: 1,
  debugStreaming: false,
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
let heartbeat;
let lastPong = 0;

const chatWindow = document.getElementById('chat-window');
const statusPill = document.getElementById('status-pill');
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
const drawer = document.getElementById('drawer');
const openPanel = document.getElementById('open-panel');
const closePanel = document.getElementById('close-panel');
const streamToggle = document.getElementById('stream-toggle');
const streamNote = document.getElementById('stream-note');
const chatInputField = document.getElementById('chat-input');
const inputState = document.getElementById('input-state');

function initGrids() {
  const grids = [inputGrid, outputGrid, inputGridDebug, outputGridDebug].filter((grid, idx, arr) => grid && arr.indexOf(grid) === idx);
  grids.forEach(grid => {
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

function appendChat({ from, text, typing }) {
  if (typing) {
    const existing = document.getElementById('typing-bubble');
    if (existing) existing.remove();
    const bubble = document.createElement('div');
    bubble.className = `bubble ${from}`;
    bubble.dataset.from = from;
    bubble.id = 'typing-bubble';
    bubble.innerHTML = `<div class="typing-dots"><span></span><span></span><span></span></div>`;
    chatWindow.appendChild(bubble);
  } else {
    const bubble = document.createElement('div');
    bubble.className = `bubble ${from}`;
    bubble.dataset.from = from;
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
    state.outputs = state.outputs.filter(p => p !== pin);
  } else {
    const mask = 1 << pin;
    sendCmd('set_output', { mask });
    pushLog(`Output ${pin} activated (mask: ${mask})`);
    state.outputs = [pin];
  }
  updateIOGrid(outputGrid, state.outputs);
  updateIOGrid(outputGridDebug, state.outputs);
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

function startHeartbeat() {
  clearInterval(heartbeat);
  heartbeat = setInterval(() => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    const now = Date.now();
    if (now - lastPong > 10000) {
      console.log('Heartbeat timeout, reconnecting...');
      ws.close();
      return;
    }
    ws.send(JSON.stringify({ cmd: 'ping', ts: now }));
  }, 3000);
}

function connectWS() {
  statusPill.textContent = 'Connecting...';
  statusPill.classList.add('connecting');
  statusPill.classList.remove('disconnected');
  statusPill.classList.remove('connected');
  ws = new WebSocket(`ws://${wsHost}:81`);
  ws.onopen = () => {
    state.connected = true;
    lastPong = Date.now();
    startHeartbeat();
    if (streamToggle) streamToggle.checked = true;
    state.debugStreaming = true;
    if (streamNote) streamNote.textContent = 'Live on';
    statusPill.textContent = 'Connected';
    statusPill.classList.remove('disconnected');
    statusPill.classList.remove('connecting');
    statusPill.classList.add('connected');
    sendCmd('set_debug_streaming', { enabled: true });
    sendCmd('request_status');
    sendCmd('request_alphabet');
  };
  ws.onclose = () => {
    state.connected = false;
    clearInterval(heartbeat);
    statusPill.textContent = 'Reconnecting...';
    statusPill.classList.add('disconnected');
    statusPill.classList.remove('connecting');
    statusPill.classList.remove('connected');
    setTimeout(connectWS, 1500);
  };
  ws.onmessage = (ev) => {
    lastPong = Date.now();
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
      state.debugStreaming = msg.debug_streaming ?? state.debugStreaming;
      if (statusMode) statusMode.textContent = state.mode;
      thresholdLabel.textContent = state.threshold;
      if (streamToggle) streamToggle.checked = !!state.debugStreaming;
      if (streamNote) streamNote.textContent = state.debugStreaming ? 'Live on' : 'Live off';
      if (!state.debugStreaming && inputState) inputState.textContent = 'Live off';
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
      if (inputState) inputState.textContent = `Active inputs: ${state.inputs.join(', ') || 'none'}`;
      break;
    case 'input_idle':
      state.inputs = [];
      state.capacitance = new Array(16).fill(0);
      updateIOGrid(inputGrid, []);
      updateIOGrid(inputGridDebug, []);
      updateCapacitanceBars();
      if (inputState) inputState.textContent = 'No input detected';
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
    case 'pong':
      lastPong = Date.now();
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
if (openPanel && drawer) openPanel.onclick = () => drawer.classList.add('open');
if (closePanel && drawer) closePanel.onclick = () => drawer.classList.remove('open');

const chatSendBtn = document.getElementById('chat-send');
if (chatSendBtn) {
  chatSendBtn.onclick = () => {
    const clean = sanitize(chatInputField.value);
    if (!clean) return;
    appendChat({ from: 'me', typing: true });
    sendCmd('send_message', { text: clean });
    chatInputField.value = '';
  };
}
if (chatInputField) {
  chatInputField.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') chatSendBtn.click();
  });
  setTimeout(() => chatInputField.focus(), 50);
}

const thresholdSlider = document.getElementById('threshold-slider');
if (thresholdSlider) {
  thresholdSlider.addEventListener('input', (e) => {
    thresholdLabel.textContent = e.target.value;
    sendCmd('set_threshold', { value: Number(e.target.value) });
  });
}

const timingApply = document.getElementById('timing-apply');
if (timingApply) {
  timingApply.onclick = () => {
    sendCmd('set_timing', {
      on: Number(document.getElementById('timing-on').value),
      off: Number(document.getElementById('timing-off').value),
      gap: Number(document.getElementById('timing-gap').value),
    });
  };
}

const animApply = document.getElementById('anim-apply');
if (animApply) {
  animApply.onclick = () => {
    sendCmd('set_animation', {
      name: document.getElementById('anim-select').value,
      color: parseInt(document.getElementById('anim-color').value.slice(1), 16),
      speed: Number(document.getElementById('anim-speed').value),
      count: Number(document.getElementById('anim-count').value || 0),
    });
  };
}

if (streamToggle) {
  streamToggle.onchange = (e) => {
    state.debugStreaming = e.target.checked;
    if (streamNote) streamNote.textContent = state.debugStreaming ? 'Live on' : 'Live off';
    sendCmd('set_debug_streaming', { enabled: state.debugStreaming });
    if (state.debugStreaming) {
      sendCmd('request_status');
    } else {
      updateIOGrid(inputGrid, []);
      updateIOGrid(inputGridDebug, []);
      updateCapacitanceBars();
      if (inputState) inputState.textContent = 'Live off';
    }
  };
}

const refreshGrids = document.getElementById('refresh-grids');
if (refreshGrids) refreshGrids.onclick = () => sendCmd('request_status');
const reqAlphabet = document.getElementById('request-alphabet');
if (reqAlphabet) reqAlphabet.onclick = () => sendCmd('request_alphabet');

const downloadBtn = document.getElementById('download-config');
if (downloadBtn) {
  downloadBtn.onclick = () => {
    const payload = { gestures: state.gestures || [] };
    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'glove-gestures.json';
    a.click();
    URL.revokeObjectURL(url);
  };
}
const uploadBtn = document.getElementById('upload-config');
if (uploadBtn) uploadBtn.onclick = () => document.getElementById('config-file').click();
const configFile = document.getElementById('config-file');
if (configFile) {
  configFile.addEventListener('change', (e) => {
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
}

initGrids();
renderAlphabet();
connectWS();
