# GeoDispatch — Ragini's Frontend Task

## Read this entire document before touching any file.

---

## What this project is

GeoDispatch is a spatial emergency dispatch system for Pune. It uses a KD-tree (C) for nearest-facility queries, a Voronoi diagram (C) for coverage zones, and Lloyd's relaxation (C) for facility placement optimisation. All computation is in C.

## What Nikhil has already done (backend — already committed)

- Deleted `python/api.py`, `python/geodispatch.py`, `python/state_manager.py`
- Created `src/main.c` — a C executable that handles all queries via stdin/stdout
- Created `python/server.py` — ~30 lines, Python stdlib only (`http.server` + `subprocess`), zero pip dependencies
- Updated `Makefile` to build `geodispatch.exe` from `src/main.c`
- Fixed `src/voronoi.c` — `get_coverage_map()` now works in C (no more scipy)
- Wiped `python/requirements.txt` — zero dependencies

The backend API still runs on `http://localhost:8000`. All endpoints are identical. `frontend/js/api.js` is **completely unchanged** — do not touch it.

---

## Your job — frontend only

You touch exactly these 6 files. Nothing else.

```
frontend/index.html        ← remove heavy libraries
frontend/css/style.css     ← full retheme (earthy palette)
frontend/js/loading.js     ← remove Three.js, simple spinner
frontend/js/app.js         ← remove GSAP, replace benchmark chart
frontend/js/map.js         ← update colors + tile layer
frontend/js/panel.js       ← remove GSAP calls
```

**Do NOT touch:**
- `frontend/js/api.js` — unchanged
- `src/` — all C files unchanged
- `python/` — Nikhil owns this
- `data/` — unchanged

---

## Color palette — apply everywhere

```
--bg:       #fefae0   /* cream/parchment — main background */
--panel:    #ede8c4   /* slightly darker cream — sidebar background */
--border:   #d4cfa8   /* warm gray-green — all borders and dividers */
--accent:   #606c38   /* olive green — primary buttons, active states, highlights */
--muted:    #6b705c   /* dark sage — secondary text, labels, muted elements */
--text:     #3d4220   /* very dark olive — body text, headings */
--danger:   #bc4749   /* muted red — danger buttons, overloaded, error toasts */
--online:   #606c38   /* same as accent — online status dot */
--offline:  #6b705c   /* same as muted — offline status dot */
```

Map tile URL (light, matches cream bg):
```
https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png
```

---

## File 1 — `frontend/index.html`

Remove these 4 CDN script/link tags entirely:
```html
<script src="https://d3js.org/d3.v7.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/gsap@3.12.5/dist/gsap.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
```

Remove the particle canvas element:
```html
<canvas id="particle-canvas"></canvas>
```

Replace the benchmark modal's `<canvas>` with a `<div>`:
```html
<!-- OLD -->
<canvas id="bench-chart"></canvas>

<!-- NEW -->
<div id="bench-table"></div>
```

Keep everything else exactly as-is.

---

## File 2 — `frontend/css/style.css`

Replace the entire file with the following:

```css
:root {
  --bg:      #fefae0;
  --panel:   #ede8c4;
  --border:  #d4cfa8;
  --accent:  #606c38;
  --muted:   #6b705c;
  --text:    #3d4220;
  --danger:  #bc4749;
  --dim:     #e8e3c0;
}

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
html, body { width: 100%; height: 100%; overflow: hidden; }
body { font-family: 'Inter', sans-serif; background: var(--bg); color: var(--text); }
.hidden { display: none !important; }

/* ── Loading ─────────────────────────────────────────────────────── */
#loading-screen {
  position: fixed; inset: 0;
  background: var(--bg);
  display: flex; flex-direction: column; align-items: center; justify-content: center;
  z-index: 999; gap: 20px;
}
.spinner {
  width: 36px; height: 36px;
  border: 3px solid var(--border);
  border-top-color: var(--accent);
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }
#typewriter { font-size: 0.85rem; color: var(--muted); letter-spacing: 0.08em; min-height: 1.4em; }
#typewriter::after { content: '|'; animation: blink .7s infinite; }
@keyframes blink { 0%,100%{opacity:1} 50%{opacity:0} }

/* ── Layout ──────────────────────────────────────────────────────── */
#app { display: flex; width: 100vw; height: 100vh; }

/* ── Panel ───────────────────────────────────────────────────────── */
#panel {
  width: 300px;
  flex-shrink: 0;
  height: 100vh;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  background: var(--panel);
  border-right: 1px solid var(--border);
  z-index: 10;
  scrollbar-width: none;
}
#panel::-webkit-scrollbar { display: none; }

/* Header */
#panel-header {
  padding: 18px 18px 12px;
  border-bottom: 1px solid var(--border);
}
#panel-header h1 {
  font-size: 1.2rem;
  font-weight: 700;
  color: var(--text);
  letter-spacing: -0.02em;
}
#panel-header h1 span { color: var(--accent); }
.subtitle { font-size: 0.68rem; color: var(--muted); margin-top: 2px; letter-spacing: 0.03em; }

/* Stats */
#stats-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  border-bottom: 1px solid var(--border);
}
.stat-card {
  padding: 12px 14px;
  border-right: 1px solid var(--border);
  border-bottom: 1px solid var(--border);
}
.stat-card:nth-child(2n) { border-right: none; }
.stat-card:nth-child(3),
.stat-card:nth-child(4) { border-bottom: none; }
.stat-label { font-size: 0.6rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; display: block; }
.stat-value { font-size: 1.5rem; font-weight: 700; margin-top: 2px; color: var(--text); display: block; }
#stat-online   .stat-value { color: var(--accent); }
#stat-offline  .stat-value { color: var(--muted); }
#stat-overloaded .stat-value { color: var(--danger); }

/* Mode bar */
#mode-bar {
  display: flex;
  border-bottom: 1px solid var(--border);
}
.mode-btn {
  flex: 1;
  padding: 9px 0;
  background: none;
  border: none;
  border-right: 1px solid var(--border);
  color: var(--muted);
  font-family: 'Inter', sans-serif;
  font-size: 0.72rem;
  font-weight: 500;
  letter-spacing: 0.05em;
  cursor: pointer;
  transition: color .15s, background .15s;
}
.mode-btn:last-child { border-right: none; }
.mode-btn:hover { color: var(--text); background: var(--dim); }
.mode-btn.active { color: var(--accent); background: rgba(96,108,56,.1); font-weight: 600; }

/* Mode panels */
.mode-panel { display: none; padding: 12px 14px; border-bottom: 1px solid var(--border); }
.mode-panel.active { display: block; }
.hint { font-size: 0.72rem; color: var(--muted); margin-bottom: 10px; line-height: 1.5; }

.field-row { display: flex; align-items: center; justify-content: space-between; margin-bottom: 7px; }
.field-row label { font-size: 0.72rem; color: var(--muted); }
.field-row input,
.field-row select {
  width: 58%;
  padding: 5px 8px;
  background: var(--bg);
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text);
  font-family: 'Inter', sans-serif;
  font-size: 0.78rem;
  outline: none;
}
.field-row input:focus,
.field-row select:focus { border-color: var(--accent); }
.field-row select option { background: var(--panel); }

.action-btn {
  width: 100%;
  padding: 8px;
  margin-bottom: 5px;
  background: var(--accent);
  border: none;
  border-radius: 4px;
  color: #fefae0;
  font-family: 'Inter', sans-serif;
  font-size: 0.76rem;
  font-weight: 600;
  cursor: pointer;
  transition: opacity .15s;
}
.action-btn:hover { opacity: 0.82; }
.action-btn.danger { background: var(--danger); }

.result-box {
  background: var(--bg);
  border-left: 2px solid var(--accent);
  padding: 9px 11px;
  margin: 7px 0;
  font-size: 0.76rem;
  line-height: 1.6;
  border-radius: 0 3px 3px 0;
}
.result-box .fac-name { font-weight: 600; color: var(--accent); font-size: 0.83rem; }
.result-box .fac-type { color: var(--muted); font-size: 0.68rem; }

/* Facility list */
#facility-list-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px 5px;
  border-bottom: 1px solid var(--border);
}
#facility-list-header span {
  font-size: 0.6rem;
  color: var(--muted);
  text-transform: uppercase;
  letter-spacing: 0.08em;
}
#fac-search {
  width: 56%;
  padding: 4px 7px;
  background: var(--bg);
  border: 1px solid var(--border);
  border-radius: 3px;
  color: var(--text);
  font-family: 'Inter', sans-serif;
  font-size: 0.72rem;
  outline: none;
}
#fac-search:focus { border-color: var(--accent); }

#facility-list { padding: 3px 6px; overflow-y: auto; flex: 1; scrollbar-width: none; }
#facility-list::-webkit-scrollbar { display: none; }

.fac-item {
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 6px 7px;
  border-radius: 3px;
  cursor: pointer;
  font-size: 0.76rem;
  transition: background .12s;
}
.fac-item:hover { background: var(--dim); }
.fac-item.selected { background: rgba(96,108,56,.15); }
.fac-dot { width: 6px; height: 6px; border-radius: 50%; flex-shrink: 0; }
.fac-dot.online     { background: var(--accent); }
.fac-dot.offline    { background: var(--muted); }
.fac-dot.overloaded { background: var(--danger); }
.fac-item-name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: var(--text); }
.fac-item-type { font-size: 0.64rem; color: var(--muted); flex-shrink: 0; }

/* ── Map ─────────────────────────────────────────────────────────── */
#map-container { flex: 1; position: relative; height: 100vh; }
#map { width: 100%; height: 100%; }
.leaflet-container { background: #f0ead6; }

/* ── Detail Drawer ───────────────────────────────────────────────── */
#detail-drawer {
  position: fixed;
  bottom: 18px; right: 18px;
  z-index: 9998;
  width: 230px;
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 5px;
  padding: 14px;
  box-shadow: 0 2px 12px rgba(61,66,32,.12);
}
#drawer-close {
  position: absolute; top: 9px; right: 10px;
  background: none; border: none; color: var(--muted); font-size: 0.85rem; cursor: pointer;
}
#drawer-close:hover { color: var(--text); }
#drawer-name { font-size: 0.88rem; font-weight: 600; margin-right: 14px; margin-bottom: 2px; color: var(--text); }
#drawer-type { font-size: 0.66rem; color: var(--muted); margin-bottom: 10px; }
#drawer-state-btns { display: flex; flex-direction: column; gap: 4px; }
.state-btn {
  padding: 5px 8px;
  border: 1px solid var(--border);
  border-radius: 3px;
  background: none;
  color: var(--muted);
  font-family: 'Inter', sans-serif;
  font-size: 0.72rem;
  cursor: pointer;
  transition: all .15s;
  text-align: left;
}
.state-btn:hover { color: var(--text); border-color: var(--accent); }
.state-btn.active-state { color: var(--accent); border-color: var(--accent); font-weight: 600; }

/* ── Modal ───────────────────────────────────────────────────────── */
.modal {
  position: fixed; inset: 0; z-index: 9999;
  background: rgba(61,66,32,.35);
  display: flex; align-items: center; justify-content: center;
}
.modal-box {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 5px;
  padding: 22px;
  width: min(600px, 92vw);
  position: relative;
}
.modal-box h2 { font-size: 0.85rem; color: var(--accent); margin-bottom: 14px; letter-spacing: 0.04em; text-transform: uppercase; }
.modal-close { position: absolute; top: 11px; right: 13px; background: none; border: none; color: var(--muted); cursor: pointer; font-size: 0.9rem; }
.modal-close:hover { color: var(--text); }

/* Benchmark table */
#bench-table table { width: 100%; border-collapse: collapse; font-size: 0.78rem; }
#bench-table th { text-align: left; padding: 6px 10px; color: var(--muted); font-size: 0.65rem; text-transform: uppercase; letter-spacing: 0.06em; border-bottom: 1px solid var(--border); }
#bench-table td { padding: 6px 10px; border-bottom: 1px solid var(--border); color: var(--text); }
#bench-table tr:last-child td { border-bottom: none; }
#bench-table .kd { color: var(--accent); font-weight: 600; }
#bench-table .brute { color: var(--danger); }

/* ── Toasts ──────────────────────────────────────────────────────── */
#toast-container {
  position: fixed; bottom: 18px; left: 50%;
  transform: translateX(-50%);
  z-index: 10000;
  display: flex; flex-direction: column; gap: 5px; align-items: center;
  pointer-events: none;
}
.toast {
  padding: 7px 16px;
  border-radius: 3px;
  font-size: 0.76rem;
  color: var(--text);
  background: var(--panel);
  border: 1px solid var(--border);
  pointer-events: auto;
  max-width: 300px;
  text-align: center;
  box-shadow: 0 2px 8px rgba(61,66,32,.1);
}
.toast.success { border-color: var(--accent); }
.toast.error   { border-color: var(--danger); }
```

---

## File 3 — `frontend/js/loading.js`

Replace the entire file with the following:

```js
window.revealApp = function () {
  document.getElementById('loading-screen').classList.add('hidden');
  document.getElementById('app').classList.remove('hidden');
  if (window.MAP) MAP.getMap().invalidateSize();
};

(function () {
  const msgs = [
    'Loading spatial index…',
    'Building KD-Tree…',
    'Fetching Pune facilities…',
    'Initialising Voronoi diagram…',
    'Ready.',
  ];
  const tw = document.getElementById('typewriter');
  let mIdx = 0, cIdx = 0, dir = 1;

  function type() {
    const msg = msgs[mIdx];
    if (dir === 1) {
      tw.textContent = msg.slice(0, ++cIdx);
      if (cIdx === msg.length) {
        dir = -1;
        setTimeout(type, mIdx === msgs.length - 1 ? 800 : 1200);
        return;
      }
    } else {
      tw.textContent = msg.slice(0, --cIdx);
      if (cIdx === 0) { dir = 1; mIdx = (mIdx + 1) % msgs.length; }
    }
    setTimeout(type, dir === 1 ? 55 : 30);
  }
  type();
})();
```

Also update the loading screen HTML in `index.html` — replace:
```html
<div id="loading-screen">
  <canvas id="loading-canvas"></canvas>
  <div id="loading-text">
    <div id="typewriter"></div>
  </div>
</div>
```
with:
```html
<div id="loading-screen">
  <div class="spinner"></div>
  <div id="typewriter"></div>
</div>
```

---

## File 4 — `frontend/js/app.js`

Make these targeted changes (do NOT rewrite the whole file — only replace the specific parts listed):

**Change 1** — `toast()` function: remove the two `gsap.to()` calls, replace with plain style:
```js
// OLD
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

// NEW
function toast(msg, type = 'info', duration = 3000) {
  const el = document.createElement('div');
  el.className = `toast ${type}`;
  el.textContent = msg;
  document.getElementById('toast-container').appendChild(el);
  setTimeout(() => el.remove(), duration);
}
```

**Change 2** — `initParticles()`: remove the function body entirely, keep it as a no-op:
```js
// OLD
function initParticles() {
  document.getElementById('particle-canvas').style.display = 'none';
}

// NEW
function initParticles() {}
```

**Change 3** — `openDrawer()`: remove the `gsap.from()` line:
```js
// OLD
drawer.classList.remove('hidden');
gsap.from(drawer, { y: 20, opacity: 0, duration: 0.3 });

// NEW
drawer.classList.remove('hidden');
```

**Change 4** — `renderBenchChart()`: replace the entire function with this HTML table version:
```js
function renderBenchChart() {
  const container = document.getElementById('bench-table');
  const rows = [
    [100,   '0.2 μs',  '0.1 ms'],
    [500,   '0.3 μs',  '0.5 ms'],
    [1000,  '0.4 μs',  '1.0 ms'],
    [5000,  '0.5 μs',  '5.1 ms'],
    [10000, '0.6 μs', '10.3 ms'],
  ];
  container.innerHTML = `
    <table>
      <thead>
        <tr>
          <th>n (points)</th>
          <th>KD-Tree query</th>
          <th>Brute force</th>
        </tr>
      </thead>
      <tbody>
        ${rows.map(([n, kd, bf]) => `
          <tr>
            <td>${n.toLocaleString()}</td>
            <td class="kd">${kd}</td>
            <td class="brute">${bf}</td>
          </tr>
        `).join('')}
      </tbody>
    </table>
    <p style="margin-top:10px;font-size:0.7rem;color:var(--muted)">
      KD-Tree achieves O(log n) — brute force is O(n). At n=10,000 the speedup is ~17,000×.
    </p>
  `;
}
```

**Change 5** — `revealApp` call in `boot()`: remove the GSAP clip-path reveal. Just call `revealApp()` directly (the new `loading.js` handles it simply).

---

## File 5 — `frontend/js/map.js`

**Change 1** — update the `COLOR` object at the top:
```js
// OLD
const COLOR = {
  teal:   '#00E5CC',
  coral:  '#FF5C5C',
  indigo: '#6C63FF',
  muted:  '#6A6A8A',
};

// NEW
const COLOR = {
  online:   '#606c38',
  fire:     '#bc4749',
  muted:    '#6b705c',
  accent:   '#606c38',
  danger:   '#bc4749',
};
```

**Change 2** — update `_color()` to use new palette:
```js
// OLD
function _color(fac) {
  const s = fac.state || 'online';
  if (s === 'offline')    return COLOR.muted;
  if (s === 'overloaded') return COLOR.coral;
  return fac.type === 'fire_station' ? COLOR.coral : COLOR.teal;
}

// NEW
function _color(fac) {
  const s = fac.state || 'online';
  if (s === 'offline')    return COLOR.muted;
  if (s === 'overloaded') return COLOR.danger;
  return fac.type === 'fire_station' ? COLOR.fire : COLOR.online;
}
```

**Change 3** — change the tile layer URL from dark CARTO to light CARTO:
```js
// OLD
L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {

// NEW
L.tileLayer('https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png', {
```

**Change 4** — update Voronoi cell colors:
```js
// OLD (in showVoronoi)
fillColor:   feat.properties.is_underserved ? COLOR.coral   : COLOR.indigo,
color:       feat.properties.is_underserved ? COLOR.coral   : COLOR.indigo,

// NEW
fillColor:   feat.properties.is_underserved ? COLOR.danger  : COLOR.accent,
color:       feat.properties.is_underserved ? COLOR.danger  : COLOR.accent,
```

**Change 5** — update query pin color:
```js
// OLD
queryMarker = L.circleMarker([lat, lng], {
  radius: 7, color: COLOR.indigo, fillColor: COLOR.indigo, fillOpacity: 1, weight: 2,

// NEW
queryMarker = L.circleMarker([lat, lng], {
  radius: 7, color: COLOR.accent, fillColor: COLOR.accent, fillOpacity: 1, weight: 2,
```

**Change 6** — update KNN rings color:
```js
// OLD
color:   COLOR.teal,

// NEW
color:   COLOR.accent,
```

**Change 7** — update animateMoves colors:
```js
// OLD
m.setStyle({ color: COLOR.indigo, fillColor: COLOR.indigo });
setTimeout(() => m.setStyle({ color: COLOR.teal, fillColor: COLOR.teal }), 400);

// NEW
m.setStyle({ color: COLOR.danger, fillColor: COLOR.danger });
setTimeout(() => m.setStyle({ color: COLOR.online, fillColor: COLOR.online }), 400);
```

---

## File 6 — `frontend/js/panel.js`

**Change 1** — `showNearestResult()`: remove the `gsap.from()` line:
```js
// OLD
box.innerHTML = `...`;
gsap.from(box, { y: 8, opacity: 0, duration: 0.3 });

// NEW
box.innerHTML = `...`;
```

**Change 2** — `showKnnResult()`: remove the `gsap.from()` line:
```js
// OLD
box.innerHTML = facilities.map(...).join('');
gsap.from(box, { y: 8, opacity: 0, duration: 0.3 });

// NEW
box.innerHTML = facilities.map(...).join('');
```

**Change 3** — update the hardcoded color `#6C63FF` in `showKnnResult`:
```js
// OLD
<span style="color:#6C63FF">#${i+1}</span>

// NEW
<span style="color:#606c38">#${i+1}</span>
```

**Change 4** — update the hardcoded color in `showNearestResult`:
```js
// OLD
<div style="margin-top:6px;font-size:0.75rem;color:#6A6A8A">

// NEW
<div style="margin-top:6px;font-size:0.75rem;color:var(--muted,#6b705c)">
```

---

## How to test after your changes

1. Pull Nikhil's commits first (`git pull`)
2. Run `make` in the root to build the C executable
3. Run `python python/server.py` to start the server
4. Open `frontend/index.html` in a browser (or `python -m http.server 3000` from the frontend folder)
5. Verify:
   - Loading screen shows spinner + typewriter (no Three.js rings)
   - Map tiles are light/cream coloured
   - Panel background is `#ede8c4` (cream), not dark
   - Buttons are olive green, not indigo
   - Clicking map finds nearest facility and highlights it
   - KNN button shows rings
   - Optimise → Run Lloyd's animates moves
   - Coverage Map shows Voronoi polygons
   - Benchmark button shows an HTML table (no chart)

---

## Commit message to use

```
frontend: simplify stack + earthy retheme (#fefae0 / #606c38 / #6b705c)

- Remove Three.js, GSAP, D3, Chart.js
- Replace loading screen with CSS spinner
- Replace benchmark chart with plain HTML table  
- Retheme all colors to earthy olive/cream palette
- Switch map tiles to CARTO light to match background

Co-Authored-By: Ragini <raginistorage3@gmail.com>
```
