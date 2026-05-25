#pragma once
#include <SDL2/SDL_ttf.h>
#include <unordered_map>
#include <string>

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// FontManager — SDL2_ttf 폰트 캐시 (사이즈별 lazy load)
// ─────────────────────────────────────────────────────────────────────────────
class FontManager {
public:
    bool init();
    void shutdown();

    /// 일반 폰트 (ptSize 기준 캐시)
    TTF_Font* get(int ptSize);
    /// 고정폭 폰트 (HUD 숫자용)
    TTF_Font* mono(int ptSize);

private:
    std::unordered_map<int, TTF_Font*> m_regular;
    std::unordered_map<int, TTF_Font*> m_mono;
    std::string m_regularPath;
    std::string m_monoPath;

    TTF_Font* load(const std::string& path, int ptSize);
    static std::string findFont(std::initializer_list<const char*> candidates);
};

} // namespace dz
