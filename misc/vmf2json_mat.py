import re, json, math
import numpy as np
from scipy.spatial import HalfspaceIntersection
from scipy.optimize import linprog

# ============ 1. 解析 VMF (基于你最初逐行读的有效逻辑) ============
def parse_vmf(path):
    with open(path, 'r', errors='ignore') as f:
        lines = f.readlines()

    solids = []
    solids_bbox = []  # 👈 新增：每块 solid 的 AABB
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
                    # 计算该 solid 的 bbox
                    all_pts = []
                    for s in current_solid_sides:
                        if 'p1' in s: all_pts.append(s['p1'])
                        if 'p2' in s: all_pts.append(s['p2'])
                        if 'p3' in s: all_pts.append(s['p3'])
                    if all_pts:
                        xs = [p[0] for p in all_pts]
                        ys = [p[1] for p in all_pts]
                        zs = [p[2] for p in all_pts]
                        solids_bbox.append(((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))))
                    else:
                        solids_bbox.append(None)
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
            
            # 👈 新增：解析 model "*N" 对应的 solid bbox
            bbox = None
            model_str = e.get('model', '')
            if model_str.startswith('*'):
                try:
                    idx = int(model_str[1:]) - 1  # VMF 的 model 编号从 1 开始
                    if 0 <= idx < len(solids_bbox):
                        bbox = solids_bbox[idx]
                except ValueError:
                    pass
            
            result_entities.append({'kv': e, 'origin': o, 'bbox': bbox})

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
    except Exception as e:
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
            json_faces.append({
                'normal': [f['normal'][0], f['normal'][2], -f['normal'][1]],
                'material': f['material'],
                'vertices': [conv(v) for v in f['vertices']],
                'uaxis': convert_axis_str(f['uaxis']),
                'vaxis': convert_axis_str(f['vaxis']),
            })

        #out['brushes'].append({
        #    'position': [float(x) for x in center],
        #    'halfSize': [float(x) for x in half],
        #    'material': default_mat,
        #    'faces': json_faces,
        #})
        # 👈 改成使用 conv 转换中心点：
        out['brushes'].append({
            'position': conv(center),   # 把中心点从 Z-up 转成 Y-up
            'halfSize': [half[0], half[2], half[1]], # 👈 注意 halfSize 的 XYZ 也要转，Y和Z互换
            'material': default_mat,
            'faces': json_faces,
        })        

    primary_spawn_added = False
    for e in entities:
        cn = e['kv'].get('classname', '')
        
        # 👈 新增：计算 Y-up 的 bbox（相对实体原点）
        def get_y_up_bbox(e):
            if not e.get('bbox'): return None
            mn, mx = e['bbox']
            o = e['origin']
            # VMF 的 mins/maxs 减去 origin，得到相对偏移
            rel_mins = [mn[0]-o[0], mn[1]-o[1], mn[2]-o[2]]
            rel_maxs = [mx[0]-o[0], mx[1]-o[1], mx[2]-o[2]]
            # Z-up -> Y-up：X 不变，Y=Z，Z=-Y
            y_up_mins = [rel_mins[0], rel_mins[2], -rel_maxs[1]]
            y_up_maxs = [rel_maxs[0], rel_maxs[2], -rel_mins[1]]
            return y_up_mins, y_up_maxs
        
        def get_default_bbox(classname):
            # 没找到 brush 时的默认值
            if classname in ('info_player_start', 'npc_zombie'):
                return [-16, 0, -16], [16, 72, 16]
            elif classname == 'func_door':
                return [-32, -64, -8], [32, 64, 8]
            elif classname == 'trigger_multiple':
                return [-32, -32, -32], [32, 32, 32]
            else:
                return [-8, -8, -8], [8, 8, 8]
        
        # 尝试从 brush 读 bbox，失败则用默认
        bbox_result = get_y_up_bbox(e)
        if bbox_result is None:
            bbox_mins, bbox_maxs = get_default_bbox(cn)
        else:
            bbox_mins, bbox_maxs = bbox_result

        if cn == 'info_player_start':
            targetname = e['kv'].get('targetname', '')
            if targetname == 'primary' or not primary_spawn_added:
                o = e['origin']
                out['entities'].append({
                    'classname': cn,
                    'origin': [o[0], o[2], -o[1]],
                    'angles': [0.0, 0.0, 0.0],
                    'bbox_mins': bbox_mins,
                    'bbox_maxs': bbox_maxs,
                    'properties': {},
                    'customs': {},
                    'connections': [],
                })
                primary_spawn_added = True
            continue 
            
        if cn in ('info_node', 'npc_zombie', 'func_door', 'trigger_multiple', 'light', 'light_spot'):
            o = e['origin']
            # 读取 angles（HL2 的 angles 是 yaw pitch roll 的 Z-up 格式）
            angles_str = e['kv'].get('angles', '0 0 0')
            try:
                a_parts = [float(x) for x in angles_str.split()]
                angles = [a_parts[0], a_parts[2], -a_parts[1]] if len(a_parts) == 3 else [0.0, 0.0, 0.0]
            except ValueError:
                angles = [0.0, 0.0, 0.0]
            
            out['entities'].append({
                'classname': cn,
                'origin': [o[0], o[2], -o[1]],
                'angles': angles,
                'bbox_mins': bbox_mins,
                'bbox_maxs': bbox_maxs,
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