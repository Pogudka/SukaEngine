#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cmath>

#include "Core.hpp"

namespace suka {

struct InputState {
    float joystickX = 0.0f;
    float joystickY = 0.0f;
    bool jumpPressed = false;
    bool attackPressed = false;
};

struct Context {
    InputState input;
    double time = 0.0;

    int score = 0;
    Vec2 playerPos;
    bool hasPlayer = false;

    bool coinCollectedThisFrame = false;
    bool jumpPressedThisFrame = false;

    std::map<std::string, double> vars;
};

enum class TouchType { Down, Move, Up };

struct TouchEvent {
    TouchType type = TouchType::Down;
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct VirtualJoystick {
    bool active = false;
    int fingerId = -1;

    float baseX = 0, baseY = 0;
    float stickX = 0, stickY = 0;
    float radius = 140.0f;

    float axisX = 0, axisY = 0;

    void reset() {
        active = false;
        fingerId = -1;
        axisX = 0;
        axisY = 0;
    }

    void updateStick(float x, float y) {
        float dx = x - baseX;
        float dy = y - baseY;
        float len = std::sqrt(dx * dx + dy * dy);

        if (len > radius) {
            dx = dx / len * radius;
            dy = dy / len * radius;
        }

        stickX = baseX + dx;
        stickY = baseY + dy;

        axisX = dx / radius;
        axisY = dy / radius;
    }

    void handle(const TouchEvent& e, float screenW, float screenH) {
        if (e.type == TouchType::Down) {
            if (!active && e.x < screenW * 0.5f) {
                active = true;
                fingerId = e.id;
                baseX = e.x; baseY = e.y;
                stickX = e.x; stickY = e.y;
                axisX = 0; axisY = 0;
            } else if (active && e.id == fingerId) {
                updateStick(e.x, e.y);
            }
        }
        else if (e.type == TouchType::Move) {
            if (active && e.id == fingerId) updateStick(e.x, e.y);
        }
        else if (e.type == TouchType::Up) {
            if (active && e.id == fingerId) reset();
        }
    }
};

struct TouchButton {
    std::string id;
    Rect rect;
    bool pressed = false;
    int fingerId = -1;
    bool pressEdge = false;

    bool handle(const TouchEvent& e) {
        if (e.type == TouchType::Down) {
            if (!pressed && rect.contains(e.x, e.y)) {
                pressed = true;
                fingerId = e.id;
                pressEdge = true;
                return true;
            }
            return false;
        }
        if (e.type == TouchType::Up) {
            if (pressed && e.id == fingerId) {
                pressed = false;
                fingerId = -1;
                return true;
            }
            return false;
        }
        return false;
    }
};

struct UiButton {
    TouchButton touch;
    std::string text;
    std::string action;
    unsigned int color = 0xFF3A3A4A;
    float angle = 0.0f;              // BTN-ROT: поворот кнопки в градусах
    std::string texture;             // BTN-TEX: относительный путь к картинке
};

class InputManager {
public:
    float screenWidth = 1280.0f;
    float screenHeight = 720.0f;

    VirtualJoystick joystick;
    std::vector<UiButton>* ui = nullptr;

    void setUi(std::vector<UiButton>* uiButtons) {
        ui = uiButtons;
    }

    void feed(const TouchEvent& e) {
        bool consumed = false;

        if (ui) {
            for (auto& b : *ui) {
                if (b.touch.handle(e)) consumed = true;
            }
        }

        if (e.type == TouchType::Down && consumed) return;

        joystick.handle(e, screenWidth, screenHeight);
    }

    InputState state() const {
        InputState s;
        s.joystickX = joystick.axisX;
        s.joystickY = joystick.axisY;

        if (ui) {
            for (const auto& b : *ui) {
                if (b.touch.id == "jump") s.jumpPressed = b.touch.pressed;
                if (b.touch.id == "attack") s.attackPressed = b.touch.pressed;
            }
        }
        return s;
    }

    void endFrame() {
        if (ui) {
            for (auto& b : *ui) b.touch.pressEdge = false;
        }
    }

    void printStatus() const {
        std::cout << "  input: joy=(" << joystick.axisX << ", " << joystick.axisY << ")";
        if (ui) {
            for (const auto& b : *ui) {
                std::cout << " " << b.touch.id << "=" << (b.touch.pressed ? 1 : 0);
            }
        }
        std::cout << "\n";
    }
};

} // namespace suka
