import re, json, math
from itertools import combinations

import numpy as np
from scipy.spatial import HalfspaceIntersection
from scipy.optimize import linprog

import re, json, math
from itertools import combinations

def parse_vmf(path):
    with open(path, 'r', errors='ignore') as f:
        lines = f.readlines()

    solids = []
    entities = []

    # 记录当前深度和块名
    stack = [] 
    
    current_solid_sides = []
    current_side = {}
    current_entity = {}

    # 为了找到 { 前面的名字，我们需要看一下上一行
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
            
            if block_name == 'side':
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
            # 读取 Key-Value
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
                elif stack and stack[-1] == 'entity':
                    current_entity[key] = val
            else:
                # 不是 KV 对，那就是块名
                pass
        
        last_line = line

    # 处理实体的 origin
    result_entities = []
    for e in entities:
        cn = e.get('classname', '')
        if cn in ('info_player_start', 'info_node', 'npc_zombie', 'func_door', 'trigger_multiple'):
            o_str = e.get('origin', '0 0 0')
            parts = o_str.split()
            if len(parts) == 3:
                o = [float(x) for x in parts]
            else:
                o = [0.0, 0.0, 0.0]
            result_entities.append({'kv': e, 'origin': o})

    return solids, result_entities

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
    
    primary_spawn_added = False
    
    # 先把 primary 挑出来
    for e in entities:
        cn = e['kv'].get('classname', '')
        if cn == 'info_player_start':
            targetname = e['kv'].get('targetname', '')
            if targetname == 'primary' or not primary_spawn_added:
                o = e['origin'] or [0, 0, 0]
                o_yup = [o[0], o[2], -o[1]]
                out['entities'].append({
                    'classname': cn,
                    'origin': o_yup,
                    'properties': {},
                    'customs': {},
                    'connections': [],
                })
                primary_spawn_added = True
            continue # 其他的 info_player_start 直接忽略
            
        # 其他实体（info_node, npc_zombie 等）照常处理
        if cn in ('info_node', 'npc_zombie', 'func_door', 'trigger_multiple'):
            o = e['origin'] or [0, 0, 0]
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
    solids, entities = parse_vmf('d1_town_05.vmf')
    to_my_json(solids, entities, 'd1_town_05.json')