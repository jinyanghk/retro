import re, json, math
import numpy as np
from scipy.spatial import HalfspaceIntersection
from scipy.optimize import linprog
import math
import random

# ============ 0. 纯 Python 值噪声（无第三方依赖） ============
class SimpleNoise:
    def __init__(self, seed=42):
        random.seed(seed)
        self.p = list(range(256))
        random.shuffle(self.p)
        self.p = self.p * 2

    def _fade(self, t):
        return t * t * t * (t * (t * 6 - 15) + 10)

    def _lerp(self, a, b, t):
        return a + t * (b - a)

    def _grad(self, hash, x, y):
        h = hash & 3
        u = x if h < 2 else -x
        v = y if h & 1 == 0 else -y
        return u + v

    def noise(self, x, y):
        X = int(math.floor(x)) & 255
        Y = int(math.floor(y)) & 255
        x -= math.floor(x)
        y -= math.floor(y)
        u = self._fade(x)
        v = self._fade(y)
        A = self.p[X] + Y
        B = self.p[X + 1] + Y
        return self._lerp(
            self._lerp(self._grad(self.p[A], x, y), self._grad(self.p[B], x - 1, y), u),
            self._lerp(self._grad(self.p[A + 1], x, y - 1), self._grad(self.p[B + 1], x - 1, y - 1), u),
            v
        )

_noise = SimpleNoise(seed=42)

# ============ 1. 解析 VMF ============
def parse_vmf(path):
    with open(path, 'r', errors='ignore') as f:
        lines = f.readlines()

    solids = []
    entities = []
    stack = [] 
    current_solid_sides = []
    current_side = {}
    current_entity = {}
    last_line = ""

    for line in lines:
        line = line.strip()
        if not line or line.startswith('//'): continue

        if line == '{':
            block_name = last_line.strip('"')
            stack.append(block_name)
        elif line == '}':
            if not stack: continue
            block_name = stack.pop()
            
            if block_name == 'dispinfo':
                # 👈 dispinfo 块结束，说明当前 side 是位移地形
                if current_side:
                    current_side['is_displacement'] = True
            elif block_name == 'side':
                if current_side:
                    current_solid_sides.append(current_side)
                    current_side = {}
            elif block_name == 'solid':
                if len(current_solid_sides) >= 4:
                    solids.append(current_solid_sides)
                current_solid_sides = []
            elif block_name == 'entity':
                if 'classname' in current_entity:
                    entities.append(current_entity)
                current_entity = {}
        else:
            parts = line.split('"')
            if len(parts) >= 3:
                key = parts[1]
                val = parts[3]
                
                if stack and stack[-1] == 'side':
                    if key == 'plane':
                        m = re.findall(r'\(([^)]+)\)', val)
                        if len(m) == 3:
                            current_side['p1'] = tuple(float(x) for x in m[0].split())
                            current_side['p2'] = tuple(float(x) for x in m[1].split())
                            current_side['p3'] = tuple(float(x) for x in m[2].split())
                    elif key == 'material':
                        current_side['mat'] = val
                    elif key == 'uaxis':
                        current_side['uaxis'] = val
                    elif key == 'vaxis':
                        current_side['vaxis'] = val
                elif stack and stack[-1] == 'entity':
                    current_entity[key] = val
        
        last_line = line

    result_entities = []
    for e in entities:
        cn = e.get('classname', '')
        if cn in ('info_player_start', 'info_node', 'npc_zombie', 'func_door', 'trigger_multiple', 'light', 'light_spot'):
            o_str = e.get('origin', '0 0 0')
            parts = o_str.split()
            o = [float(x) for x in parts] if len(parts) == 3 else [0.0, 0.0, 0.0]
            result_entities.append({'kv': e, 'origin': o})

    return solids, result_entities

# ============ 2. 计算面多边形 ============
def brush_faces(sides):
    points = []
    for s in sides:
        if 'p1' in s and 'p2' in s and 'p3' in s:
            points.extend([s['p1'], s['p2'], s['p3']])
            
    if len(points) < 12: return []
    points = np.array(points, dtype=float)
    interior = points.mean(axis=0)
    
    halfspaces = []
    valid_sides = []
    for s in sides:
        if 'p1' not in s: continue
        p1 = np.array(s['p1'], dtype=float)
        p2 = np.array(s['p2'], dtype=float)
        p3 = np.array(s['p3'], dtype=float)
        
        normal = np.cross(p2 - p1, p3 - p1)
        length = np.linalg.norm(normal)
        if length < 1e-6: continue
        normal = normal / length
        d = -np.dot(normal, p1)
        halfspaces.append([normal[0], normal[1], normal[2], d])
        valid_sides.append(s)
    
    if len(halfspaces) < 4: return []
    halfspaces = np.array(halfspaces)
    
    all_verts = None
    try:
        hs = HalfspaceIntersection(halfspaces, interior)
        all_verts = hs.intersections
    except Exception:
        try:
            A = halfspaces[:, :3]
            b = -halfspaces[:, 3]
            res = linprog(c=np.zeros(3), A_ub=A, b_ub=b, bounds=[(None, None)]*3, method='highs')
            if res.success:
                hs = HalfspaceIntersection(halfspaces, res.x)
                all_verts = hs.intersections
        except Exception:
            pass
    
    if all_verts is None or len(all_verts) < 4:
        xs = [s['p1'][0] for s in valid_sides if 'p1' in s]
        ys = [s['p1'][1] for s in valid_sides if 'p1' in s]
        zs = [s['p1'][2] for s in valid_sides if 'p1' in s]
        if not xs: return []
        mn = (min(xs), min(ys), min(zs))
        mx = (max(xs), max(ys), max(zs))
        all_verts = np.array([
            (mn[0], mn[1], mn[2]), (mx[0], mn[1], mn[2]),
            (mx[0], mx[1], mn[2]), (mn[0], mx[1], mn[2]),
            (mn[0], mn[1], mx[2]), (mx[0], mn[1], mx[2]),
            (mx[0], mx[1], mx[2]), (mn[0], mx[1], mx[2]),
        ])

    faces_out = []
    for i, s in enumerate(valid_sides):
        n = halfspaces[i][:3]
        d = halfspaces[i][3]
        
        face_verts = []
        for v in all_verts:
            if abs(np.dot(n, v) + d) < 0.05:
                face_verts.append((float(v[0]), float(v[1]), float(v[2])))
                
        if len(face_verts) >= 3:
            center = np.mean(face_verts, axis=0)
            v1 = np.array(face_verts[0]) - center
            if np.linalg.norm(v1) < 1e-6: continue
            v1 = v1 / np.linalg.norm(v1)
            v2 = np.cross(n, v1)
            
            sorted_verts = sorted(face_verts, key=lambda p: math.atan2(
                np.dot(np.array(p) - center, v2),
                np.dot(np.array(p) - center, v1)
            ))

            mat_val = s.get('mat', 'DEV/DEV_MEASUREGENERIC01')
            if not mat_val: mat_val = 'DEV/DEV_MEASUREGENERIC01'
            
            faces_out.append({
                'material': mat_val,
                'vertices': sorted_verts,
                'normal': [float(n[0]), float(n[1]), float(n[2])],
                'uaxis': s.get('uaxis', '[1 0 0 0] 1'),
                'vaxis': s.get('vaxis', '[0 1 0 0] 1'),
                'is_displacement': s.get('is_displacement', False),  # 👈 透传
            })
            
    return faces_out

# ============ 2.5 细分 displacement ============
def subdivide_face_with_noise(face, grid_size=8, noise_scale=0.05, height_amp=30.0):
    # 👈 只处理真正的 displacement
    if not face.get('is_displacement', False):
        return [face]
    
    normal = np.array(face['normal'], dtype=float)
    verts = [np.array(v, dtype=float) for v in face['vertices']]
    
    mat_lower = face['material'].lower()
    if any(kw in mat_lower for kw in ['toolsskybox', 'toolsnodraw', 'water', 'slime', 'lava', 'trigger']):
        return [face]
    
    # 必须是四边形
    if len(verts) != 4:
        return [face]
    
    v0, v1, v2, v3 = verts
    u_vec = v1 - v0
    v_vec = v3 - v0
    
    # 生成 (N+1) x (N+1) 顶点网格
    new_verts = []
    for iy in range(grid_size + 1):
        for ix in range(grid_size + 1):
            fu = ix / grid_size
            fv = iy / grid_size
            p = v0 + u_vec * fu + v_vec * fv
            
            # 噪声
            nx = p[0] * noise_scale
            nz = p[2] * noise_scale
            h = _noise.noise(nx, nz) * height_amp
            
            # 边界系数，让边缘贴合其它 brush
            edge_factor = 4.0 * fu * (1-fu) * fv * (1-fv)
            p[1] += h * edge_factor
            
            new_verts.append(p)
    
    # 生成三角面
    result_faces = []
    for iy in range(grid_size):
        for ix in range(grid_size):
            idx00 = iy * (grid_size + 1) + ix
            idx10 = idx00 + 1
            idx01 = idx00 + (grid_size + 1)
            idx11 = idx01 + 1
            
            tri1 = [new_verts[idx00], new_verts[idx01], new_verts[idx10]]
            tri2 = [new_verts[idx10], new_verts[idx01], new_verts[idx11]]
            
            for tri in (tri1, tri2):
                e1 = tri[1] - tri[0]
                e2 = tri[2] - tri[0]
                n = np.cross(e1, e2)
                ln = np.linalg.norm(n)
                if ln > 1e-6:
                    n = n / ln
                else:
                    n = normal
                
                result_faces.append({
                    'normal': [float(n[0]), float(n[1]), float(n[2])],
                    'material': face['material'],
                    'vertices': [[float(v[0]), float(v[1]), float(v[2])] for v in tri],
                    'uaxis': face['uaxis'],
                    'vaxis': face['vaxis'],
                })
    
    return result_faces

# ============ 3. 转 JSON ============
def to_my_json(solids, entities, out_path):
    out = {'brushes': [], 'entities': [], 'format': 'retro_map_v2'}
    
    for sides in solids:
        faces = brush_faces(sides)
        if not faces: continue
        
        all_v = []
        for f in faces: all_v.extend(f['vertices'])
        xs = [v[0] for v in all_v]; ys = [v[1] for v in all_v]; zs = [v[2] for v in all_v]
        
        mn = (min(xs), min(ys), min(zs))
        mx = (max(xs), max(ys), max(zs))
        center = ((mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2)
        half = ((mx[0]-mn[0])/2, (mx[1]-mn[1])/2, (mx[2]-mn[2])/2)
        
        default_mat = faces[0]['material']
        def conv(v): return [v[0], v[2], -v[1]] # Z-up -> Y-up
        
        def convert_axis_str(axis_str):
            if not axis_str: return "[1 0 0 0] 1"
            m = re.match(r'\[([^\]]+)\]\s*(.*)', axis_str)
            if not m: return axis_str
            vec_str = m.group(1)
            scale_str = m.group(2)
            parts = [float(x) for x in vec_str.split()]
            if len(parts) == 4:
                nx, ny, nz = parts[0], parts[2], -parts[1]
                new_vec_str = f"{nx:.6f} {ny:.6f} {nz:.6f} {parts[3]:.6f}"
                return f"[{new_vec_str}] {scale_str}"
            return axis_str

        json_faces = []
        for f in faces:
            # 👈 只处理 displacement
            sub_faces = subdivide_face_with_noise(f)
            
            for sf in sub_faces:
                json_faces.append({
                    'normal': [sf['normal'][0], sf['normal'][2], -sf['normal'][1]],
                    'material': sf['material'],
                    'vertices': [conv(v) for v in sf['vertices']],
                    'uaxis': convert_axis_str(sf['uaxis']),
                    'vaxis': convert_axis_str(sf['vaxis']),
                })

        out['brushes'].append({
            'position': [float(x) for x in center],
            'halfSize': [float(x) for x in half],
            'material': default_mat,
            'faces': json_faces,
        })

    primary_spawn_added = False
    for e in entities:
        cn = e['kv'].get('classname', '')
        if cn == 'info_player_start':
            targetname = e['kv'].get('targetname', '')
            if targetname == 'primary' or not primary_spawn_added:
                o = e['origin']
                out['entities'].append({
                    'classname': cn,
                    'origin': [o[0], o[2], -o[1]],
                    'properties': {},
                    'customs': {},
                    'connections': [],
                })
                primary_spawn_added = True
            continue 
            
        if cn in ('info_node', 'npc_zombie', 'func_door', 'trigger_multiple', 'light', 'light_spot'):
            o = e['origin']
            out['entities'].append({
                'classname': cn,
                'origin': [o[0], o[2], -o[1]],
                'properties': {k: v for k, v in e['kv'].items() if k in ('targetname', 'target', 'speed', 'lip', '_light', '_distance')},
                'customs': {},
                'connections': [],
            })
    
    with open(out_path, 'w') as f:
        json.dump(out, f, indent=2)
    print(f'Wrote {len(out["brushes"])} brushes, {len(out["entities"])} entities')

if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        vmf_file = sys.argv[1]
        json_file = vmf_file.replace('.vmf', '.json')
        solids, entities = parse_vmf(vmf_file)
        to_my_json(solids, entities, json_file)