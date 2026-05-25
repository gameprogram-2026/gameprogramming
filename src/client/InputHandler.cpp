#include "InputHandler.h"
#include <SDL2/SDL.h>
#include <cmath>

namespace dz {

bool InputHandler::pollUI() {
    m_hasClick = false;
    m_hasMouseUp = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { m_quit = true; }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.scancode < SDL_NUM_SCANCODES) {
                if (!e.key.repeat) m_keys[e.key.keysym.scancode] = true;
            }
            if (e.key.keysym.sym == SDLK_BACKSPACE && m_focusedTextInput && !m_focusedTextInput->empty()) {
                m_focusedTextInput->pop_back();
            }
        }
        if (e.type == SDL_KEYUP) {
            if (e.key.keysym.scancode < SDL_NUM_SCANCODES)
                m_keys[e.key.keysym.scancode] = false;
        }
        if (e.type == SDL_TEXTINPUT && m_focusedTextInput) {
            // Very simple ASCII-only limit, up to 15 chars (fits in char[16] with null terminator)
            if (m_focusedTextInput->size() + std::strlen(e.text.text) < 16) {
                *m_focusedTextInput += e.text.text;
            }
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            m_hasClick = true; m_clickX = e.button.x; m_clickY = e.button.y;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            m_hasMouseUp = true; m_mouseUpX = e.button.x; m_mouseUpY = e.button.y;
        }
    }
    SDL_GetMouseState(&m_mouseX, &m_mouseY);
    return !m_quit;
}

bool InputHandler::consumeClick(int& x, int& y) noexcept {
    if (!m_hasClick) return false;
    x = m_clickX; y = m_clickY; m_hasClick = false; return true;
}

bool InputHandler::consumeMouseUp(int& x, int& y) noexcept {
    if (!m_hasMouseUp) return false;
    x = m_mouseUpX; y = m_mouseUpY; m_hasMouseUp = false; return true;
}

void InputHandler::mousePos(int& x, int& y) const noexcept {
    x = m_mouseX; y = m_mouseY;
}

bool InputHandler::poll(InputState& out, float playerScreenX, float playerScreenY) {
    m_hasClick   = false;
    m_hasMouseUp = false;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { m_quit = true; }

        // 포커스 이벤트 처리
        if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                m_hasFocus      = false;
                m_focusLostTime = SDL_GetTicks();
            }
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                m_hasFocus = true;
                // 포커스 복구 시 SDL 실제 키 상태로 동기화
                const uint8_t* sdlKeys = SDL_GetKeyboardState(nullptr);
                for (int i = 0; i < SDL_NUM_SCANCODES; ++i)
                    m_keys[i] = (sdlKeys[i] != 0);
            }
        }

        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.scancode < SDL_NUM_SCANCODES)
                m_keys[e.key.keysym.scancode] = true;
        }
        if (e.type == SDL_KEYUP) {
            // FOCUS_LOST 후 200ms 이내의 KEYUP은 포커스 전환에 의한 것 — 무시
            bool isFocusLossKeyUp = !m_hasFocus ||
                                    (SDL_GetTicks() - m_focusLostTime < 200);
            if (!isFocusLossKeyUp) {
                if (e.key.keysym.scancode < SDL_NUM_SCANCODES)
                    m_keys[e.key.keysym.scancode] = false;
            }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            m_hasClick = true; m_clickX = e.button.x; m_clickY = e.button.y;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            m_hasMouseUp = true; m_mouseUpX = e.button.x; m_mouseUpY = e.button.y;
        }
    }

    // 이동: 이벤트 추적 배열 사용
    out.moveX = 0.0f; out.moveY = 0.0f;
    if (m_keys[SDL_SCANCODE_W] || m_keys[SDL_SCANCODE_UP])    out.moveY -= 1.0f;
    if (m_keys[SDL_SCANCODE_S] || m_keys[SDL_SCANCODE_DOWN])  out.moveY += 1.0f;
    if (m_keys[SDL_SCANCODE_A] || m_keys[SDL_SCANCODE_LEFT])  out.moveX -= 1.0f;
    if (m_keys[SDL_SCANCODE_D] || m_keys[SDL_SCANCODE_RIGHT]) out.moveX += 1.0f;

    float len = std::sqrt(out.moveX * out.moveX + out.moveY * out.moveY);
    if (len > 0.01f) { out.moveX /= len; out.moveY /= len; }

    out.actions = ACT_NONE;
    if (m_keys[SDL_SCANCODE_LSHIFT]) out.actions |= ACT_SPRINT;
    if (m_keys[SDL_SCANCODE_LCTRL])  out.actions |= ACT_CROUCH;
    if (m_keys[SDL_SCANCODE_R])      out.actions |= ACT_RELOAD;
    if (m_keys[SDL_SCANCODE_F])      out.actions |= ACT_INTERACT;
    if (m_keys[SDL_SCANCODE_TAB])    out.actions |= ACT_INVENTORY;
    if (m_keys[SDL_SCANCODE_M])      out.actions |= ACT_MAP;

    int mx, my;
    uint32_t mb = SDL_GetMouseState(&mx, &my);
    m_mouseX = mx; m_mouseY = my;
    if (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  out.actions |= ACT_SHOOT | ACT_MELEE;
    if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) out.actions |= ACT_AIM;

    float dx = static_cast<float>(mx) - playerScreenX;
    float dy = static_cast<float>(my) - playerScreenY;
    out.aimAngle = std::atan2(dx, -dy) * (180.0f / 3.14159265f);
    if (out.aimAngle < 0.0f) out.aimAngle += 360.0f;

    out.seqNum = ++m_seq;
    return !m_quit;
}

} // namespace dz
