import srctools.vpk
import os

VPK_PATH = "/media/pi/backup/source-engine/hl2/hl2_textures_dir.vpk"
OUT_DIR = "/tmp/vpk_full"

vpk = srctools.vpk.VPK(VPK_PATH)
os.makedirs(OUT_DIR, exist_ok=True)

# 遍历所有文件并解压
for path, file_info in vpk:
    if "concretewall071f" in path:
        data = file_info.read()
        print(f"Found: {path}, {len(data)} bytes")
        out_path = os.path.join(OUT_DIR, path)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(data)
        break