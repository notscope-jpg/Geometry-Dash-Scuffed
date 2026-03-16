import sys
import os
import subprocess
from PIL import Image

def main():
    if len(sys.argv) < 3:
        print(f"Usage: python {sys.argv[0]} <image.png> <0|1> [levelGen args...]")
        print("  0 = sprite mode (runs this.py)")
        print("  1 = level mode  (runs levelGen.py, pass xSize ySize after the flag)")
        sys.exit(1)

    png_path = sys.argv[1]
    flag = sys.argv[2]

    if not os.path.isfile(png_path):
        print(f"Error: '{png_path}' not found")
        sys.exit(1)

    # Convert to BMP
    bmp_path = os.path.splitext(png_path)[0] + ".bmp"
    img = Image.open(png_path).convert("RGB")
    img.save(bmp_path, "BMP")
    print(f"Converted '{png_path}' -> '{bmp_path}'")

    script_dir = os.path.dirname(os.path.abspath(__file__))

    if flag == "0":
        script = os.path.join(script_dir, "this.py")
        subprocess.run([sys.executable, script, bmp_path])
    elif flag == "1":
        script = os.path.join(script_dir, "levelGen.py")
        extra_args = sys.argv[3:]
        subprocess.run([sys.executable, script, bmp_path] + extra_args)
    else:
        print("Error: flag must be 0 (sprite) or 1 (level)")
        sys.exit(1)

if __name__ == "__main__":
    main()
