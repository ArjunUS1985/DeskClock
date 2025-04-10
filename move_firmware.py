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

    if os.path.exists(firmware_path):
        os.makedirs(os.path.dirname(destination_path), exist_ok=True)
        if os.path.exists(destination_path):
            print("Removing existing firmware file to ensure Git detects the change.")
            os.remove(destination_path)  # Remove the existing file
        shutil.move(firmware_path, destination_path)
        print(f"Moved firmware to {destination_path}")

        # Explicitly remove and re-add the file to Git's index
        try:
            subprocess.run(["git", "rm", "--cached", destination_path], check=True)
            print(f"Removed {destination_path} from Git index.")
        except subprocess.CalledProcessError:
            print(f"File {destination_path} was not in Git index.")

        try:
            subprocess.run(["git", "add", destination_path], check=True)
            print(f"Staged {destination_path} in Git.")
        except subprocess.CalledProcessError as e:
            print(f"Failed to stage {destination_path} in Git: {e}")

        # Force add the file to Git
        try:
            subprocess.run(["git", "add", "-f", destination_path], check=True)
            print(f"Force added {destination_path} to Git.")
        except subprocess.CalledProcessError as e:
            print(f"Failed to force add {destination_path} to Git: {e}")

    else:
        print("Firmware file not found!")

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