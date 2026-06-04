#pragma once
#include <string>
#include <unordered_map>
#include <SDL2/SDL.h>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// TextureCache — loads and owns SDL textures, keyed by string id
// ─────────────────────────────────────────────────────────────────────────────
class TextureCache {
public:
    bool init(SDL_Renderer* renderer);
    void shutdown();

    /// Load texture from path and store under key. Returns false on failure.
    bool load(const std::string& key, const std::string& path);

    /// Slice a single cell from a sprite sheet and store under key.
    /// cellW/cellH: size of each cell. col/row: 0-based index.
    bool loadFromSheet(const std::string& key, const std::string& sheetPath,
                       int cellW, int cellH, int col, int row);

    /// Load all item icons from sheet_items.png
    void loadItemIcons();
    /// Load world sprites (tiles, characters, props) from sheet_world.png
    void loadWorldSprites();
    /// Load character animation frames (player + 3 zombie types)
    void loadCharacterSprites();
    /// Slice using explicit pixel rect (for non-uniform grids)
    bool loadFromRect(const std::string& key, const std::string& sheetPath,
                      int srcX, int srcY, int srcW, int srcH);

    /// Returns texture or nullptr if not found.
    SDL_Texture* get(const std::string& key) const noexcept;

    /// Returns a 1x1 solid color texture (for placeholder drawing).
    SDL_Texture* placeholder(SDL_Renderer* r, uint8_t red, uint8_t green, uint8_t blue);

    void unload(const std::string& key);
    void clear();

private:
    SDL_Renderer*                                    m_renderer = nullptr;
    std::unordered_map<std::string, SDL_Texture*>    m_textures;
    std::unordered_map<std::string, SDL_Texture*>    m_sheetCache; // full sheet cache

    SDL_Texture* getOrLoadSheet(const std::string& path);
};

} // namespace dz
