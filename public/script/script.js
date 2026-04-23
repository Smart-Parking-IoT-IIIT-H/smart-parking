/* ── Firebase ── */
const firebaseConfig = {
  apiKey: "AIzaSyA73-b9qGJdzO34u7gjFIkOSqqTKn8a76A",
  authDomain: "smart-parking-1df76.firebaseapp.com",
  databaseURL: "https://smart-parking-1df76-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "smart-parking-1df76",
  storageBucket: "smart-parking-1df76.firebasestorage.app",
  messagingSenderId: "448571319292",
  appId: "1:448571319292:web:627000f798c7c42d658879"
};
firebase.initializeApp(firebaseConfig);
const db = firebase.database();

/* ── State ── */
const state = {
  slots: { 1: null, 2: null, 3: null, 4: null },
  vehiclesServed: 0,
  lastHb: Date.now(),
  history: [],
  startTime: Date.now()
};

/* ── Build large slot cards (banner — billboard) ── */
function buildBannerMiniSlots() {
  const g = document.getElementById('b-slots-grid');
  g.innerHTML = '';
  for (let i = 1; i <= 4; i++) {
    g.innerHTML += `
      <div class="b-slot-card" id="bms${i}">
        <div class="b-slot-icon" id="bmsi${i}">🅿</div>
        <div class="b-slot-id">SLOT 0${i}</div>
        <div class="b-slot-sensors">
          <span class="b-slot-sensor" id="bmir${i}">IR ?</span>
          <span class="b-slot-sensor" id="bmus${i}">US ?</span>
        </div>
        <div class="b-slot-tag loading" id="bmst${i}">—</div>
      </div>`;
  }
}

/* ── Build phone slots ── */
function buildPhoneSlots() {
  const g = document.getElementById('p-slots');
  g.innerHTML = '';
  for (let i = 1; i <= 4; i++) {
    g.innerHTML += `
      <div class="p-slot" id="ps${i}" style="animation-delay:${(i - 1) * .07}s">
        <div class="p-slot-icon" id="psi${i}">🅿</div>
        <div class="p-slot-info">
          <div class="p-slot-id">SLOT_0${i}</div>
          <div class="p-slot-sensors">
            <span class="p-sensor" id="pir${i}">IR ?</span>
            <span class="p-sensor" id="pus${i}">US ?</span>
          </div>
          <div class="p-slot-time" id="ptm${i}">waiting…</div>
        </div>
        <span class="p-tag loading" id="pst${i}">—</span>
      </div>`;
  }
  const segs = document.getElementById('p-segs');
  segs.innerHTML = '';
  for (let i = 1; i <= 4; i++) segs.innerHTML += `<div class="p-seg" id="pseg${i}"></div>`;
}

/* ── Build banner segs ── */
function buildBannerSegs() {
  const g = document.getElementById('b-segs');
  g.innerHTML = '';
  for (let i = 1; i <= 4; i++) g.innerHTML += `<div class="b-seg" id="bseg${i}"></div>`;
}

/* ── Update single slot ── */
function updateSlot(id, data) {
  const prev = state.slots[id];
  state.slots[id] = data;

  if (prev !== null && prev && !prev.occupied && data.occupied) {
    state.vehiclesServed++;
    document.getElementById('b-served').textContent = state.vehiclesServed;
    document.getElementById('p-served').textContent = state.vehiclesServed;
  }

  const cls = data.error ? 'error' : data.occupied ? 'occupied' : 'free';
  const icon = data.error ? '⚠️' : data.occupied ? '🚗' : '🅿';
  const label = data.error ? 'fault' : data.occupied ? 'occupied' : 'free';
  const ts = data.timestamp ? new Date(data.timestamp * 1000) : new Date();
  const timeStr = ts.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit', second: '2-digit' });

  const bms = document.getElementById('bms' + id);
  if (bms) {
    bms.className = 'b-slot-card ' + cls;
    document.getElementById('bmsi' + id).textContent = icon;
    const bmst = document.getElementById('bmst' + id);
    bmst.className = 'b-slot-tag ' + cls;
    bmst.textContent = label;
    const bmir = document.getElementById('bmir' + id);
    const bmus = document.getElementById('bmus' + id);
    bmir.className = 'b-slot-sensor ' + (data.ir ? 'triggered' : 'clear');
    bmus.className = 'b-slot-sensor ' + (data.us ? 'triggered' : 'clear');
    bmir.textContent = 'IR ' + (data.ir ? '1' : '0');
    bmus.textContent = 'US ' + (data.us ? '1' : '0');
    const bseg = document.getElementById('bseg' + id);
    if (bseg) bseg.className = 'b-seg ' + cls;
  }

  const pc = document.getElementById('ps' + id);
  if (pc) {
    pc.className = 'p-slot ' + cls;
    document.getElementById('psi' + id).textContent = icon;
    const pst = document.getElementById('pst' + id);
    pst.className = 'p-tag ' + cls;
    pst.textContent = label;
    const pir = document.getElementById('pir' + id);
    const pus = document.getElementById('pus' + id);
    pir.className = 'p-sensor ' + (data.ir ? 'triggered' : 'clear');
    pus.className = 'p-sensor ' + (data.us ? 'triggered' : 'clear');
    pir.textContent = 'IR ' + (data.ir ? '1' : '0');
    pus.textContent = 'US ' + (data.us ? '1' : '0');
    document.getElementById('ptm' + id).textContent = timeStr;
    const pseg = document.getElementById('pseg' + id);
    if (pseg) pseg.className = 'p-seg ' + cls;
  }

  state.lastHb = Date.now();
  updateHero();
}

/* ── Update hero ── */
function updateHero() {
  const slots = Object.values(state.slots).filter(s => s !== null);
  if (!slots.length) return;

  const free = slots.filter(s => !s.occupied && !s.error).length;
  const occ = slots.filter(s => s.occupied).length;
  const pct = Math.round((occ / 4) * 100);
  const now = new Date().toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  const isFull = free === 0;

  const bf = document.getElementById('b-free');
  bf.textContent = free;
  bf.className = 'b-free-num' + (isFull ? ' zero' : '');
  document.getElementById('b-free-small').textContent = free;
  document.getElementById('b-free-small').style.color = isFull ? 'var(--red)' : 'var(--green)';

  const bPct = document.getElementById('b-pct');
  bPct.textContent = pct + '%';
  bPct.className = 'b-util-pct' + (pct >= 100 ? ' empty' : pct >= 50 ? ' warn' : '');

  const heroCard = document.getElementById('b-hero-card');
  heroCard.className = 'b-card b-card-hero' + (isFull ? ' full' : '');

  const occEl = document.getElementById('b-occ-txt');
  occEl.textContent = occ + ' occupied';
  occEl.className = 'accent' + (isFull ? ' zero' : '');
  occEl.style.color = isFull ? 'var(--red)' : 'var(--green)';

  document.getElementById('b-alert-overlay').style.display = isFull ? 'flex' : 'none';
  document.getElementById('b-time').textContent = now;
  document.getElementById('b-footer-time').textContent = now;
  document.getElementById('b-last-update').textContent = 'last update: ' + now;

  const uptimeMins = Math.round((Date.now() - state.startTime) / 60000);
  document.getElementById('b-uptime').textContent = uptimeMins + 'm';

  const pf = document.getElementById('p-free');
  pf.textContent = free;
  pf.className = 'p-free-big' + (isFull ? ' zero' : '');
  document.getElementById('p-pct').textContent = pct + '%';
  document.getElementById('p-alert').className = 'p-alert' + (isFull ? ' show' : '');
  document.getElementById('p-time').textContent = now;
  document.getElementById('p-update').textContent = 'updated ' + now;
  document.getElementById('p-update2').textContent = now;
  document.getElementById('p-hero-card').className = 'p-hero' + (isFull ? ' full' : '');

  state.history.push(occ);
  if (state.history.length > 20) state.history.shift();
  const avg = Math.round((state.history.reduce((a, v) => a + v, 0) / state.history.length / 4) * 100);
  document.getElementById('b-avg').innerHTML = avg + '<span class="b-stat-unit">%</span>';
  document.getElementById('p-avg').innerHTML = avg + '<span class="p-stat-unit"> %</span>';

  pushCharts(occ, now);
}

/* ── Charts ── */
let bChart, pChart;

function makeChartOptions() {
  return {
    responsive: true,
    maintainAspectRatio: false,
    animation: { duration: 300 },
    plugins: {
      legend: { display: false },
      tooltip: {
        backgroundColor: '#101020',
        borderColor: 'rgba(255,255,255,0.1)',
        borderWidth: 1,
        titleColor: 'rgba(255,255,255,0.4)',
        bodyColor: '#eeeef5',
        titleFont: { family: "'Space Mono'", size: 9 },
        bodyFont: { family: "'Space Mono'", size: 10 },
        padding: 10,
        callbacks: { title: i => i[0].label, label: i => `occupied: ${i.raw}/4` }
      }
    },
    scales: {
      x: {
        ticks: { color: 'rgba(255,255,255,0.15)', font: { family: "'Space Mono'", size: 8 }, maxTicksLimit: 4, maxRotation: 0 },
        grid: { color: 'rgba(255,255,255,0.04)' },
        border: { color: 'rgba(255,255,255,0.06)' }
      },
      y: {
        min: 0, max: 4,
        ticks: { stepSize: 1, color: 'rgba(255,255,255,0.15)', font: { family: "'Space Mono'", size: 8 } },
        grid: { color: 'rgba(255,255,255,0.04)' },
        border: { color: 'rgba(255,255,255,0.06)' }
      }
    }
  };
}

function initCharts() {
  const ds = () => ({
    data: [],
    borderColor: '#00ff88',
    backgroundColor: (ctx) => {
      const g = ctx.chart.ctx.createLinearGradient(0, 0, 0, ctx.chart.height);
      g.addColorStop(0, 'rgba(0,255,136,0.15)');
      g.addColorStop(1, 'rgba(0,255,136,0)');
      return g;
    },
    borderWidth: 1.5,
    tension: .4,
    fill: true,
    pointRadius: 2.5,
    pointBackgroundColor: '#00ff88',
    pointBorderWidth: 0,
    pointHoverRadius: 5,
    pointHoverBackgroundColor: '#00ff88',
    pointHoverBorderColor: 'rgba(0,255,136,0.3)',
    pointHoverBorderWidth: 4
  });

  bChart = new Chart(document.getElementById('b-chart').getContext('2d'),
    { type: 'line', data: { labels: [], datasets: [ds()] }, options: makeChartOptions() });
  pChart = new Chart(document.getElementById('p-chart').getContext('2d'),
    { type: 'line', data: { labels: [], datasets: [ds()] }, options: makeChartOptions() });
}

function pushCharts(count, label) {
  [bChart, pChart].forEach(c => {
    if (!c) return;
    c.data.labels.push(label);
    c.data.datasets[0].data.push(count);
    if (c.data.labels.length > 20) { c.data.labels.shift(); c.data.datasets[0].data.shift(); }
    const col = count === 4 ? '#ff4466' : count >= 3 ? '#ffbb44' : '#00ff88';
    c.data.datasets[0].borderColor = col;
    c.update('none');
  });
}

/* ── Watchdog ── */
function startWatchdog() {
  setInterval(() => {
    const stale = Date.now() - state.lastHb > 15000;
    ['b-dot', 'p-dot'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.className = 'dot' + (stale ? ' off' : '');
    });
    ['b-status', 'p-status'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.textContent = stale ? 'offline' : 'live';
    });
  }, 3000);
}

/* ── Firebase slot listeners ── */
function attachListeners() {
  for (let i = 1; i <= 4; i++) {
    const ii = i;
    db.ref('slots/' + ii).on('value', snap => {
      const d = snap.val();
      if (d !== null) { updateSlot(ii, d); state.lastHb = Date.now(); }
    });
  }
  db.ref('heartbeat').on('value', snap => {
    if (snap.val() !== null) state.lastHb = Date.now();
  });
}

/* ── Init slot UI ── */
buildBannerMiniSlots();
buildBannerSegs();
buildPhoneSlots();
initCharts();
startWatchdog();
attachListeners();

/* ══════════════════════════════════════════════════════════════
   EXIT PAYMENT — Firebase Queue (shared across all clients)
   Flow:
     ESP32 publishes parking/exit/bill via MQTT
     → Dashboard JS receives via WebSocket
     → Pushes bill to Firebase exitQueue/bills
     → All clients listen to exitQueue/active (same bill, same QR)
     → Car at exit IR → gateStatus = countdown → all clients sync timer
     → Gate opens → active cleared → next bill promoted from queue
══════════════════════════════════════════════════════════════ */

const EXIT_UPI_ID = 'f61579784@oksbi';
const EXIT_UPI_NAME = 'SmartPark';
const PAYMENT_WINDOW_SECS = 15;
const GATE_HOLD_SECS = 5;

let mqttClient = null;
let exitTimer = null;
let currentActive = null;       // locally tracked active bill (for change detection)
let isLeader = false;           // true if THIS tab is driving the countdown

/* ─────────────────────────────────────────
   FIREBASE QUEUE HELPERS
───────────────────────────────────────── */

/* Push a new bill to the Firebase queue, then try to promote */
function pushBillToFirebase(bill) {
  bill.timestamp = Math.floor(Date.now() / 1000);
  // Key = slot + timestamp to allow multiple exits from same slot,
  // while still deduplicating across multiple browser tabs receiving the same MQTT message
  const key = 'slot_' + bill.slot + '_' + bill.timestamp;
  db.ref('exitQueue/bills/' + key).set(bill, (err) => {
    if (err) {
      console.error('[QUEUE] Firebase write failed:', err);
      return;
    }
    console.log('[QUEUE] Bill stored in Firebase:', bill);
    // Only now that the bill is confirmed written, try to promote
    processFirebaseQueue();
  });
}

/* Promote next bill from queue → active (idempotent — checks first) */
function processFirebaseQueue() {
  db.ref('exitQueue/active').once('value', snap => {
    if (snap.val() !== null) {
      console.log('[QUEUE] Active bill already exists — skipping promotion');
      return;
    }
    db.ref('exitQueue/bills').limitToFirst(1).once('value', snap => {
      if (!snap.val()) {
        console.log('[QUEUE] Queue empty — going idle');
        showIdleState();
        return;
      }
      const key = Object.keys(snap.val())[0];
      const bill = snap.val()[key];
      // Atomically promote to active + remove from queue
      const updates = {};
      updates['exitQueue/active'] = bill;
      updates['exitQueue/bills/' + key] = null;
      updates['exitQueue/gateStatus'] = 'idle';
      db.ref().update(updates);
      console.log('[QUEUE] Promoted bill to active:', bill);
    });
  });
}

/* ─────────────────────────────────────────
   FIREBASE LISTENERS — all clients react
───────────────────────────────────────── */

function attachExitListeners() {

  /* Active bill — render QR on all clients when this changes */
  db.ref('exitQueue/active').on('value', snap => {
    const bill = snap.val();
    if (bill) {
      const billStr = JSON.stringify(bill);
      if (billStr !== JSON.stringify(currentActive)) {
        currentActive = bill;
        console.log('[ACTIVE] New active bill received:', bill);
        renderBill(bill);
      }
    } else {
      // Active cleared — go idle
      if (currentActive !== null) {
        currentActive = null;
        showIdleState();
      }
    }
  });

  /* Queue length — update waiting badge on all clients */
  db.ref('exitQueue/bills').on('value', snap => {
    const count = snap.val() ? Object.keys(snap.val()).length : 0;
    ['b', 'p'].forEach(prefix => {
      const queueEl = document.getElementById(prefix + '-exit-queue');
      if (queueEl) {
        queueEl.textContent = count > 0 ? count + ' waiting' : '';
        queueEl.style.display = count > 0 ? 'block' : 'none';
      }
    });
  });

  /* Gate status — sync countdown and gate-open state across all clients */
  db.ref('exitQueue/gateStatus').on('value', snap => {
    const status = snap.val();
    console.log('[GATE STATUS]', status);

    if (status === 'countdown' && !exitTimer && currentActive) {
      // Start countdown on this client (all clients do this simultaneously)
      startExitCountdown();
    }

    if (status === 'open') {
      // Show gate opened on all clients even if they didn't drive the countdown
      ['b', 'p'].forEach(prefix => {
        const cdEl = document.getElementById(prefix + '-exit-countdown');
        if (cdEl) { cdEl.textContent = '✓ GATE OPENED'; cdEl.classList.add('done'); }
      });
    }

    if (status === 'idle' || status === null) {
      // Clear any running timer on this client
      if (exitTimer) { clearInterval(exitTimer); exitTimer = null; }
    }
  });
}

/* ─────────────────────────────────────────
   RENDER BILL — show QR + cost on all clients
───────────────────────────────────────── */

function renderBill(bill) {
  const slot = bill.slot || 1;
  const dur = bill.duration_secs || 0;
  const cost = bill.cost_inr || 0;
  const upiUri = buildUpiUri(cost, slot);

  // Clear any existing timer
  if (exitTimer) { clearInterval(exitTimer); exitTimer = null; }

  ['b', 'p'].forEach(prefix => {
    const card = document.getElementById(prefix + '-exit-card');
    const idle = document.getElementById(prefix + '-exit-idle');
    const active = document.getElementById(prefix + '-exit-active');
    const slotEl = document.getElementById(prefix + '-exit-slot');
    const costEl = document.getElementById(prefix + '-exit-cost');
    const durEl = document.getElementById(prefix + '-exit-dur');
    const cdEl = document.getElementById(prefix + '-exit-countdown');
    const qrDiv = document.getElementById(prefix + '-exit-qr');
    const queueEl = document.getElementById(prefix + '-exit-queue');

    if (!card) return;

    card.classList.add('billing');
    idle.style.display = 'none';
    active.style.display = 'flex';

    slotEl.textContent = 'SLOT 0' + slot;
    costEl.textContent = '₹' + cost.toFixed(2);
    durEl.textContent = dur + ' sec · 1 paise/sec';
    cdEl.textContent = 'Waiting for car at exit…';
    cdEl.classList.remove('done');

    // QR code
    qrDiv.innerHTML = '';
    const qrSize = prefix === 'b' ? 140 : 120;
    new QRCode(qrDiv, {
      text: upiUri, width: qrSize, height: qrSize,
      colorDark: '#000', colorLight: '#fff',
      correctLevel: QRCode.CorrectLevel.M
    });
  });
}

/* ─────────────────────────────────────────
   COUNTDOWN — runs on all clients simultaneously
   driven by Firebase gateStatus = 'countdown'
───────────────────────────────────────── */

function startExitCountdown() {
  if (exitTimer) { clearInterval(exitTimer); exitTimer = null; }

  let remaining = PAYMENT_WINDOW_SECS;

  exitTimer = setInterval(() => {
    remaining--;

    ['b', 'p'].forEach(prefix => {
      const cdEl = document.getElementById(prefix + '-exit-countdown');
      if (!cdEl) return;
      if (remaining > 0) {
        cdEl.textContent = 'Pay now · ' + remaining + 's';
      } else {
        cdEl.textContent = '✓ GATE OPENED';
        cdEl.classList.add('done');
      }
    });

    if (remaining <= 0) {
      clearInterval(exitTimer);
      exitTimer = null;

      // Send gate open command via MQTT
      if (mqttClient && mqttClient.connected) {
        mqttClient.publish('parking/gate/exit/open', JSON.stringify({ action: 'open' }));
        console.log('[MQTT-WS] Gate open command sent');
      }

      // Update Firebase — mark gate open, then after hold time clear active + promote next
      db.ref('exitQueue/gateStatus').set('open');

      setTimeout(() => {
        db.ref('exitQueue/active').remove();
        db.ref('exitQueue/gateStatus').set('idle');
        processFirebaseQueue();
      }, GATE_HOLD_SECS * 1000);
    }
  }, 1000);
}

/* ─────────────────────────────────────────
   IDLE STATE
───────────────────────────────────────── */

function showIdleState() {
  if (exitTimer) { clearInterval(exitTimer); exitTimer = null; }

  ['b', 'p'].forEach(prefix => {
    const card = document.getElementById(prefix + '-exit-card');
    const idle = document.getElementById(prefix + '-exit-idle');
    const active = document.getElementById(prefix + '-exit-active');
    const queueEl = document.getElementById(prefix + '-exit-queue');

    if (!card) return;
    card.classList.remove('billing');
    idle.style.display = 'flex';
    active.style.display = 'none';
    if (queueEl) { queueEl.textContent = ''; queueEl.style.display = 'none'; }
  });
  console.log('[QUEUE] Idle state shown');
}

/* ─────────────────────────────────────────
   UPI URI BUILDER
───────────────────────────────────────── */

function buildUpiUri(cost, slotNum) {
  const params = new URLSearchParams({
    pa: EXIT_UPI_ID,
    pn: EXIT_UPI_NAME,
    am: cost.toFixed(2),
    cu: 'INR',
    tn: 'Parking Slot ' + slotNum
  });
  return 'upi://pay?' + params.toString();
}

/* ─────────────────────────────────────────
   MQTT — receive bills + car detection
───────────────────────────────────────── */

function initMqtt() {
  const url = 'wss://6bf52feab0aa462a94eda4f44fdf671c.s1.eu.hivemq.cloud:8884/mqtt';
  mqttClient = mqtt.connect(url, {
    username: 'esp32-park',
    password: 'IoTesp32-Park',
    clientId: 'dashboard-' + Math.random().toString(16).slice(2, 8),
    protocolVersion: 5,
    clean: true,
    reconnectPeriod: 3000
  });

  mqttClient.on('connect', () => {
    console.log('[MQTT-WS] Connected');
    mqttClient.subscribe('parking/exit/bill', { qos: 0 });
    mqttClient.subscribe('parking/exit/car/detected', { qos: 0 });
  });

  mqttClient.on('message', (topic, message) => {

    /* ── Bill received → push to Firebase queue ── */
    if (topic === 'parking/exit/bill') {
      try {
        const bill = JSON.parse(message.toString());
        console.log('[MQTT-WS] Bill received:', bill);
        pushBillToFirebase(bill);      // writes to Firebase, then promotes automatically
      } catch (e) {
        console.error('[MQTT-WS] Bill parse error:', e);
      }
    }

    /* ── Exit IR triggered → set gateStatus = countdown in Firebase ── */
    if (topic === 'parking/exit/car/detected') {
      console.log('[IR] Car detected at exit');
      db.ref('exitQueue/active').once('value', snap => {
        if (!snap.val()) {
          console.log('[IR] No active bill — ignoring car detection');
          return;
        }
        db.ref('exitQueue/gateStatus').once('value', s => {
          const status = s.val();
          if (status === 'idle' || status === null) {
            console.log('[IR] Setting gateStatus = countdown');
            db.ref('exitQueue/gateStatus').set('countdown');
          } else {
            console.log('[IR] gateStatus already', status, '— ignoring');
          }
        });
      });
    }
  });

  mqttClient.on('error', err => console.error('[MQTT-WS] Error:', err));
  mqttClient.on('reconnect', () => console.log('[MQTT-WS] Reconnecting…'));
  mqttClient.on('offline', () => console.log('[MQTT-WS] Offline'));
}

/* ─────────────────────────────────────────
   BOOT
───────────────────────────────────────── */
initMqtt();
attachExitListeners();
processFirebaseQueue();   // recover any active bill on page load/refresh