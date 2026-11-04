from valve_parsers import VPKFile
from vtf2img import Parser
import os

VPK_PATH = "/media/pi/backup/SteamLibrary/steamapps/common/Half-Life 2/hl2/hl2_textures_dir.vpk"
OUT_BASE = "/home/pi/Desktop/hl2/materials/dev"
os.makedirs(OUT_BASE, exist_ok=True)

MATERIALS = [
    "concrete/concretewall071f",
    "concrete/concretewall041f",
    "wood/woodfloor005a",
    "stone/stonearch003a",
    "brick/brickwall034a",
    "nature/cavewall005a",
    "metal/metalwall014a_cheap",
]

print("Loading VPK...")
vpk = VPKFile(VPK_PATH).parse_directory()
print(f"VPK loaded: {len(vpk.list_files())} files")

# 先建一个所有 vtf 路径的集合（统一小写）
all_files = {f.lower(): f for f in vpk.list_files()}

for mat in MATERIALS:
    target = f"materials/{mat}.vtf".lower()
    if target not in all_files:
        print(f"[Skip] {target} not in VPK (after lowercase match)")
        continue
    
    # 拿到 VPK 里真实大小写的路径
    real_path = all_files[target]
    png_name = mat.split('/')[-1] + ".png"
    out_png = os.path.join(OUT_BASE, png_name)
    tmp_vtf = "/tmp/tmp_extract.vtf"
    
    try:
        vpk.extract_file(real_path, tmp_vtf)
        print(f"[Extract] {real_path} -> {tmp_vtf} ({os.path.getsize(tmp_vtf)} bytes)")
    except Exception as e:
        print(f"[Fail extract] {real_path}: {e}")
        continue
    
    try:
        parser = Parser(tmp_vtf)
        img = parser.get_image()
        img.save(out_png, "PNG")
        print(f"[OK] {mat} -> {out_png} ({img.width}x{img.height})")
    except Exception as e:
        print(f"[Fail convert] {mat}: {e}")