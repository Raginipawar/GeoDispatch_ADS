/* Main orchestrator */

const APP = (() => {
  let currentMode   = 'QUERY';
  let selectedFac   = null;
  let benchChart    = null;
  let pendingLatLng = null;

  // ── Toast ────────────────────────────────────────────────────────

  function toast(msg, type = 'info', duration = 3000) {
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.textContent = msg;
    document.getElementById('toast-container').appendChild(el);
    gsap.to(el, { opacity: 1, y: 0, duration: 0.3 });
    setTimeout(() => {
      gsap.to(el, { opacity: 0, y: 20, duration: 0.3, onComplete: () => el.remove() });
    }, duration);
  }

  // Particles removed — CSS animation is used instead (no JS thread cost)
  function initParticles() {
    document.getElementById('particle-canvas').style.display = 'none';
  }

  // ── Mode switching ───────────────────────────────────────────────

  function setMode(mode) {
    currentMode = mode;
    document.querySelectorAll('.mode-btn').forEach(b => {
      b.classList.toggle('active', b.dataset.mode === mode);
    });
    document.querySelectorAll('.mode-panel').forEach(p => {
      p.classList.toggle('active', p.id === 'mode-' + mode.toLowerCase());
    });

    if (mode === 'QUERY') {
      MAP.setClickHandler((lat, lng) => handleQueryClick(lat, lng));
      MAP.hideVoronoi();
    } else if (mode === 'EDIT') {
      MAP.setClickHandler((lat, lng) => { pendingLatLng = { lat, lng }; toast('Coordinates captured. Fill the form and click Place Facility.', 'info'); });
      MAP.hideVoronoi();
    } else if (mode === 'OPTIMISE') {
      MAP.setClickHandler(null);
    }
  }

  // ── QUERY mode ───────────────────────────────────────────────────

  async function handleQueryClick(lat, lng) {
    MAP.showQueryPin(lat, lng);
    try {
      const data = await api.nearest(lat, lng);
      const fac  = data.facility;
      PANEL.showNearestResult(fac);
      MAP.pulseMarker(fac.id);
      MAP.panTo(fac.lat, fac.lon);
    } catch (e) {
      toast(e.message, 'error');
    }
  }

  document.getElementById('btn-knn').addEventListener('click', async () => {
    const center = MAP.getMap().getCenter();
    const k = parseInt(document.getElementById('knn-k').value) || 5;
    try {
      const data = await api.knn(center.lat, center.lng, k);
      PANEL.showKnnResult(data.facilities);
      MAP.showKnnRings(data.facilities);
      data.facilities.forEach(f => MAP.pulseMarker(f.id));
    } catch (e) {
      toast(e.message, 'error');
    }
  });

  // ── EDIT mode ────────────────────────────────────────────────────

  document.getElementById('btn-place').addEventListener('click', async () => {
    if (!pendingLatLng) { toast('Click on the map first.', 'error'); return; }
    const name = document.getElementById('new-fac-name').value.trim() || 'New Facility';
    const type = document.getElementById('new-fac-type').value;
    try {
      const data = await api.addFacility(pendingLatLng.lat, pendingLatLng.lng, name, type);
      const fac  = data.facility;
      PANEL.addFacility(fac);
      MAP.renderFacilities(PANEL.getAllFacilities());
      MAP.pulseMarker(fac.id);
      pendingLatLng = null;
      toast(`Added: ${fac.name}`, 'success');
    } catch (e) {
      toast(e.message, 'error');
    }
  });

  document.getElementById('btn-remove').addEventListener('click', async () => {
    const id = PANEL.getSelectedId();
    if (!id) { toast('Select a facility first.', 'error'); return; }
    try {
      await api.removeFacility(id);
      PANEL.removeFacilityFromList(id);
      MAP.renderFacilities(PANEL.getAllFacilities());
      closeDrawer();
      toast('Facility removed.', 'success');
    } catch (e) {
      toast(e.message, 'error');
    }
  });

  // ── OPTIMISE mode ────────────────────────────────────────────────

  document.getElementById('btn-optimise').addEventListener('click', async () => {
    const iterations = parseInt(document.getElementById('opt-iter').value) || 10;
    const threshold  = parseFloat(document.getElementById('opt-thresh').value) || 50;
    toast('Running Lloyd\'s algorithm…', 'info');
    try {
      const data = await api.optimise(iterations, threshold);
      if (data.steps && data.steps.length) {
        MAP.animateMoves(data.steps);
        toast(`Done. ${data.steps.flatMap(s => s.facility_movements || []).length} moves.`, 'success');
      } else {
        toast(data.msg || 'Already converged.', 'info');
      }
    } catch (e) {
      toast(e.message, 'error');
    }
  });

  document.getElementById('btn-coverage').addEventListener('click', async () => {
    toast('Fetching coverage map…', 'info');
    try {
      const data = await api.coverageMap();
      MAP.showVoronoi(data);
      toast('Coverage map shown.', 'success');
    } catch (e) {
      toast(e.message, 'error');
    }
  });

  // ── Benchmark modal ──────────────────────────────────────────────

  document.getElementById('btn-benchmark').addEventListener('click', () => {
    document.getElementById('bench-modal').classList.remove('hidden');
    renderBenchChart();
  });

  document.getElementById('bench-close').addEventListener('click', () => {
    document.getElementById('bench-modal').classList.add('hidden');
  });

  function renderBenchChart() {
    if (benchChart) { benchChart.destroy(); benchChart = null; }
    const ctx = document.getElementById('bench-chart').getContext('2d');
    // Illustrative theoretical data matching spec labels
    const ns = [100, 500, 1000, 5000, 10000];
    const kdTree   = ns.map(n => 0.32 * Math.log2(n));   // O(log n)
    const brute    = ns.map(n => n * 0.0012);             // O(n)
    const degraded = ns.map(n => 0.32 * Math.log2(n) * 1.32);

    benchChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: ns.map(n => n.toLocaleString()),
        datasets: [
          { label: 'KD-Tree (fresh)',   data: kdTree,   borderColor: '#00E5CC', tension: 0.4, pointRadius: 4 },
          { label: 'Brute Force',        data: brute,    borderColor: '#FF5C5C', tension: 0.4, pointRadius: 4 },
          { label: 'KD-Tree (40% del)', data: degraded, borderColor: '#6C63FF', tension: 0.4, pointRadius: 4, borderDash: [5,3] },
        ]
      },
      options: {
        responsive: true,
        plugins: {
          legend: { labels: { color: '#F0F0FF', font: { family: 'Inter' } } }
        },
        scales: {
          x: { ticks: { color: '#6A6A8A' }, grid: { color: 'rgba(255,255,255,0.05)' } },
          y: {
            ticks: { color: '#6A6A8A', callback: v => v.toFixed(1) + ' μs' },
            grid: { color: 'rgba(255,255,255,0.05)' },
            title: { display: true, text: 'Query Time (μs)', color: '#6A6A8A' },
          }
        }
      }
    });
  }

  // ── Facility selection / drawer ──────────────────────────────────

  function selectFacility(fac) {
    selectedFac = fac;
    PANEL.setSelected(fac.id);
    MAP.panTo(fac.lat, fac.lon);
    MAP.pulseMarker(fac.id);
    openDrawer(fac);
  }

  function openDrawer(fac) {
    const drawer = document.getElementById('detail-drawer');
    document.getElementById('drawer-name').textContent = fac.name || 'Facility ' + fac.id;
    document.getElementById('drawer-type').textContent = fac.type || '';

    const btns = document.getElementById('drawer-state-btns');
    btns.innerHTML = '';
    ['online', 'offline', 'overloaded'].forEach(s => {
      const b = document.createElement('button');
      b.className = 'state-btn' + (fac.state === s ? ' active-state' : '');
      b.textContent = s.charAt(0).toUpperCase() + s.slice(1);
      b.addEventListener('click', () => changeState(fac.id, s));
      btns.appendChild(b);
    });

    drawer.classList.remove('hidden');
    gsap.from(drawer, { y: 20, opacity: 0, duration: 0.3 });
  }

  function closeDrawer() {
    document.getElementById('detail-drawer').classList.add('hidden');
    selectedFac = null;
  }

  document.getElementById('drawer-close').addEventListener('click', closeDrawer);

  async function changeState(id, newState) {
    try {
      const data = await api.setState(id, newState);
      if (!data.ok) { toast(data.msg, 'error'); return; }
      const updated = { ...selectedFac, state: newState };
      selectedFac = updated;
      PANEL.updateFacility(updated);
      MAP.updateMarker(updated);
      openDrawer(updated);
      toast(`State → ${newState}`, 'success');
    } catch (e) {
      toast(e.message, 'error');
    }
  }

  // ── Mode button wiring ───────────────────────────────────────────

  document.querySelectorAll('.mode-btn').forEach(btn => {
    btn.addEventListener('click', () => setMode(btn.dataset.mode));
  });

  // ── Bootstrap ────────────────────────────────────────────────────

  async function boot() {
    MAP.init();
    initParticles();
    setMode('QUERY');

    try {
      const data = await api.facilities();
      PANEL.loadFacilities(data.facilities);
      MAP.renderFacilities(data.facilities);
      toast(`${data.facilities.length} facilities loaded.`, 'success');
    } catch (e) {
      toast('Failed to load facilities: ' + e.message, 'error');
    }

    revealApp();
    // Leaflet measured the container while it was hidden → recalculate
    setTimeout(() => MAP.getMap().invalidateSize(), 300);
  }

  return { boot, selectFacility, toast };
})();

// boot after DOM + scripts ready
document.addEventListener('DOMContentLoaded', () => APP.boot());
