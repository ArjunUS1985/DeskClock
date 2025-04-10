import os
import shutil
import subprocess
import time

def after_build(source, target, env):
    print("Executing after_build script...")
    firmware_path = os.path.join(env.subst(".pio/build/${PIOENV}"), "firmware.bin")
    destination_path = os.path.join(env.subst("$PROJECT_DIR"), "fwroot", "firmware.bin")

    print(f"Firmware path: {firmware_path}")
    print(f"Destination path: {destination_path}")

    # Delete the old firmware file if it exists
    if os.path.exists(destination_path):
        os.remove(destination_path)
        print(f"Deleted old firmware from {destination_path}.")

        # Check in the delete to Git
        try:
            subprocess.run(["git", "add", destination_path], check=True)
            subprocess.run(["git", "commit", "-m", "deleted old fw"], check=True)
            print("Checked in deletion of old firmware with comment 'deleted old fw'.")
        except subprocess.CalledProcessError as e:
            print(f"Failed to check in deletion of old firmware: {e}")

    # Copy the new firmware file
    if os.path.exists(firmware_path):
        shutil.copy(firmware_path, destination_path)
        print(f"Copied new firmware to {destination_path}.")

        # Check in the new firmware to Git
        try:
            subprocess.run(["git", "add", destination_path], check=True)
            subprocess.run(["git", "commit", "-m", "New fw upload"], check=True)
            print("Checked in new firmware with comment 'New fw upload'.")
        except subprocess.CalledProcessError as e:
            print(f"Failed to check in new firmware: {e}")
    else:
        print("New firmware file not found!")

def generate(env):
    print("Registering after_build script...")
    env.AddPostAction("buildprog", after_build)

if __name__ == "__main__":
    class MockEnv:
        def subst(self, var):
            if var == "$BUILD_DIR":
                return "./.pio/build/esp12e_ota"
            if var == "$PROJECT_DIR":
                return os.getcwd()
            if var == ".pio/build/${PIOENV}":
                return "./.pio/build/esp12e_ota"

    mock_env = MockEnv()
    after_build(None, None, mock_env)