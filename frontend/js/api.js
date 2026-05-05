const API = 'http://localhost:8000';

async function _req(method, path, body) {
  const opts = { method, headers: { 'Content-Type': 'application/json' } };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(API + path, opts);
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error(err.detail || 'Request failed');
  }
  return res.json();
}

const api = {
  facilities:    ()           => _req('GET',    '/facilities'),
  facStates:     ()           => _req('GET',    '/facility-states'),
  liveFacilities: ()          => _req('GET',    '/live-facilities'),
  nearest:       (lat, lon)   => _req('POST',   '/query-nearest',  { lat, lon }),
  knn:           (lat, lon, k)=> _req('POST',   '/query-knn',      { lat, lon, k }),
  optimise:      (iterations, convergence_threshold) =>
                               _req('POST',   '/optimise', { iterations, convergence_threshold }),
  setState:      (facility_id, new_state) =>
                               _req('POST',   '/set-state', { facility_id, new_state }),
  addFacility:   (lat, lon, name, type) =>
                               _req('POST',   '/add-facility', { lat, lon, name, type }),
  removeFacility:(id)         => _req('DELETE',  `/remove-facility/${id}`),
  coverageMap:   ()           => _req('GET',    '/coverage-map'),
  reset:         ()           => _req('POST',   '/reset'),
};
