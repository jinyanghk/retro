import struct
import re
import json
import math
import sys

# Source BSP v19/v20 Lump Indices
LUMP_ENTITIES = 0
LUMP_PLANES = 1
LUMP_TEXDATA = 2
LUMP_VERTEXES = 3
LUMP_TEXINFO = 6
LUMP_FACES = 7
LUMP_EDGES = 12
LUMP_SURFEDGES = 13
LUMP_MODELS = 14
LUMP_DISPINFO = 26
LUMP_DISP_VERTS = 33
LUMP_TEXDATA_STRING_DATA = 43
LUMP_TEXDATA_STRING_TABLE = 44

# Surface flags to skip from solid world geometry
SURF_LIGHT = 0x0001
SURF_SKY2D = 0x0002
SURF_SKY = 0x0004
SURF_WARP = 0x0008
SURF_NODRAW = 0x0080
SURF_HINT = 0x0100
SURF_SKIP = 0x0200
SURF_TRIGGER = 0x0400

# 保持昨晚成功的跳过逻辑，不剔除 NODRAW 导致碰撞丢失
SKIP_FLAGS = SURF_SKY2D | SURF_SKY | SURF_HINT | SURF_SKIP | SURF_TRIGGER


def read_lump(data, lumps, idx):
    offset, length, version, fourcc = lumps[idx]
    return data[offset:offset + length]


def conv_pt(x, y, z):
    # Source Z-up -> RetroEngine Y-up
    return [float(x), float(z), float(-y)]


def conv_axis(vec4, scale_hint=1.0):
    nx, ny, nz, shift = vec4
    rx, ry, rz = float(nx), float(nz), float(-ny)
    length = math.sqrt(rx * rx + ry * ry + rz * rz)
    if length > 1e-6:
        scale = 1.0 / length
        rx *= scale
        ry *= scale
        rz *= scale
    else:
        scale = scale_hint
    return f"[{rx:.6f} {ry:.6f} {rz:.6f} {float(shift):.6f}] {scale:.6f}"


# 保留昨晚成功的贴图清洗逻辑
_AUTO_TEX_RE = re.compile(r'^maps/[^/]+/(.+?)_(-?\d+)_(-?\d+)_(-?\d+)$', re.IGNORECASE)
_WVT_PATCH_RE = re.compile(r'^maps/[^/]+/(.+?)_wvt_patch$', re.IGNORECASE)

def clean_material_name(mat):
    if not mat: return "dev/dev_measuregeneric01"
    m = _AUTO_TEX_RE.match(mat)
    if not m:
        m = _WVT_PATCH_RE.match(mat)
    if m:
        mat = m.group(1)
    return mat.lower()


def parse_bsp(bsp_path):
    with open(bsp_path, 'rb') as f:
        data = f.read()

    magic, version = struct.unpack_from('<4sI', data, 0)
    if magic != b'VBSP':
        raise ValueError(f"Not a valid Source VBSP file: {magic}")
    print(f"[BSP] Loaded VBSP version {version}")

    lumps = []
    for i in range(64):
        lumps.append(struct.unpack_from('<III4s', data, 8 + i * 16))

    # 1. Vertexes (12 bytes: float x, y, z)
    v_raw = read_lump(data, lumps, LUMP_VERTEXES)
    num_verts = len(v_raw) // 12
    verts = [struct.unpack_from('<fff', v_raw, i * 12) for i in range(num_verts)]

    # 2. Edges (4 bytes: uint16 v0, v1)
    e_raw = read_lump(data, lumps, LUMP_EDGES)
    num_edges = len(e_raw) // 4
    edges = [struct.unpack_from('<HH', e_raw, i * 4) for i in range(num_edges)]

    # 3. Surfedges (4 bytes: int32)
    se_raw = read_lump(data, lumps, LUMP_SURFEDGES)
    num_se = len(se_raw) // 4
    surfedges = struct.unpack_from(f'<{num_se}i', se_raw, 0)

    # 4. Planes (20 bytes: float nx, ny, nz, dist; int32 type)
    p_raw = read_lump(data, lumps, LUMP_PLANES)
    num_planes = len(p_raw) // 20
    planes = [struct.unpack_from('<fffi', p_raw, i * 20) for i in range(num_planes)]

    # 5. Textures
    str_data = read_lump(data, lumps, LUMP_TEXDATA_STRING_DATA)
    str_tbl_raw = read_lump(data, lumps, LUMP_TEXDATA_STRING_TABLE)
    num_strs = len(str_tbl_raw) // 4
    str_offsets = struct.unpack_from(f'<{num_strs}i', str_tbl_raw, 0)

    def get_tex_name(str_id):
        if str_id < 0 or str_id >= len(str_offsets):
            return "DEV/DEV_MEASUREGENERIC01"
        start = str_offsets[str_id]
        end = str_data.find(b'\x00', start)
        if end == -1:
            end = len(str_data)
        return str_data[start:end].decode('ascii', errors='ignore')

    td_raw = read_lump(data, lumps, LUMP_TEXDATA)
    num_td = len(td_raw) // 32
    texdatas = [struct.unpack_from('<fffiiiII', td_raw, i * 32) for i in range(num_td)]

    ti_raw = read_lump(data, lumps, LUMP_TEXINFO)
    num_ti = len(ti_raw) // 72
    texinfos = []
    for i in range(num_ti):
        off = i * 72
        uvec = struct.unpack_from('<ffff', ti_raw, off)
        vvec = struct.unpack_from('<ffff', ti_raw, off + 16)
        flags, td_idx = struct.unpack_from('<ii', ti_raw, off + 64)
        texinfos.append((uvec, vvec, flags, td_idx))

    # 6. Displacements
    di_raw = read_lump(data, lumps, LUMP_DISPINFO)
    num_di = len(di_raw) // 176
    dispinfos = []
    for i in range(num_di):
        off = i * 176
        sx, sy, sz = struct.unpack_from('<fff', di_raw, off)
        disp_vert_start, disp_tri_start, power = struct.unpack_from('<iii', di_raw, off + 12)
        dispinfos.append(((sx, sy, sz), disp_vert_start, power))

    dv_raw = read_lump(data, lumps, LUMP_DISP_VERTS)
    num_dv = len(dv_raw) // 20
    dispverts = [struct.unpack_from('<fffff', dv_raw, i * 20) for i in range(num_dv)]

    # 7. Faces
    f_raw = read_lump(data, lumps, LUMP_FACES)
    num_faces = len(f_raw) // 56
    faces = []
    for i in range(num_faces):
        off = i * 56
        planenum, side, onNode = struct.unpack_from('<HBB', f_raw, off)
        firstedge, numedges, texinfo_idx, dispinfo_idx = struct.unpack_from('<ihhh', f_raw, off + 4)
        faces.append((planenum, side, firstedge, numedges, texinfo_idx, dispinfo_idx))

    # 8. Models
    m_raw = read_lump(data, lumps, LUMP_MODELS)
    num_models = len(m_raw) // 48
    models = []
    for i in range(num_models):
        off = i * 48
        mn = struct.unpack_from('<fff', m_raw, off)
        mx = struct.unpack_from('<fff', m_raw, off + 12)
        org = struct.unpack_from('<fff', m_raw, off + 24)
        headnode, firstface, numfaces = struct.unpack_from('<iii', m_raw, off + 36)
        models.append({'mins': mn, 'maxs': mx, 'origin': org, 'firstface': firstface, 'numfaces': numfaces})

    # 9. Entities
    ent_text = read_lump(data, lumps, LUMP_ENTITIES).decode('latin1', errors='ignore')

    return {
        'verts': verts, 'edges': edges, 'surfedges': surfedges,
        'planes': planes, 'texinfos': texinfos, 'texdatas': texdatas,
        'get_tex_name': get_tex_name, 'dispinfos': dispinfos,
        'dispverts': dispverts, 'faces': faces, 'models': models,
        'ent_text': ent_text
    }

def parse_entities_text(ent_text):
    entities = []
    cur = {}
    for raw_line in ent_text.splitlines():
        line = raw_line.strip()
        if line == '{':
            cur = {}
        elif line == '}':
            if cur:
                entities.append(cur)
            cur = {}
        elif line.startswith('"'):
            parts = line.split('"')
            if len(parts) >= 4:
                cur[parts[1]] = parts[3]
    return entities


def build_face_poly(bsp, face_idx):
    planenum, side, firstedge, numedges, texinfo_idx, dispinfo_idx = bsp['faces'][face_idx]
    if texinfo_idx < 0 or texinfo_idx >= len(bsp['texinfos']):
        return None

    uvec, vvec, flags, td_idx = bsp['texinfos'][texinfo_idx]
    if flags & SKIP_FLAGS:
        return None

    mat_name = "DEV/DEV_MEASUREGENERIC01"
    if 0 <= td_idx < len(bsp['texdatas']):
        str_id = bsp['texdatas'][td_idx][3]
        mat_name = bsp['get_tex_name'](str_id)
    
    mat_name = clean_material_name(mat_name)
    if 'tools/' in mat_name:
        return None

    poly_verts = []
    for k in range(numedges):
        se = bsp['surfedges'][firstedge + k]
        if se >= 0:
            v_idx = bsp['edges'][se][0]
        else:
            v_idx = bsp['edges'][-se][1]
        poly_verts.append(bsp['verts'][v_idx])

    if len(poly_verts) < 3:
        return None

    nx, ny, nz, _ = bsp['planes'][planenum]
    if side != 0:
        nx, ny, nz = -nx, -ny, -nz

    return {
        'verts': poly_verts,
        'normal': (nx, ny, nz),
        'uaxis': conv_axis(uvec),
        'vaxis': conv_axis(vvec),
        'material': mat_name,
        'dispinfo_idx': dispinfo_idx
    }


def make_wedge_brush(t0, t1, t2, mat, uaxis, vaxis, thickness=16.0):
    e1 = (t1[0] - t0[0], t1[1] - t0[1], t1[2] - t0[2])
    e2 = (t2[0] - t0[0], t2[1] - t0[1], t2[2] - t0[2])
    nx = e1[1] * e2[2] - e1[2] * e2[1]
    ny = e1[2] * e2[0] - e1[0] * e2[2]
    nz = e1[0] * e2[1] - e1[1] * e2[0]
    nlen = math.sqrt(nx * nx + ny * ny + nz * nz)
    if nlen < 1e-5:
        return None
    nx, ny, nz = nx / nlen, ny / nlen, nz / nlen

    b0 = (t0[0], t0[1], t0[2] - thickness)
    b1 = (t1[0], t1[1], t1[2] - thickness)
    b2 = (t2[0], t2[1], t2[2] - thickness)

    pts = [t0, t1, t2, b0, b1, b2]
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    zs = [p[2] for p in pts]
    mn = (min(xs), min(ys), min(zs))
    mx = (max(xs), max(ys), max(zs))
    center = ((mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5)
    half = ((mx[0] - mn[0]) * 0.5, (mx[1] - mn[1]) * 0.5, (mx[2] - mn[2]) * 0.5)

    faces = [
        {
            'normal': [nx, nz, -ny],
            'material': mat,
            'vertices': [conv_pt(*t0), conv_pt(*t2), conv_pt(*t1)],
            'uaxis': uaxis,
            'vaxis': vaxis,
        },
        {
            'normal': [-nx, -nz, ny],
            'material': 'TOOLS/TOOLSNODRAW',
            'vertices': [conv_pt(*b0), conv_pt(*b1), conv_pt(*b2)],
            'uaxis': uaxis,
            'vaxis': vaxis,
        }
    ]
    return {
        'position': conv_pt(*center),
        'halfSize': [half[0], half[2], half[1]],
        'material': mat,
        'faces': faces
    }


def export_to_json(bsp, out_path):
    out = {'brushes': [], 'entities': [], 'format': 'retro_map_v2', 'nav_nodes': [], 'nav_links': []}

    world_model = bsp['models'][0]
    first_face = world_model['firstface']
    num_faces = world_model['numfaces']

    face_brushes = 0
    disp_brushes = 0

    for fi in range(first_face, first_face + num_faces):
        fp = build_face_poly(bsp, fi)
        if not fp:
            continue

        di_idx = fp['dispinfo_idx']
        if di_idx >= 0 and di_idx < len(bsp['dispinfos']) and len(fp['verts']) == 4:
            start_pos, dv_start, power = bsp['dispinfos'][di_idx]
            dim = (1 << power) + 1
            corners = fp['verts']
            start_c = min(range(4), key=lambda idx: (
                (corners[idx][0] - start_pos[0]) ** 2 +
                (corners[idx][1] - start_pos[1]) ** 2 +
                (corners[idx][2] - start_pos[2]) ** 2
            ))
            c00 = corners[start_c]
            c10 = corners[(start_c + 1) % 4]
            c11 = corners[(start_c + 2) % 4]
            c01 = corners[(start_c + 3) % 4]

            grid = []
            for r in range(dim):
                vf = r / float(dim - 1)
                row = []
                for c in range(dim):
                    uf = c / float(dim - 1)
                    bx = (1 - uf) * (1 - vf) * c00[0] + uf * (1 - vf) * c10[0] + uf * vf * c11[0] + (1 - uf) * vf * c01[0]
                    by = (1 - uf) * (1 - vf) * c00[1] + uf * (1 - vf) * c10[1] + uf * vf * c11[1] + (1 - uf) * vf * c01[1]
                    bz = (1 - uf) * (1 - vf) * c00[2] + uf * (1 - vf) * c10[2] + uf * vf * c11[2] + (1 - uf) * vf * c01[2]
                    vx, vy, vz, dist, _ = bsp['dispverts'][dv_start + r * dim + c]
                    row.append((bx + vx * dist, by + vy * dist, bz + vz * dist))
                grid.append(row)

            for r in range(dim - 1):
                for c in range(dim - 1):
                    p00, p10, p11, p01 = grid[r][c], grid[r][c + 1], grid[r + 1][c + 1], grid[r + 1][c]
                    for tri in ((p00, p10, p11), (p00, p11, p01)):
                        wb = make_wedge_brush(tri[0], tri[1], tri[2], fp['material'], fp['uaxis'], fp['vaxis'])
                        if wb:
                            wb['id'] = len(out['brushes']) + 1
                            out['brushes'].append(wb)
                            disp_brushes += 1
        else:
            nx, ny, nz = fp['normal']
            front = fp['verts']
            back = [(v[0] - nx * 8.0, v[1] - ny * 8.0, v[2] - nz * 8.0) for v in front]
            all_p = front + back
            xs = [p[0] for p in all_p]
            ys = [p[1] for p in all_p]
            zs = [p[2] for p in all_p]
            mn = (min(xs), min(ys), min(zs))
            mx = (max(xs), max(ys), max(zs))
            center = ((mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5)
            half = ((mx[0] - mn[0]) * 0.5, (mx[1] - mn[1]) * 0.5, (mx[2] - mn[2]) * 0.5)

            json_faces = [
                {
                    'normal': [nx, nz, -ny],
                    'material': fp['material'],
                    'vertices': [conv_pt(*v) for v in reversed(front)],
                    'uaxis': fp['uaxis'],
                    'vaxis': fp['vaxis'],
                },
                {
                    'normal': [-nx, -nz, ny],
                    'material': 'TOOLS/TOOLSNODRAW',
                    'vertices': [conv_pt(*v) for v in back],
                    'uaxis': fp['uaxis'],
                    'vaxis': fp['vaxis'],
                }
            ]
            out['brushes'].append({
                'id': len(out['brushes']) + 1,
                'position': conv_pt(*center),
                'halfSize': [half[0], half[2], half[1]],
                'material': fp['material'],
                'faces': json_faces,
            })
            face_brushes += 1

    # Parse Entities & Valve's Explicit Node Graph
    raw_ents = parse_entities_text(bsp['ent_text'])
    nodeid_to_idx = {}
    node_links_raw = []
    primary_spawn_added = False

    for idx, kv in enumerate(raw_ents):
        cn = kv.get('classname', '')
        if cn == 'info_node_link':
            node_links_raw.append(kv)
            continue

        if cn not in ('info_player_start', 'info_node', 'npc_zombie', 'func_door',
                      'trigger_multiple', 'trigger_once', 'light', 'light_spot',
                      'prop_physics', 'func_physbox', 'light_environment'):
            continue

        o_parts = [float(x) for x in kv.get('origin', '0 0 0').split()]
        origin = o_parts if len(o_parts) == 3 else [0.0, 0.0, 0.0]

        bbox_mins = [-16.0, 0.0, -16.0]
        bbox_maxs = [16.0, 72.0, 16.0]

        model_str = kv.get('model', '')
        if model_str.startswith('*'):
            m_idx = int(model_str[1:])
            if 0 < m_idx < len(bsp['models']):
                m = bsp['models'][m_idx]
                mn, mx = m['mins'], m['maxs']
                if origin == [0.0, 0.0, 0.0]:
                    origin = [(mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5]
                bbox_mins = [mn[0] - origin[0], mn[2] - origin[2], -(mx[1] - origin[1])]
                bbox_maxs = [mx[0] - origin[0], mx[2] - origin[2], -(mn[1] - origin[1])]

        if cn == 'info_node':
            node_idx = len(out['nav_nodes'])
            if 'nodeid' in kv:
                nodeid_to_idx[kv['nodeid']] = node_idx
            out['nav_nodes'].append(conv_pt(origin[0], origin[1], origin[2] + 8.0))

        if cn == 'info_player_start':
            if kv.get('targetname', '') != 'primary' and primary_spawn_added:
                continue
            primary_spawn_added = True

        y_lift = 8.0 if cn == 'info_node' else 0.0
        props = {k: v for k, v in kv.items()
                 if k in ('targetname', 'target', 'speed', 'lip', '_light', '_distance', 'spawnflags', 'nodeid')}

        out['entities'].append({
            'id': int(kv.get('hammerid', idx + 1)),
            'classname': cn,
            'origin': conv_pt(origin[0], origin[1], origin[2] + y_lift),
            'angles': [0.0, 0.0, 0.0],
            'bbox_mins': bbox_mins,
            'bbox_maxs': bbox_maxs,
            'properties': props,
            'customs': {},
            'connections': [],
        })

    for lk in node_links_raw:
        if lk.get('initialstate', '0') == '1':
            continue
        s_id = lk.get('StartNode', '')
        e_id = lk.get('EndNode', '')
        if s_id in nodeid_to_idx and e_id in nodeid_to_idx:
            out['nav_links'].append([nodeid_to_idx[s_id], nodeid_to_idx[e_id]])
    
    # ================= 重新加入自动分组 =================
    groups = []
    nav_entities = []
    enemy_entities = []
    light_entities = []
    spawn_entities = []

    for ent in out['entities']:
        cn = ent['classname']
        eid = ent['id']
        if cn == 'info_node':
            nav_entities.append(eid)
        elif cn == 'npc_zombie':
            enemy_entities.append(eid)
        elif cn in ('light', 'light_spot', 'light_environment'):
            light_entities.append(eid)
        elif cn == 'info_player_start':
            spawn_entities.append(eid)

    def add_group(name, entities):
        if not entities: return
        groups.append({
            "name": name,
            "visible": True,
            "brushes": [],
            "entities": entities
        })

    add_group("Nav_Nodes", nav_entities)
    add_group("Enemy_Spawns", enemy_entities)
    add_group("Lighting", light_entities)
    add_group("Player_Spawn", spawn_entities)

    out['groups'] = groups
    # ====================================================

    with open(out_path, 'w') as f:
        json.dump(out, f, indent=2)
    print(f"[BSP->JSON] Wrote {face_brushes} world faces + {disp_brushes} disp triangles, "
        f"{len(out['entities'])} entities, {len(out['nav_nodes'])} nav_nodes, {len(out['nav_links'])} explicit links, {len(out['groups'])} groups")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python bsp2json.py d1_town_05.bsp")
        sys.exit(1)
    bsp_file = sys.argv[1]
    json_file = bsp_file.rsplit('.', 1)[0] + '.json'
    parsed = parse_bsp(bsp_file)
    export_to_json(parsed, json_file)