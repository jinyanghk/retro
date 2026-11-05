import re, json, math
import numpy as np
from scipy.spatial import HalfspaceIntersection
from scipy.optimize import linprog

# ============ 1. 解析 VMF (基于你最初逐行读的有效逻辑) ============
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
        # 如果内部点算错，会直接抛出异常。我们先尝试标准求交
        hs = HalfspaceIntersection(halfspaces, interior)
        all_verts = hs.intersections
    except Exception as e:
        # 👈 如果失败，用 linprog 找可行域内的点再试一次
        try:
            A = halfspaces[:, :3]
            b = -halfspaces[:, 3]
            res = linprog(c=np.zeros(3), A_ub=A, b_ub=b, bounds=[(None, None)]*3, method='highs')
            if res.success:
                hs = HalfspaceIntersection(halfspaces, res.x)
                all_verts = hs.intersections
        except Exception as e2:
            pass # 彻底失败
    
    # 如果求交彻底失败，回退到 AABB 防止丢块
    if all_verts is None or len(all_verts) < 4:
        xs = [s['p1'][0] for s in valid_sides if 'p1' in s]
        ys = [s['p1'][1] for s in valid_sides if 'p1' in s]
        zs = [s['p1'][2] for s in valid_sides if 'p1' in s]
        if not xs: return []
        mn = (min(xs), min(ys), min(zs))
        mx = (max(xs), max(ys), max(zs))
        # 用 8 个点构造 AABB
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
            })
            
    return faces_out

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
        
        # 👈 新增：转换轴向量字符串
        def convert_axis_str(axis_str):
            # 解析类似 "[1 0 0 0] 0.25"
            if not axis_str: return "[1 0 0 0] 1"
            import re
            m = re.match(r'\[([^\]]+)\]\s*(.*)', axis_str)
            if not m: return axis_str
            vec_str = m.group(1)
            scale_str = m.group(2)
            
            parts = [float(x) for x in vec_str.split()]
            if len(parts) == 4:
                # 转换 x y z（第4个是 shift，不动）
                nx, ny, nz = parts[0], parts[2], -parts[1]
                new_vec_str = f"{nx:.6f} {ny:.6f} {nz:.6f} {parts[3]:.6f}"
                return f"[{new_vec_str}] {scale_str}"
            return axis_str

        json_faces = []
        for f in faces:
            json_faces.append({
                'normal': [f['normal'][0], f['normal'][2], -f['normal'][1]],
                'material': f['material'],
                'vertices': [conv(v) for v in f['vertices']],
                'uaxis': convert_axis_str(f['uaxis']), # 👈 修改这里
                'vaxis': convert_axis_str(f['vaxis']), # 👈 修改这里
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