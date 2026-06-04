import json
import random
from pathlib import Path


def generate_city_map():
    width, height = 200, 200
    grass, road, wall, debris, wood_floor, ash = 0, 1, 2, 3, 4, 5
    main_roads_x = [5, 65, 100, 135, 195]
    main_roads_y = [5, 65, 100, 135, 195]
    main_w = 5
    secondary_w = 3

    districts = [
        {"id": 0, "key": "residential", "label": "주거 구역", "theme": 0, "x": 0, "y": 0, "w": 100, "h": 100},
        {"id": 1, "key": "commercial", "label": "상업 구역", "theme": 1, "x": 100, "y": 0, "w": 100, "h": 100},
        {"id": 2, "key": "industrial", "label": "공업 구역", "theme": 2, "x": 100, "y": 100, "w": 100, "h": 100},
        {"id": 3, "key": "military", "label": "군사 구역", "theme": 3, "x": 0, "y": 100, "w": 100, "h": 100},
    ]

    data = [grass] * (width * height)

    def in_bounds(x, y):
        return 0 <= x < width and 0 <= y < height

    def get_tile(x, y):
        return data[y * width + x] if in_bounds(x, y) else grass

    def set_tile(x, y, value):
        if in_bounds(x, y):
            data[y * width + x] = value

    def rect(x, y, w, h, value):
        for tile_y in range(y, y + h):
            for tile_x in range(x, x + w):
                set_tile(tile_x, tile_y, value)

    random.seed(42)

    for road_x in main_roads_x:
        rect(road_x, 0, main_w, height, road)
    for road_y in main_roads_y:
        rect(0, road_y, width, main_w, road)

    def mid_blocks(roads):
        return [(roads[i] + main_w + roads[i + 1]) // 2 for i in range(len(roads) - 1)]

    secondary_x = mid_blocks(main_roads_x)
    secondary_y = mid_blocks(main_roads_y)

    for road_x in secondary_x:
        rect(road_x, 0, secondary_w, height, road)
    for road_y in secondary_y:
        rect(0, road_y, width, secondary_w, road)

    def zone_theme(cx, cy):
        if cx < 100 and cy < 100:
            return 0
        if cx >= 100 and cy < 100:
            return 1
        if cx >= 100 and cy >= 100:
            return 2
        return 3

    def block_bounds():
        all_x = sorted(set(main_roads_x + secondary_x + [0, width]))
        all_y = sorted(set(main_roads_y + secondary_y + [0, height]))
        blocks = []

        for ix in range(len(all_x) - 1):
            for iy in range(len(all_y) - 1):
                x1, x2 = all_x[ix], all_x[ix + 1]
                y1, y2 = all_y[iy], all_y[iy + 1]
                bx, by = x1, y1
                bw, bh = x2 - x1, y2 - y1

                if get_tile(bx, by + bh // 2) == road:
                    bx += main_w if bx in main_roads_x else secondary_w
                if get_tile(bx + bw // 2, by) == road:
                    by += main_w if by in main_roads_y else secondary_w

                bw = x2 - bx
                bh = y2 - by
                if bw > 8 and bh > 8:
                    blocks.append((bx, by, bw, bh))

        return blocks

    def can_place(x, y, w, h):
        for tile_y in range(y - 1, y + h + 1):
            for tile_x in range(x - 1, x + w + 1):
                if get_tile(tile_x, tile_y) in (road, wall, wood_floor):
                    return False
        return True

    def is_road_tile(x, y):
        return in_bounds(x, y) and get_tile(x, y) == road

    def place_building(x, y, w, h, theme):
        rect(x, y, w, h, wood_floor)

        for tile_x in range(x, x + w):
            set_tile(tile_x, y, wall)
            set_tile(tile_x, y + h - 1, wall)
        for tile_y in range(y, y + h):
            set_tile(x, tile_y, wall)
            set_tile(x + w - 1, tile_y, wall)

        doors_added = []

        def add_horizontal_door(wall_y):
            check_y = wall_y - 1 if wall_y == y else wall_y + 1
            if not any(is_road_tile(tile_x, check_y) for tile_x in range(x + 1, x + w - 1)):
                return

            door_x = (x + x + w) // 2
            if door_x + 1 >= x + w - 1:
                door_x -= 1
            set_tile(door_x, wall_y, wood_floor)
            set_tile(door_x + 1, wall_y, wood_floor)
            doors_added.extend([(door_x, wall_y), (door_x + 1, wall_y)])

        def add_vertical_door(wall_x):
            check_x = wall_x - 1 if wall_x == x else wall_x + 1
            if not any(is_road_tile(check_x, tile_y) for tile_y in range(y + 1, y + h - 1)):
                return

            door_y = (y + y + h) // 2
            if door_y + 1 >= y + h - 1:
                door_y -= 1
            set_tile(wall_x, door_y, wood_floor)
            set_tile(wall_x, door_y + 1, wood_floor)
            doors_added.extend([(wall_x, door_y), (wall_x, door_y + 1)])

        add_horizontal_door(y)
        add_horizontal_door(y + h - 1)
        add_vertical_door(x)
        add_vertical_door(x + w - 1)

        if not doors_added:
            door_x = (x + x + w) // 2
            set_tile(door_x, y, wood_floor)
            set_tile(door_x + 1, y, wood_floor)

        return {"x": x, "y": y, "w": w, "h": h, "theme": theme}

    size_by_theme = {
        0: ((7, 14), (6, 11)),
        1: ((10, 20), (8, 15)),
        2: ((14, 28), (10, 22)),
        3: ((12, 24), (10, 18)),
    }
    limit_by_theme = {0: 10, 1: 7, 2: 8, 3: 8}
    buildings = []

    for bx, by, bw, bh in block_bounds():
        if bw < 10 or bh < 10:
            continue

        cx, cy = bx + bw // 2, by + bh // 2
        theme = zone_theme(cx, cy)
        (w_min, w_max), (h_min, h_max) = size_by_theme[theme]
        area = bw * bh
        attempts = (1 if area < 600 else 2 if area < 1800 else 3) * 4

        for _ in range(attempts):
            theme_count = sum(1 for building in buildings if zone_theme(building["x"] + building["w"] // 2, building["y"] + building["h"] // 2) == theme)
            if theme_count >= limit_by_theme[theme]:
                break

            max_w = min(w_max, bw - 4)
            max_h = min(h_max, bh - 4)
            if max_w < w_min or max_h < h_min:
                break

            building_w = random.randint(w_min, max_w)
            building_h = random.randint(h_min, max_h)
            margin = 2
            x = random.randint(bx + margin, bx + bw - building_w - margin)
            y = random.randint(by + margin, by + bh - building_h - margin)

            if can_place(x, y, building_w, building_h):
                buildings.append(place_building(x, y, building_w, building_h, theme))
                break

    for _ in range(600):
        x = random.randint(0, width - 1)
        y = random.randint(0, height - 1)
        if get_tile(x, y) != grass:
            continue

        theme = zone_theme(x, y)
        if theme == 2 and random.random() < 0.4:
            set_tile(x, y, debris)
        elif theme == 3 and random.random() < 0.3:
            set_tile(x, y, ash)
        elif theme == 1 and random.random() < 0.15:
            set_tile(x, y, debris)

    def open_area_near(cx, cy, size=4):
        for radius in range(0, 30, 2):
            for dx in range(-radius, radius + 1, 2):
                for dy in range(-radius, radius + 1, 2):
                    x, y = cx + dx, cy + dy
                    if not (2 <= x < width - size - 2 and 2 <= y < height - size - 2):
                        continue
                    if all(get_tile(x + ox, y + oy) in (grass, road) for ox in range(size) for oy in range(size)):
                        return x, y
        return cx, cy

    extraction_zones = []
    for zone_id, (cx, cy) in enumerate([(35, 35), (155, 35), (155, 165), (35, 165)]):
        x, y = open_area_near(cx, cy, 4)
        for ox in range(4):
            for oy in range(4):
                set_tile(x + ox, y + oy, road)
        extraction_zones.append({
            "id": zone_id,
            "tileX": x,
            "tileY": y,
            "w": 4,
            "h": 4,
            "label": ["Alpha Extract", "Bravo Extract", "Charlie Extract", "Delta Extract"][zone_id],
        })

    def find_spawn(cx, cy):
        for radius in range(0, 25):
            for dx in range(-radius, radius + 1):
                for dy in range(-radius, radius + 1):
                    if abs(dx) != radius and abs(dy) != radius:
                        continue
                    x, y = cx + dx, cy + dy
                    if in_bounds(x, y) and get_tile(x, y) in (grass, road):
                        return x, y
        return cx, cy

    player_spawns = [
        {"team": index + 1, "x": x, "y": y}
        for index, (x, y) in enumerate(find_spawn(cx, cy) for cx, cy in [(10, 10), (185, 10), (185, 185), (10, 185)])
    ]

    map_dict = {
        "map": {
            "name": "City Ruins",
            "tileSize": 32,
            "width": width,
            "height": height,
            "tileset": "assets/sprites/tileset.png",
            "districts": districts,
            "extractionZones": extraction_zones,
            "buildings": buildings,
            "playerSpawns": player_spawns,
            "zombieSpawns": [],
            "tileTypes": {
                "0": {"name": "grass", "solid": False, "flammable": False},
                "1": {"name": "road", "solid": False, "flammable": False},
                "2": {"name": "wall", "solid": True, "flammable": False},
                "3": {"name": "debris", "solid": False, "flammable": True},
                "4": {"name": "wood_floor", "solid": False, "flammable": True},
                "5": {"name": "ash", "solid": False, "flammable": False},
            },
            "layers": [{"name": "base", "comment": "City layout", "data": data}],
        }
    }

    output_path = Path(__file__).resolve().parents[1] / "data" / "map.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w") as file:
        json.dump(map_dict, file, separators=(",", ":"))

    theme_counts = {theme: sum(1 for building in buildings if building["theme"] == theme) for theme in range(4)}
    print(f"Map generated successfully with {len(buildings)} buildings: {output_path}")
    print(f"District buildings: residential={theme_counts[0]} commercial={theme_counts[1]} industrial={theme_counts[2]} military={theme_counts[3]}")


if __name__ == "__main__":
    generate_city_map()
