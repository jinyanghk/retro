import re, json, math
from itertools import combinations

import numpy as np
from scipy.spatial import HalfspaceIntersection
from scipy.optimize import linprog

# ============ 1. 解析 Quake .map ============
import re, json, math
from itertools import combinations

import re, json, math
from itertools import combinations

def parse_quake_map(path):
    with open(path, 'r', errors='ignore') as f:
        text = f.read()
    text = re.sub(r'//.*', '', text)
    
    solids = []
    entities = []
    
    pos = 0
    while True:
        start = text.find('{', pos)
        if start < 0: break
        depth = 0
        end = start
        for i in range(start, len(text)):
            if text[i] == '{': depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    end = i
                    break
        block = text[start+1:end]
        pos = end + 1
        
        if 'classname' in block:
            kv = dict(re.findall(r'"(\w+)"\s+"([^"]*)"', block))
            origin = None
            m = re.search(r'"origin"\s+"([\d\.\-\s]+)"', block)
            if m:
                origin = [float(x) for x in m.group(1).split()]
            
            if kv.get('classname') == 'worldspawn':
                # 👈 内层切割：从 block 里找所有 { ... }，每个是 solid
                inner_pos = 0
                while True:
                    s2 = block.find('{', inner_pos)
                    if s2 < 0: break
                    d2 = 0
                    e2 = s2
                    for j in range(s2, len(block)):
                        if block[j] == '{': d2 += 1
                        elif block[j] == '}':
                            d2 -= 1
                            if d2 == 0:
                                e2 = j
                                break
                    inner = block[s2+1:e2]
                    inner_pos = e2 + 1
                    
                    sides = []
                    for line in inner.split('\n'):
                        line = line.strip()
                        if not line.startswith('('): continue
                        m2 = re.match(
                            r'\(\s*([\d\.\-]+)\s+([\d\.\-]+)\s+([\d\.\-]+)\s*\)\s*'
                            r'\(\s*([\d\.\-]+)\s+([\d\.\-]+)\s+([\d\.\-]+)\s*\)\s*'
                            r'\(\s*([\d\.\-]+)\s+([\d\.\-]+)\s+([\d\.\-]+)\s*\)\s*'
                            r'(\S+)', line)
                        if m2:
                            sides.append({
                                'p1': (float(m2.group(1)), float(m2.group(2)), float(m2.group(3))),
                                'p2': (float(m2.group(4)), float(m2.group(5)), float(m2.group(6))),
                                'p3': (float(m2.group(7)), float(m2.group(8)), float(m2.group(9))),
                                'mat': m2.group(10),
                            })
                    if sides:
                        solids.append(sides)
            else:
                entities.append({'kv': kv, 'origin': origin})
        else:
            # 顶层 solid（没有 classname）
            sides = []
            for line in block.split('\n'):
                line = line.strip()
                if not line.startswith('('): continue
                m2 = re.match(
                    r'\(\s*([\d\.\-]+)\s+([\d\.\-]+)\s+([\d\.\-]+)\s*\)\s*'
                    r'\(\s*([\d\.\-]+)\s+([\d\.\-]+)\s+([\d\.\-]+)\s*\)\s*'
                    r'\(\s*([\d\.\-]+)\s+([\d\.\-]+)\s+([\d\.\-]+)\s*\)\s*'
                    r'(\S+)', line)
                if m2:
                    sides.append({
                        'p1': (float(m2.group(1)), float(m2.group(2)), float(m2.group(3))),
                        'p2': (float(m2.group(4)), float(m2.group(5)), float(m2.group(6))),
                        'p3': (float(m2.group(7)), float(m2.group(8)), float(m2.group(9))),
                        'mat': m2.group(10),
                    })
            if sides:
                solids.append(sides)
    
    return solids, entities

# ============ 2. 平面求交 → 顶点 ============
def plane_from_3points(p1, p2, p3):
    v1 = (p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2])
    v2 = (p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2])
    n = (v1[1]*v2[2] - v1[2]*v2[1],
         v1[2]*v2[0] - v1[0]*v2[2],
         v1[0]*v2[1] - v1[1]*v2[0])
    length = math.sqrt(n[0]**2 + n[1]**2 + n[2]**2)
    if length < 1e-6: return None
    # 👈 Quake 的三个点是"从外侧看顺时针"，叉积朝外，取反才是朝内
    n = (-n[0]/length, -n[1]/length, -n[2]/length)
    d = -(n[0]*p1[0] + n[1]*p1[1] + n[2]*p1[2])
    return (n, d)

def intersect_3planes(pl1, pl2, pl3):
    # 解 3x3 线性方程组
    (n1, d1), (n2, d2), (n3, d3) = pl1, pl2, pl3
    det = (n1[0]*(n2[1]*n3[2] - n2[2]*n3[1])
         - n1[1]*(n2[0]*n3[2] - n2[2]*n3[0])
         + n1[2]*(n2[0]*n3[1] - n2[1]*n3[0]))
    if abs(det) < 1e-6: return None
    x = (-d1*(n2[1]*n3[2] - n2[2]*n3[1])
       + n1[1]*(d2*n3[2] - n2[2]*d3)
       - n1[2]*(d2*n3[1] - n2[1]*d3)) / det
    y = (n1[0]*(d2*n3[2] - n2[2]*d3)
       + d1*(n2[0]*n3[2] - n2[2]*n3[0])
       - n1[2]*(n2[0]*d3 - d2*n3[0])) / det
    z = (n1[0]*(n2[1]*d3 - d2*n3[1])
       - n1[1]*(n2[0]*d3 - d2*n3[0])
       + d1*(n2[0]*n3[1] - n2[1]*n3[0])) / det
    return (x, y, z)

def brush_vertices(sides):
    # 收集所有点，用于找内部点
    points = []
    for s in sides:
        points.extend([s['p1'], s['p2'], s['p3']])
    points = np.array(points, dtype=float)
    
    # 构造半空间：n·x + d <= 0
    halfspaces = []
    for s in sides:
        p1 = np.array(s['p1'], dtype=float)
        p2 = np.array(s['p2'], dtype=float)
        p3 = np.array(s['p3'], dtype=float)
        normal = np.cross(p2 - p1, p3 - p1)
        length = np.linalg.norm(normal)
        if length < 1e-6:
            continue
        normal = normal / length
        d = -np.dot(normal, p1)
        halfspaces.append([normal[0], normal[1], normal[2], d])
    
    if len(halfspaces) < 4:
        return []
    
    halfspaces = np.array(halfspaces)
    
    # 尝试用 points 的平均作为内部点
    interior = points.mean(axis=0)
    
    try:
        hs = HalfspaceIntersection(halfspaces, interior)
        verts = [tuple(v) for v in hs.intersections]
        if len(verts) >= 4:
            return verts
    except Exception:
        pass
    
    # fallback：用 linprog 找可行点
    try:
        A = halfspaces[:, :3]
        b = -halfspaces[:, 3]
        res = linprog(c=np.zeros(3), A_ub=A, b_ub=b, bounds=[(None, None)]*3)
        if res.success:
            hs = HalfspaceIntersection(halfspaces, res.x)
            verts = [tuple(v) for v in hs.intersections]
            if len(verts) >= 4:
                return verts
    except Exception:
        pass
    
    # 最终 fallback：AABB（保证至少能跑）
    mn = points.min(axis=0)
    mx = points.max(axis=0)
    return [
        (mn[0], mn[1], mn[2]), (mx[0], mn[1], mx[2]),
        (mx[0], mx[1], mn[2]), (mn[0], mx[1], mn[2]),
        (mn[0], mn[1], mx[2]), (mx[0], mn[1], mx[2]),
        (mx[0], mx[1], mx[2]), (mn[0], mx[1], mx[2]),
    ]

# ============ 3. 转 JSON ============
def to_my_json(solids, entities, out_path):
    out = {'brushes': [], 'entities': [], 'format': 'retro_map_v1'}
    
    for sides in solids:
        verts_q = brush_vertices(sides)  # Quake 的 Z-up
        #print(f"brush: {len(sides)} sides, {len(verts_q)} verts")   # 👈 加这行
        if len(verts_q) < 4: continue
        
        # Z-up → Y-up
        verts = [(x, z, -y) for (x, y, z) in verts_q]
        
        # 算 AABB（你的 BrushBuilder 如果只吃 AABB，就先框成盒子）
        xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
        mn = (min(xs), min(ys), min(zs))
        mx = (max(xs), max(ys), max(zs))
        center = ((mn[0]+mx[0])/2, (mn[1]+mx[1])/2, (mn[2]+mx[2])/2)
        half = ((mx[0]-mn[0])/2, (mx[1]-mn[1])/2, (mx[2]-mn[2])/2)
        
        # 材质统一替换
        mat = 'DEV/DEV_MEASUREGENERIC01'
        
        out['brushes'].append({
            'position': list(center),
            'halfSize': list(half),
            'material': mat,
            'vertices': [list(v) for v in verts],
            'faces': [],  # TODO: 非 AABB 的 faces 需要三角化
        })
    
    for e in entities:
        cn = e['kv'].get('classname', '')
        # 只转认识的
        if cn in ('info_player_start', 'info_node', 'npc_zombie', 'func_door', 'trigger_multiple'):
            o = e['origin'] or [0, 0, 0]
            # Z-up → Y-up
            o_yup = [o[0], o[2], -o[1]]
            out['entities'].append({
                'classname': cn,
                'origin': o_yup,
                'properties': {k: v for k, v in e['kv'].items() if k in ('targetname', 'target', 'speed', 'lip')},
                'customs': {},
                'connections': [],
            })
    
    with open(out_path, 'w') as f:
        json.dump(out, f, indent=2)
    print(f'Wrote {len(out["brushes"])} brushes, {len(out["entities"])} entities')

if __name__ == '__main__':
    solids, entities = parse_quake_map('start.map')
    to_my_json(solids, entities, 'start.json')