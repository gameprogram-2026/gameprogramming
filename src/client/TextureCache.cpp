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

    SDL_Texture* sheet = IMG_LoadTexture(m_renderer, path.c_str());
    if (!sheet) {
        DZ_LOG_WARN("TextureCache: sheet load failed '%s': %s", path.c_str(), IMG_GetError());
        return nullptr;
    }
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
// loadItemIcons
// Sheet layout (sheet_weapons.png, 4 cols x 2 rows, each ~256px):
//   [0,0]=pipe  [1,0]=axe  [2,0]=pistol  [3,0]=flamethrower
//   [0,1]=ammo  [1,1]=medkit  [2,1]=bandage  [3,1]=food_can
//
// Sheet layout (sheet_materials.png, 4 cols x 3 rows):
//   [0,0]=wood  [1,0]=metal  [2,0]=electronic  [3,0]=barricade_item
//   [0,1]=rag   [1,1]=alcohol  [2,1]=rope  [3,1]=battery
//   [0,2]=wire  [1,2]=wire2(skip) [2,2]=building_turret  [3,2]=empty
// ─────────────────────────────────────────────────────────────────────────────
void TextureCache::loadItemIcons() {
    const std::string wSheet = "assets/sprites/items/sheet_weapons.png";
    const std::string mSheet = "assets/sprites/items/sheet_materials.png";

    // Sheet dimensions: 1024x1024, 4 cols x 2 rows → each cell ~256x512
    const int WCW = 256, WCH = 512; // weapon sheet cell
    // Sheet_weapons.png
    struct { const char* key; int col; int row; } weaponCells[] = {
        {"icon_scrap_pipe",    0, 0},
        {"icon_axe",           1, 0},
        {"icon_pistol_9mm",    2, 0},
        {"icon_flamethrower",  3, 0},
        {"icon_9mm_ammo",      0, 1},
        {"icon_medkit",        1, 1},
        {"icon_bandage",       2, 1},
        {"icon_food_can",      3, 1},
    };
    for (auto& c : weaponCells)
        loadFromSheet(c.key, wSheet, WCW, WCH, c.col, c.row);

    // Sheet_materials.png — 1024x1024, 4x3 rows
    const int MCW = 256, MCH = 341;
    struct { const char* key; int col; int row; } matCells[] = {
        {"icon_wood_plank",     0, 0},
        {"icon_metal_sheet",    1, 0},
        {"icon_electronic",     2, 0},
        {"icon_barricade_item", 3, 0},
        {"icon_rag",            0, 1},
        {"icon_alcohol",        1, 1},
        {"icon_rope",           2, 1},
        {"icon_battery",        3, 1},
        {"icon_wire",           0, 2},
        {"icon_turret_item",    2, 2},
    };
    for (auto& c : matCells)
        loadFromSheet(c.key, mSheet, MCW, MCH, c.col, c.row);

    DZ_LOG_INFO("[TextureCache] Item icons loaded");
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
