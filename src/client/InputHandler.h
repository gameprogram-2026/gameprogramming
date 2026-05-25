#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <SDL2/SDL.h>
#include <SDL2/SDL_scancode.h>
#include "shared/network/Protocol.h"

namespace dz {

struct InputState {
    float    moveX     = 0.0f;
    float    moveY     = 0.0f;
    float    aimAngle  = 0.0f;
    uint16_t actions   = ACT_NONE;
    uint32_t seqNum    = 0;
    float    dt        = 0.0f;
};

class InputHandler {
public:
    bool poll(InputState& out, float playerScreenX, float playerScreenY);
    bool pollUI();

    bool wantsQuit() const noexcept { return m_quit; }

    bool peekClick(int& x, int& y) const noexcept {
        if (!m_hasClick) return false;
        x = m_clickX; y = m_clickY; return true;
    }
    bool consumeClick(int& x, int& y) noexcept;
    bool consumeMouseUp(int& x, int& y) noexcept;
    void mousePos(int& x, int& y) const noexcept;

    void setFocusedTextInput(std::string* target) {
        m_focusedTextInput = target;
        if (target) SDL_StartTextInput();
        else SDL_StopTextInput();
    }

    bool isKeyDown(SDL_Scancode sc) const noexcept {
        return sc < SDL_NUM_SCANCODES && m_keys[sc];
    }

private:
    bool     m_quit          = false;
    uint32_t m_seq           = 0;
    bool     m_hasClick      = false;
    int      m_clickX        = 0;
    int      m_clickY        = 0;
    bool     m_hasMouseUp    = false;
    int      m_mouseUpX      = 0;
    int      m_mouseUpY      = 0;
    int      m_mouseX        = 0;
    int      m_mouseY        = 0;

    std::string* m_focusedTextInput = nullptr;

    // 포커스 추적: FOCUS_LOST 직후 KEYUP은 실제 사용자 입력이 아니므로 무시
    bool     m_hasFocus      = true;
    uint32_t m_focusLostTime = 0;

    std::array<bool, SDL_NUM_SCANCODES> m_keys{};
};

} // namespace dz
