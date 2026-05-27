import json
import random

def generate_city_map():
    W, H = 200, 200
    
    # 0: grass, 1: road, 2: wall, 3: debris, 4: wood_floor, 5: ash
    data = [0] * (W * H)
    
    def set_tile(x, y, v):
        if 0 <= x < W and 0 <= y < H:
            data[y * W + x] = v
            
    def get_tile(x, y):
        if 0 <= x < W and 0 <= y < H:
            return data[y * W + x]
        return 0

    def draw_rect(x, y, w, h, v):
        for j in range(y, y+h):
            for i in range(x, x+w):
                set_tile(i, j, v)

    # 1. Generate Irregular Roads (Node-based)
    nodes = []
    # Corner nodes to ensure map is well connected
    nodes.extend([(20, 20), (W-20, 20), (20, H-20), (W-20, H-20), (W//2, H//2)])
    # Random nodes
    for _ in range(25):
        nodes.append((random.randint(10, W-10), random.randint(10, H-10)))
    
    # Sort nodes by X to connect them linearly and avoid complete chaos
    nodes.sort(key=lambda pt: pt[0])
    
    for i in range(len(nodes)-1):
        x1, y1 = nodes[i]
        # Connect to a nearby node rather than strictly next in X
        # Find closest node among the next few
        best_j = i+1
        best_dist = 99999
        for j in range(i+1, min(len(nodes), i+5)):
            x2, y2 = nodes[j]
            d = abs(x2-x1) + abs(y2-y1)
            if d < best_dist:
                best_dist = d
                best_j = j
                
        x2, y2 = nodes[best_j]
        width = random.randint(4, 7)
        
        start_x, end_x = min(x1, x2), max(x1, x2)
        draw_rect(start_x, y1 - width//2, end_x - start_x + 1, width, 1)
        
        start_y, end_y = min(y1, y2), max(y1, y2)
        draw_rect(x2 - width//2, start_y, width, end_y - start_y + 1, 1)

    buildings = []

    def overlap(x, y, w, h):
        for j in range(y-2, y+h+2):
            for i in range(x-2, x+w+2):
                if get_tile(i, j) != 0:
                    return True
        return False

    def bsp_rooms(x, y, w, h, depth=0):
        if depth >= 3 or w < 8 or h < 8:
            return
        
        split_horiz = random.choice([True, False])
        if w > h * 1.5: split_horiz = False
        elif h > w * 1.5: split_horiz = True

        if split_horiz:
            if h < 8: return
            split_y = random.randint(y + 3, y + h - 4)
            for i in range(x, x + w):
                set_tile(i, split_y, 2)
            # Door
            door_x = random.randint(x + 1, x + w - 2)
            set_tile(door_x, split_y, 4)
            set_tile(door_x, split_y+1, 4) # make it 2-wide for safety
            
            bsp_rooms(x, y, w, split_y - y, depth+1)
            bsp_rooms(x, split_y + 1, w, y + h - split_y - 1, depth+1)
        else:
            if w < 8: return
            split_x = random.randint(x + 3, x + w - 4)
            for j in range(y, y + h):
                set_tile(split_x, j, 2)
            # Door
            door_y = random.randint(y + 1, y + h - 2)
            set_tile(split_x, door_y, 4)
            set_tile(split_x+1, door_y, 4)
            
            bsp_rooms(x, y, split_x - x, h, depth+1)
            bsp_rooms(split_x + 1, y, x + w - split_x - 1, h, depth+1)

    def find_road(cx, cy):
        for r in range(1, 20):
            for j in range(cy-r, cy+r+1):
                if get_tile(cx-r, j) == 1: return (cx-r, j)
                if get_tile(cx+r, j) == 1: return (cx+r, j)
            for i in range(cx-r+1, cx+r):
                if get_tile(i, cy-r) == 1: return (i, cy-r)
                if get_tile(i, cy+r) == 1: return (i, cy+r)
        return None

    # 2. Place Buildings
    attempts = 0
    while len(buildings) < 45 and attempts < 2000:
        attempts += 1
        w = random.randint(12, 32)
        h = random.randint(12, 32)
        x = random.randint(5, W - w - 5)
        y = random.randint(5, H - h - 5)
        
        if not overlap(x, y, w, h):
            cx, cy = x + w//2, y + h//2
            rp = find_road(cx, cy)
            if rp:
                # Draw path to road
                rx, ry = rp
                path_w = 3
                start_x, end_x = min(cx, rx), max(cx, rx)
                draw_rect(start_x, cy - path_w//2, end_x - start_x + 1, path_w, 1)
                start_y, end_y = min(cy, ry), max(cy, ry)
                draw_rect(rx - path_w//2, start_y, path_w, end_y - start_y + 1, 1)
            
            # Place floor
            draw_rect(x, y, w, h, 4)
            # Place exterior walls
            for i in range(x, x+w):
                set_tile(i, y, 2)
                set_tile(i, y+h-1, 2)
            for j in range(y, y+h):
                set_tile(x, j, 2)
                set_tile(x+w-1, j, 2)

            # Exterior doors (1 or 2)
            doors = random.randint(1, 2)
            for _ in range(doors):
                if random.choice([True, False]): # Top or bottom
                    door_x = random.randint(x+2, x+w-3)
                    door_y = random.choice([y, y+h-1])
                    set_tile(door_x, door_y, 4)
                    set_tile(door_x+1, door_y, 4)
                else: # Left or right
                    door_x = random.choice([x, x+w-1])
                    door_y = random.randint(y+2, y+h-3)
                    set_tile(door_x, door_y, 4)
                    set_tile(door_x, door_y+1, 4)

            theme = random.randint(0, 3)
            buildings.append({
                "x": x,
                "y": y,
                "w": w,
                "h": h,
                "theme": theme
            })

    # 탈출존 배치 — 건물/벽과 겹치지 않는 위치 탐색
    def find_open_area(cx, cy, size=4):
        for r in range(0, 30):
            for dx in range(-r, r+1):
                for dy in range(-r, r+1):
                    if abs(dx) != r and abs(dy) != r:
                        continue
                    tx, ty = cx+dx, cy+dy
                    if tx < 2 or ty < 2 or tx+size >= W-2 or ty+size >= H-2:
                        continue
                    if all(get_tile(tx+i, ty+j) in (0, 1) for i in range(size) for j in range(size)):
                        return (tx, ty)
        return (cx, cy)

    ez0 = find_open_area(W//2 - 20, H//2 - 20)
    ez1 = find_open_area(W//2 + 20, H//2 + 20)

    # 탈출존 위치에 도로(1) 타일 깔기 (접근 가능하도록)
    for i in range(4):
        for j in range(4):
            set_tile(ez0[0]+i, ez0[1]+j, 1)
            set_tile(ez1[0]+i, ez1[1]+j, 1)

    extractionZones = [
      { "id": 0, "tileX": ez0[0], "tileY": ez0[1], "w": 4, "h": 4, "label": "Alpha Extract" },
      { "id": 1, "tileX": ez1[0], "tileY": ez1[1], "w": 4, "h": 4, "label": "Bravo Extract" }
    ]

    # 플레이어 스폰 위치 — 빈 타일(grass=0) 보장
    def find_spawn(cx, cy):
        for r in range(0, 20):
            for dx in range(-r, r+1):
                for dy in range(-r, r+1):
                    if abs(dx) != r and abs(dy) != r:
                        continue
                    tx, ty = cx+dx, cy+dy
                    if 0 <= tx < W and 0 <= ty < H and get_tile(tx, ty) in (0, 1):
                        return (tx, ty)
        return (cx, cy)

    sp1 = find_spawn(8, 8)
    sp2 = find_spawn(W-8, 8)
    sp3 = find_spawn(8, H-8)
    sp4 = find_spawn(W-8, H-8)

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
          { "team": 1, "x": sp1[0], "y": sp1[1] },
          { "team": 2, "x": sp2[0], "y": sp2[1] },
          { "team": 3, "x": sp3[0], "y": sp3[1] },
          { "team": 4, "x": sp4[0], "y": sp4[1] }
        ],
        "zombieSpawns": [],
        "tileTypes": {
          "0": { "name": "grass",     "solid": False, "flammable": False },
          "1": { "name": "road",      "solid": False, "flammable": False },
          "2": { "name": "wall",      "solid": True,  "flammable": False },
          "3": { "name": "debris",    "solid": False, "flammable": True  },
          "4": { "name": "wood_floor","solid": False, "flammable": True  },
          "5": { "name": "ash",       "solid": False, "flammable": False }
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
    
    print(f"Map generated successfully with {len(buildings)} buildings.")

if __name__ == '__main__':
    generate_city_map()
