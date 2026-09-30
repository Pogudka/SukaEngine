#pragma once

#include "Scene.hpp"
#include "Input.hpp"

namespace suka {

struct RawTouch {
    enum class Action { Down, Move, Up } action = Action::Down;
    float x = 0, y = 0;
    int id = 0;
};

class TouchProcessor {
public:
    void onTouch(const RawTouch& raw, Scene& scene, InputManager& input) {
        for (auto& b : scene.ui) {
            const bool inside =
                raw.x >= b.touch.rect.x && raw.x <= b.touch.rect.x + b.touch.rect.w &&
                raw.y >= b.touch.rect.y && raw.y <= b.touch.rect.y + b.touch.rect.h;

            if (raw.action == RawTouch::Action::Down && inside && !b.touch.pressed) {
                b.touch.pressed = true;
                b.touch.fingerId = raw.id;
                b.touch.pressEdge = true;
            }
            if (raw.action == RawTouch::Action::Up &&
                b.touch.pressed && b.touch.fingerId == raw.id) {
                b.touch.pressed = false;
                b.touch.fingerId = -1;
            }
        }

        if (raw.action == RawTouch::Action::Down || raw.action == RawTouch::Action::Move) {
            if (raw.x < 640) {
                if (!input.joystick.active && raw.action == RawTouch::Action::Down) {
                    input.joystick.active = true;
                    joyFinger_ = raw.id;
                }
                if (input.joystick.active && raw.id == joyFinger_) {
                    input.joystick.axisX = (raw.x - 320) / 320.0f;
                    input.joystick.axisY = (raw.y - 360) / 360.0f;
                    if (input.joystick.axisX >  1.0f) input.joystick.axisX =  1.0f;
                    if (input.joystick.axisX < -1.0f) input.joystick.axisX = -1.0f;
                    if (input.joystick.axisY >  1.0f) input.joystick.axisY =  1.0f;
                    if (input.joystick.axisY < -1.0f) input.joystick.axisY = -1.0f;
                }
            }
        }

        if (raw.action == RawTouch::Action::Up && raw.id == joyFinger_) {
            input.joystick.active = false;
            input.joystick.axisX = 0;
            input.joystick.axisY = 0;
            joyFinger_ = -1;
        }
    }

    void resetJoystick() { joyFinger_ = -1; }

private:
    int joyFinger_ = -1;
};

} // namespace suka
