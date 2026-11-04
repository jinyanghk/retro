from valve_parsers import VPKFile

VPK_PATH = "/media/pi/backup/SteamLibrary/steamapps/common/Half-Life 2/hl2/hl2_textures_dir.vpk"
vpk = VPKFile(VPK_PATH).parse_directory()

# 搜索包含 concretewall071f 的所有文件
matches = [f for f in vpk.list_files() if "concretewall071f" in f.lower()]
print("Matches for concretewall071f:", matches)

# 顺便看看 concrete 文件夹下到底有什么
concrete_files = [f for f in vpk.list_files() if f.lower().startswith("materials/concrete/")]
print(f"Total files in materials/concrete/: {len(concrete_files)}")
print("Sample:", concrete_files[:10])