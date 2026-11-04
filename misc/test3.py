from valve_parsers import VPKFile
VPK_PATH = "/media/pi/backup/SteamLibrary/steamapps/common/Half-Life 2/hl2/hl2_textures_dir.vpk"
vpk = VPKFile(VPK_PATH).parse_directory()
vpk.extract_file("materials/concrete/concretewall071f.vtf", "/tmp/check.vtf")

with open("/tmp/check.vtf", "rb") as f:
    header = f.read(16)
print("Magic:", header[:4])   # 应该是 b'VTF\0'
print("Version:", int.from_bytes(header[4:8], 'little'))
print("Header size:", int.from_bytes(header[8:12], 'little'))