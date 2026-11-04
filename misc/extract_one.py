from valve_parsers import VPKFile
from vtf2img import Parser
import os

# 改成你家 Linux 上 HL2 的 VPK 路径
VPK_PATH = "/media/pi/backup/SteamLibrary/steamapps/common/Half-Life 2/hl2/hl2_misc_dir.vpk"
# 目标材质（不带扩展名）
MATERIAL = "materials/dev/dev_measuregeneric01"

OUT_VTF = "/tmp/dev_measuregeneric01.vtf"
OUT_PNG = "/tmp/dev_measuregeneric01.png"

# 1. 打开 VPK
vpk = VPKFile(VPK_PATH).parse_directory()
print(f"VPK loaded, {len(vpk.list_files())} files")

# 2. 检查目标 VTF 是否存在
vtf_in_vpk = MATERIAL + ".vtf"
if not vpk.find_file_path(vtf_in_vpk):
    # 有些材质可能在 materials/ 下但路径大小写不同，先搜一下
    candidates = vpk.find_files("materials/dev/*.vtf")
    print("Available in materials/dev:", candidates[:10])
    raise SystemExit(f"Not found: {vtf_in_vpk}")

# 3. 提取 VTF 到临时文件
vpk.extract_file(vtf_in_vpk, OUT_VTF)
print(f"Extracted VTF: {OUT_VTF}")

# 4. 转 PNG
parser = Parser(OUT_VTF)
img = parser.get_image()
img.save(OUT_PNG, "PNG")
print(f"Saved PNG: {OUT_PNG} ({img.width}x{img.height})")