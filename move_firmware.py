import os
import shutil

def after_build(source, target, env):
    firmware_path = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
    destination_path = os.path.join(env.subst("$PROJECT_DIR"), "fwroot", "firmware.bin")

    if os.path.exists(firmware_path):
        os.makedirs(os.path.dirname(destination_path), exist_ok=True)
        if os.path.exists(destination_path):
            os.remove(destination_path)  # Remove the existing file
        shutil.move(firmware_path, destination_path)
        print(f"Moved firmware to {destination_path}")
    else:
        print("Firmware file not found!")

def generate(env):
    env.AddPostAction("buildprog", after_build)