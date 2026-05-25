#include "FontManager.h"
#include "shared/util/Logger.h"
#include <fstream>

namespace dz {

bool FontManager::init() {
    if (TTF_Init() != 0) {
        DZ_LOG_ERROR("TTF_Init: %s", TTF_GetError());
        return false;
    }

    // 한글 지원 폰트 우선 — AppleSDGothicNeo (macOS 내장)
    m_regularPath = findFont({
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Geneva.ttf",
        "/Library/Fonts/Arial Unicode MS.ttf",
        "/Library/Fonts/Arial.ttf",
    });

    // 모노는 한글 지원 폰트 → 수 폴백
    m_monoPath = findFont({
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/SFNSMono.ttf",
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Menlo.ttc",
    });

    if (m_regularPath.empty()) {
        DZ_LOG_WARN("[Font] 일반 폰트 없음 — 모노 폴백 사용");
        m_regularPath = m_monoPath;
    }
    if (m_monoPath.empty()) {
        DZ_LOG_WARN("[Font] 고정폭 폰트 없음 — 일반 폴백 사용");
        m_monoPath = m_regularPath;
    }

    DZ_LOG_INFO("[Font] 일반: %s", m_regularPath.c_str());
    DZ_LOG_INFO("[Font] 모노: %s",  m_monoPath.c_str());
    return true;
}

void FontManager::shutdown() {
    for (auto& [sz, f] : m_regular) TTF_CloseFont(f);
    for (auto& [sz, f] : m_mono)    TTF_CloseFont(f);
    m_regular.clear();
    m_mono.clear();
    TTF_Quit();
}

TTF_Font* FontManager::get(int ptSize) {
    auto it = m_regular.find(ptSize);
    if (it != m_regular.end()) return it->second;
    TTF_Font* f = load(m_regularPath, ptSize);
    if (f) m_regular[ptSize] = f;
    return f;
}

TTF_Font* FontManager::mono(int ptSize) {
    auto it = m_mono.find(ptSize);
    if (it != m_mono.end()) return it->second;
    TTF_Font* f = load(m_monoPath, ptSize);
    if (f) m_mono[ptSize] = f;
    return f;
}

TTF_Font* FontManager::load(const std::string& path, int ptSize) {
    if (path.empty()) return nullptr;
    TTF_Font* f = TTF_OpenFont(path.c_str(), ptSize);
    if (!f) DZ_LOG_WARN("[Font] TTF_OpenFont(%s, %d): %s",
                         path.c_str(), ptSize, TTF_GetError());
    return f;
}

std::string FontManager::findFont(std::initializer_list<const char*> candidates) {
    for (const char* path : candidates) {
        std::ifstream f(path);
        if (f.good()) return path;
    }
    return {};
}

} // namespace dz
