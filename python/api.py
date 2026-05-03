from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
import geodispatch as gd
import state_manager
import math

app = FastAPI(title="GeoDispatch", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Models ────────────────────────────────────────────────────

class QueryRequest(BaseModel):
    lat: float
    lon: float

class KNNRequest(BaseModel):
    lat: float
    lon: float
    k: int = 3

class OptimiseRequest(BaseModel):
    iterations: int = Field(default=10, ge=1, le=200)
    convergence_threshold: float = Field(default=50.0, ge=0.1)

class SetStateRequest(BaseModel):
    facility_id: int
    new_state: str = Field(..., pattern="^(online|offline|overloaded)$")

class AddFacilityRequest(BaseModel):
    lat: float
    lon: float
    name: str = ""
    type: str = "hospital"

# ── P1 — Ragini ───────────────────────────────────────────────

@app.post("/query-nearest")
def query_nearest(body: QueryRequest):
    result = gd.kd_nearest(body.lat, body.lon)
    if result.get("id") == -1:
        return {"error": "No facilities available"}
    return {"facility": result}


@app.post("/query-knn")
def query_knn(body: KNNRequest):
    results = gd.kd_knn(body.lat, body.lon, body.k)
    return {"k": body.k, "facilities": results}

# ── P2 — Nikhil ───────────────────────────────────────────────

@app.post("/optimise")
def optimise(body: OptimiseRequest):
    # Run Lloyd's in C via ctypes bridge
    steps = gd.run_lloyds(body.iterations, body.convergence_threshold)

    if not steps:
        return {"steps": [], "msg": "Already converged — no facility needs to move."}

    # Sync gd._meta with the new positions so queries stay consistent
    LAT0, LON0, R = 18.5204, 73.8567, 6_371_000.0
    cos_lat0 = math.cos(math.radians(LAT0))
    for step in steps:
        for mv in step.get("facility_movements", []):
            fid = mv["id"]
            new_x = mv["to"]["x"]
            new_y = mv["to"]["y"]
            new_lat = math.degrees(new_y / R) + LAT0
            new_lon = math.degrees(new_x / (cos_lat0 * R)) + LON0
            if fid in gd._meta:
                gd._meta[fid]["lat"] = new_lat
                gd._meta[fid]["lon"] = new_lon

    total_moves = sum(len(s.get("facility_movements", [])) for s in steps)
    return {"steps": steps, "total_moves": total_moves, "iterations_run": len(steps)}


@app.post("/set-state")
def set_facility_state(body: SetStateRequest):
    """
    Transition a facility between online / offline / overloaded.
    Syncs with C engine via state_manager -> geodispatch bridge.
    """
    result = state_manager.set_state(body.facility_id, body.new_state)
    if not result["ok"]:
        raise HTTPException(status_code=400, detail=result["msg"])
    return result


@app.get("/facility-states")
def facility_states():
    """Return { facility_id: state } for all facilities."""
    return state_manager.get_all_states()


@app.get("/live-facilities")
def live_facilities():
    """Return list of facility IDs that are currently online."""
    return {"ids": state_manager.get_live_facilities()}


@app.get("/facilities")
def get_facilities():
    states = state_manager.get_all_states()
    facilities = []
    for fid, meta in gd._meta.items():
        facilities.append({
            "id": fid,
            "lat": meta.get("lat"),
            "lon": meta.get("lon"),
            "name": meta.get("name", ""),
            "type": meta.get("type", ""),
            "state": states.get(fid, "online"),
        })
    return {"facilities": facilities}


@app.post("/add-facility")
def add_facility(body: AddFacilityRequest):
    import math as _math
    LAT0, LON0, R = 18.5204, 73.8567, 6_371_000.0
    x = _math.radians(body.lon - LON0) * _math.cos(_math.radians(LAT0)) * R
    y = _math.radians(body.lat - LAT0) * R
    new_id = state_manager.register_new_facility(x, y, body.name, body.type)
    facility = {"id": new_id, "lat": body.lat, "lon": body.lon,
                "x": x, "y": y, "name": body.name, "type": body.type}
    gd.kd_insert(facility)
    if hasattr(gd, "voronoi_insert_site"):
        gd.voronoi_insert_site(facility)
    return {"ok": True, "facility": {**facility, "state": "online"}}


@app.delete("/remove-facility/{facility_id}")
def remove_facility(facility_id: int):
    if facility_id not in gd._meta:
        raise HTTPException(status_code=404, detail="Facility not found")
    gd.kd_delete(facility_id)
    del gd._meta[facility_id]
    state_manager.remove_facility(facility_id)
    return {"ok": True, "removed_id": facility_id}


# ── Coverage map — scipy Voronoi (C ring extraction is broken in voronoi.c) ──

@app.get('/coverage-map')
def coverage_map():
    import numpy as np
    from scipy.spatial import Voronoi

    states = state_manager.get_all_states()
    entries = [(fid, meta) for fid, meta in gd._meta.items()
               if states.get(fid, 'online') != 'offline']
    if len(entries) < 3:
        return {"type": "FeatureCollection", "features": []}

    LAT0, LON0, R = 18.5204, 73.8567, 6_371_000.0
    cos_lat0 = math.cos(math.radians(LAT0))

    def _xy(lat, lon):
        return math.radians(lon - LON0) * cos_lat0 * R, math.radians(lat - LAT0) * R

    def _latlon(x, y):
        return math.degrees(y / R) + LAT0, math.degrees(x / (cos_lat0 * R)) + LON0

    fac_ids = [fid for fid, _ in entries]
    pts = np.array([_xy(m['lat'], m['lon']) for _, m in entries])

    pad = 8000.0
    xmin, ymin = pts.min(axis=0) - pad
    xmax, ymax = pts.max(axis=0) + pad
    far = max(xmax - xmin, ymax - ymin) * 8
    corners = np.array([[xmin-far, ymin-far], [xmax+far, ymin-far],
                         [xmin-far, ymax+far], [xmax+far, ymax+far]])
    vor = Voronoi(np.vstack([pts, corners]))

    def _clip_edge(poly, ex0, ey0, ex1, ey1):
        out = []
        def inside(p): return (ex1-ex0)*(p[1]-ey0) - (ey1-ey0)*(p[0]-ex0) >= 0
        def intersect(a, b):
            dx,dy = b[0]-a[0], b[1]-a[1]; ex,ey = ex1-ex0, ey1-ey0
            t = ((ex0-a[0])*ey - (ey0-a[1])*ex) / (dx*ey - dy*ex + 1e-12)
            return [a[0]+t*dx, a[1]+t*dy]
        for i, cur in enumerate(poly):
            prev = poly[i-1]
            if inside(cur):
                if not inside(prev): out.append(intersect(prev, cur))
                out.append(cur)
            elif inside(prev): out.append(intersect(prev, cur))
        return out

    def _clip_bbox(poly):
        for ex0,ey0,ex1,ey1 in [(xmin,ymin,xmax,ymin),(xmax,ymin,xmax,ymax),
                                  (xmax,ymax,xmin,ymax),(xmin,ymax,xmin,ymin)]:
            if not poly: break
            poly = _clip_edge(poly, ex0, ey0, ex1, ey1)
        return poly

    raw = []
    for i, fid in enumerate(fac_ids):
        region = vor.regions[vor.point_region[i]]
        if -1 in region or len(region) < 3: continue
        poly = [vor.vertices[v].tolist() for v in region]
        poly = _clip_bbox(poly)
        if len(poly) < 3: continue
        n = len(poly)
        area = 0.5 * abs(sum(poly[j][0]*poly[(j+1)%n][1] - poly[(j+1)%n][0]*poly[j][1] for j in range(n)))
        ring = []
        for x, y in poly + [poly[0]]:
            lat, lon = _latlon(x, y)
            ring.append([lon, lat])
        raw.append({"fid": fid, "area": area, "ring": ring,
                    "name": entries[i][1].get("name", f"Facility {fid}")})

    if not raw: return {"type": "FeatureCollection", "features": []}

    areas = [f["area"] for f in raw]
    mean_a = sum(areas) / len(areas)
    std_a = (sum((a-mean_a)**2 for a in areas) / len(areas)) ** 0.5
    thresh = mean_a + 0.8 * std_a

    return {"type": "FeatureCollection", "features": [
        {"type": "Feature",
         "properties": {"site_id": f["fid"], "area": f["area"],
                        "is_underserved": 1 if f["area"] > thresh else 0,
                        "facility_name": f["name"]},
         "geometry": {"type": "Polygon", "coordinates": [f["ring"]]}}
        for f in raw
    ]}
