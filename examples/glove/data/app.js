let wsHost = location.hostname;
let ws;
let modalContext = 'capture';
let modalSymbol = null;

if (wsHost == 'localhost') wsHost = '192.168.3.141';

const CAPTURE_STABLE_MS = 100; // Time input must be stable to register as step

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
  capture: { active: false, symbol: null, steps: [], lastMask: 0, stableMask: 0, lastChange: 0 },
  sequential: { active: false, currentIndex: 0, letters: [] },
  freeMemory: 0,
};
let heartbeat;
let lastPong = 0;

const chatWindow = document.getElementById('chat-window');
const statusPill = document.getElementById('status-pill');
const thresholdLabel = document.getElementById('threshold-label');
const thresholdSlider = document.getElementById('threshold-slider');
const statusMode = document.getElementById('status-mode');
const statusBuffer = document.getElementById('status-buffer');
const statusPlayback = document.getElementById('status-playback');
const statusMemory = document.getElementById('status-memory');
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
const sequentialModal = document.getElementById('sequential-modal');
const sequentialSubtitle = document.getElementById('sequential-subtitle');
const sequentialProgress = document.getElementById('sequential-progress');
const sequentialSteps = document.getElementById('sequential-steps');
const sequentialSave = document.getElementById('sequential-save');
const sequentialSkip = document.getElementById('sequential-skip');
const sequentialCancel = document.getElementById('sequential-cancel');
const sequentialClose = document.getElementById('sequential-close');
const drawer = document.getElementById('drawer');
const openPanel = document.getElementById('open-panel');
const closePanel = document.getElementById('close-panel');
const streamToggle = document.getElementById('stream-toggle');
const streamNote = document.getElementById('stream-note');
const chatInputField = document.getElementById('chat-input');
const inputState = document.getElementById('input-state');
const handPanel = document.getElementById('hand-panel');
const layout = document.getElementById('layout');
const themeToggle = document.getElementById('theme-toggle');
const bodyEl = document.body;
const baseCircleColors = { light: '#000000', dark: '#6b7280' };
let handSvgDoc = null;

function setTheme(mode = 'light') {
  const isDark = mode === 'dark';
  bodyEl.classList.toggle('theme-dark', isDark);
  if (themeToggle) themeToggle.textContent = isDark ? 'Light' : 'Dark';
  localStorage.setItem('glove-theme', mode);
  updateHandPanelColors();
}

const savedTheme = localStorage.getItem('glove-theme');
setTheme(savedTheme === 'dark' ? 'dark' : 'light');

if (themeToggle) {
  themeToggle.onclick = () => {
    const next = bodyEl.classList.contains('theme-dark') ? 'light' : 'dark';
    setTheme(next);
  };
}

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
    bubble.innerHTML = `<div class="typing-indicator"></div>`;
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
  base.push('W');
  return base;
}

function openCapture(symbol) {
  modalContext = 'capture';
  modalSymbol = symbol;
  state.capture = { active: true, symbol, steps: [], lastMask: 0, stableMask: 0, lastChange: Date.now() };
  modal.classList.remove('hidden');
  modalTitle.textContent = 'Record gesture';
  modalSubtitle.textContent = `Letter: ${symbol}`;
  modalSave.textContent = 'Save';
  modalCancel.textContent = 'Cancel';
  modalSteps.innerHTML = '';
  sendCmd('set_debug_streaming', { enabled: true });
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
  state.capture = { active: false, symbol: null, steps: [], lastMask: 0, stableMask: 0, lastChange: 0 };
  modalSteps.innerHTML = '';
}

function openSequential() {
  const letters = [];
  for (let i = 0; i < 26; i++) letters.push(String.fromCharCode(97 + i));
  letters.push('W');

  state.sequential = { active: true, currentIndex: 0, letters };
  sequentialModal.classList.remove('hidden');
  updateSequentialDisplay();
  startSequentialCapture();
}

function closeSequential() {
  sequentialModal.classList.add('hidden');
  state.sequential = { active: false, currentIndex: 0, letters: [] };
  state.capture = { active: false, symbol: null, steps: [], lastMask: 0, stableMask: 0, lastChange: 0 };
  sequentialSteps.innerHTML = '';
}

function updateSequentialDisplay() {
  const currentLetter = state.sequential.letters[state.sequential.currentIndex];
  sequentialSubtitle.textContent = `Letter: ${currentLetter}`;

  sequentialProgress.innerHTML = '';
  state.sequential.letters.forEach((letter, idx) => {
    const chip = document.createElement('div');
    chip.className = 'step-chip';
    if (idx < state.sequential.currentIndex) {
      chip.style.backgroundColor = '#10b981';
      chip.style.color = 'white';
    } else if (idx === state.sequential.currentIndex) {
      chip.style.backgroundColor = '#3b82f6';
      chip.style.color = 'white';
    }
    chip.textContent = letter;
    sequentialProgress.appendChild(chip);
  });
}

function startSequentialCapture() {
  const currentLetter = state.sequential.letters[state.sequential.currentIndex];
  state.capture = { active: true, symbol: currentLetter, steps: [], lastMask: 0, stableMask: 0, lastChange: Date.now() };
  sequentialSteps.innerHTML = '';
  sendCmd('set_debug_streaming', { enabled: true });
}

function saveAndNextSequential() {
  if (state.capture.steps.length > 0) {
    saveGestureToDevice(state.capture.symbol, state.capture.steps);
  }
  setTimeout(() => {
    moveToNextLetter();
  }, 150);
}

function skipSequential() {
  state.capture = { active: false, symbol: null, steps: [], lastMask: 0, stableMask: 0, lastChange: 0 };
  setTimeout(() => {
    moveToNextLetter();
  }, 150);
}

function saveGestureToDevice(symbol, steps) {
  // Update local gestures array to match new format
  const existingIdx = state.gestures.findIndex(g => g.symbol === symbol);
  const gestureObj = { symbol, steps };
  if (existingIdx >= 0) {
    state.gestures[existingIdx] = gestureObj;
  } else {
    state.gestures.push(gestureObj);
  }

  // Convert to new simplified format for device
  const simplifiedGestures = {};
  state.gestures.forEach(g => {
    simplifiedGestures[g.symbol] = g.steps.map(step =>
      typeof step === 'object' ? step.mask : step
    );
  });

  // Send updated gestures to device in new format
  sendCmd('upload_config', simplifiedGestures);
}

function moveToNextLetter() {
  state.sequential.currentIndex++;
  if (state.sequential.currentIndex < state.sequential.letters.length) {
    updateSequentialDisplay();
    startSequentialCapture();
  } else {
    closeSequential();
    setTimeout(() => sendCmd('request_alphabet'), 150);
  }
}

modalCancel.onclick = closeModal;
modalClose.onclick = closeModal;
sequentialCancel.onclick = closeSequential;
sequentialClose.onclick = closeSequential;
sequentialSave.onclick = saveAndNextSequential;
sequentialSkip.onclick = skipSequential;
modalSave.onclick = () => {
  if (modalContext === 'capture') {
    if (state.capture.steps.length > 0) {
      saveGestureToDevice(state.capture.symbol, state.capture.steps);
    }
    closeModal();
    setTimeout(() => sendCmd('request_alphabet'), 150);
  } else {
    sendCmd('play_letter', { symbol: modalSymbol });
    closeModal();
  }
};

function maskToPinString(mask) {
  if (mask === 0) return 'none';
  const pins = [];
  for (let i = 0; i < 16; i++) {
    if (mask & (1 << i)) {
      pins.push(i);
    }
  }
  return pins.join(' + ');
}

function renderCaptureSteps(steps, targetElement = modalSteps) {
  targetElement.innerHTML = '';
  steps.forEach((step, idx) => {
    const mask = typeof step === 'object' ? step.mask : step;
    const chip = document.createElement('div');
    chip.className = 'step-chip';
    chip.textContent = `#${idx+1}: ${maskToPinString(mask)}`;
    targetElement.appendChild(chip);
  });
}

function pinsToMask(pins) {
  let mask = 0;
  for (const pin of pins) {
    mask |= (1 << pin);
  }
  return mask;
}

function processInputForCapture(pins) {
  if (!state.capture.active) return;

  const now = Date.now();
  const mask = pinsToMask(pins);

  // Track mask changes
  if (mask !== state.capture.lastMask) {
    state.capture.lastMask = mask;
    state.capture.lastChange = now;
  }

  // Check if mask has been stable long enough
  if (mask !== state.capture.stableMask && (now - state.capture.lastChange) >= CAPTURE_STABLE_MS) {
    state.capture.stableMask = mask;

    // Only add non-zero masks as steps
    if (mask !== 0) {
      state.capture.steps.push(mask);

      // Update UI
      if (modalContext === 'capture' && !modal.classList.contains('hidden')) {
        renderCaptureSteps(state.capture.steps);
      }
      if (state.sequential.active && !sequentialModal.classList.contains('hidden')) {
        renderCaptureSteps(state.capture.steps, sequentialSteps);
      }
    }
  }
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
      state.freeMemory = msg.free_memory ?? state.freeMemory;
      if (statusMode) statusMode.textContent = state.mode;
      thresholdLabel.textContent = state.threshold;
      if (thresholdSlider) thresholdSlider.value = state.threshold;
      if (streamToggle) streamToggle.checked = !!state.debugStreaming;
      if (streamNote) streamNote.textContent = state.debugStreaming ? 'Live on' : 'Live off';
      if (!state.debugStreaming && inputState) inputState.textContent = 'Live off';
      statusPlayback.textContent = msg.playback_active ? 'playing' : 'idle';
      statusBuffer.textContent = msg.buffer || '-';
      if (statusMemory) statusMemory.textContent = state.freeMemory || '-';

      // Initialize hardware timing sliders
      if (msg.hardware_timings) {
        const timings = msg.hardware_timings;
        if (timings.latch_delay_us !== undefined && latchDelaySlider) {
          latchDelaySlider.value = timings.latch_delay_us;
          if (latchDelayLabel) latchDelayLabel.textContent = `${timings.latch_delay_us}μs`;
        }
        if (timings.soft_shift_delay_us !== undefined && softShiftDelaySlider) {
          softShiftDelaySlider.value = timings.soft_shift_delay_us;
          if (softShiftDelayLabel) softShiftDelayLabel.textContent = `${timings.soft_shift_delay_us}μs`;
        }
        if (timings.mux_delay_us !== undefined && muxDelaySlider) {
          muxDelaySlider.value = timings.mux_delay_us;
          if (muxDelayLabel) muxDelayLabel.textContent = `${timings.mux_delay_us}μs`;
        }
        if (timings.capacitance_delay_us !== undefined && capacitanceDelaySlider) {
          capacitanceDelaySlider.value = timings.capacitance_delay_us;
          if (capacitanceDelayLabel) capacitanceDelayLabel.textContent = `${timings.capacitance_delay_us}μs`;
        }
      }
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
      showDetectedLetter(msg.value);
      break;
    case 'input':
      state.inputs = msg.pins || [];
      if (msg.capacitance) state.capacitance = msg.capacitance;
      updateIOGrid(inputGrid, state.inputs);
      updateIOGrid(inputGridDebug, state.inputs);
      updateCapacitanceBars();
      updateHandPanelColors();
      if (inputState) inputState.textContent = `Active inputs: ${state.inputs.join(', ') || 'none'}`;
      // Process input for JS-based capture
      processInputForCapture(state.inputs);
      break;
    case 'input_idle':
      state.inputs = [];
      state.capacitance = new Array(16).fill(0);
      updateIOGrid(inputGrid, []);
      updateIOGrid(inputGridDebug, []);
      updateCapacitanceBars();
      updateHandPanelColors();
      if (inputState) inputState.textContent = 'No input detected';
      // Process idle input for JS-based capture
      processInputForCapture([]);
      break;
    case 'output':
      state.outputs = msg.pins || [];
      updateIOGrid(outputGrid, msg.pins || []);
      updateIOGrid(outputGridDebug, msg.pins || []);
      updateHandPanelColors();
      break;
    case 'alphabet':
      state.gestures = msg.gestures || [];
      state.alphabet = state.gestures.map(g => g.symbol);
      renderAlphabet();
      updateHandPanelLabels();
      updateHandPanelColors();
      break;
    case 'gesture_state':
      renderGestureState(msg);
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
    chip.textContent = `#${idx+1}: ${maskToPinString(mask)}`;
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
function setPanelOpen(isOpen) {
  if (!drawer) return;
  drawer.classList.toggle('open', isOpen);
  if (layout) layout.classList.toggle('panel-open', isOpen);
  if (openPanel) openPanel.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
}

if (openPanel && drawer) openPanel.onclick = () => setPanelOpen(!drawer.classList.contains('open'));
if (closePanel && drawer) closePanel.onclick = () => setPanelOpen(false);

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

// Hardware timing controls
const latchDelaySlider = document.getElementById('latch-delay-slider');
const softShiftDelaySlider = document.getElementById('soft-shift-delay-slider');
const muxDelaySlider = document.getElementById('mux-delay-slider');
const capacitanceDelaySlider = document.getElementById('capacitance-delay-slider');
const latchDelayLabel = document.getElementById('latch-delay-label');
const softShiftDelayLabel = document.getElementById('soft-shift-delay-label');
const muxDelayLabel = document.getElementById('mux-delay-label');
const capacitanceDelayLabel = document.getElementById('capacitance-delay-label');

// Update labels when sliders change
if (latchDelaySlider) {
  latchDelaySlider.addEventListener('input', (e) => {
    latchDelayLabel.textContent = `${e.target.value}μs`;
  });
}
if (softShiftDelaySlider) {
  softShiftDelaySlider.addEventListener('input', (e) => {
    softShiftDelayLabel.textContent = `${e.target.value}μs`;
  });
}
if (muxDelaySlider) {
  muxDelaySlider.addEventListener('input', (e) => {
    muxDelayLabel.textContent = `${e.target.value}μs`;
  });
}
if (capacitanceDelaySlider) {
  capacitanceDelaySlider.addEventListener('input', (e) => {
    capacitanceDelayLabel.textContent = `${e.target.value}μs`;
  });
}

const hardwareTimingApply = document.getElementById('hardware-timing-apply');
if (hardwareTimingApply) {
  hardwareTimingApply.onclick = () => {
    sendCmd('set_hardware_timings', {
      latch_delay_us: Number(latchDelaySlider.value),
      soft_shift_delay_us: Number(softShiftDelaySlider.value),
      mux_delay_us: Number(muxDelaySlider.value),
      capacitance_delay_us: Number(capacitanceDelaySlider.value),
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
    // Convert to new simplified format for download
    const simplifiedGestures = {};
    (state.gestures || []).forEach(g => {
      simplifiedGestures[g.symbol] = g.steps.map(step =>
        typeof step === 'object' ? step.mask : step
      );
    });
    const blob = new Blob([JSON.stringify(simplifiedGestures, null, 2)], { type: 'application/json' });
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
const sequentialBtn = document.getElementById('sequential-config');
if (sequentialBtn) sequentialBtn.onclick = () => openSequential();
const resetBtn = document.getElementById('reset-gestures');
if (resetBtn) resetBtn.onclick = () => sendCmd('reset_gestures');
const configFile = document.getElementById('config-file');
if (configFile) {
  configFile.addEventListener('change', (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      try {
        const data = JSON.parse(reader.result);
        // Handle new simplified format directly
        if (Object.keys(data).length > 0 && !data.gestures) {
          sendCmd('upload_config', data);
        }
        // Handle old format for backward compatibility
        else if (data.gestures) {
          const simplifiedGestures = {};
          data.gestures.forEach(g => {
            simplifiedGestures[g.symbol] = g.steps.map(step =>
              typeof step === 'object' ? step.mask : step
            );
          });
          sendCmd('upload_config', simplifiedGestures);
        }
      } catch (err) {
        alert('Invalid file');
      }
    };
    reader.readAsText(file);
  });
}

function initHandPanel() {
  fetch('hand.svg')
    .then(res => res.text())
    .then(svgText => {
      handPanel.innerHTML = svgText;
      handSvgDoc = handPanel.querySelector('svg');
      setupHandPanelInteractions();
      updateHandPanelLabels();
    })
    .catch(err => console.error('Failed to load hand.svg:', err));
}

function getIoBitForId(id) {
  let symbol;

  // Special case: id 16 always maps to 'w'
  if (id === 16) {
    symbol = 'w';
  } else {
    // id is 1-16, alphabet index is id-1
    const alphabetIdx = id - 1;
    symbol = state.alphabet[alphabetIdx];
  }

  if (!symbol) return null;

  // Find the gesture for this symbol to get its IO bit
  const gesture = state.gestures.find(g => g.symbol === symbol);
  if (!gesture || !gesture.steps || gesture.steps.length === 0) return null;

  // Get the first step's mask (used for positioning double tap letters)
  const firstMask = typeof gesture.steps[0] === 'object' ? gesture.steps[0].mask : gesture.steps[0];
  if (firstMask === 0) return null;

  // Find the bit position (log2 of the lowest set bit)
  for (let bit = 0; bit < 16; bit++) {
    if (firstMask & (1 << bit)) return bit;
  }
  return null;
}

function setupHandPanelInteractions() {
  if (!handSvgDoc) return;
  for (let id = 1; id <= 16; id++) {
    const circle = handSvgDoc.querySelector(`#circle-${id}`);
    if (circle) {
      circle.addEventListener('click', () => {
        const ioBit = getIoBitForId(id);
        if (ioBit === null) {
          pushLog(`Hand panel: no IO mapping for id ${id}`);
          return;
        }
        // Activate motor for 50ms
        const mask = 1 << ioBit;
        sendCmd('set_output', { mask });
        pushLog(`Hand panel: activated motor bit ${ioBit} (mask: ${mask}) for 50ms`);
        // Auto-deactivate after 50ms
        setTimeout(() => {
          sendCmd('set_output', { mask: 0 });
        }, 50);
      });
    }
  }
}

function updateHandPanelLabels() {
  if (!handSvgDoc) return;
  for (let id = 1; id <= 16; id++) {
    const text = handSvgDoc.querySelector(`#text-${id}`);
    if (text) {
      const ioBit = getIoBitForId(id);
      text.textContent = ioBit !== null ? ioBit.toString() : '-';
    }
  }
}

function updateHandPanelColors() {
  if (!handSvgDoc) return;
  const threshold = state.threshold;
  const isDark = bodyEl.classList.contains('theme-dark');
  const baseIdle = isDark ? baseCircleColors.dark : baseCircleColors.light;
  const outline = handSvgDoc.querySelector('path');
  if (outline) outline.setAttribute('stroke', isDark ? '#ffffff' : '#000000');

  // Define vibrant colors for dark mode
  const outputColor = isDark ? '#ff6b6b' : '#ef4444';  // Brighter red in dark mode
  const inputColor = isDark ? '#68fe9fff' : '#22c55e';   // Brighter green in dark mode

  for (let id = 1; id <= 16; id++) {
    const circle = handSvgDoc.querySelector(`#circle-${id}`);
    if (!circle) continue;

    const ioBit = getIoBitForId(id);
    if (ioBit === null) {
      circle.setAttribute('fill', '#000000');
      continue;
    }

    const capacitance = state.capacitance[ioBit] || 0;
    const isInput = state.inputs.includes(ioBit);
    const isOutput = state.outputs.includes(ioBit);

    if (isOutput) {
      // Output active: vibrant red
      circle.setAttribute('fill', outputColor);
    } else if (isInput) {
      // Input active: vibrant green
      circle.setAttribute('fill', inputColor);
    } else if (capacitance > 0) {
      // Has capacitance but not above threshold: brighter gradient for dark mode
      const intensity = Math.min(1, capacitance / threshold);
      const greenValue = isDark ? Math.round(100 + 155 * intensity) : Math.round(200 * intensity);
      circle.setAttribute('fill', `rgb(0, ${greenValue}, 0)`);
    } else {
      // Inactive: black
      circle.setAttribute('fill', baseIdle);
    }
  }
}

function showDetectedLetter(symbol) {
  if (!handSvgDoc || !symbol) return;

  // Check if hand panel is visible (drawer is open)
  const drawer = document.getElementById('drawer');
  if (!drawer || !drawer.classList.contains('open')) return;

  // Find which id corresponds to this symbol by matching IO bits
  let targetId = null;

  // Get the IO bit for the detected symbol
  const gesture = state.gestures.find(g => g.symbol === symbol);
  if (!gesture || !gesture.steps || gesture.steps.length === 0) return;

  const firstMask = typeof gesture.steps[0] === 'object' ? gesture.steps[0].mask : gesture.steps[0];
  if (firstMask === 0) return;

  // Find the bit position for this symbol
  let detectedBit = null;
  for (let bit = 0; bit < 16; bit++) {
    if (firstMask & (1 << bit)) {
      detectedBit = bit;
      break;
    }
  }
  if (detectedBit === null) return;

  // Special case: 'w' always maps to ID 16
  if (symbol === 'w') {
    targetId = 16;
  } else {
    // Find which ID has the same IO bit as the detected symbol
    for (let id = 1; id <= 15; id++) {
      const idBit = getIoBitForId(id);
      if (idBit === detectedBit) {
        targetId = id;
        break;
      }
    }
  }

  if (targetId === null) return;

  const circle = handSvgDoc.querySelector(`#circle-${targetId}`);
  if (!circle) return;

  // Get circle position relative to hand panel
  const circleRect = circle.getBoundingClientRect();
  const handPanelRect = handPanel.getBoundingClientRect();

  const relativeX = circleRect.left - handPanelRect.left + circleRect.width / 2;
  const relativeY = circleRect.top - handPanelRect.top - circleRect.height;

  // Create floating letter element
  const floatingLetter = document.createElement('div');
  floatingLetter.className = 'detected-letter';
  floatingLetter.textContent = symbol; // Preserve original case

  // Position at circle center with small random offset for overlap handling
  const randomOffset = (Math.random() - 0.5) * 10; // ±5px
  floatingLetter.style.left = `${relativeX + randomOffset}px`;
  floatingLetter.style.top = `${relativeY}px`;
  floatingLetter.style.transform = 'translate(-50%, -50%)';

  handPanel.appendChild(floatingLetter);

  // Remove element after animation completes
  setTimeout(() => {
    if (floatingLetter.parentNode) {
      floatingLetter.parentNode.removeChild(floatingLetter);
    }
  }, 2000);
}

initGrids();
initHandPanel();
renderAlphabet();
connectWS();

function initializeChat() {
  const initialMessages = [
    { from: 'device', text: 'bom dia' },
    { from: 'device', text: 'tudo bem?' },
    { from: 'me', text: 'tudo bem, e você?' },
  ];

  initialMessages.forEach((msg, index) => {
    setTimeout(() => {
      appendChat(msg);
    }, index * 800);
  });
}

setTimeout(initializeChat, 100);
