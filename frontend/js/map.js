/* Leaflet map — canvas markers + native GeoJSON Voronoi layer */

const MAP = (() => {
  let map;
  let markers = {};        // id -> L.circleMarker
  let voronoiLayer = null; // L.geoJSON layer
  let clickHandler = null;
  let queryMarker  = null;
  let knnRings     = [];

  const COLOR = {
    teal:   '#00E5CC',
    coral:  '#FF5C5C',
    indigo: '#6C63FF',
    muted:  '#6A6A8A',
  };

  function _color(fac) {
    const s = fac.state || 'online';
    if (s === 'offline')    return COLOR.muted;
    if (s === 'overloaded') return COLOR.coral;
    return fac.type === 'fire_station' ? COLOR.coral : COLOR.teal;
  }

  // ── Init ─────────────────────────────────────────────────────────

  function init() {
    map = L.map('map', {
      center: [18.5204, 73.8567],
      zoom: 13,
      zoomControl: false,
      preferCanvas: true,   // all vector layers use one <canvas>
    });

    L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
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

    // Remove stale markers
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
    const m = markers[fac.id];
    if (!m) return;
    const col = _color(fac);
    m.setStyle({ color: col, fillColor: col });
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

  // ── Coverage / Voronoi — native L.geoJSON (smooth, no jitter) ────

  function showVoronoi(geojson) {
    if (voronoiLayer) { map.removeLayer(voronoiLayer); voronoiLayer = null; }

    // Filter out degenerate cells (< 3 unique coords)
    const valid = {
      type: 'FeatureCollection',
      features: (geojson.features || []).filter(f => {
        const coords = f.geometry.coordinates[0];
        const unique = new Set(coords.map(c => c.join(','))).size;
        return unique >= 3;
      }),
    };

    if (!valid.features.length) return;

    voronoiLayer = L.geoJSON(valid, {
      style: feat => ({
        fillColor:   feat.properties.is_underserved ? COLOR.coral   : COLOR.indigo,
        fillOpacity: feat.properties.is_underserved ? 0.12          : 0.07,
        color:       feat.properties.is_underserved ? COLOR.coral   : COLOR.indigo,
        opacity:     0.5,
        weight:      1,
        smoothFactor: 1,
      }),
      onEachFeature: (feat, layer) => {
        layer.bindTooltip(
          `${feat.properties.facility_name}<br>Area: ${(feat.properties.area/1e6).toFixed(2)} km²` +
          (feat.properties.is_underserved ? '<br><b style="color:#FF5C5C">Underserved</b>' : ''),
          { sticky: true }
        );
        layer.on('mouseover', function () {
          this.setStyle({ fillOpacity: this.feature.properties.is_underserved ? 0.28 : 0.18 });
        });
        layer.on('mouseout', function () {
          voronoiLayer.resetStyle(this);
        });
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
      radius: 7, color: COLOR.indigo, fillColor: COLOR.indigo, fillOpacity: 1, weight: 2,
    }).addTo(map);
  }

  function showKnnRings(facilities) {
    knnRings.forEach(r => map.removeLayer(r));
    knnRings = facilities.map((fac, i) =>
      L.circleMarker([fac.lat, fac.lon], {
        radius:  10 + i * 5,
        color:   COLOR.teal,
        fill:    false,
        opacity: Math.max(0.1, 0.65 - i * 0.08),
        weight:  1,
      }).addTo(map)
    );
  }

  // ── Optimise animation ────────────────────────────────────────────

  function animateMoves(steps) {
    const moves = steps.flatMap(s => s.facility_movements || []);
    moves.forEach((mv, i) => {
      setTimeout(() => {
        const m = markers[mv.id];
        if (!m) return;
        m.setStyle({ color: COLOR.indigo, fillColor: COLOR.indigo });
        setTimeout(() => m.setStyle({ color: COLOR.teal, fillColor: COLOR.teal }), 400);
      }, i * 80);
    });
  }

  return {
    init, renderFacilities, updateMarker, pulseMarker, panTo,
    showVoronoi, hideVoronoi, showQueryPin, showKnnRings, animateMoves,
    setClickHandler: fn => { clickHandler = fn; },
    getMap: () => map,
  };
})();
