import sys
from PIL import Image

# Tile color mapping (RGB tuples)
COLOR_MAP = {
    (0, 0, 0):       0,  # black = empty
    (255, 0, 0):     1,  # red = kill triangle
    (0, 0, 255):     2,  # blue = platform block
    (255, 222, 0):   3,  # yellow = jump pad
}

def closest_tile(r, g, b):
    """Find the closest matching tile type for a pixel color."""
    best = 0
    best_dist = float('inf')
    for (cr, cg, cb), tile in COLOR_MAP.items():
        dist = (r - cr)**2 + (g - cg)**2 + (b - cb)**2
        if dist < best_dist:
            best_dist = dist
            best = tile
    return best

def main():
    if len(sys.argv) != 4:
        print(f"Usage: python {sys.argv[0]} <bmp> <xSize> <ySize>")
        print("  xSize = number of columns (LEVEL_LENGTH)")
        print("  ySize = number of rows (LEVEL_ROWS)")
        sys.exit(1)

    bmp_path = sys.argv[1]
    x_size = int(sys.argv[2])  # columns = LEVEL_LENGTH
    y_size = int(sys.argv[3])  # rows = LEVEL_ROWS

    img = Image.open(bmp_path).convert("RGB")
    w, h = img.size

    if w != x_size or h != y_size:
        print(f"Warning: image is {w}x{h}, expected {x_size}x{y_size}. Resizing.")
        img = img.resize((x_size, y_size), Image.NEAREST)

    # Build level data — row 0 = ground (bottom of image), row N = top
    # So we flip Y: image row (y_size-1) = level row 0
    level = []
    for row in range(y_size):
        img_y = y_size - 1 - row  # flip: bottom of image = row 0
        row_data = []
        for col in range(x_size):
            r, g, b = img.getpixel((col, img_y))
            row_data.append(closest_tile(r, g, b))
        level.append(row_data)

    # Output C array
    print(f"#define LEVEL_LENGTH {x_size}")
    print(f"#define LEVEL_ROWS {y_size}")
    print()
    print(f"const uint8_t levelData[LEVEL_ROWS][LEVEL_LENGTH] = {{")
    for row in range(y_size):
        label = "ground level" if row == 0 else f"{row} block(s) above ground"
        print(f"\t// Row {row} ({label})")
        print("\t{")
        line = "\t\t"
        for col in range(x_size):
            line += f"{level[row][col]}, "
            if (col + 1) % 6 == 0:
                print(line)
                line = "\t\t"
        if line.strip():
            print(line)
        print("\t},")
    print("};")

if __name__ == "__main__":
    main()
