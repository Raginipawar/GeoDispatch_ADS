/* Panel: stats, facility list, mode UI */

const PANEL = (() => {
  let allFacilities = [], filtered = [], selectedId = null;

  // ── Stats with countUp ───────────────────────────────────────────

  function _countUp(el, target, duration = 600) {
    const start = parseInt(el.textContent) || 0;
    const step  = (target - start) / (duration / 16);
    let   cur   = start;
    const timer = setInterval(() => {
      cur += step;
      if ((step > 0 && cur >= target) || (step < 0 && cur <= target)) {
        el.textContent = target;
        clearInterval(timer);
      } else {
        el.textContent = Math.round(cur);
      }
    }, 16);
  }

  function updateStats(facilities) {
    const total      = facilities.length;
    const online     = facilities.filter(f => f.state === 'online').length;
    const offline    = facilities.filter(f => f.state === 'offline').length;
    const overloaded = facilities.filter(f => f.state === 'overloaded').length;
    _countUp(document.getElementById('val-total'),      total);
    _countUp(document.getElementById('val-online'),     online);
    _countUp(document.getElementById('val-offline'),    offline);
    _countUp(document.getElementById('val-overloaded'), overloaded);
  }

  // ── Facility list ────────────────────────────────────────────────

  function _stateColor(state) {
    if (state === 'online')     return 'online';
    if (state === 'overloaded') return 'overloaded';
    return 'offline';
  }

  function _renderList(list) {
    const container = document.getElementById('facility-list');
    container.innerHTML = '';
    list.forEach(fac => {
      const div = document.createElement('div');
      div.className = 'fac-item' + (fac.id === selectedId ? ' selected' : '');
      div.dataset.id = fac.id;
      div.innerHTML = `
        <span class="fac-dot ${_stateColor(fac.state)}"></span>
        <span class="fac-item-name">${fac.name || 'Facility ' + fac.id}</span>
        <span class="fac-item-type">${fac.type || ''}</span>
      `;
      div.addEventListener('click', () => APP.selectFacility(fac));
      container.appendChild(div);
    });
  }

  function loadFacilities(facilities) {
    allFacilities = facilities;
    filtered = [...facilities];
    updateStats(facilities);
    _renderList(filtered);
  }

  function updateFacility(updatedFac) {
    const idx = allFacilities.findIndex(f => f.id === updatedFac.id);
    if (idx !== -1) allFacilities[idx] = { ...allFacilities[idx], ...updatedFac };
    else allFacilities.push(updatedFac);

    // Re-apply filter
    const q = document.getElementById('fac-search').value.toLowerCase();
    filtered = q
      ? allFacilities.filter(f => (f.name || '').toLowerCase().includes(q))
      : [...allFacilities];
    updateStats(allFacilities);
    _renderList(filtered);
  }

  function addFacility(fac) {
    updateFacility(fac);
  }

  function removeFacilityFromList(id) {
    allFacilities = allFacilities.filter(f => f.id !== id);
    filtered      = filtered.filter(f => f.id !== id);
    updateStats(allFacilities);
    _renderList(filtered);
  }

  function setSelected(id) {
    selectedId = id;
    document.querySelectorAll('.fac-item').forEach(el => {
      el.classList.toggle('selected', +el.dataset.id === id);
    });
  }

  function getSelectedId()   { return selectedId; }
  function getAllFacilities() { return allFacilities; }

  // ── Search ───────────────────────────────────────────────────────

  document.getElementById('fac-search').addEventListener('input', e => {
    const q = e.target.value.toLowerCase();
    filtered = q
      ? allFacilities.filter(f => (f.name || '').toLowerCase().includes(q))
      : [...allFacilities];
    _renderList(filtered);
  });

  // ── Query result display ─────────────────────────────────────────

  function showNearestResult(fac) {
    const box = document.getElementById('query-result');
    box.classList.remove('hidden');
    box.innerHTML = `
      <div class="fac-name">${fac.name || 'Facility ' + fac.id}</div>
      <div class="fac-type">${fac.type || ''} · id ${fac.id}</div>
      <div style="margin-top:6px;font-size:0.75rem;color:#6A6A8A">
        ${fac.lat?.toFixed(5)}, ${fac.lon?.toFixed(5)}
      </div>
    `;
    gsap.from(box, { y: 8, opacity: 0, duration: 0.3 });
  }

  function showKnnResult(facilities) {
    const box = document.getElementById('knn-result');
    box.classList.remove('hidden');
    box.innerHTML = facilities.map((f, i) =>
      `<div style="margin-bottom:5px">
        <span style="color:#6C63FF">#${i+1}</span>
        ${f.name || 'Facility ' + f.id}
        <span class="fac-type"> · ${f.type || ''}</span>
      </div>`
    ).join('');
    gsap.from(box, { y: 8, opacity: 0, duration: 0.3 });
  }

  return {
    loadFacilities, updateFacility, addFacility, removeFacilityFromList,
    setSelected, getSelectedId, getAllFacilities,
    updateStats, showNearestResult, showKnnResult,
  };
})();
