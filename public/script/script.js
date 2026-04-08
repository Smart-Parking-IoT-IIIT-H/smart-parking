/* ── Firebase ── */
const firebaseConfig = {
  apiKey:            "AIzaSyA73-b9qGJdzO34u7gjFIkOSqqTKn8a76A",
  authDomain:        "smart-parking-1df76.firebaseapp.com",
  databaseURL:       "https://smart-parking-1df76-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId:         "smart-parking-1df76",
  storageBucket:     "smart-parking-1df76.firebasestorage.app",
  messagingSenderId: "448571319292",
  appId:             "1:448571319292:web:627000f798c7c42d658879"
};
firebase.initializeApp(firebaseConfig);
const db = firebase.database();

/* ── State ── */
const state = {
  slots: {1:null, 2:null, 3:null, 4:null},
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
      <div class="p-slot" id="ps${i}" style="animation-delay:${(i-1)*.07}s">
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
  /* segs */
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

  const cls   = data.error ? 'error' : data.occupied ? 'occupied' : 'free';
  const icon  = data.error ? '⚠️' : data.occupied ? '🚗' : '🅿';
  const label = data.error ? 'fault' : data.occupied ? 'occupied' : 'free';
  const ts    = data.timestamp ? new Date(data.timestamp * 1000) : new Date();
  const timeStr = ts.toLocaleTimeString('en-IN', {hour:'2-digit', minute:'2-digit', second:'2-digit'});

  /* Banner slot card */
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
    /* seg */
    const bseg = document.getElementById('bseg' + id);
    if (bseg) bseg.className = 'b-seg ' + cls;
  }

  /* Phone */
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
  const occ  = slots.filter(s => s.occupied).length;
  const pct  = Math.round((occ / 4) * 100);
  const freePct = 100 - pct;
  const now  = new Date().toLocaleTimeString('en-IN', {hour:'2-digit', minute:'2-digit', second:'2-digit'});
  const isFull = free === 0;

  /* Banner free num */
  const bf = document.getElementById('b-free');
  bf.textContent = free;
  bf.className = 'b-free-num' + (isFull ? ' zero' : '');
  document.getElementById('b-free-small').textContent = free;
  document.getElementById('b-free-small').style.color = isFull ? 'var(--red)' : 'var(--green)';

  /* Utilization % (occupied) */
  const bPct = document.getElementById('b-pct');
  bPct.textContent = pct + '%';
  bPct.className = 'b-util-pct' + (pct >= 100 ? ' empty' : pct >= 50 ? ' warn' : '');

  /* Hero card glow */
  const heroCard = document.getElementById('b-hero-card');
  heroCard.className = 'b-card b-card-hero' + (isFull ? ' full' : '');

  /* Occ text */
  const occEl = document.getElementById('b-occ-txt');
  occEl.textContent = occ + ' occupied';
  occEl.className = 'accent' + (isFull ? ' zero' : '');
  occEl.style.color = isFull ? 'var(--red)' : 'var(--green)';

  /* Alert */
  const alertOv = document.getElementById('b-alert-overlay');
  alertOv.style.display = isFull ? 'flex' : 'none';

  /* Times */
  document.getElementById('b-time').textContent = now;
  document.getElementById('b-footer-time').textContent = now;
  document.getElementById('b-last-update').textContent = 'last update: ' + now;

  /* Uptime */
  const uptimeMins = Math.round((Date.now() - state.startTime) / 60000);
  document.getElementById('b-uptime').textContent = uptimeMins + 'm';

  /* Phone */
  const pf = document.getElementById('p-free');
  pf.textContent = free;
  pf.className = 'p-free-big' + (isFull ? ' zero' : '');
  document.getElementById('p-pct').textContent = pct + '%';
  document.getElementById('p-alert').className = 'p-alert' + (isFull ? ' show' : '');
  document.getElementById('p-time').textContent = now;
  document.getElementById('p-update').textContent = 'updated ' + now;
  document.getElementById('p-update2').textContent = now;
  const pheroCard = document.getElementById('p-hero-card');
  pheroCard.className = 'p-hero' + (isFull ? ' full' : '');

  /* History */
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
    /* update color based on count */
    const col = count === 4 ? '#ff4466' : count >= 3 ? '#ffbb44' : '#00ff88';
    c.data.datasets[0].borderColor = col;
    c.update('none');
  });
}

/* ── Watchdog ── */
function startWatchdog() {
  setInterval(() => {
    const stale = Date.now() - state.lastHb > 15000;
    ['b-dot','p-dot'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.className = 'dot' + (stale ? ' off' : '');
    });
    ['b-status','p-status'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.textContent = stale ? 'offline' : 'live';
    });
  }, 3000);
}

/* ── Firebase listeners ── */
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

/* ── Init ── */
buildBannerMiniSlots();
buildBannerSegs();
buildPhoneSlots();
initCharts();
startWatchdog();
attachListeners();
