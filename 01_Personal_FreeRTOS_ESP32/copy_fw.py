# Sau khi build: chép firmware.bin/.elf về <project>/.pio/wokwi/ để Wokwi và người dùng dễ tìm
import os, shutil
Import("env")  # noqa: F821


def _copy(source, target, env):
    dst = os.path.join(env.subst("$PROJECT_DIR"), ".pio", "wokwi")
    os.makedirs(dst, exist_ok=True)
    for ext in (".bin", ".elf"):
        src = env.subst("$BUILD_DIR/${PROGNAME}") + ext
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(dst, "firmware" + ext))
    print("Da chep firmware ->", dst)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy)  # noqa: F821
