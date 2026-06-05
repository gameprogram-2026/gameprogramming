"""
DeadZone City Ruins Map Generator
격자형 도로 + 구역별 건물 + 문이 도로를 향함
200×200 타일, 4구역(NW=주거 NE=상업 SW=군사 SE=공업)
"""
import json, random

W, H = 200, 200
GRASS, ROAD, WALL, DEBRIS, WOOD_FLOOR, ASH = 0, 1, 2, 3, 4, 5

data = [GRASS] * (W * H)

def s(x, y):        return 0 <= x < W and 0 <= y < H
def g(x, y):        return data[y*W+x] if s(x,y) else GRASS
def t(x, y, v):
    if s(x,y): data[y*W+x] = v

def rect(x, y, w, h, v):
    for j in range(y, y+h):
        for i in range(x, x+w):
            t(i, j, v)

# ─────────────────────────────────────────────────────────────────────────────
# 1. 격자형 도로망
# ─────────────────────────────────────────────────────────────────────────────
# 주 도로(폭 5): x/y = 5, 65, 100, 135, 195 → 5개 선
# 블록 크기: 주도로 사이 ~55타일 → 각 블록 내 보조도로 1개(폭 3)
MAIN_ROADS_X = [5, 65, 100, 135, 195]
MAIN_ROADS_Y = [5, 65, 100, 135, 195]
MAIN_W = 5   # 주 도로 폭
SEC_W  = 3   # 보조 도로 폭

# 주 도로 그리기
for rx in MAIN_ROADS_X:
    rect(rx, 0, MAIN_W, H, ROAD)
for ry in MAIN_ROADS_Y:
    rect(0, ry, W, MAIN_W, ROAD)

# 보조 도로: 각 블록 중앙
def mid_blocks(roads):
    segs = []
    for i in range(len(roads)-1):
        a = roads[i] + MAIN_W
        b = roads[i+1]
        mid = (a + b) // 2
        segs.append(mid)
    return segs

SEC_X = mid_blocks(MAIN_ROADS_X)  # [37, 82, 117, 165]
SEC_Y = mid_blocks(MAIN_ROADS_Y)

for sx in SEC_X:
    rect(sx, 0, SEC_W, H, ROAD)
for sy in SEC_Y:
    rect(0, sy, W, SEC_W, ROAD)

# ─────────────────────────────────────────────────────────────────────────────
# 2. 건물 배치
# ─────────────────────────────────────────────────────────────────────────────
def is_road_tile(x, y):
    return s(x,y) and g(x,y) == ROAD

def zone_theme(cx, cy):
    """구역별 테마: NW=0주거, NE=1상업, SW=3군사, SE=2공업"""
    if cx < 100 and cy < 100: return 0  # NW 주거
    if cx >= 100 and cy < 100: return 1  # NE 상업
    if cx < 100 and cy >= 100: return 3  # SW 군사
    return 2                              # SE 공업

def block_bounds():
    """격자 블록(도로 사이 공간) 목록 반환"""
    all_x = sorted(set(MAIN_ROADS_X + SEC_X + [0, W]))
    all_y = sorted(set(MAIN_ROADS_Y + SEC_Y + [0, H]))
    blocks = []
    for i in range(len(all_x)-1):
        for j in range(len(all_y)-1):
            x1 = all_x[i]
            y1 = all_y[j]
            x2 = all_x[i+1]
            y2 = all_y[j+1]
            # 도로 타일이 있는 구간 건너뜀
            # 블록 시작을 도로 끝으로 조정
            bx = x1
            by = y1
            bw = x2 - x1
            bh = y2 - y1
            # 도로 폭 제거: 시작점이 도로면 넘기기
            if g(bx, by + bh//2) == ROAD: bx += MAIN_W if bx in MAIN_ROADS_X else SEC_W
            if g(bx + bw//2, by) == ROAD: by += MAIN_W if by in MAIN_ROADS_Y else SEC_W
            bw = x2 - bx
            bh = y2 - by
            if bw > 8 and bh > 8:
                blocks.append((bx, by, bw, bh))
    return blocks

def can_place(x, y, w, h):
    """건물 영역이 도로나 기존 건물과 겹치지 않는지 확인 (1타일 마진)"""
    for j in range(y-1, y+h+1):
        for i in range(x-1, x+w+1):
            v = g(i, j)
            if v in (ROAD, WALL, WOOD_FLOOR): return False
    return True

def place_building(x, y, w, h, theme):
    """건물 배치: 바닥 + 외벽. 도로를 향한 면에 문 생성"""
    rect(x, y, w, h, WOOD_FLOOR)
    # 외벽
    for i in range(x, x+w):
        t(i, y,     WALL)
        t(i, y+h-1, WALL)
    for j in range(y, y+h):
        t(x,     j, WALL)
        t(x+w-1, j, WALL)

    doors_added = []

    def add_door_on_side(wall_x_list, wall_y, horizontal=True):
        """벽 한 면에 도로 접촉 여부 확인 후 문 추가"""
        if horizontal:
            # 북쪽/남쪽 벽: 벽 바깥이 도로인지 확인
            check_y = wall_y - 1 if wall_y == y else wall_y + 1
            road_adjacent = any(is_road_tile(i, check_y) for i in range(x+1, x+w-1))
            if road_adjacent:
                mid = (x + x+w) // 2
                door_x = mid if mid+1 < x+w-1 else mid-1
                t(door_x,   wall_y, WOOD_FLOOR)
                t(door_x+1, wall_y, WOOD_FLOOR)
                doors_added.append((door_x, wall_y))
                doors_added.append((door_x+1, wall_y))
        else:
            # 동쪽/서쪽 벽: 벽 바깥이 도로인지 확인
            check_x = wall_x_list - 1 if wall_x_list == x else wall_x_list + 1
            road_adjacent = any(is_road_tile(check_x, j) for j in range(y+1, y+h-1))
            if road_adjacent:
                mid = (y + y+h) // 2
                door_y = mid if mid+1 < y+h-1 else mid-1
                t(wall_x_list, door_y,   WOOD_FLOOR)
                t(wall_x_list, door_y+1, WOOD_FLOOR)
                doors_added.append((wall_x_list, door_y))
                doors_added.append((wall_x_list, door_y+1))

    add_door_on_side(None, y,     horizontal=True)   # 북쪽 벽
    add_door_on_side(None, y+h-1, horizontal=True)   # 남쪽 벽
    add_door_on_side(x,     None, horizontal=False)  # 서쪽 벽
    add_door_on_side(x+w-1, None, horizontal=False)  # 동쪽 벽

    # 도로와 전혀 안 붙어있으면 가장 가까운 면에 강제로 문 추가
    if not doors_added:
        mid_x = (x + x+w) // 2
        t(mid_x, y, WOOD_FLOOR)      # 북쪽 강제 문
        t(mid_x+1, y, WOOD_FLOOR)

    return {"x": x, "y": y, "w": w, "h": h, "theme": theme}

buildings = []
random.seed(42)  # 재현 가능한 맵

# 구역별 건물 크기 범위
SIZE_BY_THEME = {
    0: ((7,14),  (6,11)),   # 주거: 작은 집들
    1: ((10,20), (8,15)),   # 상업: 중형 빌딩
    2: ((14,28), (10,22)),  # 공업: 대형 창고
    3: ((12,24), (10,18)),  # 군사: 직사각 벙커
}

blocks = block_bounds()
for (bx, by, bw, bh) in blocks:
    if bw < 10 or bh < 10: continue
    cx, cy = bx + bw//2, by + bh//2
    theme = zone_theme(cx, cy)
    (wmin, wmax), (hmin, hmax) = SIZE_BY_THEME[theme]

    # 블록 크기에 맞게 건물 수 결정
    area = bw * bh
    n_buildings = 1 if area < 600 else (2 if area < 1800 else 3)

    for _ in range(n_buildings * 4):  # 배치 시도
        if len([b for b in buildings if zone_theme(b['x']+b['w']//2, b['y']+b['h']//2)==theme]) >= {0:10,1:7,2:8,3:8}[theme]:
            break
        max_w = min(wmax, bw-4)
        max_h = min(hmax, bh-4)
        if max_w < wmin or max_h < hmin: break
        bld_w = random.randint(wmin, max_w)
        bld_h = random.randint(hmin, max_h)
        margin = 2
        rx = random.randint(bx+margin, bx+bw-bld_w-margin)
        ry = random.randint(by+margin, by+bh-bld_h-margin)
        if can_place(rx, ry, bld_w, bld_h):
            b = place_building(rx, ry, bld_w, bld_h, theme)
            buildings.append(b)
            break

# ─────────────────────────────────────────────────────────────────────────────
# 3. 구역별 환경 디테일
# ─────────────────────────────────────────────────────────────────────────────
def add_zone_detail():
    for _ in range(600):
        x = random.randint(0, W-1)
        y = random.randint(0, H-1)
        if g(x, y) != GRASS: continue
        theme = zone_theme(x, y)
        if theme == 2:   # 공업: 잔해/파편
            if random.random() < 0.4: t(x, y, DEBRIS)
        elif theme == 3: # 군사: 재
            if random.random() < 0.3: t(x, y, ASH)
        elif theme == 1: # 상업: 가끔 잔해
            if random.random() < 0.15: t(x, y, DEBRIS)

add_zone_detail()

# ─────────────────────────────────────────────────────────────────────────────
# 4. 탈출존 — 각 구역 중심 도로 교차점
# ─────────────────────────────────────────────────────────────────────────────
def open_area_near(cx, cy, size=4):
    for r in range(0, 30, 2):
        for dx in range(-r, r+1, 2):
            for dy in range(-r, r+1, 2):
                tx, ty = cx+dx, cy+dy
                if not (2 <= tx < W-size-2 and 2 <= ty < H-size-2): continue
                if all(g(tx+i, ty+j) in (GRASS, ROAD) for i in range(size) for j in range(size)):
                    return tx, ty
    return cx, cy

ez_centers = [(35, 35), (155, 35), (35, 165), (155, 165)]
extraction_zones = []
for idx, (ecx, ecy) in enumerate(ez_centers):
    ex, ey = open_area_near(ecx, ecy, 4)
    for i in range(4):
        for j in range(4):
            t(ex+i, ey+j, ROAD)
    extraction_zones.append({"id": idx, "tileX": ex, "tileY": ey, "w": 4, "h": 4,
                              "label": ["Alpha","Bravo","Charlie","Delta"][idx]+" Extract"})

# ─────────────────────────────────────────────────────────────────────────────
# 5. 플레이어 스폰 — 각 구역 코너, 도로 위
# ─────────────────────────────────────────────────────────────────────────────
def find_spawn(cx, cy):
    for r in range(0, 25):
        for dx in range(-r, r+1):
            for dy in range(-r, r+1):
                if abs(dx)!=r and abs(dy)!=r: continue
                tx, ty = cx+dx, cy+dy
                if s(tx,ty) and g(tx,ty) in (GRASS, ROAD):
                    return tx, ty
    return cx, cy

spawn_corners = [(25, 25), (150, 25), (25, 170), (165, 170)]
player_spawns = [{"team": i+1, "x": x, "y": y}
                 for i, (x, y) in enumerate(find_spawn(*c) for c in spawn_corners)]

# ─────────────────────────────────────────────────────────────────────────────
# 6. 저장
# ─────────────────────────────────────────────────────────────────────────────
map_dict = {
  "map": {
    "name": "City Ruins",
    "tileSize": 32,
    "width": W, "height": H,
    "tileset": "assets/sprites/tileset.png",
    "extractionZones": extraction_zones,
    "buildings": buildings,
    "playerSpawns": player_spawns,
    "zombieSpawns": [],
    "tileTypes": {
      "0": {"name":"grass",      "solid":False,"flammable":False},
      "1": {"name":"road",       "solid":False,"flammable":False},
      "2": {"name":"wall",       "solid":True, "flammable":False},
      "3": {"name":"debris",     "solid":False,"flammable":True },
      "4": {"name":"wood_floor", "solid":False,"flammable":True },
      "5": {"name":"ash",        "solid":False,"flammable":False}
    },
    "layers": [{"name":"base","data":data}]
  }
}

out_path = "/Users/gimseongjun/Desktop/DeadZone/data/map.json"
with open(out_path, "w") as f:
    json.dump(map_dict, f, separators=(',',':'))

theme_counts = {i: sum(1 for b in buildings if b['theme']==i) for i in range(4)}
print(f"맵 생성 완료: 건물 {len(buildings)}개")
print(f"  주거(NW)={theme_counts[0]} 상업(NE)={theme_counts[1]} 공업(SE)={theme_counts[2]} 군사(SW)={theme_counts[3]}")
print(f"  탈출존 {len(extraction_zones)}개, 스폰 {len(player_spawns)}개")
