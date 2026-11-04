import srctools.vpk
import srctools.vtf

VPK_PATH = "/media/pi/backup/source-engine/hl2/hl2/hl2_textures_dir.vpk"
vpk = srctools.vpk.VPK(VPK_PATH)

# 先索引拿到 FileInfo，再 read
file_info = vpk["materials/concrete/concretewall071f.vtf"]
data = file_info.read()
print(f"Size: {len(data)} bytes")

# 验证头部
with open("/tmp/real.vtf", "wb") as f:
    f.write(data)

raw = data[:16]
print("Magic:", raw[:4])
print("Version:", int.from_bytes(raw[4:6], 'little'), ".", int.from_bytes(raw[6:8], 'little'))
print("Header size:", int.from_bytes(raw[8:12], 'little'))
print("Width:", int.from_bytes(raw[12:14], 'little'))
print("Height:", int.from_bytes(raw[14:16], 'little'))