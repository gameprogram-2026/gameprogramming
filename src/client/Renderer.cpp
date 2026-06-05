#include "Renderer.h"
#include "shared/util/Logger.h"
#include "shared/network/Packet.h"
#include "shared/ItemData.h"
#include "shared/ecs/components/BuildingComponent.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>

// ZombieType enum (서버 헤더이지만 렌더러에서 타입 구분 필요)
enum ZombieTypeLocal : uint8_t { ZT_SHAMBLER=0, ZT_RUNNER=1, ZT_BRUTE=2 };

namespace dz {

// ─────────────────────────────────────────────────────────────────────────────
// 팔레트
// ─────────────────────────────────────────────────────────────────────────────
namespace Col {
    constexpr SDL_Color BG         = { 8,  10,  14, 255};
    constexpr SDL_Color PANEL      = {14,  18,  26, 230};
    constexpr SDL_Color PANEL_DARK = { 8,  10,  18, 248};
    constexpr SDL_Color BORDER     = {35,  42,  58, 255};
    constexpr SDL_Color ACCENT     = {220, 140,  40, 255};  // 오렌지 액센트 (타르코프 스타일)
    constexpr SDL_Color ACCENT2    = { 60, 200, 100, 255};  // 그린 (탈출/아이템)
    constexpr SDL_Color WARN       = {255,  55,  40, 255};
    constexpr SDL_Color TEXT_HI    = {215, 222, 230, 255};
    constexpr SDL_Color TEXT_LO    = { 90, 105, 118, 255};
    constexpr SDL_Color SHADOW     = {  0,   0,   0, 180};
    constexpr SDL_Color GOLD       = {255, 200,  50, 255};
    constexpr SDL_Color TEAM[5]    = {
        {100,100,100,255}, { 60,140,255,255},
        {255, 60, 60,255}, { 60,210, 80,255}, {255,200, 40,255}
    };
}

static SDL_Color districtColor(int theme, uint8_t alpha) {
    switch (theme) {
        case 0: return {150, 120, 45, alpha};   // residential
        case 1: return {55, 95, 185, alpha};    // commercial
        case 2: return {65, 125, 85, alpha};    // industrial
        case 3: return {120, 90, 45, alpha};    // military
        default: return {120, 120, 120, alpha};
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// 건물 정의 — 대형 + 다중 문 (데드타운 스타일)
// 문은 벽에 색 마커로 표현 (구멍 없음)
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::init(SDL_Window* window, int w, int h) {
    m_renderer = SDL_CreateRenderer(window, -1,
                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        m_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!m_renderer) { DZ_LOG_ERROR("SDL_CreateRenderer: %s", SDL_GetError()); return false; }

    // HiDPI(레티나) 보정 — 실제 드로어블 픽셀 크기로 논리 해상도 설정
    int drawW = w, drawH = h;
    SDL_GetRendererOutputSize(m_renderer, &drawW, &drawH);
    if (drawW != w || drawH != h) {
        SDL_RenderSetLogicalSize(m_renderer, w, h);
    }

    m_screenW = w; m_screenH = h;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // 시야각 안개 텍스처 (렌더 타깃, RGBA)
    m_fowTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_TARGET, w, h);
    if (m_fowTexture)
        SDL_SetTextureBlendMode(m_fowTexture, SDL_BLENDMODE_BLEND);

    m_texCache.init(m_renderer);
    m_texCache.loadItemIcons();
    m_texCache.loadWorldSprites();
    m_texCache.loadCharacterSprites();
    m_fonts.init();
    return true;
}

void Renderer::shutdown() {
    m_fonts.shutdown();
    m_texCache.shutdown();
    if (m_fowTexture) { SDL_DestroyTexture(m_fowTexture); m_fowTexture = nullptr; }
    if (m_renderer)   { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
}

void Renderer::beginFrame() {
    SDL_SetRenderDrawColor(m_renderer, Col::BG.r, Col::BG.g, Col::BG.b, 255);
    SDL_RenderClear(m_renderer);
}

void Renderer::endFrame() { SDL_RenderPresent(m_renderer); }

// ─────────────────────────────────────────────────────────────────────────────
// 공통 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawText(const std::string& txt, int x, int y,
                        SDL_Color c, TTF_Font* f, bool centered) {
    if (!f) f = m_fonts.get(16);
    if (!f || txt.empty()) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, txt.c_str(), c);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(m_renderer, s);
    if (t) {
        SDL_Rect dst = {centered ? x - s->w/2 : x, y, s->w, s->h};
        SDL_RenderCopy(m_renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(s);
}

void Renderer::drawTextShadow(const std::string& txt, int x, int y,
                               SDL_Color c, SDL_Color sh, TTF_Font* f, bool centered) {
    drawText(txt, x+2, y+2, sh, f, centered);
    drawText(txt, x,   y,   c,  f, centered);
}

void Renderer::drawPanel(int x, int y, int w, int h,
                          SDL_Color bg, SDL_Color border, int bw) {
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(m_renderer, &r);
    SDL_SetRenderDrawColor(m_renderer, border.r, border.g, border.b, border.a);
    for (int i = 0; i < bw; ++i) {
        SDL_Rect br = {x+i, y+i, w-2*i, h-2*i};
        SDL_RenderDrawRect(m_renderer, &br);
    }
}

void Renderer::drawButton(int x, int y, int w, int h,
                           const std::string& lbl, bool hov,
                           SDL_Color base, SDL_Color hover, SDL_Color border, TTF_Font* f) {
    drawPanel(x, y, w, h, hov ? hover : base, border, 2);
    SDL_Color tc = hov ? SDL_Color{255,255,255,255} : Col::TEXT_HI;
    int fh = f ? TTF_FontHeight(f) : 14;
    drawText(lbl, x + w/2, y + h/2 - fh/2, tc, f, true);
}

void Renderer::drawHpBar(int sx, int sy, float pct, int w, int h) {
    pct = std::max(0.0f, std::min(1.0f, pct));
    SDL_SetRenderDrawColor(m_renderer, 25, 25, 28, 210);
    SDL_Rect bg = {sx, sy, w, h};
    SDL_RenderFillRect(m_renderer, &bg);
    uint8_t r = static_cast<uint8_t>((1.0f-pct)*220);
    uint8_t g = static_cast<uint8_t>(pct*200);
    SDL_SetRenderDrawColor(m_renderer, r, g, 20, 230);
    SDL_Rect fill = {sx, sy, static_cast<int>(w*pct), h};
    SDL_RenderFillRect(m_renderer, &fill);
    SDL_SetRenderDrawColor(m_renderer, 150, 150, 150, 180);
    SDL_RenderDrawRect(m_renderer, &bg);
}

void Renderer::drawFilledCircle(int cx, int cy, int r, SDL_Color c) {
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, c.a);
    for (int dy = -r; dy <= r; ++dy) {
        int dx = static_cast<int>(std::sqrt(static_cast<float>(r*r - dy*dy)));
        SDL_RenderDrawLine(m_renderer, cx-dx, cy+dy, cx+dx, cy+dy);
    }
}

void Renderer::drawScanlines(int alpha) {
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, static_cast<uint8_t>(alpha));
    for (int y = 0; y < m_screenH; y += 3)
        SDL_RenderDrawLine(m_renderer, 0, y, m_screenW, y);
}

SDL_Color Renderer::gradeColor(const std::string& g) const noexcept {
    if (g == "enhanced") return {80, 200, 100, 255};
    if (g == "rare")     return {80, 140, 255, 255};
    if (g == "unique")   return {230, 170,  40, 255};
    return Col::TEXT_LO;  // normal
}

// ─────────────────────────────────────────────────────────────────────────────
// 로비 화면 — 군사 전술 서바이벌 스타일
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawLobby(int mouseX, int mouseY,
                   const std::string& username, const std::string& password,
                   int focusIdx,
                   const std::string& status,
                   bool isRegisterTab,
                   LobbyButton& outLoginTab, LobbyButton& outRegisterTab,
                   LobbyButton& outActionBtn, LobbyButton& outQuit,
                   LobbyButton& outUserBox, LobbyButton& outPassBox) {
    TTF_Font* fSm  = m_fonts.get(13);
    TTF_Font* fMd  = m_fonts.get(18);
    TTF_Font* fLg  = m_fonts.get(54);
    TTF_Font* fXL  = m_fonts.get(72);
    TTF_Font* fMono= m_fonts.mono(13);
    int W = m_screenW, H = m_screenH;
    uint32_t ticks = SDL_GetTicks();

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // ── 배경 그라데이션 (최하단 어두운 밀리터리 톤) ────────────────────────────
    for (int y = 0; y < H; ++y) {
        float t = static_cast<float>(y) / H;
        // 상단: 깊고 어두운 블루-블랙 / 하단: 약간 따뜻한 다크그레이
        uint8_t r = static_cast<uint8_t>(6  + t * 6);
        uint8_t g = static_cast<uint8_t>(7  + t * 5);
        uint8_t b = static_cast<uint8_t>(12 + t * 8);
        SDL_SetRenderDrawColor(m_renderer, r, g, b, 255);
        SDL_RenderDrawLine(m_renderer, 0, y, W, y);
    }

    // ── 전술 그리드 (희미한) ──────────────────────────────────────────────────
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 6);
    for (int x = 0; x < W; x += 60) SDL_RenderDrawLine(m_renderer, x, 0, x, H);
    for (int y = 0; y < H; y += 60) SDL_RenderDrawLine(m_renderer, 0, y, W, y);

    // ── 스캔라인 효과 ─────────────────────────────────────────────────────────
    drawScanlines(16);

    // ── 상단 경고 바 (붉은 군사 스트립) ──────────────────────────────────────
    SDL_SetRenderDrawColor(m_renderer, 130, 12, 12, 255);
    SDL_Rect topBar = {0, 0, W, 42};
    SDL_RenderFillRect(m_renderer, &topBar);
    // 하단 오렌지 라인
    SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 180);
    SDL_Rect accentLine = {0, 42, W, 2};
    SDL_RenderFillRect(m_renderer, &accentLine);

    // 경고 바 텍스트
    drawText("◆ DEAD ZONE: ASHES  —  CLASSIFIED OPERATION  ◆", W / 2, 12,
             {255, 230, 230, 255}, fMd, true);
    // 버전 + 우측 코드
    drawText("v0.1-ALPHA", W - 14, 14, {200, 100, 100, 180}, fSm);

    // ── 제목 (글로우 효과) ────────────────────────────────────────────────────
    int titleY = 65;
    // 그림자 레이어 (여러 번 오프셋)
    for (int d = 6; d >= 1; --d) {
        uint8_t da = static_cast<uint8_t>(d * 8);
        drawText("DEAD ZONE", W / 2, titleY + d * 2, {200, 40, 40, da}, fXL, true);
    }
    // 메인 타이틀
    drawTextShadow("DEAD ZONE", W / 2, titleY, {240, 60, 50, 255}, {0, 0, 0, 200}, fXL, true);

    // 서브타이틀
    int subtY = titleY + (fXL ? TTF_FontHeight(fXL) : 72) + 2;
    // 서브타이틀 배경 스트립
    SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 40);
    SDL_Rect subBg = {0, subtY, W, 28};
    SDL_RenderFillRect(m_renderer, &subBg);
    drawText("ASHES  ▸  TEAM EXTRACTION  ▸  SURVIVE OR PERISH",
             W / 2, subtY + 5, {220, 160, 60, 220}, fSm, true);

    // ── 중앙 구분선 ───────────────────────────────────────────────────────────
    int lineY = subtY + 42;
    SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 255);
    SDL_Rect divLine = {60, lineY, W - 120, 1};
    SDL_RenderFillRect(m_renderer, &divLine);
    SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 60);
    SDL_Rect divGlow = {60, lineY + 1, W - 120, 1};
    SDL_RenderFillRect(m_renderer, &divGlow);

    // ── 로그인 패널 (중앙 정렬) ──────────────────────────────────────────────
    int connW = 400;
    int connX = (W - connW) / 2;
    int connY = lineY + 30;

    // ── 탭 (LOGIN / REGISTER) ─────────────────────────────────────────────────
    int tabW = connW / 2, tabH = 36;
    outLoginTab.rect    = {connX,          connY, tabW, tabH};
    outRegisterTab.rect = {connX + tabW,   connY, tabW, tabH};

    bool logHov = outLoginTab.hit(mouseX, mouseY);
    bool regHov = outRegisterTab.hit(mouseX, mouseY);

    // LOGIN 탭
    {
        bool active = !isRegisterTab;
        SDL_SetRenderDrawColor(m_renderer,
            active ? 14 : (logHov ? 20 : 10),
            active ? 16 : (logHov ? 22 : 11),
            active ? 24 : (logHov ? 32 : 16), 255);
        SDL_Rect t = {connX, connY, tabW, tabH};
        SDL_RenderFillRect(m_renderer, &t);
        if (active) {
            SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 255);
            SDL_Rect topAcc = {connX, connY, tabW, 2};
            SDL_RenderFillRect(m_renderer, &topAcc);
        }
        SDL_SetRenderDrawColor(m_renderer, active ? 60 : 35, active ? 72 : 42, active ? 90 : 58, 255);
        SDL_Rect tb = {connX, connY, tabW, tabH};
        SDL_RenderDrawRect(m_renderer, &tb);
        drawText("LOGIN", connX + tabW / 2, connY + 9,
                 active ? Col::TEXT_HI : Col::TEXT_LO, fMd, true);
    }
    // REGISTER 탭
    {
        bool active = isRegisterTab;
        SDL_SetRenderDrawColor(m_renderer,
            active ? 14 : (regHov ? 20 : 10),
            active ? 16 : (regHov ? 22 : 11),
            active ? 24 : (regHov ? 32 : 16), 255);
        SDL_Rect t = {connX + tabW, connY, tabW, tabH};
        SDL_RenderFillRect(m_renderer, &t);
        if (active) {
            SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 255);
            SDL_Rect topAcc = {connX + tabW, connY, tabW, 2};
            SDL_RenderFillRect(m_renderer, &topAcc);
        }
        SDL_SetRenderDrawColor(m_renderer, active ? 60 : 35, active ? 72 : 42, active ? 90 : 58, 255);
        SDL_Rect tb = {connX + tabW, connY, tabW, tabH};
        SDL_RenderDrawRect(m_renderer, &tb);
        drawText("REGISTER", connX + tabW + tabW / 2, connY + 9,
                 active ? Col::TEXT_HI : Col::TEXT_LO, fMd, true);
    }

    // ── 입력 패널 배경 ────────────────────────────────────────────────────────
    int panelH = 140;
    SDL_SetRenderDrawColor(m_renderer, 10, 12, 20, 240);
    SDL_Rect panBg = {connX, connY + tabH, connW, panelH};
    SDL_RenderFillRect(m_renderer, &panBg);
    SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 255);
    SDL_RenderDrawRect(m_renderer, &panBg);

    // ── 입력 필드 ─────────────────────────────────────────────────────────────
    int boxW = connW - 40, boxH = 36;
    int uX = connX + 20, uY = connY + tabH + 16;
    outUserBox.rect = {uX, uY, boxW, boxH};

    // ID 라벨
    drawText("USER ID", uX, uY - 12, {100, 110, 130, 200}, m_fonts.get(10));
    // ID 박스
    bool uFocus = (focusIdx == 1);
    SDL_SetRenderDrawColor(m_renderer, uFocus ? 16 : 12, uFocus ? 20 : 14, uFocus ? 30 : 22, 255);
    SDL_RenderFillRect(m_renderer, &outUserBox.rect);
    SDL_SetRenderDrawColor(m_renderer,
        uFocus ? 220 : 40, uFocus ? 140 : 48, uFocus ? 40 : 65, 255);
    SDL_RenderDrawRect(m_renderer, &outUserBox.rect);
    // 좌측 포커스 바
    if (uFocus) {
        SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 255);
        SDL_Rect fl = {uX, uY, 2, boxH};
        SDL_RenderFillRect(m_renderer, &fl);
    }
    drawText(username + (uFocus && (ticks % 1000 < 500) ? "|" : ""),
             uX + 10, uY + 10, uFocus ? Col::TEXT_HI : Col::TEXT_LO, fMono);

    // 비밀번호 박스
    int pY = uY + boxH + 18;
    outPassBox.rect = {uX, pY, boxW, boxH};
    drawText("PASSWORD", uX, pY - 12, {100, 110, 130, 200}, m_fonts.get(10));
    bool pFocus = (focusIdx == 2);
    SDL_SetRenderDrawColor(m_renderer, pFocus ? 16 : 12, pFocus ? 20 : 14, pFocus ? 30 : 22, 255);
    SDL_RenderFillRect(m_renderer, &outPassBox.rect);
    SDL_SetRenderDrawColor(m_renderer,
        pFocus ? 220 : 40, pFocus ? 140 : 48, pFocus ? 40 : 65, 255);
    SDL_RenderDrawRect(m_renderer, &outPassBox.rect);
    if (pFocus) {
        SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 255);
        SDL_Rect fl = {uX, pY, 2, boxH};
        SDL_RenderFillRect(m_renderer, &fl);
    }
    std::string passMask(password.length(), '*');
    drawText(passMask + (pFocus && (ticks % 1000 < 500) ? "|" : ""),
             uX + 10, pY + 10, pFocus ? Col::TEXT_HI : Col::TEXT_LO, fMono);

    // ── 상태 메시지 ──────────────────────────────────────────────────────────
    if (!status.empty()) {
        bool isErr = (status.find("실패") != std::string::npos ||
                      status.find("ERR")  != std::string::npos ||
                      status.find("fail") != std::string::npos);
        SDL_Color sc = isErr ? SDL_Color{220, 60, 60, 255} : SDL_Color{60, 200, 100, 255};
        drawText(status, W / 2, connY + tabH + panelH + 8, sc, fSm, true);
    }

    // ── 액션 버튼 ─────────────────────────────────────────────────────────────
    int btnW = connW, btnH = 48;
    int btnX = connX, btnY = connY + tabH + panelH + 30;
    outActionBtn.rect = {btnX, btnY, btnW, btnH};
    bool aHov = outActionBtn.hit(mouseX, mouseY);

    // 버튼 배경
    SDL_Color btnBase = !isRegisterTab ? SDL_Color{14, 35, 60, 255} : SDL_Color{14, 45, 24, 255};
    SDL_Color btnHov  = !isRegisterTab ? SDL_Color{20, 50, 85, 255} : SDL_Color{20, 65, 35, 255};
    SDL_Color btnAcc  = !isRegisterTab ? SDL_Color{50, 140, 220, 255} : SDL_Color{60, 200, 100, 255};
    SDL_SetRenderDrawColor(m_renderer,
        aHov ? btnHov.r : btnBase.r,
        aHov ? btnHov.g : btnBase.g,
        aHov ? btnHov.b : btnBase.b, 255);
    SDL_Rect btn = {btnX, btnY, btnW, btnH};
    SDL_RenderFillRect(m_renderer, &btn);
    // 상단 강조선
    SDL_SetRenderDrawColor(m_renderer, btnAcc.r, btnAcc.g, btnAcc.b, aHov ? 255 : 160);
    SDL_Rect btnTop = {btnX, btnY, btnW, 2};
    SDL_RenderFillRect(m_renderer, &btnTop);
    SDL_SetRenderDrawColor(m_renderer, btnAcc.r, btnAcc.g, btnAcc.b, 200);
    SDL_RenderDrawRect(m_renderer, &btn);
    drawText(!isRegisterTab ? "▶  DEPLOY  ( LOGIN )" : "▶  CREATE ACCOUNT",
             btnX + btnW / 2, btnY + 14, {220, 230, 240, 255}, fMd, true);

    // ── 종료 버튼 ─────────────────────────────────────────────────────────────
    int qW = 120, qH = 30;
    int qX = W / 2 - qW / 2, qY = btnY + btnH + 14;
    outQuit.rect = {qX, qY, qW, qH};
    bool qHov = outQuit.hit(mouseX, mouseY);
    SDL_SetRenderDrawColor(m_renderer, qHov ? 50 : 30, 12, 12, 230);
    SDL_Rect qBtn = {qX, qY, qW, qH};
    SDL_RenderFillRect(m_renderer, &qBtn);
    SDL_SetRenderDrawColor(m_renderer, qHov ? 180 : 100, 30, 30, 200);
    SDL_RenderDrawRect(m_renderer, &qBtn);
    drawText("[ QUIT ]", qX + qW / 2, qY + 7, {180, 80, 80, 200}, fSm, true);

    // ── 하단 조작 안내 바 ─────────────────────────────────────────────────────
    SDL_SetRenderDrawColor(m_renderer, 8, 10, 16, 240);
    SDL_Rect btmBar = {0, H - 40, W, 40};
    SDL_RenderFillRect(m_renderer, &btmBar);
    SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 200);
    SDL_RenderDrawLine(m_renderer, 0, H - 40, W, H - 40);
    drawText(
        "WASD: 이동   SHIFT: 질주   CTRL: 웅크리기   LMB: 공격   F: 파밍   I: 인벤토리   M: 지도",
        W / 2, H - 25, Col::TEXT_LO, fSm, true);

    // ── 비네트 효과 ───────────────────────────────────────────────────────────
    for (int i = 0; i < 100; ++i) {
        float t = 1.0f - static_cast<float>(i) / 100.0f;
        uint8_t a = static_cast<uint8_t>(t * t * 150);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, a);
        SDL_Rect edges[] = {
            {i, i, W - 2*i, 1}, {i, H - 1 - i, W - 2*i, 1},
            {i, i, 1, H - 2*i}, {W - 1 - i, i, 1, H - 2*i}
        };
        for (auto& e : edges) SDL_RenderFillRect(m_renderer, &e);
    }
}
void Renderer::drawMatchmaking(float elapsedTime) {
    int W = m_screenW, H = m_screenH;
    uint32_t ticks = SDL_GetTicks();

    // 배경
    for (int y = 0; y < H; ++y) {
        float t = static_cast<float>(y) / H;
        SDL_SetRenderDrawColor(m_renderer,
            static_cast<uint8_t>(6 + t * 5),
            static_cast<uint8_t>(7 + t * 5),
            static_cast<uint8_t>(12 + t * 8), 255);
        SDL_RenderDrawLine(m_renderer, 0, y, W, y);
    }
    drawScanlines(14);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // 상단 바
    SDL_SetRenderDrawColor(m_renderer, 130, 12, 12, 255);
    SDL_Rect topBar = {0, 0, W, 42};
    SDL_RenderFillRect(m_renderer, &topBar);
    SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 180);
    SDL_Rect accLine = {0, 42, W, 2};
    SDL_RenderFillRect(m_renderer, &accLine);
    drawText("◆ DEAD ZONE: ASHES  —  MATCHMAKING  ◆", W / 2, 12,
             {255, 230, 230, 255}, m_fonts.get(18), true);

    // 중앙 매치메이킹 텍스트 (맥박 효과)
    float pulse = std::sin(elapsedTime * 3.0f) * 0.5f + 0.5f;
    uint8_t pa = static_cast<uint8_t>(180 + 75 * pulse);

    TTF_Font* fLg = m_fonts.get(42);
    TTF_Font* fMd = m_fonts.get(22);
    TTF_Font* fSm = m_fonts.get(14);

    // 스피너 (회전하는 원호 시뮬레이션)
    int cx = W / 2, cy = H / 2 - 60;
    float angle = elapsedTime * 3.0f;
    int spinR = 40;
    for (int i = 0; i < 32; ++i) {
        float a = angle + i * (3.14159f * 2.0f / 32.f);
        float fa = static_cast<float>(i) / 32.f;
        uint8_t sa = static_cast<uint8_t>(fa * 220);
        SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, sa);
        int px = cx + static_cast<int>(std::cos(a) * spinR);
        int py = cy + static_cast<int>(std::sin(a) * spinR);
        SDL_Rect dot = {px - 2, py - 2, 4, 4};
        SDL_RenderFillRect(m_renderer, &dot);
    }
    // 내부 서클
    drawFilledCircle(cx, cy, spinR - 10, {10, 12, 20, 200});
    drawText("●", cx, cy - 8, {220, 140, 40, pa}, fMd, true);

    // 텍스트
    drawText("MATCHMAKING IN PROGRESS", W / 2, cy + spinR + 20, {220, 140, 40, pa}, fLg, true);

    int mins = static_cast<int>(elapsedTime) / 60;
    int secs = static_cast<int>(elapsedTime) % 60;
    char timeBuf[64];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d  경과", mins, secs);
    drawText(timeBuf, W / 2, cy + spinR + 68, Col::TEXT_HI, fMd, true);
    drawText("팀 배정 및 서버 연결 중...", W / 2, cy + spinR + 96, Col::TEXT_LO, fSm, true);

    // 팀 슬롯 4개
    int pad = 20;
    int teamW = (W - pad * 5) / 4;
    int teamH = 120;
    int teamY = H - teamH - 50;

    const char* tNames[] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA"};
    const char* tColors[] = {"청팀", "적팀", "녹팀", "황팀"};
    for (int t = 0; t < 4; ++t) {
        int tx = pad + t * (teamW + pad);
        SDL_Color tc = Col::TEAM[t + 1];

        // 패널 배경
        SDL_SetRenderDrawColor(m_renderer, 8 + tc.r / 15, 10 + tc.g / 15, 16 + tc.b / 15, 230);
        SDL_Rect panel = {tx, teamY, teamW, teamH};
        SDL_RenderFillRect(m_renderer, &panel);
        // 상단 컬러 바
        SDL_SetRenderDrawColor(m_renderer, tc.r, tc.g, tc.b, 200);
        SDL_Rect topAccent = {tx, teamY, teamW, 3};
        SDL_RenderFillRect(m_renderer, &topAccent);
        SDL_SetRenderDrawColor(m_renderer, tc.r / 2, tc.g / 2, tc.b / 2, 200);
        SDL_RenderDrawRect(m_renderer, &panel);

        drawText(tNames[t], tx + teamW / 2, teamY + 8, tc, m_fonts.get(18), true);
        drawText(tColors[t], tx + teamW / 2, teamY + 30, Col::TEXT_LO, fSm, true);

        // 슬롯 2개
        for (int s = 0; s < 2; ++s) {
            int sy = teamY + 52 + s * 30;
            SDL_SetRenderDrawColor(m_renderer, 14, 18, 28, 200);
            SDL_Rect slot = {tx + 10, sy, teamW - 20, 24};
            SDL_RenderFillRect(m_renderer, &slot);
            SDL_SetRenderDrawColor(m_renderer, 30, 36, 52, 255);
            SDL_RenderDrawRect(m_renderer, &slot);
            drawText("[ EMPTY ]", tx + teamW / 2, sy + 5, {45, 52, 68, 200}, fSm, true);
        }
    }

    // 하단 취소 힌트
    drawText("ESC — 매칭 취소 (로비로 복귀)", W / 2, H - 20, Col::TEXT_LO, fSm, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// 맵 로딩 화면
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawMapLoading(float progress) {
    int W = m_screenW, H = m_screenH;
    uint32_t ticks = SDL_GetTicks();

    // 배경 그라데이션
    for (int y = 0; y < H; ++y) {
        float t = static_cast<float>(y) / H;
        SDL_SetRenderDrawColor(m_renderer,
            static_cast<uint8_t>(6 + t * 4),
            static_cast<uint8_t>(6 + t * 4),
            static_cast<uint8_t>(10 + t * 6), 255);
        SDL_RenderDrawLine(m_renderer, 0, y, W, y);
    }
    drawScanlines(10);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // 경고 바
    SDL_SetRenderDrawColor(m_renderer, 130, 12, 12, 255);
    SDL_Rect topBar = {0, 0, W, 42};
    SDL_RenderFillRect(m_renderer, &topBar);
    SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 180);
    SDL_Rect accLine = {0, 42, W, 2};
    SDL_RenderFillRect(m_renderer, &accLine);
    drawText("◆ DEAD ZONE: ASHES  —  DEPLOYMENT  ◆", W / 2, 12,
             {255, 230, 230, 255}, m_fonts.get(18), true);

    TTF_Font* fLg = m_fonts.get(42);
    TTF_Font* fMd = m_fonts.get(20);
    TTF_Font* fSm = m_fonts.get(14);

    // 타이틀
    float pulse = std::sin(ticks * 0.003f) * 0.5f + 0.5f;
    uint8_t ta = static_cast<uint8_t>(200 + 55 * pulse);
    drawText("DEPLOYING TO ASHES", W / 2, H / 2 - 80, {200, 60, 50, ta}, fLg, true);

    // 진행 바 (세련된 스타일)
    int barW = 500, barH = 18;
    int barX = W / 2 - barW / 2, barY = H / 2 + 10;

    // 배경
    SDL_SetRenderDrawColor(m_renderer, 14, 18, 28, 255);
    SDL_Rect barBg = {barX, barY, barW, barH};
    SDL_RenderFillRect(m_renderer, &barBg);

    // 진행
    int filled = static_cast<int>(barW * progress);
    SDL_SetRenderDrawColor(m_renderer, 220, 140, 40, 255);
    SDL_Rect barFill = {barX, barY, filled, barH};
    SDL_RenderFillRect(m_renderer, &barFill);

    // 하이라이트 (상단 밝은 줄)
    SDL_SetRenderDrawColor(m_renderer, 255, 200, 100, 160);
    SDL_Rect barHL = {barX, barY, filled, 2};
    SDL_RenderFillRect(m_renderer, &barHL);

    // 외곽선
    SDL_SetRenderDrawColor(m_renderer, 60, 70, 100, 255);
    SDL_RenderDrawRect(m_renderer, &barBg);

    // % 텍스트
    char pctBuf[32];
    std::snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(progress * 100));
    drawText(pctBuf, W / 2, barY - 22, {220, 160, 60, 255}, fMd, true);

    // 하단 상태
    const char* stages[] = {"지도 로드 중...", "건물 배치 중...", "좀비 스폰 중...", "연결 준비 중..."};
    int stageIdx = static_cast<int>(progress * 4);
    if (stageIdx >= 4) stageIdx = 3;
    drawText(stages[stageIdx], W / 2, barY + 28, Col::TEXT_HI, fSm, true);

    // 비네트
    for (int i = 0; i < 80; ++i) {
        float t = 1.0f - static_cast<float>(i) / 80.0f;
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, static_cast<uint8_t>(t * t * 130));
        SDL_Rect edges[] = {
            {i, i, W - 2*i, 1}, {i, H - 1 - i, W - 2*i, 1},
            {i, i, 1, H - 2*i}, {W - 1 - i, i, 1, H - 2*i}
        };
        for (auto& e : edges) SDL_RenderFillRect(m_renderer, &e);
    }
}
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawDeathScreen(float countdown, const std::string& cause) {
    TTF_Font* fLg  = m_fonts.get(72);
    TTF_Font* fMd  = m_fonts.get(24);
    TTF_Font* fSm  = m_fonts.get(16);
    int W = m_screenW, H = m_screenH;

    // 어두운 적색 반투명 오버레이 (두 레이어로 농도 강화)
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 160);
    SDL_Rect full = {0, 0, W, H};
    SDL_RenderFillRect(m_renderer, &full);
    SDL_SetRenderDrawColor(m_renderer, 80, 0, 0, 80);
    SDL_RenderFillRect(m_renderer, &full);

    // 스캔라인 노이즈
    for (int y = 0; y < H; y += 3) {
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 30);
        SDL_RenderDrawLine(m_renderer, 0, y, W, y);
    }

    // 중앙 사망 패널
    int panW = 540, panH = 260;
    int panX = (W - panW) / 2, panY = (H - panH) / 2 - 30;
    drawPanel(panX, panY, panW, panH, {12, 4, 4, 230}, {180, 30, 30, 255}, 3);

    // 빨간 상단 바
    SDL_SetRenderDrawColor(m_renderer, 150, 20, 20, 255);
    SDL_Rect topBar = {panX+3, panY+3, panW-6, 6};
    SDL_RenderFillRect(m_renderer, &topBar);

    // "사망" 텍스트 (그림자 효과)
    int textY = panY + 30;
    drawTextShadow("사  망", W/2, textY,
                   {255, 60, 60, 255}, {80, 0, 0, 200}, fLg, true);

    drawTextShadow(cause, W/2, textY + 80,
                   {220, 220, 220, 255}, {0, 0, 0, 200}, m_fonts.mono(24), true);

    // 구분선
    SDL_SetRenderDrawColor(m_renderer, 120, 20, 20, 200);
    SDL_RenderDrawLine(m_renderer, panX+30, textY+86, panX+panW-30, textY+86);

    // 아이템 손실 안내
    drawText("모든 아이템을 잃었습니다", W/2, textY + 98,
             {220, 150, 150, 255}, fMd, true);

    // 카운트다운
    int sec = static_cast<int>(countdown) + 1;
    std::string cntTxt = std::to_string(sec) + "초 후 로비로 복귀";
    drawText(cntTxt, W/2, textY + 140, {180, 100, 100, 255}, fSm, true);

    // Enter/Space 안내
    drawText("[ ENTER ] 또는 [ SPACE ] 로 즉시 복귀", W/2, textY + 168,
             {120, 70, 70, 200}, fSm, true);

    // 비네트 (화면 가장자리 붉게)
    for (int i = 0; i < 40; ++i) {
        float t = 1.0f - static_cast<float>(i) / 40.0f;
        uint8_t a = static_cast<uint8_t>(t * t * 120);
        SDL_SetRenderDrawColor(m_renderer, 120, 0, 0, a);
        SDL_Rect edges[] = {
            {i, i, W-2*i, 1}, {i, H-1-i, W-2*i, 1},
            {i, i, 1, H-2*i}, {W-1-i, i, 1, H-2*i}
        };
        for (auto& e : edges) SDL_RenderFillRect(m_renderer, &e);
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
}

// ─────────────────────────────────────────────────────────────────────────────
// 타일맵 + 건물
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawTileMap(const TileMap& map, const Camera& cam, float localX, float localY) {
    int tsz = static_cast<int>(TILE_SIZE * cam.zoom);
    float hw = m_screenW / (2.0f * cam.zoom), hh = m_screenH / (2.0f * cam.zoom);
    int minTX = std::max(0, TileMap::worldToTile(cam.x - hw) - 1);
    int minTY = std::max(0, TileMap::worldToTile(cam.y - hh) - 1);
    int maxTX = std::min(map.width()  - 1, TileMap::worldToTile(cam.x + hw) + 1);
    int maxTY = std::min(map.height() - 1, TileMap::worldToTile(cam.y + hh) + 1);

    // SDL 뷰포트 클리핑 — 타일이 화면 밖으로 넘치지 않음 (왼쪽·아래쪽 포함)
    SDL_Rect screenClip = {0, 0, m_screenW, m_screenH};
    SDL_RenderSetClipRect(m_renderer, &screenClip);

    int baseX = static_cast<int>(std::round((minTX * TILE_SIZE - cam.x) * cam.zoom + m_screenW * 0.5f));
    int baseY = static_cast<int>(std::round((minTY * TILE_SIZE - cam.y) * cam.zoom + m_screenH * 0.5f));

    // 구역 경계: 200×200 맵을 4분할 (NW=주거, NE=공업, SW=군사, SE=상업)
    const int HALF = map.width() / 2; // 100

    for (int ty = minTY; ty <= maxTY; ++ty) {
        for (int tx = minTX; tx <= maxTX; ++tx) {
            const Tile& tile = map.at(tx, ty);

            // 구역 판별 (0=NW주거 1=NE공업 2=SW군사 3=SE상업)
            int zone = (tx >= HALF ? 1 : 0) + (ty >= HALF ? 2 : 0);

            const char* texKey = nullptr;
            switch (tile.type) {
                case TILE_GRASS: {
                    // 구역마다 단일 잔디 텍스처 — 무작위 혼합 없음
                    static const char* zoneGrass[] = {
                        "tile_grass_1",   // NW 주거: 밝은 초록
                        "tile_grass_3",   // NE 공업: 말라가는 잔디
                        "tile_grass_3",   // SW 군사: 메마른 올리브
                        "tile_concrete",  // SE 상업: 콘크리트
                    };
                    texKey = zoneGrass[zone];
                    break;
                }
                case TILE_ROAD:       texKey = "tile_asphalt";     break;
                case TILE_WALL:       texKey = "tile_wall_brick";   break;
                case TILE_DEBRIS:     texKey = "tile_debris";       break;
                case TILE_WOOD_FLOOR: texKey = "tile_floor_wood_h"; break;
                case TILE_ASH:        texKey = "tile_ash";          break;
                default:              texKey = "tile_dirt";         break;
            }

            // 타일 rect: base + 정수 누적 (float 오차 없음), w/h +1 로 틈새 방지
            SDL_Rect dst;
            dst.x = baseX + (tx - minTX) * tsz;
            dst.y = baseY + (ty - minTY) * tsz;
            dst.w = tsz + 1;
            dst.h = tsz + 1;
            // 경계 클리핑 — 아래쪽/오른쪽 넘침 방지
            if (dst.x + dst.w > m_screenW) dst.w = m_screenW - dst.x;
            if (dst.y + dst.h > m_screenH) dst.h = m_screenH - dst.y;
            if (dst.w <= 0 || dst.h <= 0) continue;

            SDL_Texture* tex = texKey ? m_texCache.get(texKey) : nullptr;
            if (tex) {
                SDL_RenderCopy(m_renderer, tex, nullptr, &dst); // flip 없음 → 경계 일치
            } else {
                SDL_Color fc = {38,48,28,255};
                switch (tile.type) {
                    case TILE_GRASS: fc = (zone==0)?SDL_Color{40,55,28,255}:(zone==3)?SDL_Color{75,75,70,255}:SDL_Color{50,52,30,255}; break;
                    case TILE_ROAD:  fc = {65,65,70,255}; break;
                    default:         fc = {58,48,35,255}; break;
                }
                SDL_SetRenderDrawColor(m_renderer, fc.r, fc.g, fc.b, fc.a);
                SDL_RenderFillRect(m_renderer, &dst);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    for (const auto& d : map.getDistricts()) {
        int sx1, sy1, sx2, sy2;
        cam.worldToScreen(d.x * TILE_SIZE, d.y * TILE_SIZE, sx1, sy1);
        cam.worldToScreen((d.x + d.w) * TILE_SIZE, (d.y + d.h) * TILE_SIZE, sx2, sy2);
        if (sx2 < 0 || sx1 > m_screenW || sy2 < 0 || sy1 > m_screenH) continue;

        SDL_Color fill = districtColor(d.theme, 22);
        SDL_SetRenderDrawColor(m_renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_Rect z = {sx1, sy1, sx2 - sx1, sy2 - sy1};
        SDL_RenderFillRect(m_renderer, &z);

        SDL_Color border = districtColor(d.theme, 95);
        SDL_SetRenderDrawColor(m_renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(m_renderer, &z);

        if (z.w > 70 && z.h > 46) {
            const std::string label = d.label.empty() ? d.key : d.label;
            const int lx = sx1 + z.w / 2;
            const int ly = sy1 + std::max(10, z.h / 10);
            const int panelW = std::min(128, std::max(74, z.w - 16));
            drawPanel(lx - panelW / 2, ly - 5, panelW, 24,
                      {8, 10, 14, 105}, districtColor(d.theme, 150), 1);
            drawTextShadow(label, lx, ly,
                           {245, 246, 232, 215}, {0, 0, 0, 180},
                           m_fonts.get(13), true);
        }
    }
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetClipRect(m_renderer, nullptr); // 타일 클리핑 해제

    drawBuildings(map, cam, localX, localY);
}

// ─────────────────────────────────────────────────────────────────────────────
// 건물 렌더링 — 문은 마커로 (구멍 없음)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawBuildings(const TileMap& map, const Camera& cam, float localX, float localY) {
    const int WALL_T = static_cast<int>(TILE_SIZE * cam.zoom);

    const auto& buildings = map.getBuildings();
    for (size_t buildingIdx = 0; buildingIdx < buildings.size(); ++buildingIdx) {
        const auto& b = buildings[buildingIdx];
        int sx1, sy1, sx2, sy2;
        cam.worldToScreen(b.x*TILE_SIZE,          b.y*TILE_SIZE,          sx1, sy1);
        cam.worldToScreen((b.x+b.w)*TILE_SIZE,   (b.y+b.h)*TILE_SIZE,   sx2, sy2);
        if (sx2 < -10 || sx1 > m_screenW+10 || sy2 < -10 || sy1 > m_screenH+10) continue;

        int pw = sx2-sx1, ph = sy2-sy1;
        int tsz = static_cast<int>(TILE_SIZE * cam.zoom); 
        
        SDL_Color floorCol, wallCol, roofCol;
        bool hasRoom = false;
        // Themes: 0=주거(Residential)  1=상업(Commercial)  2=공업(Industrial)  3=군사(Military)
        switch(b.theme) {
            case 0: // 주거 — 벽돌/원목 느낌
                floorCol = {108,84,52,255};  // 원목 바닥
                wallCol  = {215,198,168,255}; // 크림색 벽
                roofCol  = {188,62,44,255};   // 기와 적갈색
                hasRoom  = true;
                break;
            case 1: // 상업 — 현대식 유리/콘크리트
                floorCol = {158,152,148,255}; // 밝은 타일
                wallCol  = {225,222,215,255}; // 흰 콘크리트
                roofCol  = {88,108,165,255};  // 강철 청회색
                break;
            case 2: // 공업 — 금속/암회색
                floorCol = {70,70,65,255};    // 콘크리트/철판
                wallCol  = {122,115,105,255}; // 산업용 회색
                roofCol  = {72,80,76,255};    // 어두운 금속
                break;
            case 3: // 군사 — 콘크리트 벙커
                floorCol = {52,62,42,255};    // 짙은 올리브
                wallCol  = {82,94,68,255};    // 군복색
                roofCol  = {45,55,35,255};    // 위장 다크그린
                hasRoom  = true;
                break;
            default:
                floorCol = {100,100,100,255}; wallCol = {150,150,150,255}; roofCol = {120,120,120,255};
                break;
        }

        float bxWorld = b.x * TILE_SIZE;
        float byWorld = b.y * TILE_SIZE;
        float bwWorld = b.w * TILE_SIZE;
        float bhWorld = b.h * TILE_SIZE;
        
        const float revealMargin = TILE_SIZE * 2.0f;
        bool isInside = (localX >= bxWorld && localX <= bxWorld + bwWorld &&
                         localY >= byWorld && localY <= byWorld + bhWorld);
        bool isNear = (localX >= bxWorld - revealMargin && localX <= bxWorld + bwWorld + revealMargin &&
                       localY >= byWorld - revealMargin && localY <= byWorld + bhWorld + revealMargin);
        bool revealInterior = isInside || isNear;

        // 1. 그림자
        SDL_SetRenderDrawColor(m_renderer, 0,0,0, 60);
        SDL_Rect shadow = {sx1+6, sy1+6, pw, ph};
        SDL_RenderFillRect(m_renderer, &shadow);

        if (revealInterior) {
            // 내부 바닥
            SDL_SetRenderDrawColor(m_renderer, floorCol.r, floorCol.g, floorCol.b, 255);
            SDL_Rect interiorFloor = {sx1+WALL_T, sy1+WALL_T, pw-2*WALL_T, ph-2*WALL_T};
            if (interiorFloor.w > 0 && interiorFloor.h > 0)
                SDL_RenderFillRect(m_renderer, &interiorFloor);
                
            // 테마별 바닥 패턴
            if (b.theme == 0) {
                // 주거: 원목 마룻바닥 (수평선)
                SDL_SetRenderDrawColor(m_renderer, 80,55,28, 32);
                int plankH = std::max(2, tsz/3);
                for (int gy = sy1+WALL_T; gy <= sy2-WALL_T; gy += plankH)
                    SDL_RenderDrawLine(m_renderer, sx1+WALL_T, gy, sx2-WALL_T, gy);
            } else if (b.theme == 1) {
                // 상업: 체커보드 타일
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                for (int gy = sy1+WALL_T; gy < sy2-WALL_T; gy += tsz)
                    for (int gx = sx1+WALL_T; gx < sx2-WALL_T; gx += tsz) {
                        bool chk = ((gx/tsz + gy/tsz) % 2 == 0);
                        SDL_SetRenderDrawColor(m_renderer, 0,0,0, chk ? 22 : 6);
                        SDL_Rect cr = {gx, gy, tsz, tsz};
                        SDL_RenderFillRect(m_renderer, &cr);
                    }
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            } else if (b.theme == 2) {
                // 공업: 철망 그레이팅 (촘촘한 격자)
                SDL_SetRenderDrawColor(m_renderer, 0,0,0, 38);
                int gs = std::max(3, tsz/4);
                for (int gy = sy1+WALL_T; gy <= sy2-WALL_T; gy += gs)
                    SDL_RenderDrawLine(m_renderer, sx1+WALL_T, gy, sx2-WALL_T, gy);
                for (int gx = sx1+WALL_T; gx <= sx2-WALL_T; gx += gs)
                    SDL_RenderDrawLine(m_renderer, gx, sy1+WALL_T, gx, sy2-WALL_T);
            } else if (b.theme == 3) {
                // 군사: 콘크리트 균열 (듬성한 수평선)
                SDL_SetRenderDrawColor(m_renderer, 0,0,0, 20);
                for (int gy = sy1+WALL_T; gy <= sy2-WALL_T; gy += tsz)
                    SDL_RenderDrawLine(m_renderer, sx1+WALL_T, gy, sx2-WALL_T, gy);
            }
                
            // 벽 (문 위치를 제외하고 그리기)
            SDL_SetRenderDrawColor(m_renderer, wallCol.r, wallCol.g, wallCol.b, 255);
            auto drawWallSeg = [&](int x, int y, int w, int h) {
                SDL_Rect r = {x, y, w, h};
                SDL_RenderFillRect(m_renderer, &r);
            };

            // 북쪽 벽
            drawWallSeg(sx1, sy1, pw, WALL_T);
            // 남쪽 벽
            drawWallSeg(sx1, sy2 - WALL_T, pw, WALL_T);
            // 동쪽 벽
            drawWallSeg(sx2 - WALL_T, sy1, WALL_T, ph);
            // 서쪽 벽
            drawWallSeg(sx1, sy1, WALL_T, ph);

            // 벽 내면 디테일
            SDL_SetRenderDrawColor(m_renderer,
                std::min(255,wallCol.r+25), std::min(255,wallCol.g+25), std::min(255,wallCol.b+25), 180);
            SDL_Rect ni = {sx1+2, sy1+WALL_T-4, pw-4, 4};
            SDL_Rect si2 = {sx1+2, sy2-WALL_T, pw-4, 4};
            SDL_Rect wi2 = {sx1+WALL_T-4, sy1+2, 4, ph-4};
            SDL_Rect ei2 = {sx2-WALL_T, sy1+2, 4, ph-4};
            SDL_RenderFillRect(m_renderer, &ni);
            SDL_RenderFillRect(m_renderer, &si2);
            SDL_RenderFillRect(m_renderer, &wi2);
            SDL_RenderFillRect(m_renderer, &ei2);
            
            // 방 칸막이
            if (hasRoom && pw > WALL_T*3 && ph > WALL_T*3) {
                SDL_SetRenderDrawColor(m_renderer,
                    std::max(0,wallCol.r-30), std::max(0,wallCol.g-30), std::max(0,wallCol.b-30), 220);
                int divX = sx1 + pw*2/3;
                int divY = sy1 + ph/2;
                SDL_Rect hDiv = {sx1+WALL_T, divY-2, divX-sx1-WALL_T, 4};
                SDL_Rect vDiv = {divX-2, sy1+WALL_T, 4, divY-sy1-WALL_T};
                SDL_RenderFillRect(m_renderer, &hDiv);
                SDL_RenderFillRect(m_renderer, &vDiv);
            }

            // 테마별 내부 소품
            int iX = sx1+WALL_T+2, iY = sy1+WALL_T+2;
            int iW = pw-2*WALL_T-4, iH = ph-2*WALL_T-4;
            int p  = std::max(3, tsz/6);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            if (b.theme == 0 && iW > p*6 && iH > p*6) {
                // 침대
                SDL_SetRenderDrawColor(m_renderer, 145,125,100,190);
                SDL_Rect bed = {iX, iY, iW/3, iH*2/5};
                SDL_RenderFillRect(m_renderer, &bed);
                SDL_SetRenderDrawColor(m_renderer, 225,210,185,200);
                SDL_Rect pillow = {iX+p, iY+p, iW/7, iH/8};
                SDL_RenderFillRect(m_renderer, &pillow);
                // 테이블
                SDL_SetRenderDrawColor(m_renderer, 118,88,52,190);
                SDL_Rect tbl = {iX+iW*2/3, iY+iH/2, iW/4, iH/3};
                SDL_RenderFillRect(m_renderer, &tbl);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,40);
                SDL_RenderDrawRect(m_renderer, &tbl);
            } else if (b.theme == 1 && iW > p*6 && iH > p*6) {
                // 계산대
                SDL_SetRenderDrawColor(m_renderer, 175,170,162,200);
                SDL_Rect counter = {iX, iY+iH*2/3, iW*3/5, p*3};
                SDL_RenderFillRect(m_renderer, &counter);
                // 선반 3개
                SDL_SetRenderDrawColor(m_renderer, 190,188,182,180);
                for (int si = 0; si < 3; ++si) {
                    SDL_Rect shelf = {iX + si*(iW/3), iY, iW/4, p*2};
                    SDL_RenderFillRect(m_renderer, &shelf);
                }
            } else if (b.theme == 2 && iW > p*6 && iH > p*6) {
                // 기계 블록
                SDL_SetRenderDrawColor(m_renderer, 85,83,78,200);
                SDL_Rect mach = {iX+iW/4, iY+iH/4, iW/2, iH/2};
                SDL_RenderFillRect(m_renderer, &mach);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,60);
                SDL_RenderDrawRect(m_renderer, &mach);
                // 드럼통 2개
                SDL_SetRenderDrawColor(m_renderer, 85,72,38,200);
                for (int bi = 0; bi < 2; ++bi) {
                    SDL_Rect barrel = {iX+bi*(p*3+3), iY+iH*3/4, p*3, p*3};
                    SDL_RenderFillRect(m_renderer, &barrel);
                    SDL_SetRenderDrawColor(m_renderer, 0,0,0,50);
                    SDL_RenderDrawRect(m_renderer, &barrel);
                    SDL_SetRenderDrawColor(m_renderer, 85,72,38,200);
                }
            } else if (b.theme == 3 && iW > p*6 && iH > p*6) {
                // 지휘 테이블 (중앙)
                SDL_SetRenderDrawColor(m_renderer, 58,70,48,210);
                SDL_Rect ctbl = {iX+iW/3, iY+iH/3, iW/3, iH/3};
                SDL_RenderFillRect(m_renderer, &ctbl);
                SDL_SetRenderDrawColor(m_renderer, 95,115,75,200);
                SDL_RenderDrawRect(m_renderer, &ctbl);
                // 모래주머니 (코너)
                SDL_SetRenderDrawColor(m_renderer, 98,88,58,200);
                SDL_Rect sb1 = {iX,      iY,      p*4, p*2};
                SDL_Rect sb2 = {iX+iW-p*4, iY,    p*4, p*2};
                SDL_RenderFillRect(m_renderer, &sb1);
                SDL_RenderFillRect(m_renderer, &sb2);
            }
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

            // 내부 시야: 지붕은 완전히 사라지지 않고 낮은 알파로 남겨 건물 경계를 유지
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, roofCol.r, roofCol.g, roofCol.b, isInside ? 42 : 72);
            SDL_Rect roofTint = {sx1, sy1, pw, ph};
            SDL_RenderFillRect(m_renderer, &roofTint);
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
        } else {
            // 지붕: 테마별 타일 텍스처로 채우기
            static const char* roofTex[] = {
                "tile_wall_brick",    // 0 주거: 벽돌
                "tile_wall_conc",     // 1 상업: 콘크리트
                "tile_wall_metal",    // 2 공업: 금속
                "tile_wall_mil",      // 3 군사: 군용 콘크리트
            };
            int themeIdx = std::min(3, b.theme);
            SDL_Texture* roofT = m_texCache.get(roofTex[themeIdx]);
            SDL_Rect roofRect = {sx1, sy1, pw, ph};
            if (roofT) {
                // 타일 텍스처로 지붕 전체 채우기 (tiling)
                int tileW = std::max(1, static_cast<int>(64 * cam.zoom));
                int tileH = tileW;
                for (int ry = sy1; ry < sy1 + ph; ry += tileH)
                    for (int rx = sx1; rx < sx1 + pw; rx += tileW) {
                        SDL_Rect cell = {rx, ry, std::min(tileW, sx1+pw-rx), std::min(tileH, sy1+ph-ry)};
                        SDL_RenderCopy(m_renderer, roofT, nullptr, &cell);
                    }
                // 테마별 색조 오버레이
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, roofCol.r, roofCol.g, roofCol.b, 80);
                SDL_RenderFillRect(m_renderer, &roofRect);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            } else {
                SDL_SetRenderDrawColor(m_renderer, roofCol.r, roofCol.g, roofCol.b, 255);
                SDL_RenderFillRect(m_renderer, &roofRect);
            }
            
            // 테마별 지붕 외관
            if (b.theme == 0) {
                // 주거: 박공지붕 능선 + 굴뚝
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,65);
                SDL_RenderDrawLine(m_renderer, sx1,  sy1,      sx1+pw/2, sy1+ph/3);
                SDL_RenderDrawLine(m_renderer, sx2,  sy1,      sx1+pw/2, sy1+ph/3);
                SDL_RenderDrawLine(m_renderer, sx1+pw/2, sy1+ph/3, sx1+pw/2, sy2-ph/3);
                int chW = std::max(4, pw/8), chH = std::max(4, ph/8);
                SDL_SetRenderDrawColor(m_renderer, 155,75,55,255);
                SDL_Rect chimney = {sx1+pw*3/4, sy1+ph/10, chW, chH};
                SDL_RenderFillRect(m_renderer, &chimney);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,50);
                SDL_RenderDrawRect(m_renderer, &chimney);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 255,255,255,30);
                SDL_RenderDrawLine(m_renderer, sx1, sy1, sx2, sy1);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
            } else if (b.theme == 1) {
                // 상업: 평지붕 + 유리 띠 + AC 유닛
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 180,225,255,55);
                SDL_Rect gl1 = {sx1, sy1, pw, WALL_T};
                SDL_Rect gl2 = {sx1, sy2-WALL_T, pw, WALL_T};
                SDL_RenderFillRect(m_renderer, &gl1);
                SDL_RenderFillRect(m_renderer, &gl2);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,40);
                SDL_Rect inner = {sx1+WALL_T, sy1+WALL_T, pw-2*WALL_T, ph-2*WALL_T};
                SDL_RenderDrawRect(m_renderer, &inner);
                int acSz = std::max(5, pw/7);
                SDL_SetRenderDrawColor(m_renderer, 158,165,172,255);
                SDL_Rect ac1 = {sx1+pw/5,     sy1+ph/4, acSz, acSz};
                SDL_Rect ac2 = {sx2-pw/5-acSz, sy1+ph/4, acSz, acSz};
                SDL_RenderFillRect(m_renderer, &ac1);
                SDL_RenderFillRect(m_renderer, &ac2);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,55);
                SDL_RenderDrawRect(m_renderer, &ac1);
                SDL_RenderDrawRect(m_renderer, &ac2);
            } else if (b.theme == 2) {
                // 공업: 물결 금속 지붕 + 환기구
                int step = std::max(3, static_cast<int>(5*cam.zoom));
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,50);
                for (int gx = sx1; gx < sx2; gx += step)
                    SDL_RenderDrawLine(m_renderer, gx, sy1, gx, sy2);
                SDL_SetRenderDrawColor(m_renderer, 255,255,255,18);
                for (int gx = sx1; gx < sx2; gx += step*2)
                    SDL_RenderDrawLine(m_renderer, gx, sy1, gx, sy2);
                int vSz = std::max(5, pw/9);
                SDL_SetRenderDrawColor(m_renderer, 52,58,56,255);
                SDL_Rect vent = {sx1+pw/2-vSz/2, sy1+ph/2-vSz/2, vSz, vSz};
                SDL_RenderFillRect(m_renderer, &vent);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,80);
                SDL_RenderDrawRect(m_renderer, &vent);
            } else if (b.theme == 3) {
                // 군사: 벙커 — 강화 코너 + 위장 패치 + X
                int bt = WALL_T*2;
                SDL_SetRenderDrawColor(m_renderer, std::max(0,roofCol.r-18), std::max(0,roofCol.g-18), std::max(0,roofCol.b-18), 255);
                SDL_Rect corners[4] = {
                    {sx1, sy1, bt, bt}, {sx2-bt, sy1, bt, bt},
                    {sx1, sy2-bt, bt, bt}, {sx2-bt, sy2-bt, bt, bt}
                };
                for (auto& cr : corners) SDL_RenderFillRect(m_renderer, &cr);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,28);
                SDL_Rect patch1 = {sx1+pw/4,    sy1+ph/5, pw/4, ph/5};
                SDL_Rect patch2 = {sx2-pw*5/12, sy2-ph/3, pw/5, ph/4};
                SDL_RenderFillRect(m_renderer, &patch1);
                SDL_RenderFillRect(m_renderer, &patch2);
                SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(m_renderer, 0,0,0,48);
                SDL_RenderDrawLine(m_renderer, sx1+bt, sy1+bt, sx2-bt, sy2-bt);
                SDL_RenderDrawLine(m_renderer, sx1+bt, sy2-bt, sx2-bt, sy1+bt);
            }
        }
        
        if (revealInterior) {
            // 문(입구)은 지붕이 투명해졌을 때만 내부 벽의 구멍으로 렌더링한다.
            // 밖에서는 지붕 위에 문 스프라이트가 떠 보이지 않게 숨긴다.
            for (const auto& door : map.getDoors()) {
                if (door.building != buildingIdx) continue;

                int dsx, dsy;
                cam.worldToScreen(door.tx * TILE_SIZE, door.ty * TILE_SIZE, dsx, dsy);

                if (door.broken) {
                    SDL_Rect gapRect = {dsx, dsy, tsz, tsz};
                    SDL_SetRenderDrawColor(m_renderer, floorCol.r, floorCol.g, floorCol.b, 255);
                    SDL_RenderFillRect(m_renderer, &gapRect);
                    SDL_SetRenderDrawColor(m_renderer, 95, 58, 32, 230);
                    SDL_RenderDrawLine(m_renderer, dsx + 5, dsy + 7, dsx + tsz - 7, dsy + tsz - 5);
                    SDL_RenderDrawLine(m_renderer, dsx + 8, dsy + tsz - 8, dsx + tsz - 4, dsy + 6);
                    SDL_RenderDrawLine(m_renderer, dsx + tsz / 2, dsy + 4, dsx + tsz / 2 - 5, dsy + tsz - 4);
                } else if (door.open) {
                    SDL_Rect gapRect = {dsx, dsy, tsz, tsz};
                    SDL_SetRenderDrawColor(m_renderer, floorCol.r, floorCol.g, floorCol.b, 255);
                    SDL_RenderFillRect(m_renderer, &gapRect);

                    SDL_SetRenderDrawColor(m_renderer, 130, 90, 50, 255);
                    SDL_Rect doorOpen;
                    if (door.ty == b.y || door.ty == b.y + b.h - 1) {
                        doorOpen = {dsx, dsy, std::max(3, tsz / 6), tsz};
                    } else {
                        doorOpen = {dsx, dsy, tsz, std::max(3, tsz / 6)};
                    }
                    SDL_RenderFillRect(m_renderer, &doorOpen);
                } else {
                    SDL_Rect doorRect = {dsx, dsy, tsz, tsz};
                    SDL_SetRenderDrawColor(m_renderer, 105, 70, 38, 255);
                    SDL_RenderFillRect(m_renderer, &doorRect);
                    SDL_SetRenderDrawColor(m_renderer, 40, 25, 15, 255);
                    SDL_RenderDrawRect(m_renderer, &doorRect);
                    SDL_Rect knob = {dsx + tsz - 6, dsy + tsz/2 - 2, 4, 4};
                    SDL_SetRenderDrawColor(m_renderer, 200, 180, 50, 255);
                    SDL_RenderFillRect(m_renderer, &knob);
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 파밍 상자 단일 렌더링
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawLootBox(const LootBoxView& box, const Camera& cam) {
    float ticks = SDL_GetTicks() * 0.001f;
    TTF_Font* fSm = m_fonts.get(12);

    int sx, sy;
    cam.worldToScreen(box.wx, box.wy, sx, sy);

    if (sx < -30 || sx > m_screenW+30 || sy < -30 || sy > m_screenH+30) return;

    int bw = static_cast<int>(18 * cam.zoom);
    int bh = static_cast<int>(14 * cam.zoom);

    if (!box.isBuilding && !box.looted) {
        SDL_SetRenderDrawColor(m_renderer, 0,0,0,80);
        SDL_Rect sh = {sx-bw/2+2, sy-bh/2+2, bw, bh};
        SDL_RenderFillRect(m_renderer, &sh);
        SDL_SetRenderDrawColor(m_renderer, 140, 100, 38, 255);
        SDL_Rect body = {sx-bw/2, sy-bh/2, bw, bh};
        SDL_RenderFillRect(m_renderer, &body);
        int lidH = bh/3;
        SDL_SetRenderDrawColor(m_renderer, 170, 130, 55, 255);
        SDL_Rect lid = {sx-bw/2, sy-bh/2, bw, lidH};
        SDL_RenderFillRect(m_renderer, &lid);
        SDL_SetRenderDrawColor(m_renderer, 230, 200, 80, 255);
        SDL_Rect lock = {sx-2, sy-bh/2+lidH-2, 4, 5};
        SDL_RenderFillRect(m_renderer, &lock);
        SDL_SetRenderDrawColor(m_renderer, 90, 65, 20, 255);
        SDL_RenderDrawRect(m_renderer, &body);
        if (box.nearPlayer) {
            float pulse = std::sin(ticks * 4.0f) * 0.5f + 0.5f;
            uint8_t pa = static_cast<uint8_t>(150 + pulse * 105);
            SDL_Color hintCol = box.blocked
                              ? SDL_Color{255, 75, 75, pa}
                              : SDL_Color{255, 240, 100, pa};
            SDL_SetRenderDrawColor(m_renderer, hintCol.r, hintCol.g, hintCol.b, hintCol.a);
            SDL_Rect glow = {sx-bw/2-3, sy-bh/2-3, bw+6, bh+6};
            SDL_RenderDrawRect(m_renderer, &glow);
            SDL_Rect glow2 = {sx-bw/2-5, sy-bh/2-5, bw+10, bh+10};
            SDL_SetRenderDrawColor(m_renderer, hintCol.r, hintCol.g, hintCol.b, pa/3);
            SDL_RenderFillRect(m_renderer, &glow2);
            drawText(box.blocked ? "F  파밍 불가" : "F  파밍",
                     sx, sy - bh - 4, hintCol, fSm, true);
        }
    } else if (box.isBuilding) {
        // Use TextureCache sprites for buildings
        bw = static_cast<int>(40 * cam.zoom);
        bh = static_cast<int>(40 * cam.zoom);
        SDL_Rect body = {sx - bw/2, sy - bh/2, bw, bh};
        
        if (box.buildingType == 0) { // Barricade
            SDL_Texture* tex = m_texCache.get("prop_barricade");
            if (tex) SDL_RenderCopy(m_renderer, tex, nullptr, &body);
        } else if (box.buildingType == 1) { // Turret — props 시트의 단일 텍스처
            SDL_Texture* turretTex = m_texCache.get("bld_turret");
            if (turretTex) {
                double angle = static_cast<double>(box.turretDir) * 90.0;
                SDL_RenderCopyEx(m_renderer, turretTex, nullptr, &body, angle, nullptr, SDL_FLIP_NONE);
            }
        } else if (box.buildingType == 2) { // Workbench
            SDL_Texture* tex = m_texCache.get("bld_workbench");
            if (tex) SDL_RenderCopy(m_renderer, tex, nullptr, &body);
            
            if (box.nearPlayer) { // Workbench near
                float pulse = std::sin(ticks * 4.0f) * 0.5f + 0.5f;
                uint8_t pa = static_cast<uint8_t>(150 + pulse * 105);
                SDL_SetRenderDrawColor(m_renderer, 100, 255, 100, pa);
                SDL_Rect glow = {sx-bw/2-2, sy-bh/2-2, bw+4, bh+4};
                SDL_RenderDrawRect(m_renderer, &glow);
                drawText("F  제작대 사용", sx, sy - bh/2 - 12, {100, 255, 100, pa}, fSm, true);
            }
        }
    } else {
        bw = static_cast<int>(32 * cam.zoom);
        bh = static_cast<int>(32 * cam.zoom);
        SDL_Rect body = {sx - bw/2, sy - bh/2, bw, bh};
        
        SDL_Texture* tex = m_texCache.get(box.looted ? "bld_loot_open" : "bld_loot_closed");
        if (tex) SDL_RenderCopy(m_renderer, tex, nullptr, &body);
    }
}

void Renderer::drawBuildingEntitiesOverlay(const std::vector<LootBoxView>& boxes, const Camera& cam) {
    for (const auto& box : boxes) {
        if (box.isBuilding) drawLootBox(box, cam);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 건물 오버레이 (지붕 + 외곽선 + 남쪽 벽)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawBuildingOverlay(const TileMap::BuildingDef& b, const Camera& cam) {
    int sx1, sy1, sx2, sy2;
    cam.worldToScreen(b.x*TILE_SIZE,          b.y*TILE_SIZE,          sx1, sy1);
    cam.worldToScreen((b.x+b.w)*TILE_SIZE,   (b.y+b.h)*TILE_SIZE,   sx2, sy2);
    if (sx2 < -10 || sx1 > m_screenW+10 || sy2 < -10 || sy1 > m_screenH+10) return;

    int pw = sx2-sx1, ph = sy2-sy1;
    const int WALL_T = static_cast<int>(TILE_SIZE * cam.zoom);

    SDL_Color wallCol;
    switch(b.theme) {
        case 0: wallCol = {200,190,168,255}; break;
        case 1: wallCol = {215,210,198,255}; break;
        case 2: wallCol = {138,130,118,255}; break;
        case 3: wallCol = {90,100,80,255}; break;
        default: wallCol = {150,150,150,255}; break;
    }

    // 반투명 남쪽 외벽 (y-sort 시 플레이어 위에 렌더링됨)
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, wallCol.r-20, wallCol.g-20, wallCol.b-20, 140);
    SDL_Rect sWall = {sx1, sy2-WALL_T, pw, WALL_T};
    SDL_RenderFillRect(m_renderer, &sWall);
}

// ─────────────────────────────────────────────────────────────────────────────
// 화염
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawFire(const std::vector<std::pair<int,int>>& fires, const Camera& cam) {
    for (auto [tx, ty] : fires) {
        int sx = static_cast<int>((tx*TILE_SIZE-cam.x)*cam.zoom + m_screenW*0.5f);
        int sy = static_cast<int>((ty*TILE_SIZE-cam.y)*cam.zoom + m_screenH*0.5f);
        int tsz = static_cast<int>(TILE_SIZE*cam.zoom);
        float t = SDL_GetTicks()*0.008f + tx*0.5f + ty*0.7f;
        uint8_t a = 160 + static_cast<uint8_t>((std::sin(t)*0.5f+0.5f)*90);
        SDL_SetRenderDrawColor(m_renderer, 255,100,0,a);
        SDL_Rect dst = {sx,sy,tsz,tsz};
        SDL_RenderFillRect(m_renderer, &dst);
        SDL_SetRenderDrawColor(m_renderer, 255,230,60, static_cast<uint8_t>(a*0.6f));
        SDL_Rect core = {sx+tsz/4,sy+tsz/4,tsz/2,tsz/2};
        SDL_RenderFillRect(m_renderer, &core);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 탈출존
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawExtractionZones(const Camera& cam,
                                    const std::vector<std::pair<float,float>>& zones,
                                    bool open) {
    float t = SDL_GetTicks()*0.001f;
    TTF_Font* fSm = m_fonts.get(12);
    for (auto [wx,wy] : zones) {
        int sx, sy;
        cam.worldToScreen(wx, wy, sx, sy);
        int sr = static_cast<int>(48.0f*cam.zoom); // 맨홀 크기

        if (open) {
            // 맨홀 바닥 (어두운 회색)
            drawFilledCircle(sx, sy, sr, {40, 40, 45, 200});
            // 맨홀 테두리
            SDL_SetRenderDrawColor(m_renderer, 80, 80, 90, 255);
            for (int ang = 0; ang < 360; ang += 2) {
                float r = ang * 3.14159f / 180.0f;
                SDL_RenderDrawPoint(m_renderer,
                    sx + static_cast<int>(std::cos(r)*sr),
                    sy + static_cast<int>(std::sin(r)*sr));
            }
            // 맨홀 격자 무늬
            for (int i = -sr + 10; i < sr - 10; i += 12) {
                int chord = static_cast<int>(std::sqrt(sr*sr - i*i));
                SDL_RenderDrawLine(m_renderer, sx + i, sy - chord, sx + i, sy + chord);
                SDL_RenderDrawLine(m_renderer, sx - chord, sy + i, sx + chord, sy + i);
            }

            // 활성화 표시 (은은한 푸른빛 펄스)
            float pulse = std::sin(t*3.0f)*0.5f + 0.5f;
            drawFilledCircle(sx, sy, static_cast<int>(sr * 0.8f), {100, 200, 255, static_cast<uint8_t>(30 + pulse*30)});

            // 텍스트 안내
            drawText("맨홀 (탈출구)", sx, sy - sr - 22, {150, 220, 255, 255}, fSm, true);
            drawText("F  탈출 시작", sx, sy - sr - 8, {230, 245, 255, 230}, fSm, true);
            drawText("5초간 대기", sx, sy + sr + 8, {200, 200, 200, 200}, fSm, true);
        } else {
            float pulse = std::sin(t)*0.5f+0.5f;
            drawFilledCircle(sx,sy,sr,{60,60,70,static_cast<uint8_t>(25+pulse*15)});
            SDL_SetRenderDrawColor(m_renderer, 80,80,95,90);
            for (int ang=0;ang<360;ang+=5) {
                float r=ang*3.14159f/180.0f;
                SDL_RenderDrawPoint(m_renderer,
                    sx+static_cast<int>(std::cos(r)*sr),
                    sy+static_cast<int>(std::sin(r)*sr));
            }
            drawText("LOCKED", sx, sy-sr-18, {80,80,100,170}, fSm, true);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 로컬 플레이어
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// 내부 헬퍼 — 단일 캐릭터 그리기 (발 기준)
// sx,sy = 세계 중심의 스크린 좌표 / sz = 기본 크기
// ─────────────────────────────────────────────────────────────────────────────
static void drawCharacterBody(Renderer* rnd, int sx, int sy, int sz, float angleDeg,
                               SDL_Color col, bool bleeding, bool isZombie, float animTime) {
    SDL_Renderer* r = rnd->sdlRenderer();
    int feetY = sy + sz*35/100;
    
    // 발 그림자
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i=0; i<3; ++i) {
        rnd->drawFilledCircle(sx, feetY, (sz*35/100) - i, {0, 0, 0, static_cast<uint8_t>(50 - i*10)});
    }

    // 걷기 애니메이션 (상하 바운싱)
    float bob = std::sin(animTime * 15.0f) * (sz*0.08f);
    if (!isZombie && bob < 0) bob = 0; // 플레이어는 통통 튀는 느낌
    int bodyY = feetY - sz*11/10 + static_cast<int>(bob);
    
    float angleRad = angleDeg * (3.14159265f / 180.0f);
    float dx = std::sin(angleRad);
    float dy = -std::cos(angleRad);

    // 몸통 베이스 (원과 사각형으로 캡슐 형태 구성)
    SDL_Color mainCol = bleeding ? SDL_Color{180, 40, 40, 255} : col;
    SDL_Color darkCol = {static_cast<uint8_t>(mainCol.r*0.6f), static_cast<uint8_t>(mainCol.g*0.6f), static_cast<uint8_t>(mainCol.b*0.6f), 255};
    
    int radius = sz * 45 / 100;
    int torsoH = sz * 70 / 100;
    
    // 몸통 아랫부분 (골반)
    rnd->drawFilledCircle(sx, bodyY + torsoH, radius, darkCol);
    // 몸통 중간
    SDL_Rect torso = {sx - radius, bodyY, radius * 2, torsoH};
    SDL_SetRenderDrawColor(r, darkCol.r, darkCol.g, darkCol.b, 255);
    SDL_RenderFillRect(r, &torso);
    // 몸통 중간 밝은 부분 (입체감)
    SDL_Rect torsoLight = {sx - radius + 2, bodyY, radius * 2 - 4, torsoH};
    SDL_SetRenderDrawColor(r, mainCol.r, mainCol.g, mainCol.b, 255);
    SDL_RenderFillRect(r, &torsoLight);
    // 몸통 위 (어깨)
    rnd->drawFilledCircle(sx, bodyY, radius, mainCol);
    
    if (isZombie) {
        // 좀비 팔 (앞으로 뻗은 형태)
        int armRad = radius * 45 / 100;
        int armL_x = sx - static_cast<int>(std::cos(angleRad) * radius) + static_cast<int>(dx * sz * 0.8f);
        int armL_y = bodyY - static_cast<int>(std::sin(angleRad) * radius) + static_cast<int>(dy * sz * 0.8f);
        int armR_x = sx + static_cast<int>(std::cos(angleRad) * radius) + static_cast<int>(dx * sz * 0.8f);
        int armR_y = bodyY + static_cast<int>(std::sin(angleRad) * radius) + static_cast<int>(dy * sz * 0.8f);
        
        rnd->drawFilledCircle(armL_x, armL_y, armRad, darkCol);
        rnd->drawFilledCircle(armR_x, armR_y, armRad, darkCol);
        
        // 좀비 머리 (약간 비틀리고 핏자국, 크게 흔들거리며 걷기)
        float wobbleX = std::cos(animTime*10.0f) * (sz * 0.15f);
        int headX = sx + static_cast<int>(dx * radius * 0.4f) + static_cast<int>(std::cos(angleRad) * wobbleX);
        int headY = bodyY - radius + static_cast<int>(std::sin(animTime*10.0f)* (sz * 0.15f));
        int headRad = radius * 80 / 100;
        rnd->drawFilledCircle(headX, headY, headRad, {80, 130, 70, 255}); // 녹색빛 머리
        rnd->drawFilledCircle(headX + static_cast<int>(dx*headRad*0.6f), headY + static_cast<int>(dy*headRad*0.6f), 2, {200, 0, 0, 220}); // 빨간 눈
    } else {
        // 사람 배낭
        SDL_Rect pack = {sx - radius + 1, bodyY + 3, radius*2 - 2, torsoH - 6};
        SDL_SetRenderDrawColor(r, 65, 60, 55, 255);
        SDL_RenderFillRect(r, &pack);
        SDL_Rect packLight = {sx - radius + 3, bodyY + 3, radius*2 - 6, torsoH - 6};
        SDL_SetRenderDrawColor(r, 85, 80, 75, 255);
        SDL_RenderFillRect(r, &packLight);
        
        // 사람 팔 (무기 들고 있는 형태)
        int handX = sx + static_cast<int>(dx * sz * 0.9f);
        int handY = bodyY + static_cast<int>(dy * sz * 0.9f);
        rnd->drawFilledCircle(handX, handY, radius * 60 / 100, {230, 180, 150, 255}); // 살구색 손
        
        // 사람 머리
        int headX = sx - static_cast<int>(dx * radius * 0.1f);
        int headY = bodyY - radius * 90 / 100;
        int headRad = radius * 80 / 100;
        rnd->drawFilledCircle(headX, headY, headRad, {230, 180, 150, 255});
        
        // 헬멧 / 모자
        rnd->drawFilledCircle(headX, headY - 3, headRad, {50, 50, 60, 255});
        rnd->drawFilledCircle(headX + static_cast<int>(dx*headRad*0.6f), headY, 3, {20, 20, 20, 255}); // 고글
    }
}

void Renderer::drawLocalPlayer(float wx, float wy, float angle, float hpPct,
                                bool bleeding, int teamID, const Camera& cam,
                                const std::string& weaponName,
                                const std::string& weaponGrade,
                                float attackTimer, float attackAngle,
                                int charDir, bool charMoving) {
    int sx, sy;
    cam.worldToScreen(wx, wy, sx, sy);
    int sz = static_cast<int>(22*cam.zoom);
    SDL_Color tc = Col::TEAM[std::max(0,std::min(4,teamID))];
    float animT = (attackTimer > 0.0f) ? (SDL_GetTicks()*0.015f) : (SDL_GetTicks()*0.005f);

    // 캐릭터 애니메이션 스프라이트 (WASD 방향 기준)
    {
        static const char* dirs[] = {"S","N","E","W"};
        int frame = charMoving ? (static_cast<int>(SDL_GetTicks()/125) % 3 + 1) : 0;
        std::string key = std::string("char_player_") + dirs[charDir] + "_" + std::to_string(frame);
        SDL_Texture* tex = m_texCache.get(key);
        if (tex) {
            int h = sz*3, w = h*464/460;
            SDL_Rect dst = {sx-w/2, sy-h+sz/3, w, h};
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        } else {
            drawCharacterBody(this, sx, sy, sz, angle, tc, bleeding, false, animT);
        }
    }

    // 조준선 — 몸통 상단에서 뻗음
    int feetY = sy + sz*35/100;
    int bodyTop = feetY - sz*13/10;
    int bodyCenterY = (bodyTop + feetY) / 2;
    float rad = angle*(3.14159265f/180.0f);
    int ex = sx+static_cast<int>(std::sin(rad)*(sz+8));
    int ey = bodyCenterY-static_cast<int>(std::cos(rad)*(sz+8));
    SDL_SetRenderDrawColor(m_renderer, 255,255,120,255);
    SDL_RenderDrawLine(m_renderer, sx, bodyCenterY, ex, ey);
    drawFilledCircle(ex, ey, 3, {255,255,120,230});

    // ── 공격 스윙 아크 (45도, 캐릭터 근거리) ────────────────────────────────
    if (attackTimer > 0.0f) {
        const float SWING_TOTAL = 0.35f;
        float prog  = 1.0f - (attackTimer / SWING_TOTAL); // 0→1
        float alpha = attackTimer / SWING_TOTAL;           // 1→0 페이드아웃
        uint8_t a   = static_cast<uint8_t>(alpha * 220.0f);

        // 반지름: 캐릭터 크기의 1.6배 (근거리)
        float arcRad    = static_cast<float>(sz) * 1.6f;
        float baseAngle = attackAngle * (3.14159265f / 180.0f);
        // 90도 = PI/2, 절반 = PI/4 = 0.785rad
        const float HALF_ARC = 0.785f;
        // 스윙: 오른쪽(-halfArc)에서 왼쪽(+halfArc)으로 진행
        float arcStart = baseAngle - HALF_ARC;
        float arcSpan  = HALF_ARC * 2.0f * (0.4f + prog * 0.6f);

        // 아크 (두께 선으로 근사)
        SDL_SetRenderDrawColor(m_renderer, 255, 230, 80, a);
        float prevX = sx + std::sin(arcStart) * arcRad;
        float prevY = sy - std::cos(arcStart) * arcRad;
        const int STEPS = 14;
        for (int s = 1; s <= STEPS; ++s) {
            float t    = arcStart + arcSpan * s / STEPS;
            float curX = sx + std::sin(t) * arcRad;
            float curY = sy - std::cos(t) * arcRad;
            for (int off = -2; off <= 2; ++off) {
                SDL_RenderDrawLine(m_renderer,
                    static_cast<int>(prevX), static_cast<int>(prevY) + off,
                    static_cast<int>(curX),  static_cast<int>(curY)  + off);
            }
            prevX = curX; prevY = curY;
        }
        // 아크 양 끝을 플레이어 중심과 선으로 연결 (부채꼴 테두리)
        float startX = sx + std::sin(arcStart) * arcRad;
        float startY = sy - std::cos(arcStart) * arcRad;
        uint8_t la = static_cast<uint8_t>(alpha * 120.0f);
        SDL_SetRenderDrawColor(m_renderer, 255, 220, 60, la);
        SDL_RenderDrawLine(m_renderer, sx, sy, static_cast<int>(startX), static_cast<int>(startY));
        SDL_RenderDrawLine(m_renderer, sx, sy, static_cast<int>(prevX),  static_cast<int>(prevY));
        // 임팩트 점
        drawFilledCircle(static_cast<int>(prevX), static_cast<int>(prevY), 3,
                         {255, 255, 160, static_cast<uint8_t>(alpha * 255.0f)});
    }

    // HP바는 머리 위
    int feetYlp = sy + sz*35/100;
    int headYlp = feetYlp - sz*13/10;
    drawHpBar(sx-14, headYlp-12, hpPct, 28, 5);

    if (bleeding) {
        uint8_t p = 80+static_cast<uint8_t>((std::sin(SDL_GetTicks()*0.01f)*0.5f+0.5f)*150);
        SDL_SetRenderDrawColor(m_renderer, 220,0,0,p);
        SDL_Rect br = {sx-sz/2-3, sy-sz/2-3, sz+6, sz+6};
        SDL_RenderDrawRect(m_renderer, &br);
    }

    // 무기 이름 텍스트 (캐릭터 아래, 등급 색상)
    if (!weaponName.empty()) {
        TTF_Font* fWep = m_fonts.get(11);
        SDL_Color wc   = gradeColor(weaponGrade);
        // 배경 패널 (가독성)
        int tw = static_cast<int>(weaponName.size()) * 6 + 6;
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 160);
        SDL_Rect wbg = {sx - tw/2, sy + sz/2 + 4, tw, 14};
        SDL_RenderFillRect(m_renderer, &wbg);
        drawText(weaponName, sx, sy + sz/2 + 4, wc, fWep, true);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFOV — 120도 시야각 안개-of-war
//
// 동작 방식:
//   1. 렌더 타깃을 m_fowTexture로 전환
//   2. 텍스처를 어두운 안개로 채움              → 시야 밖
//   3. 시야 부채꼴(120°) 영역을 alpha=0으로 뚫음  → 완전히 투명
//   4. 부채꼴 양측 경계에 ~15° 그라데이션 추가     → 자연스러운 가장자리
//   5. 렌더 타깃 복원 후 FOW 텍스처를 화면에 합성
//
// aimAngleDeg: 0=위, 90=오른쪽, 180=아래, 270=왼쪽
//              (InputHandler의 atan2(dx,-dy) 관례와 동일)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawFOV(float wx, float wy, float aimAngleDeg, const Camera& cam, float gameTime) {
    if (!m_fowTexture) return;

    // 밤/낮에 따른 안개 색상과 농도
    float timeOfDay = std::fmod(gameTime, 180.0f);
    bool isNight = timeOfDay > 120.0f;

    // 낮에는 안개 없음(0), 밤이 되기 5초 전부터 점진적으로 어두워짐
    float darkness = 0.0f;
    if (timeOfDay >= 115.0f && timeOfDay <= 120.0f) {
        darkness = (timeOfDay - 115.0f) / 5.0f;
    } else if (isNight) {
        darkness = 1.0f;
    }

    if (darkness <= 0.01f) {
        // 완전한 낮이면 시야 제한(안개)을 아예 그리지 않음
        return;
    }

    int px, py;
    cam.worldToScreen(wx, wy, px, py);

    // ── 1. FOW 텍스처에 렌더링 ─────────────────────────────────────────────
    SDL_SetRenderTarget(m_renderer, m_fowTexture);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    static constexpr float NIGHT_MAX_FOG_ALPHA = 205.0f;
    uint8_t fogAlpha = static_cast<uint8_t>(darkness * NIGHT_MAX_FOG_ALPHA);
    uint8_t r = static_cast<uint8_t>(darkness * 10.0f);
    uint8_t g = static_cast<uint8_t>(darkness * 10.0f);
    uint8_t b = static_cast<uint8_t>(darkness * 30.0f);

    // 전체를 어두운 안개로 채움
    SDL_SetRenderDrawColor(m_renderer, r, g, b, fogAlpha);
    SDL_RenderClear(m_renderer);

    // ── 파라미터 ───────────────────────────────────────────────────────────
    static constexpr float PI      = 3.14159265f;
    static constexpr float DEG2RAD = PI / 180.0f;
    static constexpr float HALF_FOV_DEG  = 60.0f;   // 120° / 2
    static constexpr float GRAD_DEG      = 18.0f;   // 양쪽 그라데이션 폭
    static constexpr int   CONE_SEGS     = 60;      // 부채꼴 세분화 (부드러운 호)
    static constexpr int   GRAD_SEGS     = 10;      // 그라데이션 세분화

    // 화면 대각선보다 충분히 큰 반지름 (시야가 화면 끝까지 닿도록)
    float R = static_cast<float>(std::max(m_screenW, m_screenH)) * 1.5f;
    if (isNight) {
        R = 350.0f; // 밤에는 시야 반경 대폭 축소 (손전등 효과)
    }

    const float aimRad  = aimAngleDeg * DEG2RAD;
    const float halfRad = HALF_FOV_DEG * DEG2RAD;
    const float gradRad = GRAD_DEG * DEG2RAD;

    const float coneStart = aimRad - halfRad;
    const float coneEnd   = aimRad + halfRad;
    const float coneStep  = (coneEnd - coneStart) / CONE_SEGS;

    // ── 2. 메인 시야 부채꼴 — alpha=0 (완전 투명) ─────────────────────────
    // SDL 스크린 좌표계: x = +sin(angle),  y = -cos(angle)  (위=0도)
    SDL_Vertex tri[3];
    tri[0].position  = {static_cast<float>(px), static_cast<float>(py)};
    tri[0].color     = {0, 0, 0, 0};
    tri[0].tex_coord = {0, 0};

    for (int i = 0; i < CONE_SEGS; ++i) {
        float a0 = coneStart + i * coneStep;
        float a1 = coneStart + (i + 1) * coneStep;

        tri[1].position  = {px + std::sin(a0) * R, py - std::cos(a0) * R};
        tri[1].color     = {0, 0, 0, 0};
        tri[1].tex_coord = {0, 0};

        tri[2].position  = {px + std::sin(a1) * R, py - std::cos(a1) * R};
        tri[2].color     = {0, 0, 0, 0};
        tri[2].tex_coord = {0, 0};

        SDL_RenderGeometry(m_renderer, nullptr, tri, 3, nullptr, 0);
    }

    // ── 3. 양쪽 경계 그라데이션 (안개→투명) ──────────────────────────────
    // 각 세그먼트를 부채꼴 바깥쪽(안개 쪽)→안쪽(투명 쪽) 방향으로 그려
    // BLENDMODE_NONE 으로 alpha를 직접 씌워 부드러운 전환을 만든다.
    auto drawGradEdge = [&](float baseAngle, float dir) {
        const float segStep = gradRad / GRAD_SEGS;
        for (int i = 0; i < GRAD_SEGS; ++i) {
            // dir < 0: 왼쪽 경계 (startA 방향으로 나가며 alpha 증가)
            // dir > 0: 오른쪽 경계 (endA 방향으로 나가며 alpha 증가)
            float a0 = baseAngle + dir * i * segStep;
            float a1 = baseAngle + dir * (i + 1) * segStep;

            // 바깥쪽일수록 불투명 (quadratic ease)
            float t = static_cast<float>(i + 1) / GRAD_SEGS;
            uint8_t alpha = static_cast<uint8_t>(t * t * fogAlpha);

            SDL_Vertex ev[3];
            ev[0].position  = {static_cast<float>(px), static_cast<float>(py)};
            ev[0].color     = {0, 0, 0, alpha};
            ev[0].tex_coord = {0, 0};

            ev[1].position  = {px + std::sin(a0) * R, py - std::cos(a0) * R};
            ev[1].color     = {0, 0, 0, alpha};
            ev[1].tex_coord = {0, 0};

            ev[2].position  = {px + std::sin(a1) * R, py - std::cos(a1) * R};
            ev[2].color     = {0, 0, 0, alpha};
            ev[2].tex_coord = {0, 0};

            SDL_RenderGeometry(m_renderer, nullptr, ev, 3, nullptr, 0);
        }
    };

    drawGradEdge(coneStart, -1.0f);   // 왼쪽 경계 (반시계 방향으로 나가며 어두워짐)
    drawGradEdge(coneEnd,    1.0f);   // 오른쪽 경계 (시계 방향으로 나가며 어두워짐)

    // ── 4. 렌더 타깃 복원 + FOW 텍스처 합성 ──────────────────────────────
    SDL_SetRenderTarget(m_renderer, nullptr);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(m_renderer, m_fowTexture, nullptr, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawRemote1 — 단일 원격 엔티티 그리기 (발 기준)
// ─────────────────────────────────────────────────────────────────────────────
static void drawRemoteEntity(Renderer* rnd, SDL_Renderer* r, TTF_Font* /*fSm*/,
                              const RemoteEntityState& rem, const Camera& cam) {
    float t  = std::min(1.0f, rem.interpT / std::max(rem.snapDt, 0.001f));
    float wx = rem.snap[0].x + (rem.snap[1].x - rem.snap[0].x) * t;
    float wy = rem.snap[0].y + (rem.snap[1].y - rem.snap[0].y) * t;
    int sx, sy;
    cam.worldToScreen(wx, wy, sx, sy);

    // 화면 밖 엔티티 컬링 (좀비 다수일 때 렌더링 부하 감소)
    const int CULL_MARGIN = 120;
    if (sx < -CULL_MARGIN || sx > cam.screenW + CULL_MARGIN ||
        sy < -CULL_MARGIN || sy > cam.screenH + CULL_MARGIN) return;

    bool bleed    = (rem.snap[1].statusFlags & STATUS_BLEEDING) != 0;
    bool onFire   = (rem.snap[1].statusFlags & STATUS_ON_FIRE)  != 0;
    bool isZombie = (rem.recType == REC_ZOMBIE);

    // 방향 계산 (이동 벡터 기준)
    auto movDir = [](float dx, float dy) -> const char* {
        if (std::abs(dx) < 0.3f && std::abs(dy) < 0.3f) return "_S";
        float a = std::atan2(dy, dx) * (180.0f / 3.14159265f);
        if (a >= -45 && a < 45)   return "_E";
        if (a >= 45  && a < 135)  return "_S";
        if (a >= -135&& a < -45)  return "_N";
        return "_W";
    };
    // 스프라이트 렌더 헬퍼: 중앙 하단 기준으로 그림
    auto drawSprite = [&](SDL_Texture* tex, int cx, int cy, int sz, uint8_t alpha=255) -> bool {
        if (!tex) return false;
        int h = sz * 3;
        int w = h * 167 / 224;
        SDL_Rect dst = {cx - w/2, cy - h + sz/3, w, h};
        SDL_SetTextureAlphaMod(tex, alpha);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
        return true;
    };

    if (isZombie) {
        uint8_t zt = (rem.snap[1].statusFlags >> 5) & 0x03; // bits5-6 (STATUS_DEAD=bit7 충돌 회피)
        int sz;
        SDL_Color bc;
        if (zt == ZT_BRUTE)       { sz = static_cast<int>(30*cam.zoom); bc = {40,130,40,255}; }
        else if (zt == ZT_RUNNER) { sz = static_cast<int>(18*cam.zoom); bc = {140,240,60,255}; }
        else                      { sz = static_cast<int>(22*cam.zoom); bc = {55,210,55,255}; }
        float dx = rem.snap[1].x - rem.snap[0].x;
        float dy = rem.snap[1].y - rem.snap[0].y;
        float aimAngle = (dx != 0 || dy != 0) ? std::atan2(dy, dx) * (180.0f / 3.14159265f) : 0.0f;

        // 애니메이션 스프라이트
        const char* dirs[] = {"S","N","E","W","D"};
        float spd = std::sqrt(dx*dx + dy*dy);
        bool zMoving = spd > 0.5f;
        int zDir = 0; // S default
        if (zMoving) {
            float a = std::atan2(dy, dx) * (180.0f / 3.14159265f);
            if (a>=-45&&a<45) zDir=2; else if (a>=45&&a<135) zDir=0;
            else if (std::abs(a)>=135) zDir=3; else zDir=1;
        }
        int zFrame = zMoving ? (static_cast<int>(SDL_GetTicks()/125) % 3 + 1) : 0;
        const char* zType = (zt==ZT_BRUTE) ? "brute" : (zt==ZT_RUNNER) ? "runner" : "shambler";
        bool isDead2 = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        std::string sprKey;
        uint8_t sprAlpha = 255;
        if (isDead2) {
            // 사망 애니: 300ms/프레임으로 0→1→2→3 천천히, 이후 frame3 고정
            // entityID로 스태거 → 모든 좀비가 동시에 같은 프레임 아님
            int deathFrame = std::min(3, static_cast<int>((SDL_GetTicks()/300 + rem.entityID * 7) % 40));
            sprKey = std::string("char_")+zType+"_D_"+std::to_string(deathFrame);
            // 시간이 지날수록 서서히 어두워짐 (시체 느낌)
            uint32_t age = (SDL_GetTicks() / 300 + rem.entityID * 7) % 40;
            sprAlpha = age >= 4 ? 160 : static_cast<uint8_t>(255 - age * 24);
        } else {
            sprKey = std::string("char_")+zType+"_"+dirs[zDir]+"_"+std::to_string(zFrame);
        }
        SDL_Texture* zTex = rnd->textures().get(sprKey);
        bool usedSprite = drawSprite(zTex, sx, sy, sz, sprAlpha);
        
        bool isDead = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        if (isDead) {
            bc.r /= 2; bc.g /= 2; bc.b /= 2;
            aimAngle += 90.0f; // lie down
        }

        float animT = SDL_GetTicks() * 0.005f + rem.entityID * 0.1f;
        if (!usedSprite)
            drawCharacterBody(rnd, sx, sy, sz, aimAngle + 90.0f, bc, bleed, true, animT);

        int bodyH = sz*13/10;
        int bodyTop = sy - bodyH;

        // HP바
        if (!isDead) {
            float hpPct2 = rem.snap[1].hp / std::max(1.0f, rem.maxHp);
            hpPct2 = std::max(0.0f, std::min(1.0f, hpPct2));
            int barW = sz + 4;
            int barX = sx - barW/2;
            int barY2 = bodyTop - 8;
            SDL_SetRenderDrawColor(r, 30, 30, 30, 200);
            SDL_Rect bg = {barX, barY2, barW, 4};
            SDL_RenderFillRect(r, &bg);
            SDL_Color hc = (hpPct2 > 0.5f) ? SDL_Color{60,220,60,255}
                         : (hpPct2 > 0.25f) ? SDL_Color{220,180,30,255}
                         :                    SDL_Color{220,50,50,255};
            SDL_SetRenderDrawColor(r, hc.r, hc.g, hc.b, 255);
            SDL_Rect fill = {barX, barY2, static_cast<int>(barW*hpPct2), 4};
            SDL_RenderFillRect(r, &fill);
        }

        if (onFire) {
            float f = SDL_GetTicks()*0.01f;
            uint8_t fa = 140+static_cast<uint8_t>(std::sin(f)*60);
            rnd->drawFilledCircle(sx, sy - sz, sz, {255, 80, 0, fa});
        }
        if (bleed) {
            SDL_SetRenderDrawColor(r, 180,0,0,120);
            SDL_Rect br = {sx-sz/2-2, bodyTop-2, sz+4, bodyH+4};
            SDL_RenderDrawRect(r, &br);
        }
    } else {
        // 타 플레이어 — drawCharacterBody 재사용
        int sz = static_cast<int>(22*cam.zoom);
        SDL_Color oc = {225,135,38,255};
        
        float dx = rem.snap[1].x - rem.snap[0].x;
        float dy = rem.snap[1].y - rem.snap[0].y;
        float aimAngle = (dx != 0 || dy != 0) ? std::atan2(dy, dx) * (180.0f / 3.14159265f) : 0.0f;
        
        bool isDead = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        if (isDead) {
            oc.r /= 2; oc.g /= 2; oc.b /= 2;
            aimAngle += 90.0f;
        }

        float animT = SDL_GetTicks() * 0.005f + rem.entityID * 0.1f;
        bool pMoving = (std::abs(dx)+std::abs(dy)) > 0.5f;
        int pDir = 0;
        if (pMoving) {
            float a = std::atan2(dy, dx) * (180.0f/3.14159265f);
            if (a>=-45&&a<45) pDir=2; else if (a>=45&&a<135) pDir=0;
            else if (std::abs(a)>=135) pDir=3; else pDir=1;
        }
        int pFrame = pMoving ? (static_cast<int>(SDL_GetTicks()/125)%3+1) : 0;
        static const char* pdirs[] = {"S","N","E","W"};
        std::string pKey = std::string("char_player_")+pdirs[pDir]+"_"+std::to_string(pFrame);
        SDL_Texture* pTex = rnd->textures().get(pKey);
        if (!drawSprite(pTex, sx, sy, sz))
            drawCharacterBody(rnd, sx, sy, sz, aimAngle + 90.0f, oc, bleed, false, animT);
        if (onFire) {
            rnd->drawFilledCircle(sx, sy - sz, sz, {255, 80, 0, 150});
        }
    }
}

void Renderer::drawRemotes(const NetworkClient& net, const Camera& cam) {
    for (int i = 0; i < net.remoteCount(); ++i) {
        const auto& rem = net.remotes()[i];
        // 좌비는 죽어도 시체로 표시, 플레이어는 죽으면 숨김
        bool isDead = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        if (isDead && rem.recType != REC_ZOMBIE) continue;
        drawRemoteEntity(this, m_renderer, nullptr, rem, cam);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawWorldEntities — 로컬 + 원격 y-sort 통합 드로잉 (입체감 핵심)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawWorldEntities(const NetworkClient& net, const Camera& cam,
                                 const class TileMap* map,
                                 const std::vector<LootBoxView>& lootBoxes,
                                 float localX, float localY, float aimAngle,
                                 float hpPct, bool bleeding, int teamID,
                                 const std::string& wName,
                                 const std::string& wGrade,
                                 float attackTimer, float attackAngle,
                                 int charDir, bool charMoving) {
    enum class EntryType { LocalPlayer, RemoteEntity, LootBox, BuildingOverlay };
    struct Entry { float wy; EntryType type; int idx; };
    std::vector<Entry> list;
    list.reserve(net.remoteCount() + lootBoxes.size() + (map ? map->getBuildings().size() : 0) + 1);

    for (int i = 0; i < net.remoteCount(); ++i) {
        const auto& rem = net.remotes()[i];
        bool isDead = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        // 좌비는 죽어도 y-sort에 포함 (시체 렌더링 위해)
        if (isDead && rem.recType != REC_ZOMBIE) continue;
        if (rem.recType == REC_BUILDING || rem.recType == REC_LOOT) continue;
        float t  = std::min(1.0f, rem.interpT / std::max(rem.snapDt, 0.001f));
        float wy = rem.snap[0].y + (rem.snap[1].y - rem.snap[0].y) * t;
        list.push_back({wy, EntryType::RemoteEntity, i});
    }
    list.push_back({localY, EntryType::LocalPlayer, -1});

    for (int i = 0; i < static_cast<int>(lootBoxes.size()); ++i) {
        list.push_back({lootBoxes[i].wy, EntryType::LootBox, i});
    }

    if (map) {
        const auto& bldgs = map->getBuildings();
        for (size_t i = 0; i < bldgs.size(); ++i) {
            float wy = (bldgs[i].y + bldgs[i].h) * TILE_SIZE;
            list.push_back({wy, EntryType::BuildingOverlay, static_cast<int>(i)});
        }
    }

    std::sort(list.begin(), list.end(),
              [](const Entry& a, const Entry& b){ return a.wy < b.wy; });

    // ── 지붕 은폐 헬퍼: 엔티티가 어느 건물 안에 있는지 반환 (-1 = 야외) ──────
    auto getBuildingIdx = [&](float wx, float wy) -> int {
        if (!map) return -1;
        int tx = static_cast<int>(wx / TILE_SIZE);
        int ty = static_cast<int>(wy / TILE_SIZE);
        const auto& bldgs = map->getBuildings();
        for (int bi = 0; bi < static_cast<int>(bldgs.size()); ++bi) {
            const auto& b = bldgs[bi];
            if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h)
                return bi;
        }
        return -1;
    };
    auto canSeeBuilding = [&](int buildingIdx) -> bool {
        if (!map || buildingIdx < 0) return true;
        const auto& b = map->getBuildings()[buildingIdx];
        const float margin = TILE_SIZE * 2.0f;
        const float bx = b.x * TILE_SIZE;
        const float by = b.y * TILE_SIZE;
        const float bw = b.w * TILE_SIZE;
        const float bh = b.h * TILE_SIZE;
        return localX >= bx - margin && localX <= bx + bw + margin &&
               localY >= by - margin && localY <= by + bh + margin;
    };
    int localBuildingIdx = getBuildingIdx(localX, localY);

    for (const auto& e : list) {
        if (e.type == EntryType::LocalPlayer) {
            drawLocalPlayer(localX, localY, aimAngle, hpPct, bleeding,
                            teamID, cam, wName, wGrade, attackTimer, attackAngle,
                            charDir, charMoving);
        } else if (e.type == EntryType::RemoteEntity) {
            const auto& rem = net.remotes()[e.idx];
            float t  = std::min(1.0f, rem.interpT / std::max(rem.snapDt, 0.001f));
            float wx = rem.snap[0].x + (rem.snap[1].x - rem.snap[0].x) * t;
            float wy = rem.snap[0].y + (rem.snap[1].y - rem.snap[0].y) * t;
            // 지붕 은폐: 좀비가 건물 안에 있으면 로컬 플레이어도 같은 건물 안에 있어야 표시
            if (rem.recType == REC_ZOMBIE) {
                int zi = getBuildingIdx(wx, wy);
                if (zi >= 0 && zi != localBuildingIdx && !canSeeBuilding(zi)) continue;
            }
            drawRemoteEntity(this, m_renderer, nullptr, rem, cam);
        } else if (e.type == EntryType::LootBox) {
            const auto& box = lootBoxes[e.idx];
            // 지붕 은폐: 집 안/근처에서는 내부 루트박스를 표시한다.
            int li = getBuildingIdx(box.wx, box.wy);
            if (li >= 0 && li != localBuildingIdx && !canSeeBuilding(li)) continue;
            drawLootBox(box, cam);
        } else if (e.type == EntryType::BuildingOverlay && map) {
            drawBuildingOverlay(map->getBuildings()[e.idx], cam);
        }
    }


    // ── 포탑 총알 트레이서 렌더링 (가장 위에 오버레이) ──────────────────────────────────
    for (const auto& beam : net.turretBeams()) {
        int sx, sy, ex, ey;
        cam.worldToScreen(beam.fromX, beam.fromY - 16.0f, sx, sy); // 포탑 총신 높이 보정
        cam.worldToScreen(beam.toX,   beam.toY   - 16.0f, ex, ey);

        // 팀 컬러 기반 총알 색상 설정 (0이면 중립 주황/노랑)
        SDL_Color c = (beam.ownerTeam > 0 && beam.ownerTeam <= 4) 
                      ? Col::TEAM[beam.ownerTeam] 
                      : SDL_Color{255, 200, 50, 255};
        
        // TTL(0.15초)을 이용하여 총알이 날아가는 위치(progress) 계산
        float ttlMax = 0.15f;
        float progress = 1.0f - (beam.ttl / ttlMax);
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        
        // 트레이서 선분 길이 (전체 거리의 20% 또는 최대 길이)
        float tracerLen = 0.2f;
        float startP = progress - tracerLen;
        float endP   = progress;
        if (startP < 0.0f) startP = 0.0f;
        
        int drawStartX = sx + static_cast<int>((ex - sx) * startP);
        int drawStartY = sy + static_cast<int>((ey - sy) * startP);
        int drawEndX   = sx + static_cast<int>((ex - sx) * endP);
        int drawEndY   = sy + static_cast<int>((ey - sy) * endP);

        uint8_t a = static_cast<uint8_t>(255.0f * (1.0f - progress)); // 끝으로 갈수록 희미해짐
        
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        
        // 머즐 플래시 (총구 화염)
        if (progress < 0.2f) {
            float flashA = (0.2f - progress) / 0.2f;
            drawFilledCircle(sx, sy, 4, {255, 220, 100, static_cast<uint8_t>(200 * flashA)});
        }

        // 트레이서 그리기
        SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, a);
        for (int offY = -1; offY <= 1; ++offY) {
            for (int offX = -1; offX <= 1; ++offX) {
                if (std::abs(offX) + std::abs(offY) > 1) continue; // 십자선 형태
                SDL_RenderDrawLine(m_renderer, drawStartX + offX, drawStartY + offY, drawEndX + offX, drawEndY + offY);
            }
        }
        
        // 임팩트 스파크 (표적 위치 도달 시)
        if (progress > 0.8f) {
            float sparkA = (progress - 0.8f) / 0.2f;
            drawFilledCircle(ex, ey, static_cast<int>(3.0f * sparkA), {255, 255, 200, static_cast<uint8_t>(200 * sparkA)});
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD — 타르코프 스타일 전술 서바이벌 게임 UI
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawHUD(float hp, float maxHp, float stamina, float maxStamina, bool bleeding,
                        float extractProg, int teamID,
                        float extractCountdown, const std::string& teamName,
                        const std::string& weaponName,
                        const std::string& weaponGrade,
                        const int teamAlive[4],
                        uint8_t allianceBits,
                        float gameTime,
                        bool isReloading) {
    TTF_Font* fTiny = m_fonts.get(11);
    TTF_Font* fSm   = m_fonts.get(13);
    TTF_Font* fMd   = m_fonts.get(16);
    TTF_Font* fLg   = m_fonts.get(20);
    TTF_Font* fMono = m_fonts.mono(13);
    SDL_Color tc = Col::TEAM[std::max(0, std::min(4, teamID))];

    float pct  = maxHp > 0 ? hp / maxHp : 0.0f;
    float spct = maxStamina > 0 ? stamina / maxStamina : 0.0f;
    uint32_t ticks = SDL_GetTicks();
    float pulse = std::sin(ticks * 0.012f) * 0.5f + 0.5f;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // ══════════════════════════════════════════════════════════════
    // 상단 낮/밤 시계 UI (Day X - Time)
    // ══════════════════════════════════════════════════════════════
    {
        float timeOfDay = std::fmod(gameTime, 180.0f);
        int currentDay = static_cast<int>(gameTime / 180.0f) + 1;
        bool isNight = timeOfDay > 120.0f;

        // 시간 포맷 (0~180초를 06:00 ~ 익일 06:00 로 변환)
        // 낮(120초) = 12시간 (06:00 ~ 18:00), 밤(60초) = 12시간 (18:00 ~ 06:00)
        int hours, minutes;
        if (!isNight) {
            float dayT = timeOfDay / 120.0f;
            float timeH = 6.0f + dayT * 12.0f;
            hours = static_cast<int>(timeH);
            minutes = static_cast<int>((timeH - hours) * 60.0f);
        } else {
            float nightT = (timeOfDay - 120.0f) / 60.0f;
            float timeH = 18.0f + nightT * 12.0f;
            hours = static_cast<int>(timeH) % 24;
            minutes = static_cast<int>((timeH - static_cast<int>(timeH)) * 60.0f);
        }
        
        char clockStr[64];
        std::snprintf(clockStr, sizeof(clockStr), "DAY %d - %02d:%02d", currentDay, hours, minutes);

        SDL_Color clockCol = isNight ? SDL_Color{220, 60, 60, 255} : SDL_Color{220, 220, 220, 255};
        
        int bw = 180, bh = 40;
        int bx = m_screenW / 2 - bw / 2;
        int by = 10;
        
        SDL_SetRenderDrawColor(m_renderer, 10, 10, 15, 220);
        SDL_Rect bg = {bx, by, bw, bh};
        SDL_RenderFillRect(m_renderer, &bg);
        
        SDL_SetRenderDrawColor(m_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(m_renderer, &bg);
        
        drawTextShadow(clockStr, bx + bw / 2, by + bh / 2, clockCol, {0,0,0,150}, fMd, true);
        
        // 상태 텍스트 (정중앙 팝업)
        if (isNight) {
            if (timeOfDay < 125.0f) {
                uint8_t a = static_cast<uint8_t>(std::max(0.0f, (1.0f - (timeOfDay - 120.0f) / 5.0f)) * 255);
                drawTextShadow("NIGHT WAVE STARTED!", m_screenW / 2, m_screenH / 4, {255, 30, 30, a}, {0,0,0,a}, fLg, true);
            }
        } else {
            if (timeOfDay < 5.0f && currentDay > 1) {
                uint8_t a = static_cast<uint8_t>(std::max(0.0f, (1.0f - timeOfDay / 5.0f)) * 255);
                drawTextShadow("SURVIVED THE NIGHT", m_screenW / 2, m_screenH / 4, {60, 255, 60, a}, {0,0,0,a}, fLg, true);
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    // 출혈 비네트 (화면 가장자리 붉게 — 드라마틱하게)
    // ══════════════════════════════════════════════════════════════
    if (bleeding) {
        uint8_t va = static_cast<uint8_t>(40 + pulse * 80);
        // 4면 그라데이션 줄무늬
        for (int i = 0; i < 60; ++i) {
            float t = 1.0f - static_cast<float>(i) / 60.0f;
            uint8_t a = static_cast<uint8_t>(t * t * va);
            SDL_SetRenderDrawColor(m_renderer, 180, 0, 0, a);
            SDL_Rect edges[] = {
                {i, i, m_screenW - 2*i, 1},
                {i, m_screenH - 1 - i, m_screenW - 2*i, 1},
                {i, i, 1, m_screenH - 2*i},
                {m_screenW - 1 - i, i, 1, m_screenH - 2*i}
            };
            for (auto& e : edges) SDL_RenderFillRect(m_renderer, &e);
        }
        // 경고 텍스트 (맥박처럼 깜빡임)
        uint8_t ta = static_cast<uint8_t>(180 + pulse * 75);
        drawTextShadow("● BLEEDING", m_screenW / 2, 40, {220, 30, 30, ta}, {0, 0, 0, 150}, fMd, true);
    }

    // ══════════════════════════════════════════════════════════════
    // HP / 스태미나 — 좌하단 전술 HUD 패널
    // ══════════════════════════════════════════════════════════════
    {
        const int PX = 16, PY = m_screenH - 120;
        const int PW = 240, PH = 100;

        // 패널 배경 (사선 테두리 — 군사 스타일)
        SDL_SetRenderDrawColor(m_renderer, 6, 8, 14, 210);
        SDL_Rect bg = {PX, PY, PW, PH};
        SDL_RenderFillRect(m_renderer, &bg);

        // 좌측 팀 컬러 바
        SDL_SetRenderDrawColor(m_renderer, tc.r, tc.g, tc.b, 200);
        SDL_Rect tcBar = {PX, PY, 3, PH};
        SDL_RenderFillRect(m_renderer, &tcBar);

        // 상단 구분선
        SDL_SetRenderDrawColor(m_renderer, tc.r, tc.g, tc.b, 80);
        SDL_Rect topLine = {PX + 3, PY, PW - 3, 1};
        SDL_RenderFillRect(m_renderer, &topLine);

        // 팀 이름 라벨
        const char* tn = teamName.empty() ? "SQUAD" : teamName.c_str();
        drawText(tn, PX + 12, PY + 6, tc, fTiny);

        // HP 아이콘 + 수치
        SDL_Color hcol = pct > 0.5f ? SDL_Color{60, 220, 80, 255}
                       : pct > 0.25f ? SDL_Color{255, 180, 30, 255}
                       : SDL_Color{255, 55, 40, 255};

        // 큰 HP 수치
        char hpBuf[16];
        std::snprintf(hpBuf, sizeof(hpBuf), "%d", static_cast<int>(hp));
        drawText("HP", PX + 12, PY + 22, {80, 90, 100, 255}, fTiny);
        drawText(hpBuf, PX + 36, PY + 18, hcol, fLg);

        // HP 바 (섬세한 스타일)
        const int BX = PX + 12, BY = PY + 46;
        const int BW = PW - 24, BH = 8;
        SDL_SetRenderDrawColor(m_renderer, 20, 24, 32, 255);
        SDL_Rect hpBg = {BX, BY, BW, BH};
        SDL_RenderFillRect(m_renderer, &hpBg);
        // 채워진 부분
        SDL_SetRenderDrawColor(m_renderer, hcol.r, hcol.g, hcol.b, 230);
        SDL_Rect hpFill = {BX, BY, static_cast<int>(BW * pct), BH};
        SDL_RenderFillRect(m_renderer, &hpFill);
        // 밝은 하이라이트 (상단 1px)
        SDL_SetRenderDrawColor(m_renderer, std::min(255, hcol.r + 60), std::min(255, hcol.g + 60), std::min(255, hcol.b + 60), 180);
        SDL_Rect hpHL = {BX, BY, static_cast<int>(BW * pct), 1};
        SDL_RenderFillRect(m_renderer, &hpHL);
        // 테두리
        SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 255);
        SDL_RenderDrawRect(m_renderer, &hpBg);

        // 스태미나 바 (작은 청색)
        const int SY = BY + 14;
        const int SH = 4;
        SDL_SetRenderDrawColor(m_renderer, 14, 18, 28, 255);
        SDL_Rect spBg = {BX, SY, BW, SH};
        SDL_RenderFillRect(m_renderer, &spBg);
        SDL_SetRenderDrawColor(m_renderer, 50, 160, 255, 200);
        SDL_Rect spFill = {BX, SY, static_cast<int>(BW * spct), SH};
        SDL_RenderFillRect(m_renderer, &spFill);
        drawText("STA", PX + 12, SY + 6, {50, 100, 160, 180}, fTiny);

        // 최대HP / 현재HP 수치 (작게 우측)
        char hpMaxBuf[24];
        std::snprintf(hpMaxBuf, sizeof(hpMaxBuf), "/ %d", static_cast<int>(maxHp));
        drawText(hpMaxBuf, PX + 36 + 60, PY + 22, {80, 90, 100, 200}, fSm);

        // 하단 외곽선
        SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 180);
        SDL_Rect outline = {PX, PY, PW, PH};
        SDL_RenderDrawRect(m_renderer, &outline);
    }

    // ══════════════════════════════════════════════════════════════
    // 무기 정보 — 우하단 (타르코프 스타일)
    // ══════════════════════════════════════════════════════════════
    {
        const int WW = 220, WH = 90;
        const int WX = m_screenW - WW - 16, WY = m_screenH - WH - 16;
        SDL_Color wc = gradeColor(weaponGrade);

        // 배경
        SDL_SetRenderDrawColor(m_renderer, 6, 8, 14, 210);
        SDL_Rect wBg = {WX, WY, WW, WH};
        SDL_RenderFillRect(m_renderer, &wBg);

        // 우측 등급 색 바
        SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 200);
        SDL_Rect gradeBar = {WX + WW - 3, WY, 3, WH};
        SDL_RenderFillRect(m_renderer, &gradeBar);

        // 상단 구분선
        SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 80);
        SDL_Rect wTopLine = {WX, WY, WW - 3, 1};
        SDL_RenderFillRect(m_renderer, &wTopLine);

        // 무기 실루엣
        SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 50);
        if (weaponName == "flamethrower") {
            SDL_Rect tank   = {WX + 14, WY + 30, 34, 44};
            SDL_Rect hose   = {WX + 48, WY + 45, 42, 8};
            SDL_Rect nozzle = {WX + 88, WY + 38, 50, 12};
            SDL_Rect grip   = {WX + 72, WY + 53, 12, 20};
            SDL_RenderFillRect(m_renderer, &tank);
            SDL_RenderFillRect(m_renderer, &hose);
            SDL_RenderFillRect(m_renderer, &nozzle);
            SDL_RenderFillRect(m_renderer, &grip);
            SDL_SetRenderDrawColor(m_renderer, 255, 110, 35, 130);
            SDL_Rect flame = {WX + 138, WY + 36, 18, 16};
            SDL_RenderFillRect(m_renderer, &flame);
            SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 120);
            SDL_RenderDrawRect(m_renderer, &tank);
            SDL_RenderDrawRect(m_renderer, &nozzle);
        } else if (!weaponName.empty() &&
                   (weaponName.find("axe") != std::string::npos ||
                    weaponName.find("bat") != std::string::npos ||
                    weaponName.find("pipe") != std::string::npos)) {
            SDL_Rect handle = {WX + 72, WY + 26, 12, 56};
            SDL_Rect head   = {WX + 52, WY + 24, 44, 14};
            SDL_RenderFillRect(m_renderer, &handle);
            SDL_RenderFillRect(m_renderer, &head);
            SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 120);
            SDL_RenderDrawRect(m_renderer, &handle);
            SDL_RenderDrawRect(m_renderer, &head);
        } else {
            SDL_Rect gunBody  = {WX + 12, WY + 30, 90, 18};
            SDL_Rect gunGrip  = {WX + 70, WY + 48, 14, 22};
            SDL_Rect gunBarrel= {WX + 102, WY + 33, 30, 10};
            SDL_Rect gunMag   = {WX + 38, WY + 48, 10, 20};
            SDL_RenderFillRect(m_renderer, &gunBody);
            SDL_RenderFillRect(m_renderer, &gunGrip);
            SDL_RenderFillRect(m_renderer, &gunBarrel);
            SDL_RenderFillRect(m_renderer, &gunMag);
            SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 120);
            SDL_RenderDrawRect(m_renderer, &gunBody);
        }

        // 무기명
        const std::string& wname = weaponName.empty() ? std::string("--- 비무장 ---") : weaponName;
        drawText(wname.c_str(), WX + 12, WY + 8, Col::TEXT_HI, fMd);

        // 등급 뱃지
        const char* gradeKr = "COMMON";
        SDL_Color gc = {90, 100, 110, 255};
        if      (weaponGrade == "enhanced") { gradeKr = "ENHANCED"; gc = {80, 200, 255, 255}; }
        else if (weaponGrade == "rare")     { gradeKr = "RARE";     gc = {180, 60, 255, 255}; }
        else if (weaponGrade == "unique")   { gradeKr = "UNIQUE";   gc = {255, 200, 40, 255}; }

        // 등급 뱃지 박스
        int tw = 0; TTF_SizeText(fTiny, gradeKr, &tw, nullptr);
        SDL_SetRenderDrawColor(m_renderer, gc.r, gc.g, gc.b, 40);
        SDL_Rect gbadge = {WX + 12, WY + 68, tw + 8, 14};
        SDL_RenderFillRect(m_renderer, &gbadge);
        SDL_SetRenderDrawColor(m_renderer, gc.r, gc.g, gc.b, 160);
        SDL_RenderDrawRect(m_renderer, &gbadge);
        drawText(gradeKr, WX + 16, WY + 69, gc, fTiny);

        // 외곽선
        SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 180);
        SDL_RenderDrawRect(m_renderer, &wBg);
    }

    // ══════════════════════════════════════════════════════════════
    // 팀 상태 패널 — 우상단 (콤팩트)
    // ══════════════════════════════════════════════════════════════
    if (teamAlive) {
        static const char* tnames[] = {"", "ALPHA", "BRAVO", "CHARLIE", "DELTA"};
        static const int pairBitA[] = {1, 1, 1, 2, 2, 3};
        static const int pairBitB[] = {2, 3, 4, 3, 4, 4};

        const int TPW = 160, TPH = 4 * 24 + 20;
        int tpx = m_screenW - TPW - 16, tpy = 16;

        // 배경
        SDL_SetRenderDrawColor(m_renderer, 6, 8, 14, 200);
        SDL_Rect tBg = {tpx, tpy, TPW, TPH};
        SDL_RenderFillRect(m_renderer, &tBg);

        // 헤더
        drawText("TEAMS", tpx + TPW / 2, tpy + 4, {80, 90, 100, 220}, fTiny, true);
        SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 200);
        SDL_Rect divLine = {tpx, tpy + 16, TPW, 1};
        SDL_RenderFillRect(m_renderer, &divLine);

        int ry = tpy + 20;
        for (int t = 1; t <= 4; ++t) {
            SDL_Color ttc = Col::TEAM[t];
            bool isMe = (t == teamID);
            bool isDead = (teamAlive[t-1] == 0);

            bool allied = false;
            for (int k = 0; k < 6 && !allied; ++k)
                if ((pairBitA[k] == teamID && pairBitB[k] == t) ||
                    (pairBitB[k] == teamID && pairBitA[k] == t))
                    if (allianceBits & (1 << k)) allied = true;

            // 행 배경 (내 팀은 더 밝게)
            if (isMe) {
                SDL_SetRenderDrawColor(m_renderer, ttc.r / 5, ttc.g / 5, ttc.b / 5, 180);
                SDL_Rect row = {tpx, ry, TPW, 22};
                SDL_RenderFillRect(m_renderer, &row);
            }

            // 팀 컬러 도트
            uint8_t dotA = isDead ? 80 : 230;
            drawFilledCircle(tpx + 10, ry + 11, 4, {ttc.r, ttc.g, ttc.b, dotA});

            // 팀 이름
            SDL_Color nameCol = isDead ? SDL_Color{50, 55, 65, 200}
                              : isMe   ? ttc
                              : Col::TEXT_HI;
            drawText(tnames[t], tpx + 22, ry + 4, nameCol, fSm);

            // 생존 인원
            if (isDead) {
                drawText("X", tpx + TPW - 24, ry + 4, {80, 40, 40, 200}, fSm);
            } else {
                char alive[8];
                std::snprintf(alive, sizeof(alive), "%d명", teamAlive[t-1]);
                drawText(alive, tpx + TPW - 30, ry + 4, {100, 110, 120, 200}, fTiny);
            }

            // 연합 뱃지
            if (allied && !isDead) {
                drawText("♦", tpx + 90, ry + 4, {80, 230, 120, 200}, fTiny);
            }
            if (isMe) {
                drawText("◄", tpx + TPW - 14, ry + 4, tc, fTiny);
            }

            // 구분선
            SDL_SetRenderDrawColor(m_renderer, 25, 30, 42, 150);
            SDL_Rect sl = {tpx + 8, ry + 22, TPW - 16, 1};
            SDL_RenderFillRect(m_renderer, &sl);

            ry += 24;
        }
        // 외곽선
        SDL_SetRenderDrawColor(m_renderer, 35, 42, 58, 200);
        SDL_RenderDrawRect(m_renderer, &tBg);
    }

    // ══════════════════════════════════════════════════════════════
    // 탈출 채널링 / 카운트다운 — 상단 중앙
    // ══════════════════════════════════════════════════════════════
    if (extractProg > 0.0f) {
        const int EW = 280, EH = 16;
        int ex = m_screenW / 2 - EW / 2, ey = 50;
        // 배경 패널
        SDL_SetRenderDrawColor(m_renderer, 6, 8, 14, 220);
        SDL_Rect eBg = {ex - 12, ey - 28, EW + 24, EH + 44};
        SDL_RenderFillRect(m_renderer, &eBg);
        SDL_SetRenderDrawColor(m_renderer, 60, 200, 100, 150);
        SDL_RenderDrawRect(m_renderer, &eBg);

        // 아이콘 + 텍스트
        float blink = std::sin(ticks * 0.025f) * 0.5f + 0.5f;
        uint8_t ba = static_cast<uint8_t>(180 + 75 * blink);
        drawText("▲  EXTRACTING", m_screenW / 2, ey - 18, {60, 220, 100, ba}, fMd, true);
        drawHpBar(ex, ey, extractProg, EW, EH);

        // 진행률 %
        char pctBuf[16];
        std::snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", extractProg * 100.f);
        drawText(pctBuf, m_screenW / 2, ey + EH + 6, {60, 200, 100, 200}, fSm, true);

    } else if (extractCountdown > 0.0f) {
        int mins = static_cast<int>(extractCountdown) / 60;
        int secs = static_cast<int>(extractCountdown) % 60;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "EXTRACT IN  %d:%02d", mins, secs);
        SDL_Color cc = extractCountdown < 60.0f ? Col::WARN : Col::TEXT_LO;

        SDL_SetRenderDrawColor(m_renderer, 6, 8, 14, 200);
        SDL_Rect eBg = {m_screenW / 2 - 134, 50, 268, 26};
        SDL_RenderFillRect(m_renderer, &eBg);
        SDL_SetRenderDrawColor(m_renderer, cc.r, cc.g, cc.b, 80);
        SDL_RenderDrawRect(m_renderer, &eBg);
        drawText(buf, m_screenW / 2, 57, cc, fMono, true);
    }

    // ══════════════════════════════════════════════════════════════
    // 장전 중 표시 — 화면 중앙 하단
    // ══════════════════════════════════════════════════════════════
    if (isReloading) {
        int rw = 120, rh = 12;
        int rx = m_screenW / 2 - rw / 2;
        int ry = m_screenH / 2 + 60;
        
        // 배경 패널
        SDL_SetRenderDrawColor(m_renderer, 10, 15, 25, 200);
        SDL_Rect rBg = {rx - 10, ry - 20, rw + 20, rh + 26};
        SDL_RenderFillRect(m_renderer, &rBg);
        
        float blink = std::sin(ticks * 0.015f) * 0.5f + 0.5f;
        uint8_t ba = static_cast<uint8_t>(150 + 105 * blink);
        drawText("RELOADING...", m_screenW / 2, ry - 10, {200, 200, 200, ba}, fSm, true);
        
        // 게이지 (단순 점멸)
        SDL_SetRenderDrawColor(m_renderer, 40, 50, 60, 255);
        SDL_Rect barBg = {rx, ry + 4, rw, rh};
        SDL_RenderFillRect(m_renderer, &barBg);
        
        SDL_SetRenderDrawColor(m_renderer, 150, 180, 200, ba);
        SDL_Rect barFill = {rx, ry + 4, static_cast<int>(rw * blink), rh};
        SDL_RenderFillRect(m_renderer, &barFill);
        
        SDL_SetRenderDrawColor(m_renderer, 100, 120, 140, 200);
        SDL_RenderDrawRect(m_renderer, &barBg);
    }

    // ══════════════════════════════════════════════════════════════
    // 하단 중앙 상태 힌트 (인벤토리 키 등)
    // ══════════════════════════════════════════════════════════════
    {
        const int HY = m_screenH - 18;
        drawText("[I] 인벤토리", m_screenW / 2 - 160, HY, Col::TEXT_LO, fTiny);
        drawText("[F] 상호작용", m_screenW / 2 - 40,  HY, Col::TEXT_LO, fTiny);
        drawText("[M] 지도",     m_screenW / 2 + 80,  HY, Col::TEXT_LO, fTiny);
    }
}






// ─────────────────────────────────────────────────────────────────────────────
// 인벤토리 UI (I 키) — 드래그 앤 드롭 지원
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawInventory(const ClientInventory& inv, int mouseX, int mouseY,
                              const InventoryItem* dragItem, bool showStash) {
    TTF_Font* fSm  = m_fonts.get(13);
    TTF_Font* fMd  = m_fonts.get(17);
    TTF_Font* fLg  = m_fonts.get(22);
    TTF_Font* fMono= m_fonts.mono(12);

    // 반투명 전체 오버레이
    SDL_SetRenderDrawColor(m_renderer, 0,0,0,175);
    SDL_Rect full = {0,0,m_screenW,m_screenH};
    SDL_RenderFillRect(m_renderer, &full);

    // 메인 패널
    const int panW = 760;
    const int panH = 500;
    
    // Stash 패널 크기
    const int stW = 280;
    const int stH = 500;
    const int gap = 20;
    
    int totalW = showStash ? (panW + gap + stW) : panW;
    int panX = m_screenW/2 - totalW/2;
    int panY = m_screenH/2 - panH/2;

    // ── Player (장비+가방) 패널 ──────────────────────────────────────────────────
    drawPanel(panX, panY, panW, panH, {14,17,26,250}, Col::BORDER, 2);

    // 헤더 바
    SDL_SetRenderDrawColor(m_renderer, 20,25,38,255);
    SDL_Rect hdr = {panX, panY, panW, 40};
    SDL_RenderFillRect(m_renderer, &hdr);
    SDL_SetRenderDrawColor(m_renderer, Col::ACCENT.r, Col::ACCENT.g, Col::ACCENT.b, 80);
    SDL_Rect hdrLine = {panX, panY+40, panW, 1};
    SDL_RenderFillRect(m_renderer, &hdrLine);
    drawText("EQUIPMENT & INVENTORY", panX+16, panY+11, Col::ACCENT, fLg);
    drawText("[I] 닫기   드래그로 아이템 이동", panX+panW-280, panY+13, Col::TEXT_LO, fSm);

    // ── 좌: 장비 슬롯 ─────────────────────────────────────────────────────────
    int eqX = panX+16, eqY = panY+56;

    drawText("EQUIPMENT", eqX, eqY, Col::TEXT_HI, fMd);
    eqY += 28;

        const int slotW=210, slotH=64;
        const int slotGap=8;

        auto drawEquipSlot = [&](const char* label, const InventoryItem& item,
                                  int x, int y, int w, int h, bool isDragSource) {
            SDL_Color bg = item.isValid() ? SDL_Color{20,30,20,220} : SDL_Color{18,20,28,220};
            SDL_Color border = isDragSource ? SDL_Color{255,220,60,255}
                             : item.isValid() ? gradeColor(item.grade) : Col::BORDER;
            bool hov = (mouseX>=x && mouseX<x+w && mouseY>=y && mouseY<y+h);
            if (hov && dragItem) border = {80,200,255,255};
            drawPanel(x, y, w, h, bg, border, 2);

            // 슬롯 라벨 태그
            SDL_SetRenderDrawColor(m_renderer, 30,38,55,255);
            SDL_Rect tag = {x+4, y+4, 56, 16};
            SDL_RenderFillRect(m_renderer, &tag);
            drawText(label, x+6, y+5, Col::TEXT_LO, m_fonts.get(11));

            if (item.isValid() && !isDragSource) {
                SDL_Color nc = gradeColor(item.grade);
                // 등급 색 상단 바
                SDL_SetRenderDrawColor(m_renderer, nc.r,nc.g,nc.b,100);
                SDL_Rect gbar = {x+2, y+2, w-4, 3};
                SDL_RenderFillRect(m_renderer, &gbar);
                drawText(item.name, x+8, y+24, nc, fSm);
                char wbuf[24];
                std::snprintf(wbuf,sizeof(wbuf),"%.1fkg  x%d", item.weight, item.qty);
                drawText(wbuf, x+8, y+h-18, Col::TEXT_LO, fMono);
            } else if (!isDragSource) {
                drawText("(비어있음)", x+8, y+26, {45,50,65,255}, fSm);
            } else {
                // 드래그 중인 슬롯 — 점선 표시
                SDL_SetRenderDrawColor(m_renderer, 80,90,110,160);
                for (int dx=x+8; dx<x+w-8; dx+=8)
                    SDL_RenderDrawPoint(m_renderer, dx, y+h/2);
            }
        };

        drawEquipSlot("주무기",   inv.primaryWeapon,   eqX, eqY,              slotW, slotH, false);
        drawEquipSlot("보조무기", inv.secondaryWeapon, eqX, eqY+slotH+slotGap, slotW, slotH, false);

        // 무게 바
        int wbY = eqY + (slotH+slotGap)*2 + 16;
        drawText("무게", eqX, wbY, Col::TEXT_LO, fSm);
        float wPct = inv.maxWeight > 0 ? inv.totalWeight/inv.maxWeight : 0.0f;
        drawHpBar(eqX, wbY+18, wPct, slotW, 8);
        char wBuf[32];
        std::snprintf(wBuf,sizeof(wBuf),"%.1f / %.1f kg", inv.totalWeight, inv.maxWeight);
        SDL_Color weightCol = wPct > 0.85f ? Col::WARN
                            : wPct > 0.6f  ? SDL_Color{255,180,40,255}
                                           : Col::TEXT_LO;
        drawText(wBuf, eqX, wbY+30, weightCol, fMono);

        // 무게 초과 경고
        if (wPct >= 1.0f) {
            float p = std::sin(SDL_GetTicks()*0.01f)*0.5f+0.5f;
            uint8_t wa = static_cast<uint8_t>(160+p*95);
            drawText("[ 무게 초과 — 이동 불가 ]", eqX, wbY+46, {255,60,60,wa}, m_fonts.get(11));
        }

        // ── 사용 안내 ─────────────────────────────────────────────────────────────
        int tipY = wbY + 70;
        drawPanel(eqX, tipY, slotW, 88, {12,15,24,220}, {30,38,55,255}, 1);
        drawText("단축키 안내", eqX+8, tipY+6, Col::TEXT_LO, m_fonts.get(11));
        drawText("[1] 주무기 선택", eqX+8, tipY+20, {80,160,255,200}, m_fonts.get(11));
        drawText("[2] 보조무기 선택", eqX+8, tipY+33, {80,160,255,200}, m_fonts.get(11));
        drawText("[3-5] 소모품 사용", eqX+8, tipY+46, {80,220,120,200}, m_fonts.get(11));
        drawText("[Q] 무기 교체", eqX+8, tipY+59, {180,180,180,180}, m_fonts.get(11));
        drawText("밖으로 드래그: 버리기", eqX+8, tipY+72, {180,180,180,180}, m_fonts.get(11));


    // ── 우: 그리드 아이템 ────────────────────────────────────────────────────
    int gridX = panX+250, gridY = panY+56;
    drawText("ITEMS", gridX, gridY, Col::TEXT_HI, fMd);

    // 아이템 수 표시
    char cntBuf[24];
    std::snprintf(cntBuf,sizeof(cntBuf),"%d / 20", inv.usedSlots);
    drawText(cntBuf, gridX+65, gridY+2, Col::TEXT_LO, m_fonts.get(11));
    gridY += 28;

    const int COLS=5, ROWS=4;
    const int CELL_W=96, CELL_H=76, CELL_GAP=6;

    // 툴팁 추적
    std::string tooltip;
    SDL_Color  tooltipCol = Col::TEXT_HI;

    for (int row=0; row<ROWS; ++row) {
        for (int col=0; col<COLS; ++col) {
            int idx = row*COLS+col;
            int cx = gridX + col*(CELL_W+CELL_GAP);
            int cy = gridY + row*(CELL_H+CELL_GAP);

            const InventoryItem& item = (idx < 20) ? inv.gridSlots[idx]
                                                   : InventoryItem{};
            bool hovered = (mouseX>=cx && mouseX<cx+CELL_W &&
                            mouseY>=cy && mouseY<cy+CELL_H);

            SDL_Color bg, border;
            if (dragItem && hovered) {
                bg     = {20,40,20,240};
                border = {80,255,130,255};
            } else if (item.isValid()) {
                bg     = {22,32,22,230};
                border = hovered ? Col::ACCENT : gradeColor(item.grade);
            } else {
                bg     = {16,18,26,200};
                border = hovered ? Col::ACCENT : Col::BORDER;
            }
            drawPanel(cx, cy, CELL_W, CELL_H, bg, border, (hovered||dragItem&&hovered)?2:1);

            char numBuf[4];
            std::snprintf(numBuf,sizeof(numBuf),"%d",idx+1);
            drawText(numBuf, cx+4, cy+4, {35,42,58,255}, m_fonts.get(11));

            if (item.isValid()) {
                SDL_Color gc = gradeColor(item.grade);
                SDL_SetRenderDrawColor(m_renderer, gc.r,gc.g,gc.b,120);
                SDL_Rect gLine = {cx+2, cy+2, CELL_W-4, 3};
                SDL_RenderFillRect(m_renderer, &gLine);

                // ── 아이콘 렌더링 ─────────────────────────────────────────────
                std::string iconKey = "icon_" + item.name;
                SDL_Texture* icon = m_texCache.get(iconKey);
                const int ICON_SZ = 32;
                SDL_Rect iconDst = {cx + CELL_W/2 - ICON_SZ/2, cy + 14, ICON_SZ, ICON_SZ};
                if (icon) {
                    SDL_SetTextureBlendMode(icon, SDL_BLENDMODE_BLEND);
                    SDL_RenderCopy(m_renderer, icon, nullptr, &iconDst);
                } else {
                    // 폴백: 카테고리 색상 사각형
                    SDL_SetRenderDrawColor(m_renderer, gc.r/2, gc.g/2, gc.b/2, 180);
                    SDL_RenderFillRect(m_renderer, &iconDst);
                    drawText(item.name.substr(0,1), cx+CELL_W/2, cy+22, gc, fSm, true);
                }

                // ── 한국어 이름 ───────────────────────────────────────────────
                const char* dispName = getDisplayName(item.name);
                std::string dname(dispName);
                // 6자 초과면 첫 줄/둘째 줄 분리
                if (dname.size() > 8) {
                    drawText(dname.substr(0,8), cx+CELL_W/2, cy+49, Col::TEXT_HI, m_fonts.get(11), true);
                } else {
                    drawText(dname, cx+CELL_W/2, cy+49, Col::TEXT_HI, fSm, true);
                }

                // 수량 배지
                char qBuf[8];
                std::snprintf(qBuf,sizeof(qBuf),"x%d", item.qty);
                SDL_SetRenderDrawColor(m_renderer, gc.r/3,gc.g/3,gc.b/3,200);
                SDL_Rect qBg = {cx+CELL_W-26, cy+2, 24, 16};
                SDL_RenderFillRect(m_renderer, &qBg);
                drawText(qBuf, cx+CELL_W-14, cy+4, gc, m_fonts.get(11), true);

                // 무게
                char wBuf2[12];
                std::snprintf(wBuf2,sizeof(wBuf2),"%.1fkg",item.weight);
                drawText(wBuf2, cx+4, cy+CELL_H-17, Col::TEXT_LO, fMono);

                // 소모품이면 초록 인디케이터
                if (!ClientInventory::isWeaponItem(item.name)) {
                    SDL_SetRenderDrawColor(m_renderer, 60,200,100,60);
                    SDL_Rect cind = {cx+CELL_W-8, cy+CELL_H-8, 6, 6};
                    SDL_RenderFillRect(m_renderer, &cind);
                }

                if (hovered && !dragItem) {
                    const char* desc = nullptr;
                    const ItemMeta* meta = findItemMeta(item.name);
                    if (meta) desc = meta->description;
                    tooltip    = std::string(dispName) + "  x" + std::to_string(item.qty);
                    if (desc) tooltip += std::string("  —  ") + desc;
                    tooltipCol = gc;
                }
            } else {
                if (dragItem && hovered) {
                    drawText("여기에 놓기", cx+CELL_W/2, cy+CELL_H/2-6,
                             {80,255,130,220}, fSm, true);
                } else {
                    SDL_SetRenderDrawColor(m_renderer, 28,34,48,200);
                    for (int d=-8;d<=8;d+=8)
                        SDL_RenderDrawPoint(m_renderer, cx+CELL_W/2+d, cy+CELL_H/2);
                }
            }
        }
    }

    if (showStash) {
        // STASH UI (분리된 우측 패널)
        int stX = panX + panW + gap;
        int stY = panY;

        drawPanel(stX, stY, stW, stH, {14,17,26,250}, Col::BORDER, 2);

        // 헤더 바
        SDL_SetRenderDrawColor(m_renderer, 20,25,38,255);
        SDL_Rect sHdr = {stX, stY, stW, 40};
        SDL_RenderFillRect(m_renderer, &sHdr);
        SDL_SetRenderDrawColor(m_renderer, Col::ACCENT.r, Col::ACCENT.g, Col::ACCENT.b, 80);
        SDL_Rect sHdrLine = {stX, stY+40, stW, 1};
        SDL_RenderFillRect(m_renderer, &sHdrLine);
        
        drawText("STASH", stX+16, stY+11, Col::ACCENT, fLg);
        drawText("보관함", stX+stW-50, stY+15, Col::TEXT_LO, fSm);

        int contentY = stY + 56;
        drawText("안전 금고", stX+16, contentY, Col::TEXT_HI, fMd);
        contentY += 28;

        const int S_COLS=5, S_ROWS=8;
        const int S_CELL_W=40, S_CELL_H=40, S_CELL_GAP=6;
        
        int gridTotalW = S_COLS * S_CELL_W + (S_COLS - 1) * S_CELL_GAP;
        int gridOffX = stX + (stW - gridTotalW) / 2;

        for (int row=0; row<S_ROWS; ++row) {
            for (int col=0; col<S_COLS; ++col) {
                int idx = row*S_COLS+col;
                int cx = gridOffX + col*(S_CELL_W+S_CELL_GAP);
                int cy = contentY + row*(S_CELL_H+S_CELL_GAP);

                const InventoryItem& item = inv.stashSlots[idx];
                bool hovered = (mouseX>=cx && mouseX<cx+S_CELL_W && mouseY>=cy && mouseY<cy+S_CELL_H);

                SDL_Color bg, border;
                if (dragItem && hovered) {
                    bg = {20,40,20,240}; border = {80,255,130,255};
                } else if (item.isValid()) {
                    bg = {22,32,22,230}; border = hovered ? Col::ACCENT : gradeColor(item.grade);
                } else {
                    bg = {16,18,26,200}; border = hovered ? Col::ACCENT : Col::BORDER;
                }
                drawPanel(cx, cy, S_CELL_W, S_CELL_H, bg, border, (hovered||dragItem&&hovered)?2:1);

                if (item.isValid()) {
                    std::string iconKey = "icon_" + item.name;
                    SDL_Texture* icon = m_texCache.get(iconKey);
                    if (icon) {
                        SDL_SetTextureBlendMode(icon, SDL_BLENDMODE_BLEND);
                        SDL_Rect iconDst = {cx + S_CELL_W/2 - 12, cy + 4, 24, 24};
                        SDL_RenderCopy(m_renderer, icon, nullptr, &iconDst);
                    } else {
                        SDL_Color gc = gradeColor(item.grade);
                        drawText(item.name.substr(0, 1), cx+S_CELL_W/2, cy+S_CELL_H/2-6, gc, m_fonts.get(11), true);
                    }
                    
                    char qBuf[8];
                    std::snprintf(qBuf,sizeof(qBuf),"x%d", item.qty);
                    drawText(qBuf, cx+4, cy+S_CELL_H-14, Col::TEXT_LO, m_fonts.get(11));
                }
            }
        }
    }

    // 툴팁 표시
    if (!tooltip.empty()) {
        int tW = 320, tH = 28;
        int tX = std::min(mouseX+14, m_screenW-tW-4);
        int tY = std::max(mouseY-36, 4);
        drawPanel(tX, tY, tW, tH, {8,10,18,245}, tooltipCol, 1);
        drawText(tooltip, tX+8, tY+7, tooltipCol, fSm);
    }

    // 드래그 중인 아이템 — 커서 따라가는 고스트
    if (dragItem && dragItem->isValid()) {
        const int GW=88, GH=68;
        int gx = mouseX - GW/2, gy = mouseY - GH/2;
        SDL_Color gc = gradeColor(dragItem->grade);

        // 반투명 배경
        SDL_SetRenderDrawColor(m_renderer, 18,28,18,210);
        SDL_Rect ghost = {gx,gy,GW,GH};
        SDL_RenderFillRect(m_renderer, &ghost);
        SDL_SetRenderDrawColor(m_renderer, gc.r,gc.g,gc.b,255);
        SDL_RenderDrawRect(m_renderer, &ghost);
        // 등급 줄
        SDL_SetRenderDrawColor(m_renderer, gc.r,gc.g,gc.b,150);
        SDL_Rect gtop = {gx+1,gy+1,GW-2,3};
        SDL_RenderFillRect(m_renderer, &gtop);

        const std::string& dn = dragItem->name;
        if (dn.size() > 6) {
            drawText(dn.substr(0,6), gx+4, gy+12, gc, fSm);
            drawText(dn.substr(6),   gx+4, gy+26, gc, fSm);
        } else {
            drawText(dn, gx+4, gy+18, gc, fSm);
        }
        char qbuf[8]; std::snprintf(qbuf,sizeof(qbuf),"x%d",dragItem->qty);
        drawText(qbuf, gx+GW-4, gy+4, gc, m_fonts.get(11));
    }

    // 하단: 슬롯 수 + 조작 안내
    drawText("드래그 앤 드롭으로 아이템 이동  |  [I] 인벤토리 닫기",
             panX+panW/2, panY+panH-22, Col::TEXT_LO, fSm, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// 핫바 UI — 화면 하단 중앙, 항상 표시
// 슬롯 1-2: 무기, 슬롯 3-5: 소모품
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawHotbar(const ClientInventory& inv, int selectedSlot,
                           const int consumableIdx[3], int mouseX, int mouseY) {
    TTF_Font* fSm  = m_fonts.get(12);
    TTF_Font* fKey  = m_fonts.get(11);

    const int SLOT_W = 60, SLOT_H = 62, GAP = 5;
    const int TOTAL_W = 5*SLOT_W + 4*GAP;
    int hbX = m_screenW/2 - TOTAL_W/2;
    int hbY = m_screenH - SLOT_H - 14;

    // 배경 패널
    drawPanel(hbX-6, hbY-6, TOTAL_W+12, SLOT_H+12,
              {10,13,20,210}, {28,35,50,255}, 1);

    // ── 슬롯 1: 주무기 ────────────────────────────────────────────────────────
    for (int s = 0; s < 5; ++s) {
        int sx = hbX + s*(SLOT_W+GAP);
        bool selected = (s == selectedSlot);
        bool hov = (mouseX>=sx && mouseX<sx+SLOT_W &&
                    mouseY>=hbY && mouseY<hbY+SLOT_H);

        // 슬롯 내용 결정
        const InventoryItem* slotItem = nullptr;
        if      (s == 0) slotItem = inv.primaryWeapon.isValid()   ? &inv.primaryWeapon   : nullptr;
        else if (s == 1) slotItem = inv.secondaryWeapon.isValid() ? &inv.secondaryWeapon : nullptr;
        else {
            int ci = consumableIdx[s-2];
            if (ci >= 0 && ci < 20 && inv.gridSlots[ci].isValid())
                slotItem = &inv.gridSlots[ci];
        }

        SDL_Color bg = selected ? SDL_Color{22,45,22,245}
                     : hov      ? SDL_Color{20,30,35,240}
                     :            SDL_Color{14,17,26,230};
        SDL_Color border = selected ? Col::ACCENT
                         : hov      ? SDL_Color{100,180,140,255}
                         : slotItem ? gradeColor(slotItem->grade)
                                    : Col::BORDER;
        int bw = selected ? 2 : 1;
        drawPanel(sx, hbY, SLOT_W, SLOT_H, bg, border, bw);

        // 선택 선택 하이라이트 (상단 밝은 줄)
        if (selected) {
            SDL_SetRenderDrawColor(m_renderer,
                Col::ACCENT.r, Col::ACCENT.g, Col::ACCENT.b, 180);
            SDL_Rect selBar = {sx+2, hbY+2, SLOT_W-4, 3};
            SDL_RenderFillRect(m_renderer, &selBar);
        }

        // 구분선: 무기(1-2) / 소모품(3-5) 사이
        if (s == 2) {
            SDL_SetRenderDrawColor(m_renderer, 50,60,80,200);
            SDL_RenderDrawLine(m_renderer, sx-GAP/2, hbY+4, sx-GAP/2, hbY+SLOT_H-4);
        }

        // 슬롯 번호 키 힌트
        char keyBuf[4]; std::snprintf(keyBuf,sizeof(keyBuf),"[%d]",s+1);
        drawText(keyBuf, sx+SLOT_W/2, hbY+3, selected?Col::ACCENT:Col::TEXT_LO, fKey, true);

        if (slotItem) {
            SDL_Color gc = gradeColor(slotItem->grade);

            // 아이콘 렌더링 (스프라이트시트에서 슬라이싱)
            std::string iconKey = "icon_" + slotItem->name;
            SDL_Texture* icon = m_texCache.get(iconKey);
            SDL_Rect iconRect = {sx+5, hbY+15, 24, 24};
            if (icon) {
                SDL_SetTextureBlendMode(icon, SDL_BLENDMODE_BLEND);
                SDL_RenderCopy(m_renderer, icon, nullptr, &iconRect);
            } else {
                // 폴백: 카테고리 색상 박스
                SDL_Color iconBg = (s < 2) ? SDL_Color{20,30,60,150} : SDL_Color{15,45,20,150};
                SDL_SetRenderDrawColor(m_renderer, iconBg.r,iconBg.g,iconBg.b,iconBg.a);
                SDL_RenderFillRect(m_renderer, &iconRect);
                SDL_SetRenderDrawColor(m_renderer, gc.r/2,gc.g/2,gc.b/2,200);
                SDL_RenderDrawRect(m_renderer, &iconRect);
                drawText(s<2?"W":"C", sx+17, hbY+20, gc, fKey, true);
            }

            // 한국어 이름 (짧게)
            const char* dispName = getDisplayName(slotItem->name);
            std::string disp(dispName);
            // UTF-8 한글은 3바이트/자 → 4자 = 12바이트로 제한
            if (disp.size() > 12) disp = disp.substr(0, 12);
            drawText(disp, sx+SLOT_W/2, hbY+42, Col::TEXT_HI, m_fonts.get(11), true);

            // 소모품 수량 배지
            if (s >= 2) {
                char qb[8]; std::snprintf(qb,sizeof(qb),"x%d",slotItem->qty);
                SDL_SetRenderDrawColor(m_renderer, gc.r/4,gc.g/4,gc.b/4,220);
                SDL_Rect qBg2 = {sx+SLOT_W-20, hbY+14, 18, 14};
                SDL_RenderFillRect(m_renderer, &qBg2);
                drawText(qb, sx+SLOT_W-11, hbY+16, gc, m_fonts.get(10), true);
            }

            // 호버 툴팁 (한국어 이름 + 설명)
            if (hov) {
                const ItemMeta* meta = findItemMeta(slotItem->name);
                std::string tip = std::string(dispName);
                if (s >= 2) tip += "  x" + std::to_string(slotItem->qty);
                if (meta) tip += std::string("  —  ") + meta->description;
                int tW = static_cast<int>(tip.size()) * 7 + 24;
                tW = std::min(tW, 340);
                int tH = 28;
                int tX = std::min(sx+SLOT_W/2-tW/2, m_screenW-tW-4);
                int tY = hbY - tH - 6;
                drawPanel(tX, tY, tW, tH, {8,10,18,245}, gc, 1);
                drawText(tip, tX+tW/2, tY+7, gc, fSm, true);
            }

        } else {
            // 빈 슬롯
            SDL_SetRenderDrawColor(m_renderer, 28,34,52,200);
            SDL_Rect emptyIcon = {sx+SLOT_W/2-10, hbY+18, 20, 20};
            SDL_RenderDrawRect(m_renderer, &emptyIcon);
            drawText("빈칸", sx+SLOT_W/2, hbY+42, {30,36,55,200}, fKey, true);
        }
    }

    // 핫바 레이블 (슬롯 아래)
    drawText("무기", hbX+SLOT_W/2+SLOT_W/2+GAP/2, hbY+SLOT_H+2,
             {80,120,200,160}, fKey, true);
    drawText("소모품", hbX+TOTAL_W-SLOT_W-SLOT_W/2, hbY+SLOT_H+2,
             {60,180,100,160}, fKey, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawCraftingUI
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawDropQuantityDialog(const InventoryItem& item, int quantity,
                                      int mouseX, int mouseY) {
    if (!item.isValid()) return;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 150);
    SDL_Rect dim{0, 0, m_screenW, m_screenH};
    SDL_RenderFillRect(m_renderer, &dim);

    const int W = 360;
    const int H = 210;
    const int X = m_screenW / 2 - W / 2;
    const int Y = m_screenH / 2 - H / 2;
    drawPanel(X, Y, W, H, {14, 18, 28, 245}, {80, 170, 255, 220}, 2);

    auto* fTitle = m_fonts.get(18);
    auto* fMd = m_fonts.get(15);
    auto* fSm = m_fonts.get(13);
    auto* fQty = m_fonts.get(20);

    drawText("아이템 버리기", X + W / 2, Y + 18, Col::TEXT_HI, fTitle, true);
    drawText(item.name, X + W / 2, Y + 52, gradeColor(item.grade), fMd, true);

    char haveBuf[48];
    std::snprintf(haveBuf, sizeof(haveBuf), "보유 %d개", item.qty);
    drawText(haveBuf, X + W / 2, Y + 74, Col::TEXT_LO, fSm, true);

    const int minusX = X + 86;
    const int qtyX = X + 140;
    const int plusX = X + 244;
    const int stepY = Y + 100;
    const int btn = 44;

    auto drawButton = [&](int bx, int by, int bw, int bh, const char* label, bool primary) {
        const bool hov = mouseX >= bx && mouseX < bx + bw && mouseY >= by && mouseY < by + bh;
        SDL_Color bg = primary ? SDL_Color{35, 105, 80, 245} : SDL_Color{28, 34, 48, 245};
        SDL_Color br = hov ? SDL_Color{120, 220, 255, 255}
                           : primary ? SDL_Color{80, 210, 150, 230} : Col::BORDER;
        drawPanel(bx, by, bw, bh, bg, br, hov ? 2 : 1);
        drawText(label, bx + bw / 2, by + 12, Col::TEXT_HI, fMd, true);
    };

    drawButton(minusX, stepY, btn, btn, "-", false);
    drawPanel(qtyX, stepY, 84, btn, {10, 14, 22, 245}, {45, 60, 82, 255}, 1);
    char qtyBuf[16];
    std::snprintf(qtyBuf, sizeof(qtyBuf), "%d", quantity);
    drawText(qtyBuf, qtyX + 42, stepY + 9, Col::TEXT_HI, fQty, true);
    drawButton(plusX, stepY, btn, btn, "+", false);

    drawButton(X + 64, Y + 158, 120, 38, "버리기", true);
    drawButton(X + 196, Y + 158, 100, 38, "취소", false);
}

void Renderer::drawCraftingUI(const ClientInventory& inv, int mouseX, int mouseY, int& outClickedRecipe, int scrollOffset) {
    outClickedRecipe = -1;
    TTF_Font* fSm  = m_fonts.get(13);
    TTF_Font* fMd  = m_fonts.get(16);
    TTF_Font* fLg  = m_fonts.get(20);
    TTF_Font* fKey = m_fonts.get(11);

    // 패널 크기
    const int CW = 540, CH = 560;
    int boxX = m_screenW/2 - CW/2;
    int boxY = m_screenH/2 - CH/2;

    // 반투명 배경
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 140);
    SDL_Rect full = {0,0,m_screenW,m_screenH};
    SDL_RenderFillRect(m_renderer, &full);

    drawPanel(boxX, boxY, CW, CH, {22,24,26,245}, {45,50,55,255}, 3);

    // 헤더 (모바일 스타일로 더 두껍고 큼직하게)
    SDL_SetRenderDrawColor(m_renderer, 15,18,20,255);
    SDL_Rect hdr = {boxX, boxY, CW, 56};
    SDL_RenderFillRect(m_renderer, &hdr);
    SDL_SetRenderDrawColor(m_renderer, Col::ACCENT.r, Col::ACCENT.g, Col::ACCENT.b, 200);
    SDL_Rect hdrLine = {boxX, boxY+54, CW, 2};
    SDL_RenderFillRect(m_renderer, &hdrLine);
    drawText("CRAFTING", boxX+24, boxY+16, {240,240,240,255}, fLg);
    drawText("X  CLOSE", boxX+CW-80, boxY+20, Col::TEXT_LO, fSm);

    // 인벤토리 재료 집계 (key → qty)
    auto countItem = [&](const char* key) -> int {
        int cnt = 0;
        for (int i = 0; i < 20; ++i) {
            const auto& s = inv.gridSlots[i];
            if (s.isValid() && s.name == key) cnt += s.qty;
        }
        return cnt;
    };

    // 레시피 목록 렌더링
    const int ROW_H  = 76;
    const int PAD    = 12;
    const int contentTop    = boxY + 66;
    const int contentBottom = boxY + CH - 32;
    int recipeY = contentTop - scrollOffset;
    const float ticks = SDL_GetTicks() * 0.001f;

    // 레시피 영역 클리핑
    SDL_Rect clipRect = {boxX, contentTop, CW, contentBottom - contentTop};
    SDL_RenderSetClipRect(m_renderer, &clipRect);

    for (int ri = 0; ri < CRAFT_RECIPE_COUNT; ++ri) {
        const CraftingRecipe& rec = CRAFT_RECIPES[ri];

        // 재료 충족 여부 확인
        bool canCraft = true;
        for (int ii = 0; ii < rec.ingredientCount; ++ii) {
            if (countItem(rec.ingredients[ii].key) < rec.ingredients[ii].qty) {
                canCraft = false; break;
            }
        }

        bool hov = (mouseX >= boxX+PAD && mouseX <= boxX+CW-PAD &&
                    mouseY >= recipeY   && mouseY <= recipeY+ROW_H-6);

        // 행 배경 (모바일 느낌의 단색 박스)
        SDL_Color rowBg = canCraft ? (hov ? SDL_Color{35,45,35,255} : SDL_Color{25,30,25,255})
                                   : SDL_Color{30,25,25,255};
        SDL_Color rowBdr = canCraft ? (hov ? Col::ACCENT : SDL_Color{50,60,50,255})
                                    : SDL_Color{50,40,40,255};
        drawPanel(boxX+PAD, recipeY, CW-PAD*2, ROW_H-6, rowBg, rowBdr, 2);

        // 결과 아이콘 (더 크고 네모나게)
        std::string resultIconKey = std::string("icon_") + rec.resultKey;
        SDL_Texture* rIcon = m_texCache.get(resultIconKey);
        const int ICON_SZ = 48;
        SDL_Rect iconBgRect = {boxX+PAD+8, recipeY+ROW_H/2-ICON_SZ/2-3, ICON_SZ, ICON_SZ};
        SDL_SetRenderDrawColor(m_renderer, 15,15,18,255);
        SDL_RenderFillRect(m_renderer, &iconBgRect);
        
        SDL_Rect iconRect = {iconBgRect.x+4, iconBgRect.y+4, ICON_SZ-8, ICON_SZ-8};
        if (rIcon) {
            SDL_SetTextureBlendMode(rIcon, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(m_renderer, rIcon, nullptr, &iconRect);
        } else {
            SDL_SetRenderDrawColor(m_renderer, 60,60,70,200);
            SDL_RenderFillRect(m_renderer, &iconRect);
        }

        // 결과물 이름
        const char* resultName = getDisplayName(rec.resultKey);
        char resultBuf[64];
        std::snprintf(resultBuf, sizeof(resultBuf), "%s  x%d", resultName, rec.resultQty);
        SDL_Color rNameCol = canCraft ? SDL_Color{255,255,255,255} : SDL_Color{150,150,150,255};
        drawText(resultBuf, boxX+PAD+ICON_SZ+20, recipeY+10, rNameCol, fMd);

        // 재료 목록 (아래쪽에 깔끔하게 배치)
        int matX = boxX+PAD+ICON_SZ+20;
        for (int ii = 0; ii < rec.ingredientCount; ++ii) {
            int have = countItem(rec.ingredients[ii].key);
            int need = rec.ingredients[ii].qty;
            const char* matName = getDisplayName(rec.ingredients[ii].key);
            char matBuf[64];
            std::snprintf(matBuf, sizeof(matBuf), "%s %d/%d", matName, have, need);
            SDL_Color mc = (have >= need) ? SDL_Color{80,220,100,255} : SDL_Color{240,80,60,255};
            drawText(matBuf, matX + ii*140, recipeY+ROW_H-32, mc, fSm);
        }

        // 제작 버튼 (크고 두꺼운 사각형)
        const int BTN_W = 100, BTN_H = 46;
        int btnX = boxX+CW-PAD-BTN_W-8;
        int btnY = recipeY + ROW_H/2 - BTN_H/2 - 3;
        
        if (canCraft) {
            SDL_SetRenderDrawColor(m_renderer, 45, 160, 65, 255);
            SDL_Rect btnRect = {btnX, btnY, BTN_W, BTN_H};
            SDL_RenderFillRect(m_renderer, &btnRect);
            
            if (hov) {
                SDL_SetRenderDrawColor(m_renderer, 80, 220, 100, 255);
                SDL_RenderDrawRect(m_renderer, &btnRect);
            }
            drawText("제 작", btnX+BTN_W/2, btnY+14, {255,255,255,255}, fMd, true);

        } else {
            SDL_SetRenderDrawColor(m_renderer, 60,30,30,255);
            SDL_Rect btnRect = {btnX, btnY, BTN_W, BTN_H};
            SDL_RenderFillRect(m_renderer, &btnRect);
            drawText("재료 부족", btnX+BTN_W/2, btnY+14, {180,100,100,255}, fSm, true);
        }

        recipeY += ROW_H;

        // 구분선
        if (ri < CRAFT_RECIPE_COUNT-1) {
            SDL_SetRenderDrawColor(m_renderer, 30,38,55,255);
            SDL_RenderDrawLine(m_renderer, boxX+PAD, recipeY-2, boxX+CW-PAD, recipeY-2);
        }
    }

    // 클리핑 해제
    SDL_RenderSetClipRect(m_renderer, nullptr);

    // 하단 안내
    drawPanel(boxX, boxY+CH-32, CW, 32, {10,13,20,240}, Col::BORDER, 0);
    drawText("클릭으로 제작  |  워크벤치 레시피는 제작대 근처에서만 가능",
             boxX+CW/2, boxY+CH-21, Col::TEXT_LO, fKey, true);
}


// ─────────────────────────────────────────────────────────────────────────────
// 미니맵
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawMinimap(const TileMap& map, const NetworkClient& net, float lx, float ly, int teamID, const std::vector<std::pair<float,float>>& zones) {
    const int MM  = 160;
    const int MMX = m_screenW - MM - 14;
    const int MMY = 14;

    // 레이더 범위: 플레이어 중심 40타일 반경
    const float RADAR_RANGE = 40.0f * 32.0f;  // 1280 world pixels

    // 외곽 패널
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    drawPanel(MMX - 2, MMY - 2, MM + 4, MM + 32, {6, 8, 14, 230}, {60, 70, 100, 200}, 2);

    // 미니맵 영역 클리핑 (이 안에서만 그려짐)
    SDL_Rect clipRect = {MMX, MMY, MM, MM};
    SDL_RenderSetClipRect(m_renderer, &clipRect);

    // 배경
    SDL_SetRenderDrawColor(m_renderer, 15, 20, 25, 255);
    SDL_RenderFillRect(m_renderer, &clipRect);

    // world → 레이더 화면 좌표 변환 (플레이어가 항상 중앙)
    auto toMM = [&](float wx, float wy, int& px, int& py) {
        px = MMX + MM/2 + static_cast<int>((wx - lx) / RADAR_RANGE * (MM/2));
        py = MMY + MM/2 + static_cast<int>((wy - ly) / RADAR_RANGE * (MM/2));
    };

    // 건물 표시
    for (const auto& b : map.getBuildings()) {
        float bx = b.x * 32.0f, by_w = b.y * 32.0f;
        float bw = b.w * 32.0f, bh = b.h * 32.0f;
        int sx, sy, ex, ey;
        toMM(bx, by_w, sx, sy);
        toMM(bx + bw, by_w + bh, ex, ey);
        int rw = ex - sx, rh = ey - sy;
        if (rw <= 0) rw = 1;
        if (rh <= 0) rh = 1;
        SDL_Color col;
        switch(b.theme) {
            case 0: col = {160, 130, 90, 200}; break;
            case 1: col = {90, 110, 160, 200}; break;
            case 2: col = {80, 85, 80, 200};   break;
            case 3: col = {70, 90, 55, 200};   break;
            default: col = {100, 100, 100, 200}; break;
        }
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, col.a);
        SDL_Rect br = {sx, sy, rw, rh};
        SDL_RenderFillRect(m_renderer, &br);
    }

    // 그리드 라인
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 12);
    for (int g = 1; g < 4; ++g) {
        int gx = MMX + MM * g / 4;
        int gy = MMY + MM * g / 4;
        SDL_RenderDrawLine(m_renderer, gx, MMY, gx, MMY + MM);
        SDL_RenderDrawLine(m_renderer, MMX, gy, MMX + MM, gy);
    }

    // 탈출 구역
    float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() * 0.004f);
    uint8_t extA = static_cast<uint8_t>(120 + 100 * pulse);
    for (const auto& z : zones) {
        int epx, epy;
        toMM(z.first, z.second, epx, epy);
        drawFilledCircle(epx, epy, 5, {50, 220, 50, static_cast<uint8_t>(60 + 40 * pulse)});
        SDL_SetRenderDrawColor(m_renderer, 50, 220, 50, extA);
        for (int a = 0; a < 360; a += 20) {
            float rad = a * 3.14159f / 180.0f;
            SDL_RenderDrawPoint(m_renderer,
                epx + static_cast<int>(std::cos(rad) * 5),
                epy + static_cast<int>(std::sin(rad) * 5));
        }
    }

    // 원격 엔티티 (좀비/플레이어)
    for (int i = 0; i < net.remoteCount(); ++i) {
        const auto& r = net.remotes()[i];
        if (r.snap[1].statusFlags & STATUS_DEAD) continue;
        int px, py;
        toMM(r.snap[1].x, r.snap[1].y, px, py);
        bool isZ = (r.recType == REC_ZOMBIE);
        if (isZ) {
            SDL_SetRenderDrawColor(m_renderer, 55, 200, 55, 200);
            SDL_Rect dot = {px - 1, py - 1, 3, 3};
            SDL_RenderFillRect(m_renderer, &dot);
        } else {
            drawFilledCircle(px, py, 3, {255, 120, 30, 220});
        }
    }

    // 로컬 플레이어 (항상 중앙)
    int lpx = MMX + MM / 2;
    int lpy = MMY + MM / 2;
    SDL_Color tc = Col::TEAM[std::max(0, std::min(4, teamID))];
    drawFilledCircle(lpx, lpy, 5, tc);
    drawFilledCircle(lpx, lpy, 3, {255, 255, 255, 255});

    // 클리핑 해제
    SDL_RenderSetClipRect(m_renderer, nullptr);

    // 테두리 (클리핑 해제 후)
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(m_renderer, 60, 70, 100, 255);
    SDL_Rect border = {MMX, MMY, MM, MM};
    SDL_RenderDrawRect(m_renderer, &border);

    // 레이블
    drawText("RADAR [M]", MMX + MM / 2, MMY + MM + 4, {140, 150, 180, 200}, m_fonts.get(10), true);
}

void Renderer::drawFullMap(const TileMap& map, const NetworkClient& net, float lx, float ly, int teamID, const std::vector<std::pair<float,float>>& zones) {
    const int W = std::min(820, m_screenW - 60);
    const int H = std::min(820, m_screenH - 60);
    const int X = (m_screenW - W) / 2;
    const int Y = (m_screenH - H) / 2;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // 어두운 반투명 오버레이
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, m_screenW, m_screenH};
    SDL_RenderFillRect(m_renderer, &overlay);

    // 메인 패널
    drawPanel(X - 4, Y - 4, W + 8, H + 48, {8, 10, 18, 255}, {60, 70, 110, 255}, 2);
    drawPanel(X, Y, W, H, {12, 14, 22, 255}, {40, 50, 80, 200}, 1);

    const float MW = map.width()  * 32.0f;
    const float MH = map.height() * 32.0f;

    auto toMap = [&](float wx, float wy, int& px, int& py) {
        px = X + static_cast<int>((wx / MW) * W);
        py = Y + static_cast<int>((wy / MH) * H);
    };

    for (const auto& z : map.getDistricts()) {
        SDL_Color col = districtColor(z.theme, 55);
        SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, col.a);
        SDL_Rect zr = {
            X + static_cast<int>((z.x * TILE_SIZE / MW) * W),
            Y + static_cast<int>((z.y * TILE_SIZE / MH) * H),
            static_cast<int>((z.w * TILE_SIZE / MW) * W),
            static_cast<int>((z.h * TILE_SIZE / MH) * H)
        };
        SDL_RenderFillRect(m_renderer, &zr);
    }

    // ── 그리드 ───────────────────────────────────────────────────────────────
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 10);
    for (int g = 1; g < 8; ++g) {
        int gx = X + W * g / 8;
        int gy = Y + H * g / 8;
        SDL_RenderDrawLine(m_renderer, gx, Y, gx, Y + H);
        SDL_RenderDrawLine(m_renderer, X, gy, X + W, gy);
    }

    // ── 건물 표시 ─────────────────────────────────────────────────────────────
    for (const auto& b : map.getBuildings()) {
        int bx1, by1, bx2, by2;
        toMap(b.x * 32.0f, b.y * 32.0f, bx1, by1);
        toMap((b.x + b.w) * 32.0f, (b.y + b.h) * 32.0f, bx2, by2);
        SDL_Color bCol;
        switch (b.theme) {
            case 0: bCol = {180, 160, 110, 200}; break; // 주거
            case 1: bCol = {120, 140, 200, 200}; break; // 상업
            case 2: bCol = {100, 110, 100, 200}; break; // 공업
            case 3: bCol = {80,  100,  60, 200}; break; // 군사
            default:bCol = {120, 120, 120, 200}; break;
        }
        SDL_SetRenderDrawColor(m_renderer, bCol.r, bCol.g, bCol.b, bCol.a);
        SDL_Rect br = {bx1, by1, bx2 - bx1, by2 - by1};
        SDL_RenderFillRect(m_renderer, &br);
        SDL_SetRenderDrawColor(m_renderer, bCol.r + 30, bCol.g + 30, bCol.b + 30, 255);
        SDL_RenderDrawRect(m_renderer, &br);
    }

    // ── 구역 이름 라벨 ───────────────────────────────────────────────────────
    TTF_Font* fLg = m_fonts.get(20);
    TTF_Font* fSm = m_fonts.get(12);
    for (const auto& z : map.getDistricts()) {
        const float cx = (z.x + z.w * 0.5f) * TILE_SIZE;
        const float cy = (z.y + z.h * 0.5f) * TILE_SIZE;
        drawText(z.label.empty() ? z.key : z.label,
            X + static_cast<int>((cx / MW) * W),
            Y + static_cast<int>((cy / MH) * H) - 10,
            {220, 220, 220, 130}, fLg, true);
    }

    // ── 탈출 구역 표시 ────────────────────────────────────────────────────────
    float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() * 0.004f);
    uint8_t extA = static_cast<uint8_t>(160 + 90 * pulse);
    for (const auto& z : zones) {
        int epx, epy;
        toMap(z.first, z.second, epx, epy);
        drawFilledCircle(epx, epy, 10, {50, 220, 50, static_cast<uint8_t>(60 + 40 * pulse)});
        SDL_SetRenderDrawColor(m_renderer, 50, 220, 50, extA);
        for (int a = 0; a < 360; a += 6) {
            float rad = a * 3.14159f / 180.f;
            SDL_RenderDrawPoint(m_renderer, epx + static_cast<int>(std::cos(rad) * 10),
                                            epy + static_cast<int>(std::sin(rad) * 10));
        }
        drawText("탈출구", epx, epy + 14, {80, 255, 80, 255}, fSm, true);
    }

    // ── 원격 엔티티 ───────────────────────────────────────────────────────────
    for (int i = 0; i < net.remoteCount(); ++i) {
        const auto& r = net.remotes()[i];
        if (r.snap[1].statusFlags & STATUS_DEAD) continue;
        int px, py;
        toMap(r.snap[1].x, r.snap[1].y, px, py);
        bool isZ = (r.recType == REC_ZOMBIE);
        if (isZ) {
            drawFilledCircle(px, py, 3, {55, 200, 55, 220});
        } else {
            drawFilledCircle(px, py, 4, {225, 115, 38, 240});
        }
    }

    // ── 로컬 플레이어 ─────────────────────────────────────────────────────────
    {
        int lpx, lpy;
        toMap(lx, ly, lpx, lpy);
        SDL_Color tc = Col::TEAM[std::max(0, std::min(4, teamID))];
        drawFilledCircle(lpx, lpy, 7, {tc.r, tc.g, tc.b, 200});
        drawFilledCircle(lpx, lpy, 4, {255, 255, 255, 255});
        // 방향 화살표 생략 (aimAngle 파라미터 없음)
    }

    // ── 범례 ────────────────────────────────────────────────────────────────
    int legX = X + W + 12;
    int legY = Y;
    if (legX + 120 > m_screenW) legX = X + 8; // 화면 안으로
    drawPanel(X + 4, Y + H - 70, 200, 66, {8, 10, 18, 220}, {40, 50, 80, 180}, 1);
    drawText("■ 건물", X + 12, Y + H - 64, {180, 160, 110, 255}, fSm);
    drawText("● 좀비", X + 12, Y + H - 50, {55, 200, 55, 255}, fSm);
    drawText("● 플레이어", X + 12, Y + H - 36, {225, 115, 38, 255}, fSm);
    drawText("● 탈출구", X + 12, Y + H - 22, {80, 255, 80, 255}, fSm);

    // ── 타이틀 ────────────────────────────────────────────────────────────────
    drawText("전체 지도", X + W / 2, Y - 28, {220, 225, 255, 255}, m_fonts.get(20), true);
    drawText("[M] 닫기", X + W - 4, Y - 20, {140, 150, 180, 200}, fSm, false);
    // 우상단 정렬
    {
        TTF_Font* fClose = m_fonts.get(13);
        int tw = 0; TTF_SizeText(fClose, "[M] 닫기", &tw, nullptr);
        drawText("[M] 닫기", X + W - tw - 2, Y - 22, {140, 150, 180, 180}, fClose);
    }
}



void Renderer::drawDisconnectPopup(int mouseX, int mouseY, bool& outClickedOK) {
    int w = 340;
    int h = 140;
    int x = (m_screenW - w) / 2;
    int y = (m_screenH - h) / 2;
    
    drawPanel(x, y, w, h, {20,20,30,245}, {200,50,50,255}, 2);
    drawText("서버와 연결이 끊어졌습니다.", x + w/2, y + 30, {255,100,100,255}, m_fonts.get(16), true);
    drawText("확인 버튼을 누르면 로그인 화면으로 이동합니다.", x + w/2, y + 60, {150,150,150,255}, m_fonts.get(12), true);
    
    SDL_Rect btn = {x + w/2 - 50, y + 90, 100, 32};
    bool hover = (mouseX >= btn.x && mouseX <= btn.x+btn.w && mouseY >= btn.y && mouseY <= btn.y+btn.h);
    
    SDL_SetRenderDrawColor(m_renderer, hover?80:50, hover?50:30, hover?50:30, 255);
    SDL_RenderFillRect(m_renderer, &btn);
    SDL_SetRenderDrawColor(m_renderer, 200, 100, 100, 255);
    SDL_RenderDrawRect(m_renderer, &btn);
    drawText("확인", x + w/2, btn.y + 7, {255,255,255,255}, m_fonts.get(14), true);
    
    outClickedOK = hover;
}

void Renderer::drawNoiseDebug(float wx, float wy, float radius, const Camera& cam) {
    int sx,sy;
    cam.worldToScreen(wx,wy,sx,sy);
    int sr=static_cast<int>(radius*cam.zoom);
    drawFilledCircle(sx,sy,sr,{255,200,0,25});
    SDL_SetRenderDrawColor(m_renderer,255,200,0,90);
    for (int a=0;a<360;a+=4) {
        float r=a*3.14159f/180.0f;
        SDL_RenderDrawPoint(m_renderer, sx+static_cast<int>(std::cos(r)*sr),
                                        sy+static_cast<int>(std::sin(r)*sr));
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// drawNotification — 화면 상단 중앙 단기 메시지 (알파 페이드)
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// drawBuildModeOverlay — 건설 모드일 때 마우스 타일 강조 + 안내 패널
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawBuildModeOverlay(bool active, int buildType,
                                     int mouseX, int mouseY, const Camera& cam,
                                     const TileMap* map, int turretDir) {
    if (!active) return;

    TTF_Font* f   = m_fonts.get(14);
    TTF_Font* fSm = m_fonts.get(11);
    constexpr int TS = 32;
    const float ticks = SDL_GetTicks() * 0.001f;

    // 마우스가 가리키는 타일 좌표
    float wx = (mouseX - cam.screenW * 0.5f) / cam.zoom + cam.x;
    float wy = (mouseY - cam.screenH * 0.5f) / cam.zoom + cam.y;
    int tx = static_cast<int>(std::floor(wx / TS));
    int ty = static_cast<int>(std::floor(wy / TS));

    // 설치 가능 여부 (간단: 화면 내 타일 범위만 체크 — 서버가 최종 검증)
    bool canPlace = map ? (tx >= 1 && ty >= 1 && tx < map->width() - 1 && ty < map->height() - 1)
                        : (tx >= 1 && ty >= 1 && tx < 79 && ty < 79);
    if (buildType == static_cast<int>(BuildingType::Door)) {
        canPlace = false;
        if (map) {
            int doorID = map->findNearestDoor(wx, wy, TILE_SIZE * 1.5f);
            if (doorID >= 0 && static_cast<size_t>(doorID) < map->getDoors().size()) {
                const auto& door = map->getDoors()[doorID];
                tx = door.tx;
                ty = door.ty;
                canPlace = door.broken;
            }
        }
    }

    int sx, sy;
    cam.worldToScreen(static_cast<float>(tx * TS), static_cast<float>(ty * TS), sx, sy);
    int sw = static_cast<int>(TS * cam.zoom);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // ── Ghost 프리뷰 — 건물 유형별 모양 ─────────────────────────────────────
    float pulse = std::sin(ticks * 4.0f) * 0.5f + 0.5f;
    uint8_t alpha = static_cast<uint8_t>(canPlace ? 110 + pulse*60 : 80);
    uint8_t borderA = static_cast<uint8_t>(canPlace ? 200 + pulse*55 : 160);

    // 색상: 바리케이드=갈색, 포탑=파란회색, 제작대=노란갈색, 문=목재 / 불가=빨강
    SDL_Color fillCol  = canPlace ? (buildType == 0 ? SDL_Color{120,80,40,alpha}
                                   : buildType == 1 ? SDL_Color{60,80,120,alpha}
                                   : buildType == 2 ? SDL_Color{160,120,50,alpha}
                                                    : SDL_Color{105,70,38,alpha})
                                  : SDL_Color{160,30,30,alpha};
    SDL_Color lineCol  = canPlace ? (buildType == 0 ? SDL_Color{200,140,60,borderA}
                                   : buildType == 1 ? SDL_Color{80,160,220,borderA}
                                   : buildType == 2 ? SDL_Color{220,180,60,borderA}
                                                    : SDL_Color{210,145,80,borderA})
                                  : SDL_Color{240,60,40,borderA};

    SDL_SetRenderDrawColor(m_renderer, fillCol.r, fillCol.g, fillCol.b, fillCol.a);
    SDL_Rect ghostRect = {sx, sy, sw, sw};
    SDL_RenderFillRect(m_renderer, &ghostRect);
    SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, lineCol.a);
    SDL_RenderDrawRect(m_renderer, &ghostRect);

    // 내부 상세 — 바리케이드는 X 패턴, 포탑은 원, 제작대는 격자
    if (buildType == 0) {
        // 바리케이드: 십자 빗장 무늬
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA/2);
        SDL_RenderDrawLine(m_renderer, sx+2, sy+2, sx+sw-2, sy+sw-2);
        SDL_RenderDrawLine(m_renderer, sx+sw-2, sy+2, sx+2, sy+sw-2);
    } else if (buildType == 1) {
        // 포탑: 원형 받침 + 방향 포신 + 사격 범위 호
        drawFilledCircle(sx+sw/2, sy+sw/2, sw/4,
                         {lineCol.r,lineCol.g,lineCol.b,static_cast<uint8_t>(borderA/2)});

        // turretDir: 0=N 1=E 2=S 3=W → 포신 끝점
        const int cx = sx+sw/2, cy = sy+sw/2;
        int ex = cx, ey = cy;
        int arcLen = sw/2 - 2;
        switch (turretDir) {
            case 0: ey = cy - arcLen; break; // 북
            case 1: ex = cx + arcLen; break; // 동
            case 2: ey = cy + arcLen; break; // 남
            case 3: ex = cx - arcLen; break; // 서
        }
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA);
        SDL_RenderDrawLine(m_renderer, cx, cy, ex, ey);

        // 사격 호 (45° 양쪽) — 점선으로 표현
        constexpr float PI = 3.14159265f;
        float baseAngle = turretDir * 90.0f;
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA/3);
        float arcR = static_cast<float>(arcLen + sw/4);
        for (int da = -45; da <= 45; da += 3) {
            float rad = (baseAngle - 90.0f + da) * (PI / 180.0f);
            int ax = cx + static_cast<int>(std::cos(rad) * arcR);
            int ay = cy + static_cast<int>(std::sin(rad) * arcR);
            SDL_RenderDrawLine(m_renderer, cx, cy, ax, ay);
        }
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA);
    } else if (buildType == 2) {
        // 제작대: 3×3 격자
        int cellSz = sw/3;
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA/3);
        for (int i=1;i<3;++i) {
            SDL_RenderDrawLine(m_renderer, sx+cellSz*i, sy, sx+cellSz*i, sy+sw);
            SDL_RenderDrawLine(m_renderer, sx, sy+cellSz*i, sx+sw, sy+cellSz*i);
        }
    } else {
        // 문: 세로 판자
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA);
        SDL_RenderDrawLine(m_renderer, sx + sw / 3, sy + 4, sx + sw / 3, sy + sw - 4);
        SDL_RenderDrawLine(m_renderer, sx + sw * 2 / 3, sy + 4, sx + sw * 2 / 3, sy + sw - 4);
    }

    // 타일 좌표 힌트
    char coordBuf[32];
    std::snprintf(coordBuf, sizeof(coordBuf), "(%d, %d)", tx, ty);
    drawText(coordBuf, sx+sw/2, sy+sw+2, canPlace ? SDL_Color{200,200,200,200} : SDL_Color{255,80,80,200},
             fSm, true);

    // 설치 불가 X 표시
    if (!canPlace) {
        SDL_SetRenderDrawColor(m_renderer, 240,50,30,200);
        SDL_RenderDrawLine(m_renderer, sx+4, sy+4, sx+sw-4, sy+sw-4);
        SDL_RenderDrawLine(m_renderer, sx+sw-4, sy+4, sx+4, sy+sw-4);
    }

    // ── 상단 건설 모드 배너 ─────────────────────────────────────────────────
    static const char* typeNames[] = {"바리케이드", "포탑", "제작대", "문"};
    const char* typeName = (buildType >= 0 && buildType < 4) ? typeNames[buildType] : "?";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[ 건설 모드 ]  %s  —  클릭: 설치  |  V: 유형 전환  |  B: 취소",
                  typeName);

    SDL_SetRenderDrawColor(m_renderer, 8, 12, 20, 215);
    SDL_Rect banner = {0, 54, m_screenW, 28};
    SDL_RenderFillRect(m_renderer, &banner);
    SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, 180);
    SDL_RenderDrawLine(m_renderer, 0, 82, m_screenW, 82);

    drawText(buf, m_screenW/2, 59, lineCol, f, true);
}

void Renderer::drawBuildRecipePanel(const ClientInventory& inv, int buildType) {
    TTF_Font* fTitle = m_fonts.get(14);
    TTF_Font* fMd    = m_fonts.get(12);
    TTF_Font* fSm    = m_fonts.get(10);

    struct BuildRecipeRow {
        const char* name;
        RecipeIngredient ingredients[3];
        int ingredientCount;
    };
    const BuildRecipeRow rows[] = {
        {"바리케이드", {{"scrap_metal", 2}, {"plank", 2}, {"", 0}}, 2},
        {"포탑",       {{"electronic_part", 2}, {"oil", 2}, {"scrap_metal", 3}}, 3},
        {"제작대",     {{"plank", 2}, {"", 0}, {"", 0}}, 1},
        {"문",         {{"plank", 3}, {"scrap_metal", 1}, {"", 0}}, 2},
    };

    auto countItem = [&](const char* key) -> int {
        int cnt = 0;
        for (const auto& slot : inv.gridSlots) {
            if (slot.isValid() && slot.name == key) cnt += slot.qty;
        }
        return cnt;
    };

    constexpr int W = 330;
    constexpr int PAD = 12;
    constexpr int ROW_H = 64;
    constexpr int BUILD_RECIPE_COUNT = 4;
    const int H = 46 + BUILD_RECIPE_COUNT * ROW_H + PAD;
    const int x = std::max(12, m_screenW - W - 18);
    const int y = 96;

    drawPanel(x, y, W, H, {12,16,20,225}, {70,76,84,230}, 3);
    drawText("건설 조합법", x + PAD, y + 12, {240,240,230,255}, fTitle);
    drawText("Z=바리케이드  X=포탑  C=제작대", x + W - PAD, y + 14, Col::TEXT_LO, fSm, true);

    int rowY = y + 42;
    for (int ri = 0; ri < BUILD_RECIPE_COUNT; ++ri) {
        const BuildRecipeRow& rec = rows[ri];
        bool canCraft = true;
        for (int ii = 0; ii < rec.ingredientCount; ++ii) {
            if (countItem(rec.ingredients[ii].key) < rec.ingredients[ii].qty) {
                canCraft = false;
                break;
            }
        }

        const bool selected = (ri == buildType);
        SDL_Color bg = selected ? SDL_Color{42,38,22,240}
                                : canCraft ? SDL_Color{24,34,26,230}
                                           : SDL_Color{32,24,24,230};
        SDL_Color br = selected ? Col::ACCENT
                                : canCraft ? SDL_Color{70,120,78,220}
                                           : SDL_Color{100,54,54,220};
        drawPanel(x + PAD, rowY, W - PAD * 2, ROW_H - 6, bg, br, 2);

        char title[64];
        std::snprintf(title, sizeof(title), "%s%s", selected ? "> " : "", rec.name);
        drawText(title, x + PAD + 10, rowY + 7,
                 canCraft ? SDL_Color{235,245,235,255} : SDL_Color{190,170,170,255}, fMd);

        int matX = x + PAD + 10;
        for (int ii = 0; ii < rec.ingredientCount; ++ii) {
            const int have = countItem(rec.ingredients[ii].key);
            const int need = rec.ingredients[ii].qty;
            char mat[56];
            std::snprintf(mat, sizeof(mat), "%s %d/%d",
                          getDisplayName(rec.ingredients[ii].key), have, need);
            drawText(mat, matX, rowY + 29,
                     have >= need ? SDL_Color{95,220,120,255} : SDL_Color{235,90,80,255}, fSm);
            matX += 92;
        }

        rowY += ROW_H;
    }
}


void Renderer::drawNotification(const std::string& msg, float alpha) {
    if (msg.empty() || alpha <= 0.0f) return;
    TTF_Font* f = m_fonts.get(20);
    if (!f) return;

    uint8_t a = static_cast<uint8_t>(std::min(1.0f, alpha) * 255.0f);
    int panW = static_cast<int>(msg.size()) * 11 + 40;
    if (panW > m_screenW - 20) panW = m_screenW - 20;
    int panH = 42;
    int panX = (m_screenW - panW) / 2;
    int panY = 90;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 180, 40, 20, static_cast<uint8_t>(a * 0.85f));
    SDL_Rect r = {panX, panY, panW, panH};
    SDL_RenderFillRect(m_renderer, &r);
    SDL_SetRenderDrawColor(m_renderer, 255, 100, 60, a);
    SDL_RenderDrawRect(m_renderer, &r);

    drawText(msg, m_screenW / 2, panY + panH / 2 - 8,
             {255, 230, 200, a}, f, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// 파티클 (Particles)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::updateParticles(float dt) {
    for (auto it = m_particles.begin(); it != m_particles.end(); ) {
        it->life -= dt;
        if (it->life <= 0.0f) {
            it = m_particles.erase(it);
        } else {
            it->x += it->vx * dt;
            it->y += it->vy * dt;
            it->vx *= (1.0f - 3.0f * dt); // friction
            it->vy *= (1.0f - 3.0f * dt);
            ++it;
        }
    }
}

void Renderer::drawParticles(const Camera& cam) {
    // 핏자국 렌더링 (배경 위)
    SDL_SetRenderDrawColor(m_renderer, 100, 10, 10, 150); // 어두운 붉은색
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    for (const auto& bs : m_bloodStains) {
        SDL_Rect r = cam.worldRect(bs.x, bs.y, bs.size, bs.size);
        SDL_RenderFillRect(m_renderer, &r);
    }

    // 파티클 렌더링
    for (const auto& p : m_particles) {
        float alpha = p.life / p.maxLife;
        SDL_SetRenderDrawColor(m_renderer, p.color.r, p.color.g, p.color.b, static_cast<uint8_t>(p.color.a * alpha));
        
        if (p.type == 1) { // 힐링 십자가
            int sx, sy;
            cam.worldToScreen(p.x, p.y, sx, sy);
            int half = static_cast<int>(p.size * cam.zoom);
            int thickness = std::max(2, half / 3);
            SDL_Rect vBar = {sx - thickness/2, sy - half, thickness, half*2};
            SDL_Rect hBar = {sx - half, sy - thickness/2, half*2, thickness};
            SDL_RenderFillRect(m_renderer, &vBar);
            SDL_RenderFillRect(m_renderer, &hBar);
        } else {
            SDL_Rect r = cam.worldRect(p.x, p.y, p.size, p.size);
            SDL_RenderFillRect(m_renderer, &r);
        }
    }
}

void Renderer::spawnBlood(float x, float y) {
    for (int i = 0; i < 15; ++i) {
        Particle p;
        p.x = x; p.y = y;
        float angle = static_cast<float>(rand() % 360) * M_PI / 180.0f;
        float speed = static_cast<float>(rand() % 150 + 50);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.life = p.maxLife = static_cast<float>(rand() % 50 + 20) / 100.0f; // 0.2 ~ 0.7 sec
        p.color = { 200, 20, 20, 255 };
        p.size = static_cast<float>(rand() % 4 + 2);
        m_particles.push_back(p);
    }
}

void Renderer::spawnMuzzleFlash(float x, float y, float angle) {
    for (int i = 0; i < 5; ++i) {
        Particle p;
        p.x = x + std::cos(angle) * 15.0f;
        p.y = y + std::sin(angle) * 15.0f;
        float spread = angle + (static_cast<float>(rand() % 40 - 20) * M_PI / 180.0f);
        float speed = static_cast<float>(rand() % 200 + 100);
        p.vx = std::cos(spread) * speed;
        p.vy = std::sin(spread) * speed;
        p.life = p.maxLife = static_cast<float>(rand() % 10 + 5) / 100.0f; // 0.05 ~ 0.15 sec
        p.color = { 255, 200, 50, 255 };
        if (rand() % 2 == 0) p.color = { 255, 255, 200, 255 }; // spark
        p.size = static_cast<float>(rand() % 3 + 2);
        m_particles.push_back(p);
    }
}

void Renderer::spawnCasing(float x, float y, float aimAngle) {
    Particle p;
    // 무기의 우측 측면에서 배출
    float rightAngle = aimAngle + (M_PI / 2.0f);
    p.x = x + std::cos(aimAngle) * 5.0f; 
    p.y = y + std::sin(aimAngle) * 5.0f;
    
    float spread = rightAngle + (static_cast<float>(rand() % 20 - 10) * M_PI / 180.0f);
    float speed = static_cast<float>(rand() % 80 + 40);
    p.vx = std::cos(spread) * speed;
    p.vy = std::sin(spread) * speed;
    p.life = p.maxLife = static_cast<float>(rand() % 50 + 50) / 100.0f; // 0.5 ~ 1.0 sec (탄피가 땅에 떨어져 머무는 느낌)
    p.color = { 200, 150, 50, 255 }; // 황동색 (탄피)
    p.size = 2.0f;
    m_particles.push_back(p);
}

void Renderer::spawnMeleeArc(float x, float y, float angle) {
    for (int i = -3; i <= 3; ++i) {
        Particle p;
        float spread = angle + (static_cast<float>(i) * 10.0f * M_PI / 180.0f);
        p.x = x + std::cos(spread) * 20.0f;
        p.y = y + std::sin(spread) * 20.0f;
        float speed = 250.0f;
        p.vx = std::cos(spread) * speed;
        p.vy = std::sin(spread) * speed;
        p.life = p.maxLife = 0.1f; // 매우 빠르게 사라짐
        p.color = { 200, 200, 200, 255 }; // 흰색 궤적
        p.size = 3.0f;
        m_particles.push_back(p);
    }
}

void Renderer::spawnBloodStain(float x, float y) {
    if (m_bloodStains.size() > 500) {
        m_bloodStains.erase(m_bloodStains.begin()); // FIFO 500개 유지
    }
    BloodStain bs;
    bs.x = x + (static_cast<float>(rand() % 20) - 10.0f);
    bs.y = y + (static_cast<float>(rand() % 20) - 10.0f);
    bs.size = static_cast<float>(rand() % 4 + 2);
    m_bloodStains.push_back(bs);
}

void Renderer::spawnHealEffect(float x, float y) {
    for (int i = 0; i < 6; ++i) {
        Particle p;
        p.x = x + static_cast<float>(rand() % 40 - 20);
        p.y = y + static_cast<float>(rand() % 40 - 20);
        p.vx = 0.0f;
        p.vy = static_cast<float>(-(rand() % 40 + 20)); // 위로 떠오름
        p.life = p.maxLife = static_cast<float>(rand() % 40 + 40) / 100.0f; // 0.4 ~ 0.8 sec
        p.color = { 80, 255, 100, 255 }; // 밝은 초록색
        p.size = static_cast<float>(rand() % 3 + 4);
        p.type = 1; // 힐링 십자가
        m_particles.push_back(p);
    }
}

void Renderer::spawnSoundRing(float x, float y, float maxRadius, SDL_Color color) {
    SoundRing r;
    r.x = x; r.y = y;
    r.currentRadius = 0.0f;
    r.maxRadius = maxRadius;
    r.life = r.maxLife = 0.75f;
    r.color = color;
    m_soundRings.push_back(r);
}

void Renderer::updateSoundRings(float dt) {
    for (auto it = m_soundRings.begin(); it != m_soundRings.end(); ) {
        it->life -= dt;
        if (it->life <= 0.0f) {
            it = m_soundRings.erase(it);
        } else {
            float progress = 1.0f - (it->life / it->maxLife);
            it->currentRadius = it->maxRadius * progress;
            ++it;
        }
    }
}

void Renderer::drawSoundRings(const Camera& cam) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    for (const auto& ring : m_soundRings) {
        float t = ring.life / ring.maxLife; // 1→0 as it expands
        uint8_t a = static_cast<uint8_t>(ring.color.a * t * t); // quadratic fade
        if (a == 0) continue;
        SDL_SetRenderDrawColor(m_renderer, ring.color.r, ring.color.g, ring.color.b, a);

        int cx, cy;
        cam.worldToScreen(ring.x, ring.y, cx, cy);
        int r = static_cast<int>(ring.currentRadius * cam.zoom);
        if (r <= 0) continue;

        // 2도 간격 점으로 원 테두리 그리기
        for (int deg = 0; deg < 360; deg += 2) {
            float rad = deg * 3.14159265f / 180.0f;
            int px = cx + static_cast<int>(std::cos(rad) * r);
            int py = cy + static_cast<int>(std::sin(rad) * r);
            SDL_RenderDrawPoint(m_renderer, px, py);
        }
    }
}

} // namespace dz


namespace dz {
// ─────────────────────────────────────────────────────────────────────────────
// setupCollisionMap — 건물 외벽 타일을 SOLID로 마킹
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::setupCollisionMap(TileMap& map) {
    for (const auto& b : map.getBuildings()) {
        // 1. 건물 전체 외벽 타일 → SOLID
        for (int tx = b.x; tx < b.x + b.w; ++tx) {
            for (int ty = b.y; ty < b.y + b.h; ++ty) {
                bool isWall = (tx == b.x || tx == b.x + b.w - 1 ||
                               ty == b.y || ty == b.y + b.h - 1);
                if (!isWall) continue;
                if (!map.inBounds(tx, ty)) continue;
                map.at(tx, ty).type   = TILE_WALL;
                map.at(tx, ty).flags |= TILE_SOLID;
            }
        }

        // 2. 문 위치 타일 (단순화: 각 벽의 중앙을 문으로 개방)
        int doorsX[2] = { b.x + b.w/2, b.x + b.w/2 };
        int doorsY[2] = { b.y, b.y + b.h - 1 };
        int doorsX2[2] = { b.x, b.x + b.w - 1 };
        int doorsY2[2] = { b.y + b.h/2, b.y + b.h/2 };

        for(int i = 0; i < 2; i++) {
            if (map.inBounds(doorsX[i], doorsY[i])) {
                map.at(doorsX[i], doorsY[i]).type = TILE_GRASS;
                map.at(doorsX[i], doorsY[i]).flags &= ~TILE_SOLID;
            }
            if (map.inBounds(doorsX2[i], doorsY2[i])) {
                map.at(doorsX2[i], doorsY2[i]).type = TILE_GRASS;
                map.at(doorsX2[i], doorsY2[i]).flags &= ~TILE_SOLID;
            }
        }
    }
}

}
