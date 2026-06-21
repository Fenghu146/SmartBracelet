/* SmartBracelet — Charts (charts.js) */
// Chart.js-based data visualization for steps, battery, and activity

// ── Data Storage (localStorage) ──
const STORAGE_KEY_STEPS = 'sb_steps_history';
const STORAGE_KEY_BATTERY = 'sb_battery_history';
const STORAGE_KEY_ACTIVITY = 'sb_activity_history';
const MAX_HISTORY_DAYS = 7;
const MAX_BATTERY_POINTS = 60; // last 60 readings

function loadHistory(key) {
  try { return JSON.parse(localStorage.getItem(key)) || []; }
  catch { return []; }
}

function saveHistory(key, data) {
  localStorage.setItem(key, JSON.stringify(data));
}

function todayKey() {
  const d = new Date();
  return `${d.getFullYear()}-${d.getMonth()+1}-${d.getDate()}`;
}

// Record today's steps (updates existing entry for today)
function recordSteps(steps) {
  const hist = loadHistory(STORAGE_KEY_STEPS);
  const tk = todayKey();
  const existing = hist.find(h => h.date === tk);
  if (existing) existing.steps = Math.max(existing.steps, steps);
  else hist.push({ date: tk, steps });
  // Keep only last N days
  while (hist.length > MAX_HISTORY_DAYS) hist.shift();
  saveHistory(STORAGE_KEY_STEPS, hist);
}

// Record battery reading
function recordBattery(pct) {
  if (pct < 0) return;
  const hist = loadHistory(STORAGE_KEY_BATTERY);
  hist.push({ time: Date.now(), pct });
  while (hist.length > MAX_BATTERY_POINTS) hist.shift();
  saveHistory(STORAGE_KEY_BATTERY, hist);
}

// Record activity duration (in seconds per type)
function recordActivity(actType) {
  const hist = loadHistory(STORAGE_KEY_ACTIVITY);
  const tk = todayKey();
  let today = hist.find(h => h.date === tk);
  if (!today) {
    today = { date: tk, walk: 0, run: 0, idle: 0 };
    hist.push(today);
  }
  // Increment by ~1 second per call (called from periodic update)
  if (actType === 0) today.walk++;
  else if (actType === 1) today.run++;
  else today.idle++;
  while (hist.length > MAX_HISTORY_DAYS) hist.shift();
  saveHistory(STORAGE_KEY_ACTIVITY, hist);
}

// ── Chart Instances ──
let stepsChart = null;
let batteryChart = null;
let activityChart = null;

const chartColors = {
  accent: '#00d4ff',
  green: '#00d488',
  red: '#ff4466',
  amber: '#ffaa00',
  text: '#8888a0',
  grid: '#2a2a45',
  bg: '#1a1a2e',
};

const chartDefaults = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: { labels: { color: chartColors.text, font: { size: 11 } } },
  },
  scales: {
    x: { ticks: { color: chartColors.text, font: { size: 10 } }, grid: { color: chartColors.grid } },
    y: { ticks: { color: chartColors.text, font: { size: 10 } }, grid: { color: chartColors.grid } },
  },
};

// ── Init Charts ──
function initCharts() {
  if (typeof Chart === 'undefined') {
    console.warn('Chart.js not loaded');
    return;
  }

  // Steps bar chart
  const stepsCtx = $('stepsChart');
  if (stepsCtx) {
    const hist = loadHistory(STORAGE_KEY_STEPS);
    const labels = hist.map(h => h.date.slice(5)); // MM-DD
    const data = hist.map(h => h.steps);
    stepsChart = new Chart(stepsCtx, {
      type: 'bar',
      data: {
        labels: labels.length ? labels : ['No data'],
        datasets: [{
          label: 'Steps',
          data: data.length ? data : [0],
          backgroundColor: chartColors.accent,
          borderRadius: 4,
          barPercentage: 0.6,
        }],
      },
      options: {
        ...chartDefaults,
        plugins: {
          ...chartDefaults.plugins,
          legend: { display: false },
        },
        scales: {
          ...chartDefaults.scales,
          y: { ...chartDefaults.scales.y, beginAtZero: true },
        },
      },
    });
  }

  // Battery line chart
  const battCtx = $('batteryChart');
  if (battCtx) {
    const hist = loadHistory(STORAGE_KEY_BATTERY);
    const labels = hist.map(h => {
      const d = new Date(h.time);
      return `${d.getHours()}:${String(d.getMinutes()).padStart(2,'0')}`;
    });
    const data = hist.map(h => h.pct);
    batteryChart = new Chart(battCtx, {
      type: 'line',
      data: {
        labels: labels.length ? labels : ['No data'],
        datasets: [{
          label: 'Battery %',
          data: data.length ? data : [0],
          borderColor: chartColors.green,
          backgroundColor: 'rgba(0,212,136,0.1)',
          fill: true,
          tension: 0.3,
          pointRadius: 0,
        }],
      },
      options: {
        ...chartDefaults,
        plugins: { ...chartDefaults.plugins, legend: { display: false } },
        scales: {
          ...chartDefaults.scales,
          y: { ...chartDefaults.scales.y, min: 0, max: 100 },
        },
      },
    });
  }

  // Activity pie chart (today)
  const actCtx = $('activityChart');
  if (actCtx) {
    const hist = loadHistory(STORAGE_KEY_ACTIVITY);
    const today = hist.find(h => h.date === todayKey()) || { walk: 0, run: 0, idle: 1 };
    activityChart = new Chart(actCtx, {
      type: 'doughnut',
      data: {
        labels: ['Walk', 'Run', 'Idle'],
        datasets: [{
          data: [today.walk || 0, today.run || 0, today.idle || 1],
          backgroundColor: [chartColors.accent, chartColors.red, chartColors.amber],
          borderWidth: 0,
        }],
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: {
            position: 'bottom',
            labels: { color: chartColors.text, font: { size: 11 }, padding: 12 },
          },
        },
      },
    });
  }
}

// ── Update Charts (called when new data arrives) ──
function updateCharts(steps, battPct, actType) {
  // Record data
  if (steps !== undefined) recordSteps(steps);
  if (battPct !== undefined && battPct >= 0) recordBattery(battPct);
  if (actType !== undefined) recordActivity(actType);

  // Update steps chart
  if (stepsChart) {
    const hist = loadHistory(STORAGE_KEY_STEPS);
    stepsChart.data.labels = hist.map(h => h.date.slice(5));
    stepsChart.data.datasets[0].data = hist.map(h => h.steps);
    stepsChart.update('none');
  }

  // Update battery chart
  if (batteryChart) {
    const hist = loadHistory(STORAGE_KEY_BATTERY);
    batteryChart.data.labels = hist.map(h => {
      const d = new Date(h.time);
      return `${d.getHours()}:${String(d.getMinutes()).padStart(2,'0')}`;
    });
    batteryChart.data.datasets[0].data = hist.map(h => h.pct);
    batteryChart.update('none');
  }

  // Update activity chart
  if (activityChart) {
    const hist = loadHistory(STORAGE_KEY_ACTIVITY);
    const today = hist.find(h => h.date === todayKey()) || { walk: 0, run: 0, idle: 1 };
    activityChart.data.datasets[0].data = [today.walk || 0, today.run || 0, today.idle || 0];
    activityChart.update('none');
  }
}
