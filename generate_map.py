import json
import random

def generate_city_map():
    W, H = 200, 200
    
    # 0: grass, 1: road, 2: wall, 3: debris, 4: wood_floor, 5: ash
    # Initialize all with grass
    data = [0] * (W * H)
    
    def set_tile(x, y, v):
        if 0 <= x < W and 0 <= y < H:
            data[y * W + x] = v
            
    def get_tile(x, y):
        if 0 <= x < W and 0 <= y < H:
            return data[y * W + x]
        return 0

    # Create roads
    road_width = 6
    block_size = 32
    for y in range(0, H, block_size):
        for x in range(W):
            for i in range(road_width):
                set_tile(x, y + i, 1) # Road
    
    for x in range(0, W, block_size):
        for y in range(H):
            for i in range(road_width):
                set_tile(x + i, y, 1) # Road

    buildings = []

    # Create buildings in the blocks between roads
    for by in range(0, H, block_size):
        for bx in range(0, W, block_size):
            # Regional theme based on coordinates
            theme = 0
            if bx >= W//2 and by < H//2: theme = 1
            elif bx < W//2 and by >= H//2: theme = 2
            elif bx >= W//2 and by >= H//2: theme = 3

            # Inside the block
            cx, cy = bx + block_size // 2, by + block_size // 2
            bw, bh = random.randint(12, 18), random.randint(12, 18)
            
            hx, hy = bw // 2, bh // 2
            for dy in range(-hy, hy + 1):
                for dx in range(-hx, hx + 1):
                    wx, wy = cx + dx, cy + dy
                    if dx == -hx or dx == hx or dy == -hy or dy == hy:
                        set_tile(wx, wy, 2) # Wall
                    else:
                        set_tile(wx, wy, 4) # Wood floor
            
            # Make doors (1-2)
            doors = random.randint(1, 2)
            for _ in range(doors):
                if random.choice([True, False]): # Vertical wall door
                    set_tile(cx + random.choice([-hx, hx]), cy + random.randint(-hy+1, hy-1), 4)
                else: # Horizontal wall door
                    set_tile(cx + random.randint(-hx+1, hx-1), cy + random.choice([-hy, hy]), 4)
                    
            # Sprinkle some debris on the road
            if random.random() < 0.5:
                set_tile(cx + random.randint(-4, 4), by + random.randint(0, 3), 3)

            buildings.append({
                "x": cx - hx,
                "y": cy - hy,
                "w": bw + 1,
                "h": bh + 1,
                "theme": theme
            })

    # Extraction zones in the center (farming areas)
    extractionZones = [
      { "id": 0, "tileX": W//2, "tileY": H//2 - 10, "w": 4, "h": 4, "label": "Center North" },
      { "id": 1, "tileX": W//2, "tileY": H//2 + 10, "w": 4, "h": 4, "label": "Center South" }
    ]

    map_dict = {
      "map": {
        "name": "City Ruins",
        "tileSize": 32,
        "width": W,
        "height": H,
        "tileset": "assets/sprites/tileset.png",
        "extractionZones": extractionZones,
        "buildings": buildings,
        "playerSpawns": [
          { "team": 1, "x": road_width//2, "y": road_width//2 },
          { "team": 2, "x": W - road_width//2, "y": road_width//2 },
          { "team": 3, "x": road_width//2, "y": H - road_width//2 },
          { "team": 4, "x": W - road_width//2, "y": H - road_width//2 }
        ],
        "zombieSpawns": [
        ],
        "tileTypes": {
          "0": { "name": "grass",    "solid": False, "flammable": False },
          "1": { "name": "road",     "solid": False, "flammable": False },
          "2": { "name": "wall",     "solid": True,  "flammable": False },
          "3": { "name": "debris",   "solid": False, "flammable": True  },
          "4": { "name": "wood_floor","solid": False, "flammable": True  },
          "5": { "name": "ash",      "solid": False, "flammable": False }
        },
        "layers": [
          {
            "name": "base",
            "comment": "City layout",
            "data": data
          }
        ]
      }
    }

    with open("/Users/gimseongjun/Desktop/DeadZone/data/map.json", "w") as f:
        json.dump(map_dict, f, separators=(',', ':'))

if __name__ == '__main__':
    generate_city_map()
