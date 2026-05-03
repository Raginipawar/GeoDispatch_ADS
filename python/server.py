"""
GeoDispatch — HTTP server (stdlib only, zero pip dependencies)

Start: python python/server.py
Runs on http://localhost:8000

All computation is done by the C executable (geodispatch / geodispatch.exe).
Python's only jobs: read JSON, route the request, call C, return JSON.
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json, subprocess, os, math
from pathlib import Path

ROOT       = Path(__file__).resolve().parent.parent
DATA_FILE  = ROOT / "data" / "pune_facilities.json"
STATE_FILE = ROOT / "data" / "state.json"
EXE        = str(ROOT / ("geodispatch.exe" if os.name == "nt" else "geodispatch"))

# Equirectangular projection (same constants as data_loader)
LAT0, LON0, R = 18.5204, 73.8567, 6_371_000.0
_COS = math.cos(math.radians(LAT0))

def _xy(lat, lon):
    return math.radians(lon - LON0) * _COS * R, math.radians(lat - LAT0) * R

# ── Load facility data once at startup ────────────────────────────
_facs    = json.loads(DATA_FILE.read_text(encoding="utf-8"))
_meta    = {f["id"]: f for f in _facs}
_next_id = max(_meta) + 1 if _meta else 0

def _load_states():
    if STATE_FILE.exists():
        return {int(k): v for k, v in json.loads(STATE_FILE.read_text()).items()}
    return {f["id"]: "online" for f in _facs}

def _save_states():
    STATE_FILE.write_text(json.dumps({str(k): v for k, v in _states.items()}))

_states = _load_states()

# ── Helpers ────────────────────────────────────────────────────────

def _active():
    """Return only online facilities."""
    return [f for f in _meta.values() if _states.get(f["id"], "online") != "offline"]

def _stdin_for(facilities):
    """Format facilities as stdin for the C executable."""
    lines = [str(len(facilities))]
    for f in facilities:
        lines.append(f"{f['x']} {f['y']} {f['id']}")
    return "\n".join(lines) + "\n"

def _call_c(command, args, facilities=None):
    """Run geodispatch executable, pipe facilities via stdin, return parsed JSON."""
    facs = facilities if facilities is not None else _active()
    result = subprocess.run(
        [EXE, command] + [str(a) for a in args],
        input=_stdin_for(facs),
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        print(f"[C] stderr: {result.stderr.strip()}", flush=True)
    return json.loads(result.stdout) if result.stdout.strip() else {}

def _enrich(pt):
    """Add name/type/lat/lon/state to a {id:N} dict from C."""
    fid = pt["id"]
    m   = _meta.get(fid, {})
    return {**pt,
            "name":  m.get("name", ""),
            "type":  m.get("type", ""),
            "lat":   m.get("lat"),
            "lon":   m.get("lon"),
            "state": _states.get(fid, "online")}

# ── HTTP handler ───────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass     # silence access log

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def _json(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type",   "application/json")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n)) if n else {}

    # ── GET ──────────────────────────────────────────────────────

    def do_GET(self):
        p = self.path.split("?")[0]

        if p == "/facilities":
            facs = [{"id": fid,
                     "lat":   m["lat"], "lon":   m["lon"],
                     "name":  m.get("name", ""),
                     "type":  m.get("type", ""),
                     "state": _states.get(fid, "online")}
                    for fid, m in _meta.items()]
            self._json({"facilities": facs})

        elif p == "/facility-states":
            self._json({str(k): v for k, v in _states.items()})

        elif p == "/live-facilities":
            self._json({"ids": [fid for fid, s in _states.items() if s == "online"]})

        elif p == "/coverage-map":
            raw = _call_c("coverage", [])
            if not raw:
                self._json({"type": "FeatureCollection", "features": []}); return
            features = []
            for cell in (raw if isinstance(raw, list) else []):
                ring = cell.get("polygon", [])
                if len(ring) < 3: continue
                ring = ring + [ring[0]]     # close GeoJSON ring
                fid  = cell["site_id"]
                features.append({
                    "type": "Feature",
                    "properties": {
                        "site_id":        fid,
                        "area":           cell["area"],
                        "is_underserved": cell["is_underserved"],
                        "facility_name":  _meta.get(fid, {}).get("name", f"Facility {fid}")
                    },
                    "geometry": {"type": "Polygon", "coordinates": [ring]}
                })
            self._json({"type": "FeatureCollection", "features": features})

        else:
            self._json({"error": "not found"}, 404)

    # ── POST ─────────────────────────────────────────────────────

    def do_POST(self):
        global _next_id
        p = self.path
        b = self._body()

        if p == "/query-nearest":
            x, y = _xy(b["lat"], b["lon"])
            res  = _call_c("nearest", [x, y])
            if not res or res.get("id", -1) == -1:
                self._json({"error": "No facilities available"}); return
            self._json({"facility": _enrich(res)})

        elif p == "/query-knn":
            x, y = _xy(b["lat"], b["lon"])
            k    = b.get("k", 3)
            res  = _call_c("knn", [x, y, k])
            self._json({"k": k, "facilities": [_enrich(r) for r in (res or [])]})

        elif p == "/optimise":
            iters  = b.get("iterations", 10)
            thresh = b.get("convergence_threshold", 50.0)
            moves  = _call_c("optimise", [iters, thresh])
            if not moves:
                self._json({"steps": [], "msg": "Already converged."}); return
            # Sync new positions back into _meta
            for mv in moves:
                fid = mv["id"]
                if fid in _meta:
                    _meta[fid]["lat"] = mv.get("to_lat", _meta[fid]["lat"])
                    _meta[fid]["lon"] = mv.get("to_lon", _meta[fid]["lon"])
                    _meta[fid]["x"]   = mv["to_x"]
                    _meta[fid]["y"]   = mv["to_y"]
            steps = [{"step_num": 1, "facility_movements": [
                {"id":   mv["id"],
                 "from": {"x": mv["from_x"], "y": mv["from_y"]},
                 "to":   {"x": mv["to_x"],   "y": mv["to_y"]}}
                for mv in moves
            ]}]
            self._json({"steps": steps, "total_moves": len(moves), "iterations_run": 1})

        elif p == "/set-state":
            fid   = b["facility_id"]
            state = b["new_state"]
            if fid not in _meta:
                self._json({"ok": False, "msg": f"Unknown facility {fid}"}); return
            _states[fid] = state
            _save_states()
            self._json({"ok": True, "prev": state, "new": state, "msg": "Transition complete"})

        elif p == "/add-facility":
            x, y = _xy(b["lat"], b["lon"])
            fid  = _next_id; _next_id += 1
            fac  = {"id": fid, "lat": b["lat"], "lon": b["lon"],
                    "x": x, "y": y,
                    "name": b.get("name", ""), "type": b.get("type", "hospital")}
            _meta[fid]   = fac
            _states[fid] = "online"
            _save_states()
            self._json({"ok": True, "facility": {**fac, "state": "online"}})

        else:
            self._json({"error": "not found"}, 404)

    # ── DELETE ───────────────────────────────────────────────────

    def do_DELETE(self):
        p = self.path
        if p.startswith("/remove-facility/"):
            try:
                fid = int(p.split("/")[-1])
            except ValueError:
                self._json({"error": "bad id"}, 400); return
            if fid not in _meta:
                self._json({"error": "not found"}, 404); return
            del _meta[fid]
            _states.pop(fid, None)
            _save_states()
            self._json({"ok": True, "removed_id": fid})
        else:
            self._json({"error": "not found"}, 404)


if __name__ == "__main__":
    port = 8000
    print(f"GeoDispatch server  →  http://localhost:{port}", flush=True)
    print(f"C executable        →  {EXE}", flush=True)
    print(f"Facilities loaded   →  {len(_meta)}", flush=True)
    HTTPServer(("localhost", port), Handler).serve_forever()
