#pragma once

#include "Scene.hpp"
#include "Input.hpp"

namespace suka {

struct RawTouch {
    enum class Action { Down, Move, Up } action = Action::Down;
    float x = 0, y = 0;
    int id = 0;
};

// TouchProcessor: только кнопки. Нативный джойстик намеренно удалён
// (раньше блок if (raw.x < 640) двигал input.joystick.axisX/Y).
// В level2 движение делается через get_button_pressed + Lua, поэтому
// джойстик здесь не нужен и не должен перехватывать левую половину экрана.
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

        // Нативный джойстик удалён. Если когда-нибудь вернёшь платформерный
        // Player и захочешь стик — восстанови блок отсюда и до resetJoystick.
    }

    void resetJoystick() { /* джойстика нет, пусто */ }

private:
    int joyFinger_ = -1;
};

} // namespace suka
