import os
import json
import re

def parse_vmt(vmt_path):
    """解析 Source 引擎的 KeyValues 格式"""
    with open(vmt_path, 'r', errors='ignore') as f:
        content = f.read()
    
    # 去除注释
    content = re.sub(r'//.*', '', content)
    
    # 简单解析 Key-Value（不支持复杂的嵌套，对于基础材质足够了）
    kv = {}
    # 匹配带引号的键值对
    matches = re.findall(r'"([^"]+)"\s+"([^"]*)"', content)
    for k, v in matches:
        kv[k.lower()] = v
        
    return kv

def convert_vmt_to_json(vmt_path, output_dir):
    kv = parse_vmt(vmt_path)
    
    # 我们主要关心基础贴图
    base_texture = kv.get('$basetexture', '')
    
    # 材质名称（通常是相对于 materials 的相对路径）
    rel_path = os.path.relpath(vmt_path, output_dir)
    material_name = os.path.splitext(rel_path)[0].replace('\\', '/')
    
    out_json = {
        "name": material_name,
        "shader": kv.get('$surfaceprop', 'default'),
        "baseTexture": base_texture,
        "scale": [1.0, 1.0],
    }
    
    # 如果 VMT 里有 $scale
    scale_str = kv.get('$scale', '[1 1]')
    try:
        scale_parts = scale_str.strip('[]').split()
        if len(scale_parts) == 2:
            out_json["scale"] = [float(scale_parts[0]), float(scale_parts[1])]
    except:
        pass
        
    out_path = os.path.splitext(vmt_path)[0] + '.json'
    with open(out_path, 'w') as f:
        json.dump(out_json, f, indent=2)
        
    print(f"[VMT] {vmt_path} -> {out_path}")

if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        convert_vmt_to_json(sys.argv[1], ".")