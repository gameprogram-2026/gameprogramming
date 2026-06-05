#include "TextureCache.h"
#include "shared/util/Logger.h"
#include <SDL2/SDL_image.h>
#include <array>

namespace dz {

bool TextureCache::init(SDL_Renderer* renderer) {
    m_renderer = renderer;
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        DZ_LOG_WARN("SDL2_image could not init: %s", IMG_GetError());
    }
    return m_renderer != nullptr;
}

void TextureCache::shutdown() {
    clear();
    for (auto& [k, tex] : m_sheetCache)
        if (tex) SDL_DestroyTexture(tex);
    m_sheetCache.clear();
    IMG_Quit();
}

bool TextureCache::load(const std::string& key, const std::string& path) {
    SDL_Texture* tex = IMG_LoadTexture(m_renderer, path.c_str());
    if (!tex) {
        DZ_LOG_WARN("TextureCache: failed to load '%s': %s", path.c_str(), IMG_GetError());
        return false;
    }
    auto it = m_textures.find(key);
    if (it != m_textures.end() && it->second) SDL_DestroyTexture(it->second);
    m_textures[key] = tex;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// getOrLoadSheet — full sheet (cached separately to avoid re-loading)
// ─────────────────────────────────────────────────────────────────────────────
SDL_Texture* TextureCache::getOrLoadSheet(const std::string& path) {
    auto it = m_sheetCache.find(path);
    if (it != m_sheetCache.end()) return it->second;

    // Surface 파이프라인으로 로드 → 마젠타 (#FF00FF) 컬러키 제거
    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) {
        DZ_LOG_WARN("TextureCache: sheet load failed '%s': %s", path.c_str(), IMG_GetError());
        return nullptr;
    }
    SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 255, 0, 255));
    SDL_Texture* sheet = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    if (!sheet) return nullptr;
    SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
    m_sheetCache[path] = sheet;
    return sheet;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadFromSheet — blit one cell into a new RGBA texture
// ─────────────────────────────────────────────────────────────────────────────
bool TextureCache::loadFromSheet(const std::string& key,
                                  const std::string& sheetPath,
                                  int cellW, int cellH, int col, int row)
{
    SDL_Texture* sheet = getOrLoadSheet(sheetPath);
    if (!sheet) return false;

    // Create target render texture
    SDL_Texture* cell = SDL_CreateTexture(m_renderer,
                                           SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET,
                                           cellW, cellH);
    if (!cell) return false;
    SDL_SetTextureBlendMode(cell, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, cell);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    SDL_Rect src  = {col * cellW, row * cellH, cellW, cellH};
    SDL_Rect dst  = {0, 0, cellW, cellH};
    SDL_RenderCopy(m_renderer, sheet, &src, &dst);
    SDL_SetRenderTarget(m_renderer, prev);

    auto it = m_textures.find(key);
    if (it != m_textures.end() && it->second) SDL_DestroyTexture(it->second);
    m_textures[key] = cell;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadItemIcons — sheet_items.png (1993×691, 8열×3행, 셀 249×230)
//
//  행0: scrap_pipe nail_bat fire_axe pistol_9mm flamethrower molotov _ _
//  행1: ammo_9mm  medkit   bandage  food_can   _            _       _ _
//  행2: scrap_metal plank  elec_part oil        wood         _       _ _
// ─────────────────────────────────────────────────────────────────────────────
void TextureCache::loadItemIcons() {
    const std::string sheet = "assets/sprites/items/sheet_items.png";
    // 4128×1024, 8열×2행, 셀 516×512
    const int CW = 516, CH = 512;

    // 8×2 실제 레이아웃:
    // Row 0: scrap_pipe nail_bat fire_axe pistol_9mm flamethrower molotov ammo_9mm medkit
    // Row 1: bandage food_can scrap_metal plank electronic_part oil wood [empty]
    struct { const char* key; int col; int row; } cells[] = {
        {"icon_scrap_pipe",      0, 0},
        {"icon_nail_bat",        1, 0},
        {"icon_fire_axe",        2, 0},
        {"icon_pistol_9mm",      3, 0},
        {"icon_flamethrower",    4, 0},
        {"icon_molotov",         5, 0},
        {"icon_ammo_9mm",        6, 0},
        {"icon_medkit",          7, 0},
        {"icon_bandage",         0, 1},
        {"icon_food_can",        1, 1},
        {"icon_scrap_metal",     2, 1},
        {"icon_plank",           3, 1},
        {"icon_wood",            3, 1},
        {"icon_electronic_part", 4, 1},
        {"icon_oil",             5, 1},
    };

    for (auto& c : cells)
        loadFromSheet(c.key, sheet, CW, CH, c.col, c.row);

    load("icon_smg_9mm", "assets/sprites/items/icon_smg_9mm.png");

    DZ_LOG_INFO("[TextureCache] Item icons loaded");
}

// ─────────────────────────────────────────────────────────────────────────────
// loadFromRect — 명시적 픽셀 좌표로 잘라내기 (비균일 그리드용)
// ─────────────────────────────────────────────────────────────────────────────
bool TextureCache::loadFromRect(const std::string& key, const std::string& sheetPath,
                                 int srcX, int srcY, int srcW, int srcH) {
    SDL_Texture* sheet = getOrLoadSheet(sheetPath);
    if (!sheet) return false;

    SDL_Texture* cell = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, srcW, srcH);
    if (!cell) return false;
    SDL_SetTextureBlendMode(cell, SDL_BLENDMODE_BLEND);

    SDL_Texture* prev = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, cell);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);
    SDL_Rect src = {srcX, srcY, srcW, srcH};
    SDL_Rect dst = {0, 0, srcW, srcH};
    SDL_RenderCopy(m_renderer, sheet, &src, &dst);
    SDL_SetRenderTarget(m_renderer, prev);

    auto it = m_textures.find(key);
    if (it != m_textures.end() && it->second) SDL_DestroyTexture(it->second);
    m_textures[key] = cell;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadWorldSprites — sheet_world.png (2000×982)
//
//  행0 (y=0,  h=185): 지면 타일 11개 (TW=182)
//  행1 (y=185,h=165): 바닥 타일 7개(TW=170) + 식물 5개(RW=162, x_off=1190)
//  행2 (y=350,h=171): 벽 타일 7개(TW=170) + 소품 4개(RW=202, x_off=1190)
//  행3 (y=521,h=208): 소품 7개(TW=170) + 설치물 5개(RW=162, x_off=1190)
//  행4 (y=729,h=224): 캐릭터 12개 (CW=167)
// ─────────────────────────────────────────────────────────────────────────────
void TextureCache::loadWorldSprites() {
    const std::string S = "assets/sprites/world/sheet_world.png";
    const int TW=182, LW=170, RW=162, CW=167; // tile/left/right/char widths
    const int RX=1190; // 오른쪽 섹션 x 시작

    // 행 높이·시작
    const int ry[]={0,185,350,521,729};
    const int rh[]={185,165,171,208,224};

    auto L = [&](const char* k, int x, int y, int w, int h){ loadFromRect(k,S,x,y,w,h); };

    // ── 행 0: 지면 타일 ─────────────────────────────────────────────────────
    L("tile_grass_1",   0*TW,ry[0],TW,rh[0]);
    L("tile_grass_2",   1*TW,ry[0],TW,rh[0]);
    L("tile_grass_3",   2*TW,ry[0],TW,rh[0]);
    L("tile_grass_4",   3*TW,ry[0],TW,rh[0]);
    L("tile_dirt",      4*TW,ry[0],TW,rh[0]);
    L("tile_concrete",  5*TW,ry[0],TW,rh[0]);
    L("tile_concrete2", 6*TW,ry[0],TW,rh[0]);
    L("tile_asphalt",   7*TW,ry[0],TW,rh[0]);
    L("tile_road_line", 8*TW,ry[0],TW,rh[0]);
    L("tile_debris",    9*TW,ry[0],TW,rh[0]);
    L("tile_ash",      10*TW,ry[0],TW,rh[0]);

    // ── 행 1: 바닥 + 식물 ───────────────────────────────────────────────────
    L("tile_floor_wood_h",      0*LW,ry[1],LW,rh[1]);
    L("tile_floor_wood_v",      1*LW,ry[1],LW,rh[1]);
    L("tile_floor_checker",     2*LW,ry[1],LW,rh[1]);
    L("tile_floor_plain",       3*LW,ry[1],LW,rh[1]);
    L("tile_floor_metal",       4*LW,ry[1],LW,rh[1]);
    L("tile_floor_concrete",    5*LW,ry[1],LW,rh[1]);
    L("tile_floor_military",    6*LW,ry[1],LW,rh[1]);
    L("prop_tree_green",  RX+0*RW,ry[1],RW,rh[1]);
    L("prop_tree_dead",   RX+1*RW,ry[1],RW,rh[1]);
    L("prop_bush_green",  RX+2*RW,ry[1],RW,rh[1]);
    L("prop_bush_dry",    RX+3*RW,ry[1],RW,rh[1]);
    L("prop_tall_grass",  RX+4*RW,ry[1],RW,rh[1]);

    // ── 행 2: 벽 + 소품 ────────────────────────────────────────────────────
    L("tile_wall_brick",    0*LW,ry[2],LW,rh[2]);
    L("tile_wall_concrete", 1*LW,ry[2],LW,rh[2]);
    L("tile_wall_metal",    2*LW,ry[2],LW,rh[2]);
    L("tile_wall_military", 3*LW,ry[2],LW,rh[2]);
    L("tile_wall_window",   4*LW,ry[2],LW,rh[2]);
    L("tile_wall_glass",    5*LW,ry[2],LW,rh[2]);
    L("tile_wall_glass2",   6*LW,ry[2],LW,rh[2]);
    const int RW2=202;
    L("prop_tall_grass2", RX+0*RW2,ry[2],RW2,rh[2]);
    L("prop_car_wreck",   RX+1*RW2,ry[2],RW2,rh[2]);
    L("prop_fence",       RX+2*RW2,ry[2],RW2,rh[2]);
    L("prop_fence_v",     RX+3*RW2,ry[2],RW2,rh[2]);

    // ── 행 3: 소품 + 설치물 ─────────────────────────────────────────────────
    L("prop_barrel_red",   0*LW,ry[3],LW,rh[3]);
    L("prop_barrel_brown", 1*LW,ry[3],LW,rh[3]);
    L("prop_crate_wood",   2*LW,ry[3],LW,rh[3]);
    L("prop_crate_metal",  3*LW,ry[3],LW,rh[3]);
    L("prop_sandbag",      4*LW,ry[3],LW,rh[3]);
    L("prop_car_wreck2",   5*LW,ry[3],LW,rh[3]);
    L("prop_barricade",    6*LW,ry[3],LW,rh[3]);
    L("bld_turret_base",  RX+0*RW,ry[3],RW,rh[3]);
    L("bld_turret_head",  RX+1*RW,ry[3],RW,rh[3]);
    L("bld_workbench",    RX+2*RW,ry[3],RW,rh[3]);
    L("bld_loot_closed",  RX+3*RW,ry[3],RW,rh[3]);
    L("bld_loot_open",    RX+4*RW,ry[3],RW,rh[3]);

    // ── 행 4: 캐릭터 (12개, 167×224) ──────────────────────────────────────
    L("char_player_S",      0*CW,ry[4],CW,rh[4]);
    L("char_player_N",      1*CW,ry[4],CW,rh[4]);
    L("char_player_E",      2*CW,ry[4],CW,rh[4]);
    L("char_player_W",      3*CW,ry[4],CW,rh[4]);
    L("char_shambler_S",    4*CW,ry[4],CW,rh[4]);
    L("char_shambler_N",    5*CW,ry[4],CW,rh[4]);
    L("char_shambler_E",    6*CW,ry[4],CW,rh[4]);
    L("char_shambler_W",    7*CW,ry[4],CW,rh[4]);
    L("char_runner_S",      8*CW,ry[4],CW,rh[4]);
    L("char_runner_N",      9*CW,ry[4],CW,rh[4]);
    L("char_brute_S",      10*CW,ry[4],CW,rh[4]);
    L("char_brute_N",      11*CW,ry[4],CW,rh[4]);

    DZ_LOG_INFO("[TextureCache] World sprites loaded");
}

// ─────────────────────────────────────────────────────────────────────────────
// loadCharacterSprites — 캐릭터 애니메이션 프레임 로드
//  각 시트: 1856×2304, 4열×5행, 셀 464×460
//  행0=S 행1=N 행2=E 행3=W 행4=Death, 열0-3=frame
//  키 형식: "char_{type}_{dir}_{frame}"
// ─────────────────────────────────────────────────────────────────────────────
void TextureCache::loadCharacterSprites() {
    const int CW = 464, CH = 460;
    const char* dirs[]  = {"S","N","E","W","D"};
    const int   rows    = 5;
    const int   frames  = 4;

    struct CharDef { const char* type; const char* path; };
    const CharDef chars[] = {
        {"player",   "assets/sprites/characters/player.png"},
        {"shambler", "assets/sprites/characters/zombie_shambler.png"},
        {"runner",   "assets/sprites/characters/zombie_runner.png"},
        {"brute",    "assets/sprites/characters/zombie_brute.png"},
    };

    for (auto& c : chars) {
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < frames; ++col) {
                std::string key = std::string("char_") + c.type
                                + "_" + dirs[row]
                                + "_" + std::to_string(col);
                loadFromRect(key, c.path, col*CW, row*CH, CW, CH);
            }
        }
    }

    // props: 2048×2048, 4×4, 셀 512×512
    const std::string P = "assets/sprites/props/sheet_props.png";
    const int PW=512, PH=512;
    auto LP=[&](const char*k,int col,int row){loadFromRect(k,P,col*PW,row*PH,PW,PH);};
    LP("prop_barricade",    0,0); LP("bld_turret",      1,0);
    LP("bld_workbench",     2,0); LP("bld_loot_closed", 3,0);
    LP("bld_loot_open",     0,1); LP("prop_barrel_red", 1,1);
    LP("prop_barrel_brown", 2,1); LP("prop_crate_wood", 3,1);
    LP("prop_crate_metal",  0,2); LP("prop_sandbag",    1,2);
    LP("prop_car_wreck",    2,2); LP("prop_tree_green", 3,2);
    LP("prop_tree_dead",    0,3); LP("prop_bush",       1,3);
    LP("prop_fence_h",      2,3); LP("prop_fence_v",    3,3);

    // tiles: 2912×1440, 8×4, 셀 364×360
    const std::string T = "assets/sprites/tiles/sheet_tiles.png";
    const int TW=364, TH=360;
    auto LT=[&](const char*k,int col,int row){loadFromRect(k,T,col*TW,row*TH,TW,TH);};
    LT("tile_grass_1",0,0); LT("tile_grass_2",1,0); LT("tile_grass_3",2,0);
    LT("tile_dirt",   3,0); LT("tile_concrete",4,0); LT("tile_asphalt",5,0);
    LT("tile_road",   6,0); LT("tile_debris",  7,0);
    LT("tile_ash",    0,1);
    LT("tile_floor_wood",   0,2); LT("tile_floor_tile",  1,2);
    LT("tile_floor_metal",  2,2); LT("tile_floor_conc",  3,2);
    LT("tile_floor_mil",    4,2);
    LT("tile_wall_brick",   0,3); LT("tile_wall_conc",   1,3);
    LT("tile_wall_metal",   2,3); LT("tile_wall_mil",    3,3);
    LT("tile_wall_window",  4,3); LT("tile_wall_glass",  5,3);

    DZ_LOG_INFO("[TextureCache] Character sprites loaded");
}

SDL_Texture* TextureCache::get(const std::string& key) const noexcept {
    auto it = m_textures.find(key);
    return (it != m_textures.end()) ? it->second : nullptr;
}

SDL_Texture* TextureCache::placeholder(SDL_Renderer* r,
                                        uint8_t red, uint8_t green, uint8_t blue) {
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24,
                                          SDL_TEXTUREACCESS_STATIC, 8, 8);
    if (!tex) return nullptr;
    std::array<uint8_t, 8*8*3> pixels;
    for (int i = 0; i < 8*8; ++i) {
        pixels[i*3+0] = red;
        pixels[i*3+1] = green;
        pixels[i*3+2] = blue;
    }
    SDL_UpdateTexture(tex, nullptr, pixels.data(), 8 * 3);
    return tex;
}

void TextureCache::unload(const std::string& key) {
    auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        if (it->second) SDL_DestroyTexture(it->second);
        m_textures.erase(it);
    }
}

void TextureCache::clear() {
    for (auto& [k, tex] : m_textures)
        if (tex) SDL_DestroyTexture(tex);
    m_textures.clear();
}

} // namespace dz
