import ctypes
import ctypes.util
import json
import math
import sys
from pathlib import Path

HERE      = Path(__file__).resolve().parent
REPO      = HERE.parent
DATA_FILE = REPO / "data" / "pune_facilities.json"

# Find and load the compiled shared library
for ext in ("so", "dll", "dylib"):
    lib_path = HERE / f"geodispatch.{ext}"
    if lib_path.exists():
        break
else:
    raise FileNotFoundError("geodispatch shared library not found. Run: make")

lib = ctypes.CDLL(str(lib_path))

# Need C's free() to release arrays malloc'd inside kd_knn
if sys.platform == "win32":
    libc = ctypes.CDLL("msvcrt")
else:
    libc = ctypes.CDLL(ctypes.util.find_library("c"))
libc.free.argtypes = [ctypes.c_void_p]
libc.free.restype  = None

# Mirrors point_t in kd.h
class PointT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double), ("id", ctypes.c_int)]

lib.kd_build.argtypes     = [ctypes.POINTER(PointT), ctypes.c_int]
lib.kd_build.restype      = ctypes.c_void_p
lib.kd_free.argtypes      = [ctypes.c_void_p]
lib.kd_free.restype       = None
lib.kd_nearest.argtypes   = [ctypes.c_void_p, PointT]
lib.kd_nearest.restype    = PointT
lib.kd_knn.argtypes       = [ctypes.c_void_p, PointT, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
lib.kd_knn.restype        = ctypes.POINTER(PointT)
lib.kd_delete.argtypes    = [ctypes.c_void_p, ctypes.c_int]
lib.kd_delete.restype     = None
lib.kd_insert.argtypes    = [ctypes.POINTER(ctypes.c_void_p), PointT]
lib.kd_insert.restype     = None
lib.kd_rebalance.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
lib.kd_rebalance.restype  = None
lib.kd_dead_ratio.argtypes = [ctypes.c_void_p]
lib.kd_dead_ratio.restype  = ctypes.c_double

# Equirectangular projection — same constants as data_loader.py
LAT0     = 18.5204
LON0     = 73.8567
R        = 6_371_000.0
COS_LAT0 = math.cos(math.radians(LAT0))

def _to_xy(lat, lon):
    return math.radians(lon - LON0) * COS_LAT0 * R, math.radians(lat - LAT0) * R

# Tree root and facility metadata loaded at import time
_root = ctypes.c_void_p(None)
_meta = {}  # id -> {name, type, lat, lon}

def _load():
    global _root, _facilities_arr, _facilities_n
    if not DATA_FILE.exists():
        raise FileNotFoundError(f"{DATA_FILE} not found. Run: python python/data_loader.py")

    facilities = json.loads(DATA_FILE.read_text(encoding="utf-8"))
    n = len(facilities)
    arr = (PointT * n)()

    for i, f in enumerate(facilities):
        arr[i] = PointT(f["x"], f["y"], f["id"])
        _meta[f["id"]] = {"name": f.get("name", ""), "type": f.get("type", ""),
                          "lat": f["lat"], "lon": f["lon"]}

    _facilities_arr = arr
    _facilities_n = n
    _root = ctypes.c_void_p(lib.kd_build(arr, ctypes.c_int(n)))
    try:
        init_dcel()
    except Exception as e:
        print(f"[geodispatch] warning: init_dcel failed: {e}", file=sys.stderr)

    # initialise state manager so /set-state and /facility-states work
    try:
        import state_manager
        state_manager.init_from_facilities(facilities)
    except Exception as e:
        print(f"[geodispatch] warning: state_manager init failed: {e}", file=sys.stderr)

    print(f"[geodispatch] {n} facilities loaded.", file=sys.stderr)

def _enrich(pt):
    info = _meta.get(pt.id, {})
    return {"id": pt.id, "lat": info.get("lat"), "lon": info.get("lon"),
            "name": info.get("name", ""), "type": info.get("type", "")}

def kd_nearest(lat, lon):
    x, y = _to_xy(lat, lon)
    return _enrich(lib.kd_nearest(_root, PointT(x, y, -1)))

def kd_knn(lat, lon, k):
    x, y  = _to_xy(lat, lon)
    count = ctypes.c_int(0)
    ptr   = lib.kd_knn(_root, PointT(x, y, -1), ctypes.c_int(k), ctypes.byref(count))
    if not ptr:
        return []
    results = [_enrich(ptr[i]) for i in range(count.value)]
    libc.free(ptr)
    return results

def kd_delete(point_id):
    lib.kd_delete(_root, ctypes.c_int(point_id))

def kd_insert(facility):
    if "x" in facility and "y" in facility:
        x, y = float(facility["x"]), float(facility["y"])
    else:
        x, y = _to_xy(float(facility["lat"]), float(facility["lon"]))
    lib.kd_insert(ctypes.byref(_root), PointT(x, y, int(facility["id"])))
    _meta[facility["id"]] = {"name": facility.get("name", ""), "type": facility.get("type", ""),
                              "lat": facility.get("lat"), "lon": facility.get("lon")}

def kd_rebalance():
    lib.kd_rebalance(ctypes.byref(_root))

def kd_dead_ratio():
    return float(lib.kd_dead_ratio(_root))

# --- P5 Integration ---
class FacilityMoveT(ctypes.Structure):
    _fields_ = [("site_id", ctypes.c_int), ("from_pt", PointT), ("to_pt", PointT)]

class LloydsResultT(ctypes.Structure):
    _fields_ = [("iterations_run", ctypes.c_int), ("moves", ctypes.POINTER(FacilityMoveT)), ("nmoves", ctypes.c_int)]

class CoverageCellT(ctypes.Structure):
    _fields_ = [("site_id", ctypes.c_int), ("area", ctypes.c_double), ("is_underserved", ctypes.c_int), 
                ("polygon_coords", ctypes.POINTER(ctypes.c_double)), ("num_points", ctypes.c_int)]

class CoverageMapT(ctypes.Structure):
    _fields_ = [("cells", ctypes.POINTER(CoverageCellT)), ("ncells", ctypes.c_int)]

lib.voronoi_build.argtypes = [ctypes.POINTER(PointT), ctypes.c_int]
lib.voronoi_build.restype  = ctypes.c_void_p
lib.voronoi_free.argtypes  = [ctypes.c_void_p]
lib.voronoi_free.restype   = None
lib.voronoi_incremental_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
lib.voronoi_incremental_update.restype  = None
lib.run_lloyds.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(PointT), ctypes.c_int, ctypes.c_int, ctypes.c_double]
lib.run_lloyds.restype = ctypes.POINTER(LloydsResultT)
lib.free_lloyds_result.argtypes = [ctypes.POINTER(LloydsResultT)]
lib.free_lloyds_result.restype = None
lib.get_coverage_map.argtypes = [ctypes.c_void_p]
lib.get_coverage_map.restype = ctypes.POINTER(CoverageMapT)
lib.free_coverage_map.argtypes = [ctypes.POINTER(CoverageMapT)]
lib.free_coverage_map.restype = None
lib.voronoi_insert_site.argtypes = [ctypes.c_void_p, PointT]
lib.voronoi_insert_site.restype = None

_dcel = ctypes.c_void_p(None)
_facilities_arr = None
_facilities_n = 0

def init_dcel():
    global _dcel
    if _dcel: lib.voronoi_free(_dcel)
    _dcel = ctypes.c_void_p(lib.voronoi_build(_facilities_arr, _facilities_n))

def voronoi_incremental_update(facility_id):
    if _dcel and _root:
        lib.voronoi_incremental_update(_dcel, _root, ctypes.c_int(facility_id))

def voronoi_insert_site(facility):
    if not _dcel: return
    x, y = _to_xy(float(facility.get("lat", 0)), float(facility.get("lon", 0)))
    if "x" in facility: x = float(facility["x"])
    if "y" in facility: y = float(facility["y"])
    lib.voronoi_insert_site(_dcel, PointT(x, y, int(facility["id"])))

def run_lloyds(iterations, convergence_threshold):
    global _root, _dcel
    if not _dcel or not _root or not _facilities_arr: return []
    res_ptr = lib.run_lloyds(ctypes.byref(_dcel), ctypes.byref(_root), _facilities_arr, _facilities_n, ctypes.c_int(iterations), ctypes.c_double(convergence_threshold))
    if not res_ptr: return []
    
    res = res_ptr.contents
    steps = []
    # group moves by iterations_run (simulated flat array in C snippet)
    # Actually the C snippet doesn't separate iterations in the output structure easily, 
    # it just outputs sequentially. We'll dump all moves into the first step for simplicity 
    # since we want to return what FastApi expects.
    moves = []
    for i in range(res.nmoves):
        m = res.moves[i]
        moves.append({
            "id": m.site_id,
            "from": {"x": m.from_pt.x, "y": m.from_pt.y},
            "to": {"x": m.to_pt.x, "y": m.to_pt.y}
        })
    steps.append({"step_num": 1, "facility_movements": moves})
    
    lib.free_lloyds_result(res_ptr)
    return steps

def get_coverage_map():
    if not _dcel: return []
    map_ptr = lib.get_coverage_map(_dcel)
    if not map_ptr: return []
    
    c_map = map_ptr.contents
    cells = []
    for i in range(c_map.ncells):
        c = c_map.cells[i]
        coords = []
        for j in range(c.num_points):
            coords.append([c.polygon_coords[j*2], c.polygon_coords[j*2+1]])
        info = _meta.get(c.site_id, {})
        cells.append({
            "site_id": c.site_id,
            "area": c.area,
            "is_underserved": c.is_underserved,
            "polygon": coords,
            "facility_name": info.get("name", f"Facility {c.site_id}")
        })
        
    lib.free_coverage_map(map_ptr)
    return cells

_load()
