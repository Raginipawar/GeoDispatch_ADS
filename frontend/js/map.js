/* Leaflet map — earthy palette, light tile layer */

const MAP = (() => {
  let map;
  let markers      = {};
  let voronoiLayer  = null;
  let clickHandler  = null;
  let queryMarker   = null;
  let knnRings      = [];
  let originalDots  = [];

  const COLOR = {
    online:  '#606c38',
    fire:    '#bc4749',
    muted:   '#6b705c',
    accent:  '#606c38',
    danger:  '#bc4749',
    purple:  '#9c89b8',
  };

  function _color(fac) {
    const s = fac.state || 'online';
    if (s === 'offline')    return COLOR.muted;
    if (s === 'overloaded') return COLOR.danger;
    return fac.type === 'fire_station' ? COLOR.fire : COLOR.online;
  }

  // ── Init ─────────────────────────────────────────────────────────

  function init() {
    map = L.map('map', {
      center: [18.5204, 73.8567],
      zoom: 13,
      zoomControl: false,
      preferCanvas: true,
    });

    L.tileLayer('https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png', {
      attribution: '© OpenStreetMap contributors © CARTO',
      maxZoom: 19,
    }).addTo(map);

    L.control.zoom({ position: 'bottomright' }).addTo(map);

    map.on('click', e => {
      if (clickHandler) clickHandler(e.latlng.lat, e.latlng.lng);
    });
  }

  // ── Markers ───────────────────────────────────────────────────────

  function renderFacilities(facilities) {
    const newIds = new Set(facilities.map(f => f.id));

    Object.keys(markers).forEach(id => {
      if (!newIds.has(+id)) { map.removeLayer(markers[id]); delete markers[id]; }
    });

    facilities.forEach(fac => {
      const col = _color(fac);

      if (markers[fac.id]) {
        markers[fac.id].setStyle({ color: col, fillColor: col });
        return;
      }

      const m = L.circleMarker([fac.lat, fac.lon], {
        radius:      5,
        color:       col,
        fillColor:   col,
        fillOpacity: 0.9,
        weight:      1.5,
      });

      m.bindTooltip(fac.name || `Facility ${fac.id}`, {
        direction: 'top', offset: [0, -6],
      });

      m.on('click',     () => APP.selectFacility(fac));
      m.on('mouseover', () => m.setRadius(9));
      m.on('mouseout',  () => m.setRadius(5));

      m.addTo(map);
      markers[fac.id] = m;
    });
  }

  function updateMarker(fac) {
    if (fac.state === 'offline') {
      if (markers[fac.id]) { map.removeLayer(markers[fac.id]); delete markers[fac.id]; }
      return;
    }
    if (!markers[fac.id]) {
      /* Re-add a single marker without touching the others */
      const col = _color(fac);
      const m = L.circleMarker([fac.lat, fac.lon], {
        radius: 5, color: col, fillColor: col, fillOpacity: 0.9, weight: 1.5,
      });
      m.bindTooltip(fac.name || `Facility ${fac.id}`, { direction: 'top', offset: [0, -6] });
      m.on('click',     () => APP.selectFacility(fac));
      m.on('mouseover', () => m.setRadius(9));
      m.on('mouseout',  () => m.setRadius(5));
      m.addTo(map);
      markers[fac.id] = m;
      return;
    }
    markers[fac.id].setStyle({ color: _color(fac), fillColor: _color(fac) });
  }

  function pulseMarker(id) {
    const m = markers[id];
    if (!m) return;
    let big = true, ticks = 0;
    const iv = setInterval(() => {
      m.setRadius(big ? 11 : 5);
      big = !big;
      if (++ticks >= 6) { clearInterval(iv); m.setRadius(5); }
    }, 180);
  }

  function panTo(lat, lng) {
    map.setView([lat, lng], Math.max(map.getZoom(), 15), { animate: true });
  }

  // ── Voronoi / Coverage ────────────────────────────────────────────

  function showVoronoi(geojson) {
    if (voronoiLayer) { map.removeLayer(voronoiLayer); voronoiLayer = null; }

    const valid = {
      type: 'FeatureCollection',
      features: (geojson.features || []).filter(f => {
        const coords = f.geometry.coordinates[0];
        return new Set(coords.map(c => c.join(','))).size >= 3;
      }),
    };
    if (!valid.features.length) return;

    voronoiLayer = L.geoJSON(valid, {
      style: feat => ({
        fillColor:   feat.properties.is_underserved ? COLOR.danger : COLOR.accent,
        fillOpacity: feat.properties.is_underserved ? 0.14        : 0.08,
        color:       feat.properties.is_underserved ? COLOR.danger : COLOR.accent,
        opacity:     0.55,
        weight:      1,
        smoothFactor: 1,
      }),
      onEachFeature: (feat, layer) => {
        layer.bindTooltip(
          `${feat.properties.facility_name}<br>` +
          `Area: ${(feat.properties.area / 1e6).toFixed(2)} km²` +
          (feat.properties.is_underserved ? '<br><b style="color:#bc4749">Underserved</b>' : ''),
          { sticky: true }
        );
        layer.on('mouseover', function () {
          this.setStyle({ fillOpacity: this.feature.properties.is_underserved ? 0.30 : 0.20 });
        });
        layer.on('mouseout', function () { voronoiLayer.resetStyle(this); });
      },
    }).addTo(map);
  }

  function hideVoronoi() {
    if (voronoiLayer) { map.removeLayer(voronoiLayer); voronoiLayer = null; }
  }

  // ── Query helpers ─────────────────────────────────────────────────

  function showQueryPin(lat, lng) {
    if (queryMarker) map.removeLayer(queryMarker);
    queryMarker = L.circleMarker([lat, lng], {
      radius: 7, color: COLOR.purple, fillColor: COLOR.purple, fillOpacity: 0.6, weight: 2,
    }).addTo(map);
  }

  /* Show light-purple ghost dots at original positions before Lloyd's */
  function showOriginalDots(facilities) {
    originalDots.forEach(d => map.removeLayer(d));
    originalDots = facilities
      .filter(f => f.lat && f.lon)
      .map(fac => L.circleMarker([fac.lat, fac.lon], {
        radius: 4, color: COLOR.purple, fillColor: COLOR.purple,
        fillOpacity: 0.35, weight: 1,
      }).bindTooltip(`Original: ${fac.name || 'Facility ' + fac.id}`, { direction: 'top', offset: [0, -5] })
        .addTo(map));
  }

  function clearOriginalDots() {
    originalDots.forEach(d => map.removeLayer(d));
    originalDots = [];
  }

  function showKnnRings(facilities) {
    knnRings.forEach(r => map.removeLayer(r));
    knnRings = facilities.map((fac, i) =>
      L.circleMarker([fac.lat, fac.lon], {
        radius:  10 + i * 5,
        color:   COLOR.accent,
        fill:    false,
        opacity: Math.max(0.1, 0.65 - i * 0.08),
        weight:  1,
      }).addTo(map)
    );
  }

  // ── Lloyd's animation ─────────────────────────────────────────────

  let _animTimer = null;

  function animateMoves(steps) {
    if (_animTimer) { clearTimeout(_animTimer); _animTimer = null; }

    const moves   = steps.flatMap(s => s.facility_movements || []);
    const movedIds = new Set(moves.map(mv => mv.id));

    /* Flash all moved facilities red at once */
    movedIds.forEach(id => {
      const m = markers[id];
      if (m) m.setStyle({ color: COLOR.danger, fillColor: COLOR.danger });
    });

    /* Return to green after 1.5 s */
    _animTimer = setTimeout(() => {
      movedIds.forEach(id => {
        const m = markers[id];
        if (m) m.setStyle({ color: COLOR.online, fillColor: COLOR.online });
      });
      _animTimer = null;
    }, 1500);
  }

  return {
    init, renderFacilities, updateMarker, pulseMarker, panTo,
    showVoronoi, hideVoronoi, showQueryPin, showKnnRings, animateMoves,
    showOriginalDots, clearOriginalDots,
    setClickHandler: fn => { clickHandler = fn; },
    getMap: () => map,
  };
})();
