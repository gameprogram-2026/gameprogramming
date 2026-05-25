#include "Renderer.h"
#include "shared/util/Logger.h"
#include "shared/network/Packet.h"
#include "shared/ItemData.h"
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
void Renderer::drawDeathScreen(float countdown) {
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

    for (int ty = minTY; ty <= maxTY; ++ty) {
        for (int tx = minTX; tx <= maxTX; ++tx) {
            const Tile& tile = map.at(tx, ty);
            SDL_Color col = {38,48,28,255};
            switch (tile.type) {
                case TILE_GRASS:      col = {40,52,28,255}; break;
                case TILE_ROAD:       col = {65,65,70,255}; break;
                case TILE_WALL:       col = {88,88,98,255}; break;
                case TILE_DEBRIS:     col = {75,65,48,255}; break;
                case TILE_WOOD_FLOOR: col = {100,80,52,255}; break;
                case TILE_ASH:        col = {48,44,42,255}; break;
            }
            SDL_SetRenderDrawColor(m_renderer, col.r, col.g, col.b, col.a);
            SDL_Rect dst;
            dst.x = static_cast<int>((tx*TILE_SIZE - cam.x)*cam.zoom + m_screenW*0.5f);
            dst.y = static_cast<int>((ty*TILE_SIZE - cam.y)*cam.zoom + m_screenH*0.5f);
            dst.w = tsz; dst.h = tsz;
            SDL_RenderFillRect(m_renderer, &dst);
            if (tile.type == TILE_ROAD) {
                SDL_SetRenderDrawColor(m_renderer, 80, 80, 85, 255);
                SDL_RenderDrawRect(m_renderer, &dst);
            }
        }
    }

    // 구역 색조
    {
        int ax1,ay1,ax2,ay2;
        cam.worldToScreen(5*TILE_SIZE, 5*TILE_SIZE, ax1, ay1);
        cam.worldToScreen(34*TILE_SIZE,34*TILE_SIZE, ax2, ay2);
        SDL_SetRenderDrawColor(m_renderer, 80,65,20,10);
        SDL_Rect z = {ax1,ay1,ax2-ax1,ay2-ay1};
        SDL_RenderFillRect(m_renderer, &z);
    }
    {
        int cx1,cy1,cx2,cy2;
        cam.worldToScreen(56*TILE_SIZE,56*TILE_SIZE, cx1, cy1);
        cam.worldToScreen(78*TILE_SIZE,78*TILE_SIZE, cx2, cy2);
        SDL_SetRenderDrawColor(m_renderer, 20,40,60,14);
        SDL_Rect z = {cx1,cy1,cx2-cx1,cy2-cy1};
        SDL_RenderFillRect(m_renderer, &z);
    }

    drawBuildings(map, cam, localX, localY);
}

// ─────────────────────────────────────────────────────────────────────────────
// 건물 렌더링 — 문은 마커로 (구멍 없음)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawBuildings(const TileMap& map, const Camera& cam, float localX, float localY) {
    const int WALL_T = static_cast<int>(TILE_SIZE * cam.zoom);

    for (const auto& b : map.getBuildings()) {
        int sx1, sy1, sx2, sy2;
        cam.worldToScreen(b.x*TILE_SIZE,          b.y*TILE_SIZE,          sx1, sy1);
        cam.worldToScreen((b.x+b.w)*TILE_SIZE,   (b.y+b.h)*TILE_SIZE,   sx2, sy2);
        if (sx2 < -10 || sx1 > m_screenW+10 || sy2 < -10 || sy1 > m_screenH+10) continue;

        int pw = sx2-sx1, ph = sy2-sy1;
        int tsz = static_cast<int>(TILE_SIZE * cam.zoom); 
        
        SDL_Color floorCol, wallCol, roofCol;
        bool hasRoom = false;
        
        // Themes: 0=Residential, 1=Commercial, 2=Industrial, 3=Military
        switch(b.theme) {
            case 0:
                floorCol = {115,90,62,255}; wallCol = {200,190,168,255}; roofCol = {180,60,50,255};
                hasRoom = true;
                break;
            case 1:
                floorCol = {145,138,128,255}; wallCol = {215,210,198,255}; roofCol = {100,120,180,255};
                break;
            case 2:
                floorCol = {88,83,76,255}; wallCol = {138,130,118,255}; roofCol = {80,90,90,255};
                break;
            case 3:
                floorCol = {60,70,50,255}; wallCol = {90,100,80,255}; roofCol = {50,60,40,255};
                hasRoom = true;
                break;
            default:
                floorCol = {100,100,100,255}; wallCol = {150,150,150,255}; roofCol = {120,120,120,255};
                break;
        }

        float bxWorld = b.x * TILE_SIZE;
        float byWorld = b.y * TILE_SIZE;
        float bwWorld = b.w * TILE_SIZE;
        float bhWorld = b.h * TILE_SIZE;
        
        bool isInside = (localX >= bxWorld && localX <= bxWorld + bwWorld &&
                         localY >= byWorld && localY <= byWorld + bhWorld);

        // 1. 그림자
        SDL_SetRenderDrawColor(m_renderer, 0,0,0, 60);
        SDL_Rect shadow = {sx1+6, sy1+6, pw, ph};
        SDL_RenderFillRect(m_renderer, &shadow);

        if (isInside) {
            // 내부 바닥
            SDL_SetRenderDrawColor(m_renderer, floorCol.r, floorCol.g, floorCol.b, 255);
            SDL_Rect interiorFloor = {sx1+WALL_T, sy1+WALL_T, pw-2*WALL_T, ph-2*WALL_T};
            if (interiorFloor.w > 0 && interiorFloor.h > 0)
                SDL_RenderFillRect(m_renderer, &interiorFloor);
                
            // 바닥 타일 그리드
            SDL_SetRenderDrawColor(m_renderer, 0,0,0, 18);
            for (int gy = sy1+WALL_T; gy <= sy2-WALL_T; gy += tsz)
                SDL_RenderDrawLine(m_renderer, sx1+WALL_T, gy, sx2-WALL_T, gy);
            for (int gx = sx1+WALL_T; gx <= sx2-WALL_T; gx += tsz)
                SDL_RenderDrawLine(m_renderer, gx, sy1+WALL_T, gx, sy2-WALL_T);
                
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
        } else {
            // Roof
            SDL_SetRenderDrawColor(m_renderer, roofCol.r, roofCol.g, roofCol.b, 255);
            SDL_Rect roofRect = {sx1, sy1, pw, ph};
            SDL_RenderFillRect(m_renderer, &roofRect);
            
            // Roof details
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 60);
            if (b.theme == 0) {
                SDL_RenderDrawLine(m_renderer, sx1, sy1, sx1+pw/2, sy1+ph/2);
                SDL_RenderDrawLine(m_renderer, sx2, sy1, sx1+pw/2, sy1+ph/2);
                SDL_RenderDrawLine(m_renderer, sx1, sy2, sx1+pw/2, sy1+ph/2);
                SDL_RenderDrawLine(m_renderer, sx2, sy2, sx1+pw/2, sy1+ph/2);
                SDL_RenderDrawLine(m_renderer, sx1, sy1+ph/2, sx2, sy1+ph/2);
            } else if (b.theme == 1) {
                SDL_Rect inner = {sx1+WALL_T, sy1+WALL_T, pw-2*WALL_T, ph-2*WALL_T};
                SDL_RenderDrawRect(m_renderer, &inner);
                SDL_Rect ac = {sx1+pw/4, sy1+ph/4, pw/6, ph/6};
                SDL_RenderFillRect(m_renderer, &ac);
            } else if (b.theme == 2) {
                for (int gx = sx1; gx < sx2; gx += static_cast<int>(8*cam.zoom))
                    SDL_RenderDrawLine(m_renderer, gx, sy1, gx, sy2);
            } else if (b.theme == 3) {
                SDL_Rect inner = {sx1+WALL_T*2, sy1+WALL_T*2, pw-4*WALL_T, ph-4*WALL_T};
                SDL_RenderDrawRect(m_renderer, &inner);
                SDL_RenderDrawLine(m_renderer, sx1, sy1, sx2, sy2);
                SDL_RenderDrawLine(m_renderer, sx1, sy2, sx2, sy1);
            }
        }
        
        // 문(입구) 렌더링
        for (int dx = 0; dx < b.w; ++dx) {
            for (int dy = 0; dy < b.h; ++dy) {
                if (dx == 0 || dx == b.w - 1 || dy == 0 || dy == b.h - 1) {
                    if (map.inBounds(b.x + dx, b.y + dy)) {
                        if (map.at(b.x + dx, b.y + dy).type != TILE_WALL) {
                            int dsx, dsy;
                            cam.worldToScreen((b.x + dx) * TILE_SIZE, (b.y + dy) * TILE_SIZE, dsx, dsy);
                            
                            // 내부일 때는 문구멍(바닥색)을 내고, 열린 문을 그림
                            if (isInside) {
                                // 벽을 덮어쓰는 바닥 타일
                                SDL_Rect gapRect = {dsx, dsy, tsz, tsz};
                                SDL_SetRenderDrawColor(m_renderer, floorCol.r, floorCol.g, floorCol.b, 255);
                                SDL_RenderFillRect(m_renderer, &gapRect);
                                
                                // 열린 문 (벽 옆에 붙어있는 형태)
                                SDL_SetRenderDrawColor(m_renderer, 130, 90, 50, 255);
                                SDL_Rect doorOpen;
                                if (dy == 0 || dy == b.h - 1) {
                                    // 북/남쪽 문 -> 열리면 세로로
                                    doorOpen = {dsx, dsy, 4, tsz};
                                } else {
                                    // 동/서쪽 문 -> 열리면 가로로
                                    doorOpen = {dsx, dsy, tsz, 4};
                                }
                                SDL_RenderFillRect(m_renderer, &doorOpen);
                            } else {
                                // 외부일 때: 남쪽 문이거나 서/동쪽 문이면 지붕 아래/옆으로 보이게 그림
                                // 북쪽 문은 지붕에 가려져 안 보임
                                if (dy == b.h - 1) { // 남쪽 문
                                    SDL_Rect doorRect = {dsx, dsy, tsz, tsz};
                                    SDL_SetRenderDrawColor(m_renderer, 100, 70, 40, 255);
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
            SDL_SetRenderDrawColor(m_renderer, 255, 220, 60, pa);
            SDL_Rect glow = {sx-bw/2-3, sy-bh/2-3, bw+6, bh+6};
            SDL_RenderDrawRect(m_renderer, &glow);
            SDL_Rect glow2 = {sx-bw/2-5, sy-bh/2-5, bw+10, bh+10};
            SDL_SetRenderDrawColor(m_renderer, 255,220,60, pa/3);
            SDL_RenderFillRect(m_renderer, &glow2);
            drawText("F  파밍", sx, sy - bh - 4, {255, 240, 100, pa}, fSm, true);
        }
    } else if (box.isBuilding) {
        // Simple distinct drawing for buildings
        bw = static_cast<int>(32 * cam.zoom);
        bh = static_cast<int>(32 * cam.zoom);
        
        SDL_Color bColor = {100, 100, 100, 255}; // Default Grey
        if (box.buildingType == 0) bColor = {139, 69, 19, 255}; // Barricade (Wood)
        else if (box.buildingType == 1) bColor = {70, 70, 90, 255}; // Turret (Metal)
        else if (box.buildingType == 2) bColor = {180, 130, 70, 255}; // Workbench
        
        SDL_SetRenderDrawColor(m_renderer, bColor.r, bColor.g, bColor.b, bColor.a);
        SDL_Rect body = {sx-bw/2, sy-bh/2, bw, bh};
        SDL_RenderFillRect(m_renderer, &body);
        SDL_SetRenderDrawColor(m_renderer, bColor.r/2, bColor.g/2, bColor.b/2, 255);
        SDL_RenderDrawRect(m_renderer, &body);
        
        if (box.buildingType == 2 && box.nearPlayer) { // Workbench near
            float pulse = std::sin(ticks * 4.0f) * 0.5f + 0.5f;
            uint8_t pa = static_cast<uint8_t>(150 + pulse * 105);
            SDL_SetRenderDrawColor(m_renderer, 100, 255, 100, pa);
            SDL_Rect glow = {sx-bw/2-2, sy-bh/2-2, bw+4, bh+4};
            SDL_RenderDrawRect(m_renderer, &glow);
            drawText("F  제작대 사용", sx, sy - bh/2 - 12, {100, 255, 100, pa}, fSm, true);
        }
    } else {
        SDL_SetRenderDrawColor(m_renderer, 65, 58, 42, 200);
        SDL_Rect body = {sx-bw/2, sy-bh/2+bh/3, bw, bh*2/3};
        SDL_RenderFillRect(m_renderer, &body);
        SDL_SetRenderDrawColor(m_renderer, 50, 45, 32, 200);
        SDL_RenderDrawRect(m_renderer, &body);
        SDL_SetRenderDrawColor(m_renderer, 75, 68, 50, 180);
        SDL_Rect openLid = {sx-bw/2+2, sy-bh/2, bw-4, bh/3};
        SDL_RenderFillRect(m_renderer, &openLid);
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
            drawText("맨홀 (탈출구)", sx, sy - sr - 20, {150, 220, 255, 255}, fSm, true);
            drawText("5초간 대기", sx, sy - sr - 6, {200, 200, 200, 200}, fSm, true);
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
                                float attackTimer, float attackAngle) {
    int sx, sy;
    cam.worldToScreen(wx, wy, sx, sy);
    int sz = static_cast<int>(22*cam.zoom);
    SDL_Color tc = Col::TEAM[std::max(0,std::min(4,teamID))];
    float animT = (attackTimer > 0.0f) ? (SDL_GetTicks()*0.015f) : (SDL_GetTicks()*0.005f);
    drawCharacterBody(this, sx, sy, sz, angle, tc, bleeding, false, animT);

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
//   2. 텍스처를 어두운 안개(alpha=195)로 채움   → 시야 밖
//   3. 시야 부채꼴(120°) 영역을 alpha=0으로 뚫음  → 완전히 투명
//   4. 부채꼴 양측 경계에 ~15° 그라데이션 추가     → 자연스러운 가장자리
//   5. 렌더 타깃 복원 후 FOW 텍스처를 화면에 합성
//
// aimAngleDeg: 0=위, 90=오른쪽, 180=아래, 270=왼쪽
//              (InputHandler의 atan2(dx,-dy) 관례와 동일)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawFOV(float wx, float wy, float aimAngleDeg, const Camera& cam, float gameTime) {
    if (!m_fowTexture) return;

    int px, py;
    cam.worldToScreen(wx, wy, px, py);

    // ── 1. FOW 텍스처에 렌더링 ─────────────────────────────────────────────
    SDL_SetRenderTarget(m_renderer, m_fowTexture);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);

    // 밤/낮에 따른 안개 색상과 농도
    uint8_t fogAlpha = 195;
    uint8_t r = 0, g = 0, b = 0;
    if (gameTime >= 240.0f) { // 밤
        fogAlpha = 240; // 더 어둡게
        r = 60; // 붉은 기운
    }

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
    if (gameTime >= 240.0f) {
        R = 300.0f; // 밤에는 시야 반경 대폭 축소
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
            uint8_t alpha = static_cast<uint8_t>(t * t * 195.0f);

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

    bool bleed    = (rem.snap[1].statusFlags & STATUS_BLEEDING) != 0;
    bool onFire   = (rem.snap[1].statusFlags & STATUS_ON_FIRE)  != 0;
    bool isZombie = (rem.recType == REC_ZOMBIE);

    if (isZombie) {
        uint8_t zt = rem.snap[1].statusFlags >> 6;
        int sz;
        SDL_Color bc;
        if (zt == ZT_BRUTE)       { sz = static_cast<int>(30*cam.zoom); bc = {40,130,40,255}; }
        else if (zt == ZT_RUNNER) { sz = static_cast<int>(18*cam.zoom); bc = {140,240,60,255}; }
        else                      { sz = static_cast<int>(22*cam.zoom); bc = {55,210,55,255}; }
        float dx = rem.snap[1].x - rem.snap[0].x;
        float dy = rem.snap[1].y - rem.snap[0].y;
        float aimAngle = (dx != 0 || dy != 0) ? std::atan2(dy, dx) * (180.0f / 3.14159265f) : 0.0f;
        
        bool isDead = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        if (isDead) {
            bc.r /= 2; bc.g /= 2; bc.b /= 2;
            aimAngle += 90.0f; // lie down
        }

        float animT = SDL_GetTicks() * 0.005f + rem.entityID * 0.1f;
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
                                 float attackTimer, float attackAngle) {
    enum class EntryType { LocalPlayer, RemoteEntity, LootBox, BuildingOverlay };
    struct Entry { float wy; EntryType type; int idx; };
    std::vector<Entry> list;
    list.reserve(net.remoteCount() + lootBoxes.size() + (map ? map->getBuildings().size() : 0) + 1);

    for (int i = 0; i < net.remoteCount(); ++i) {
        const auto& rem = net.remotes()[i];
        bool isDead = (rem.snap[1].statusFlags & STATUS_DEAD) != 0;
        // 좌비는 죽어도 y-sort에 포함 (시체 렌더링 위해)
        if (isDead && rem.recType != REC_ZOMBIE) continue;
        if (rem.recType == 2 || rem.recType == 3) continue; // Skip LootBox & Buildings
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

    for (const auto& e : list) {
        if (e.type == EntryType::LocalPlayer) {
            drawLocalPlayer(localX, localY, aimAngle, hpPct, bleeding,
                            teamID, cam, wName, wGrade, attackTimer, attackAngle);
        } else if (e.type == EntryType::RemoteEntity) {
            drawRemoteEntity(this, m_renderer, nullptr, net.remotes()[e.idx], cam);
        } else if (e.type == EntryType::LootBox) {
            drawLootBox(lootBoxes[e.idx], cam);
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
                        uint8_t allianceBits) {
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

        // 무기 실루엣 (단순 사각형 조합 — 총 모양)
        SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 50);
        SDL_Rect gunBody  = {WX + 12, WY + 30, 90, 18}; // 총신
        SDL_Rect gunGrip  = {WX + 70, WY + 48, 14, 22}; // 그립
        SDL_Rect gunBarrel= {WX + 102, WY + 33, 30, 10}; // 총구
        SDL_Rect gunMag   = {WX + 38, WY + 48, 10, 20}; // 탄창
        SDL_RenderFillRect(m_renderer, &gunBody);
        SDL_RenderFillRect(m_renderer, &gunGrip);
        SDL_RenderFillRect(m_renderer, &gunBarrel);
        SDL_RenderFillRect(m_renderer, &gunMag);
        // 실루엣 테두리
        SDL_SetRenderDrawColor(m_renderer, wc.r, wc.g, wc.b, 120);
        SDL_RenderDrawRect(m_renderer, &gunBody);

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
        drawPanel(eqX, tipY, slotW, 72, {12,15,24,220}, {30,38,55,255}, 1);
        drawText("단축키 안내", eqX+8, tipY+6, Col::TEXT_LO, m_fonts.get(11));
        drawText("[1] 주무기 선택", eqX+8, tipY+20, {80,160,255,200}, m_fonts.get(11));
        drawText("[2] 보조무기 선택", eqX+8, tipY+33, {80,160,255,200}, m_fonts.get(11));
        drawText("[3-5] 소모품 사용", eqX+8, tipY+46, {80,220,120,200}, m_fonts.get(11));
        drawText("[Q] 무기 교체", eqX+8, tipY+59, {180,180,180,180}, m_fonts.get(11));


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
void Renderer::drawCraftingUI(const ClientInventory& inv, int mouseX, int mouseY, int& outClickedRecipe) {
    outClickedRecipe = -1;
    TTF_Font* fSm  = m_fonts.get(13);
    TTF_Font* fMd  = m_fonts.get(16);
    TTF_Font* fLg  = m_fonts.get(20);
    TTF_Font* fKey = m_fonts.get(11);

    // 패널 크기
    const int CW = 540, CH = 460;
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
    const int ROW_H  = 76; // 더 굵직한 행 높이
    const int PAD    = 12;
    int recipeY = boxY + 66;
    const float ticks = SDL_GetTicks() * 0.001f;

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

            if (hov && mouseX>=btnX && mouseX<=btnX+BTN_W &&
                       mouseY>=btnY && mouseY<=btnY+BTN_H) {
                outClickedRecipe = rec.id;
            }
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

    // 하단 안내
    drawPanel(boxX, boxY+CH-32, CW, 32, {10,13,20,240}, Col::BORDER, 0);
    drawText("클릭으로 제작  |  워크벤치 표시 레시피는 근처에 워크벤치가 필요합니다",
             boxX+CW/2, boxY+CH-21, Col::TEXT_LO, fKey, true);
}


// ─────────────────────────────────────────────────────────────────────────────
// 미니맵
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::drawMinimap(const NetworkClient& net, float lx, float ly, int teamID) {
    const int MM  = 160;
    const int MMX = m_screenW - MM - 14;
    const int MMY = 14;
    const float MW = 80 * 32.0f;

    // 외곽 테두리 + 배경
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    drawPanel(MMX - 2, MMY - 2, MM + 4, MM + 32, {6, 8, 14, 230}, {60, 70, 100, 200}, 2);

    // 구역별 배경색
    // A: 주거 (좌상), B: 상업 (우중), C: 공업 (우하)
    struct ZoneColor { float x1, y1, x2, y2; SDL_Color col; };
    static const ZoneColor zones[] = {
        {0.0f, 0.0f, 0.43f, 0.43f, {80,  65,  20,  45}},   // A 주거
        {0.43f,0.43f,0.73f, 0.73f, {20,  40,  80,  45}},   // B 상업
        {0.68f,0.65f,1.0f,  1.0f,  {30,  50,  30,  45}},   // C 공업
    };
    for (auto& z : zones) {
        SDL_SetRenderDrawColor(m_renderer, z.col.r, z.col.g, z.col.b, z.col.a);
        SDL_Rect r = {
            MMX + static_cast<int>(z.x1 * MM),
            MMY + static_cast<int>(z.y1 * MM),
            static_cast<int>((z.x2 - z.x1) * MM),
            static_cast<int>((z.y2 - z.y1) * MM)
        };
        SDL_RenderFillRect(m_renderer, &r);
    }

    // 그리드 라인
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 12);
    for (int g = 1; g < 4; ++g) {
        int gx = MMX + MM * g / 4;
        int gy = MMY + MM * g / 4;
        SDL_RenderDrawLine(m_renderer, gx, MMY, gx, MMY + MM);
        SDL_RenderDrawLine(m_renderer, MMX, gy, MMX + MM, gy);
    }

    auto toMM = [&](float wx, float wy, int& px, int& py) {
        px = MMX + static_cast<int>((wx / MW) * MM);
        py = MMY + static_cast<int>((wy / MW) * MM);
    };

    // 탈출 구역 (pulsing)
    float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() * 0.004f);
    uint8_t extA = static_cast<uint8_t>(120 + 100 * pulse);
    // 맵 중앙에서 랜덤 탈출구이므로 대략적으로 표시 (실제 위치는 서버만 앎)
    // 미니맵에는 추정 위치를 보여주거나 건물 위치 기반으로 표시
    for (int i = 0; i < net.remoteCount(); ++i) {
        // extraction zone은 별도 전달이 필요 — 현재는 미니맵에서 생략
    }

    // 원격 엔티티들 (좀비 / 플레이어)
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
            // 다른 플레이어 — 팀 색으로
            SDL_Color tc = Col::TEAM[std::max(0, std::min(4, teamID))];
            drawFilledCircle(px, py, 3, {tc.r, tc.g, tc.b, 220});
        }
    }

    // 로컬 플레이어 (흰 점 + 팀 테두리)
    int lpx, lpy;
    toMM(lx, ly, lpx, lpy);
    SDL_Color tc = Col::TEAM[std::max(0, std::min(4, teamID))];
    drawFilledCircle(lpx, lpy, 5, tc);
    drawFilledCircle(lpx, lpy, 3, {255, 255, 255, 255});

    // 미니맵 테두리 재그리기 (엔티티가 경계 넘지 않게)
    SDL_SetRenderDrawColor(m_renderer, 60, 70, 100, 200);
    SDL_Rect border = {MMX, MMY, MM, MM};
    SDL_RenderDrawRect(m_renderer, &border);

    // 하단 레이블
    drawText("MAP  [M] 전체 지도", MMX + MM / 2, MMY + MM + 4, {140, 150, 180, 200}, m_fonts.get(10), true);
}

void Renderer::drawFullMap(const TileMap& map, const NetworkClient& net, float lx, float ly, int teamID) {
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

    // ── 구역 배경색 ──────────────────────────────────────────────────────────
    struct ZoneInfo { float x1, y1, x2, y2; SDL_Color col; const char* name; };
    static const ZoneInfo zoneInfos[] = {
        {0.0f,  0.0f,  0.43f, 0.43f, {80, 65, 20, 50},  "주거 구역"},
        {0.43f, 0.38f, 0.75f, 0.72f, {20, 35, 70, 50},  "상업 구역"},
        {0.68f, 0.65f, 1.0f,  1.0f,  {25, 50, 25, 50},  "공업 구역"},
        {0.0f,  0.65f, 0.42f, 1.0f,  {50, 30, 15, 35},  "외곽 지역"},
    };
    for (auto& z : zoneInfos) {
        SDL_SetRenderDrawColor(m_renderer, z.col.r, z.col.g, z.col.b, z.col.a);
        SDL_Rect zr = {
            X + static_cast<int>(z.x1 * W),
            Y + static_cast<int>(z.y1 * H),
            static_cast<int>((z.x2 - z.x1) * W),
            static_cast<int>((z.y2 - z.y1) * H)
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
    for (auto& z : zoneInfos) {
        float cx = z.x1 + (z.x2 - z.x1) * 0.5f;
        float cy = z.y1 + (z.y2 - z.y1) * 0.5f;
        drawText(z.name,
            X + static_cast<int>(cx * W),
            Y + static_cast<int>(cy * H) - 10,
            {220, 220, 220, 130}, fLg, true);
    }

    // ── 탈출 구역 표시 ────────────────────────────────────────────────────────
    float pulse = 0.5f + 0.5f * std::sin(SDL_GetTicks() * 0.004f);
    uint8_t extA = static_cast<uint8_t>(160 + 90 * pulse);
    // 탈출구역은 서버에서만 알기 때문에 일단 맵 설명만 표시
    // 실제 좌표는 S2C 패킷으로 받아야 함 — 현재는 레이블로 표시
    {
        int epx, epy;
        toMap(MW * 0.5f, MH * 0.5f, epx, epy); // 대략 중앙
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
                                     int mouseX, int mouseY, const Camera& cam) {
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
    bool canPlace = (tx >= 1 && ty >= 1 && tx < 79 && ty < 79);

    int sx, sy;
    cam.worldToScreen(static_cast<float>(tx * TS), static_cast<float>(ty * TS), sx, sy);
    int sw = static_cast<int>(TS * cam.zoom);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    // ── Ghost 프리뷰 — 건물 유형별 모양 ─────────────────────────────────────
    float pulse = std::sin(ticks * 4.0f) * 0.5f + 0.5f;
    uint8_t alpha = static_cast<uint8_t>(canPlace ? 110 + pulse*60 : 80);
    uint8_t borderA = static_cast<uint8_t>(canPlace ? 200 + pulse*55 : 160);

    // 색상: 바리케이드=갈색, 포탑=파란회색, 제작대=노란갈색 / 불가=빨강
    SDL_Color fillCol  = canPlace ? (buildType == 0 ? SDL_Color{120,80,40,alpha}
                                   : buildType == 1 ? SDL_Color{60,80,120,alpha}
                                                    : SDL_Color{160,120,50,alpha})
                                  : SDL_Color{160,30,30,alpha};
    SDL_Color lineCol  = canPlace ? (buildType == 0 ? SDL_Color{200,140,60,borderA}
                                   : buildType == 1 ? SDL_Color{80,160,220,borderA}
                                                    : SDL_Color{220,180,60,borderA})
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
        // 포탑: 원형 포신 표시
        drawFilledCircle(sx+sw/2, sy+sw/2, sw/4,
                         {lineCol.r,lineCol.g,lineCol.b,static_cast<uint8_t>(borderA/2)});
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA);
        SDL_RenderDrawLine(m_renderer, sx+sw/2, sy+sw/2, sx+sw/2, sy+2);
    } else {
        // 제작대: 3×3 격자
        int cellSz = sw/3;
        SDL_SetRenderDrawColor(m_renderer, lineCol.r, lineCol.g, lineCol.b, borderA/3);
        for (int i=1;i<3;++i) {
            SDL_RenderDrawLine(m_renderer, sx+cellSz*i, sy, sx+cellSz*i, sy+sw);
            SDL_RenderDrawLine(m_renderer, sx, sy+cellSz*i, sx+sw, sy+cellSz*i);
        }
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
    static const char* typeNames[] = {"바리케이드", "포탑", "제작대"};
    const char* typeName = (buildType >= 0 && buildType < 3) ? typeNames[buildType] : "?";
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
