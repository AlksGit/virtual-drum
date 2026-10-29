"""Export the mannequin's parts as printable STL.

The scene is a rendering model: its shapes are drawn to be looked at, so several of them are open surfaces
(a swept tube has no end discs, the patch film is a single sheet of triangles). A slicer needs closed
volumes, so every mesh is welded, its boundary loops are found, and it is either capped or given a real
thickness before it is written out.

Nothing here re-derives geometry. The shapes come from the page itself, driven through headless Chrome
with `?export=1`, so what is printed is exactly what was validated on screen.

    python tools/export_stl.py                # build the page if needed, then export
    python tools/export_stl.py --dom dump.html   # reuse an existing DOM dump (debugging)
"""

import argparse
import base64
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = os.path.join(ROOT, 'dist', 'mannequin_3d.html')
OUT_DIR = os.path.join(ROOT, 'App', 'docs', 'design', 'cad')
STL_DIR = os.path.join(OUT_DIR, 'stl')

# One file per printable part. The matrix housing ships twice: its thickness follows the link (5 mm with the
# ribbon, 7.5 mm once the radio and its cell move inside). Cables, the adhesive pad and the chair are not
# printed and are not exported.
EXPORTS = [
    ('matrix_module_wired',    'halo-wired',     'matrix'),
    ('matrix_module_wireless', 'halo-wireless',  'matrix'),
    ('controller_clip',        'halo-wired',     'controller'),
    ('headband',               'halo-wired',     'band'),
    ('indexed_arm',            'halo-wired',     'arm'),
    ('ear_hook',               'temple-wired',   'temple'),
    ('telescopic_arm',         'temple-wired',   'tarm'),
    ('boom_arm',               'boom',           'boom'),
    ('conformable_patch',      'patch-wired',    'patch'),
]

# Materials the scene draws as a single sheet of triangles. An open mesh wearing one of these is given a
# thickness rather than a lid, because a lid would close it into a volume of zero thickness.
SHEET_MATERIALS = {'film', 'ribbon', 'hydrogel'}
SHEET_THICKNESS = 1.5      # mm, printable wall for what is a fraction of a millimetre in reality

WELD_TOLERANCE = 1e-3      # mm

CHROME_CANDIDATES = [
    r'C:\Program Files\Google\Chrome\Application\chrome.exe',
    r'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
    r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
    r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
    '/usr/bin/google-chrome',
    '/usr/bin/chromium',
]


# ---------- driving the page ----------

def find_browser():
    for path in CHROME_CANDIDATES:
        if os.path.isfile(path):
            return path
    raise SystemExit('No Chrome or Edge found. Pass a DOM dump with --dom instead.')


def dump_dom(page, timeout_ms):
    """Run the page with ?export=1 and return the serialised DOM it leaves behind."""
    browser = find_browser()
    url = 'file:///' + page.replace('\\', '/') + '?export=1&still=1'
    with tempfile.TemporaryDirectory() as profile:
        proc = subprocess.run([
            browser, '--headless=new', '--hide-scrollbars', '--window-size=900,600',
            '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
            '--user-data-dir=' + profile,
            '--virtual-time-budget=%d' % timeout_ms, '--dump-dom', url,
        ], capture_output=True, timeout=timeout_ms / 1000 + 120)
    return proc.stdout.decode('utf-8', 'replace')


def read_payload(dom):
    m = re.search(r'<script type="application/json" id="export-payload">(.*?)</script>', dom, re.S)
    if not m:
        raise SystemExit('The page produced no export payload. Was it built from the current source?')
    return json.loads(m.group(1))


# ---------- mesh repair ----------

def weld(positions):
    """Merge coincident corners so that triangles share vertices and boundaries can be detected."""
    quantised = np.round(positions / WELD_TOLERANCE).astype(np.int64)
    _, first, inverse = np.unique(quantised, axis=0, return_index=True, return_inverse=True)
    verts = positions[first]
    tris = inverse.reshape(-1, 3)
    keep = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 2] != tris[:, 0])
    return verts, tris[keep]


def open_edges(tris):
    """Directed edges the mesh does not answer.

    A watertight shell uses every edge exactly once in each direction. Comparing the two directions by count,
    rather than merely asking whether the reverse exists at all, is what catches an edge shared by three
    faces: such a fin is not watertight even though every direction is present.
    """
    count = {}
    for a, b, c in tris:
        for u, v in ((a, b), (b, c), (c, a)):
            count[(u, v)] = count.get((u, v), 0) + 1
    out = []
    for (u, v), n in count.items():
        out.extend([(u, v)] * (n - count.get((v, u), 0)))     # only the copies left unanswered
    return out


def boundary_loops(tris):
    """Chain the open edges into closed loops. What refuses to chain is handed back rather than dropped."""
    following = {}
    for u, v in open_edges(tris):
        following.setdefault(u, []).append(v)
    loops, dangling = [], []
    for start in list(following):
        while following.get(start):
            loop, u = [start], following[start].pop()
            while u != start and following.get(u):
                loop.append(u)
                u = following[u].pop()
            if u == start and len(loop) >= 3:
                loops.append(loop)
            else:
                dangling.append(loop)   # never came back to its start: the mesh is not just open, it is broken
    return loops, dangling


def cap_loops(verts, tris, loops):
    """Close a tube end with a fan from the loop's centre: the loops we meet are circular sections."""
    verts = list(verts)
    tris = list(tris)
    for loop in loops:
        centre = len(verts)
        verts.append(np.mean([verts[i] for i in loop], axis=0))
        for i in range(len(loop)):
            a, b = loop[i], loop[(i + 1) % len(loop)]
            tris.append((centre, b, a))          # opposite winding to the surface, so the volume closes
    return np.array(verts, dtype=np.float64), np.array(tris, dtype=np.int64)


def vertex_normals(verts, tris):
    normals = np.zeros_like(verts)
    a, b, c = verts[tris[:, 0]], verts[tris[:, 1]], verts[tris[:, 2]]
    face = np.cross(b - a, c - a)                # unnormalised, so wide triangles weigh more
    for i in range(3):
        np.add.at(normals, tris[:, i], face)
    length = np.linalg.norm(normals, axis=1, keepdims=True)
    return normals / np.where(length < 1e-12, 1.0, length)


def solidify(verts, tris, loops, thickness):
    """Give an open sheet a wall: two offset copies of the surface, stitched along its boundary."""
    n = vertex_normals(verts, tris)
    half = thickness / 2.0
    top, bottom = verts + n * half, verts - n * half
    count = len(verts)
    out_verts = np.vstack([top, bottom])
    out_tris = [tuple(t) for t in tris]
    out_tris += [(count + t[0], count + t[2], count + t[1]) for t in tris]   # the underside faces the other way
    for loop in loops:
        for i in range(len(loop)):
            a, b = loop[i], loop[(i + 1) % len(loop)]
            out_tris.append((a, count + a, count + b))
            out_tris.append((a, count + b, b))
    return out_verts, np.array(out_tris, dtype=np.int64)


def close_mesh(positions, material):
    """Close one mesh. Returns its triangles and whatever could not be resolved, never a silent repair.

    Already-closed shapes pass through untouched.
    """
    verts, tris = weld(positions)
    if len(tris) == 0:
        return None, None
    loops, dangling = boundary_loops(tris)
    if not loops and not dangling:
        return verts[tris], None
    if material in SHEET_MATERIALS:
        verts, tris = solidify(verts, tris, loops, SHEET_THICKNESS)
    else:
        verts, tris = cap_loops(verts, tris, loops)
    note = '%d edge chain(s) that never close' % len(dangling) if dangling else None
    return verts[tris], note


# ---------- STL ----------

def lay_flat(triangles):
    """Sit the part on the bed: centred in X and Y, its lowest point on Z = 0."""
    flat = triangles.reshape(-1, 3)
    low, high = flat.min(axis=0), flat.max(axis=0)
    shift = np.array([(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, low[2]])
    return triangles - shift


def write_stl(path, triangles, name):
    a, b, c = triangles[:, 0], triangles[:, 1], triangles[:, 2]
    normals = np.cross(b - a, c - a)
    length = np.linalg.norm(normals, axis=1, keepdims=True)
    normals = normals / np.where(length < 1e-12, 1.0, length)
    with open(path, 'wb') as f:
        f.write(name.encode('ascii', 'replace')[:79].ljust(80, b' '))
        f.write(struct.pack('<I', len(triangles)))
        for i in range(len(triangles)):
            f.write(struct.pack('<12fH', *normals[i], *a[i], *b[i], *c[i], 0))


def is_closed(triangles):
    """A watertight shell leaves no edge unanswered."""
    _, tris = weld(triangles.reshape(-1, 3))
    return not open_edges(tris)


# ---------- main ----------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dom', help='reuse a DOM dump instead of running the browser')
    ap.add_argument('--timeout', type=int, default=45000, help='virtual time budget in ms')
    args = ap.parse_args()

    if args.dom:
        with open(args.dom, encoding='utf-8') as f:
            dom = f.read()
    else:
        if not os.path.isfile(PAGE):
            raise SystemExit('%s is missing. Run tools/build_mannequin.py first.' % PAGE)
        print('driving the page through headless Chrome...')
        dom = dump_dom(PAGE, args.timeout)

    payload = read_payload(dom)
    configs = {c['id']: c for c in payload['configs']}
    os.makedirs(STL_DIR, exist_ok=True)

    curves = {'units': payload['units'], 'reference': payload['reference'],
              'housing': payload['housing'], 'parts': {}}
    failures = []

    for name, config_id, part_id in EXPORTS:
        config = configs.get(config_id)
        if config is None:
            failures.append('%s: no configuration %s' % (name, config_id))
            continue
        group = next((p for p in config['parts'] if p['part'] == part_id), None)
        if group is None:
            failures.append('%s: configuration %s holds no part %s' % (name, config_id, part_id))
            continue

        shells, open_shells, notes = [], 0, []
        for m in group['meshes']:
            raw = np.frombuffer(base64.b64decode(m['position']), dtype=np.float32).astype(np.float64)
            closed, note = close_mesh(raw.reshape(-1, 3), m['material'])
            if closed is None:
                continue
            if note:
                notes.append(note)
            if not is_closed(closed):
                open_shells += 1
            shells.append(closed)
        if not shells:
            failures.append('%s: no geometry' % name)
            continue

        triangles = lay_flat(np.vstack(shells))
        path = os.path.join(STL_DIR, name + '.stl')
        write_stl(path, triangles, 'NoScan %s' % name)
        size = triangles.reshape(-1, 3)
        extent = size.max(axis=0) - size.min(axis=0)
        flag = '  %d OPEN SHELL(S)' % open_shells if open_shells else ''
        print('  %-26s %6d tri  %5.1f x %5.1f x %5.1f mm%s'
              % (name + '.stl', len(triangles), extent[0], extent[1], extent[2], flag))
        if open_shells:
            failures.append('%s: %d shell(s) still open' % (name, open_shells))
        for note in notes:
            failures.append('%s: %s' % (name, note))

        curves['parts'].setdefault(part_id, [c for c in config['curves'] if c['part'] == part_id])

    # A curve recorded outside any tagged group belongs to no exported part, but throwing it away would lose
    # shape information the CAD re-authoring may still want.
    curves['unassigned'] = [c for cfg in payload['configs'] for c in cfg['curves'] if c['part'] is None]

    with open(os.path.join(OUT_DIR, 'curves.json'), 'w', encoding='utf-8') as f:
        json.dump(curves, f, indent=1)
    with_curves = sum(1 for v in curves['parts'].values() if v)
    print('  curves.json                %d of %d part(s) carry guide curves, %d unassigned'
          % (with_curves, len(curves['parts']), len(curves['unassigned'])))

    if failures:
        print('\nProblems:')
        for line in failures:
            print('  - ' + line)
        return 1
    print('\n%d STL written to %s' % (len(EXPORTS), STL_DIR))
    return 0


if __name__ == '__main__':
    sys.exit(main())
