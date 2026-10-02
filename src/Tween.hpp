#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <functional>
#include <algorithm>

#include "Scene.hpp"

namespace suka {

inline constexpr double TWEEN_PI = 3.14159265358979323846;

enum class TweenProp {
    None,
    X,
    Y,
    Rotation,
    ScaleX,
    ScaleY,
    Scale,
    Alpha,
    Width,
    Height
};

inline std::string tweenLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline TweenProp tweenPropFromString(const std::string& raw) {
    std::string s = tweenLower(raw);

    if (s == "x" || s == "pos_x" || s == "position_x") return TweenProp::X;
    if (s == "y" || s == "pos_y" || s == "position_y") return TweenProp::Y;

    if (s == "rotation" || s == "rot" || s == "angle") return TweenProp::Rotation;

    if (s == "scale" || s == "scale_both" || s == "scalexy") return TweenProp::Scale;
    if (s == "scale_x" || s == "sx") return TweenProp::ScaleX;
    if (s == "scale_y" || s == "sy") return TweenProp::ScaleY;

    if (s == "alpha" || s == "a") return TweenProp::Alpha;

    if (s == "width" || s == "w") return TweenProp::Width;
    if (s == "height" || s == "h") return TweenProp::Height;

    return TweenProp::None;
}

inline int tweenEaseFromString(const std::string& raw) {
    std::string s = tweenLower(raw);

    if (s == "linear" || s == "0") return 0;

    if (s == "in" || s == "ease_in" || s == "ease_in_quad" || s == "1") return 1;
    if (s == "out" || s == "ease_out" || s == "ease_out_quad" || s == "2") return 2;
    if (s == "in_out" || s == "ease_in_out" || s == "ease_in_out_quad" || s == "3") return 3;

    if (s == "back" || s == "ease_out_back" || s == "4") return 4;
    if (s == "elastic" || s == "ease_out_elastic" || s == "5") return 5;

    return 0;
}

inline double tweenClamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

inline double tweenEase(int mode, double t) {
    t = tweenClamp01(t);

    switch (mode) {
        case 1:
            return t * t;

        case 2:
            return t * (2.0 - t);

        case 3:
            if (t < 0.5) return 2.0 * t * t;
            return -1.0 + (4.0 - 2.0 * t) * t;

        case 4: {
            double c1 = 1.70158;
            double c3 = c1 + 1.0;
            return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
        }

        case 5: {
            if (t == 0.0 || t == 1.0) return t;

            double p = 0.3;
            return std::pow(2.0, -10.0 * t) *
                   std::sin((t - p / 4.0) * (2.0 * TWEEN_PI) / p) +
                   1.0;
        }

        case 0:
        default:
            return t;
    }
}

inline double tweenGet(Node2D* n, TweenProp prop) {
    if (!n) return 0.0;

    switch (prop) {
        case TweenProp::X:
            return n->position.x;

        case TweenProp::Y:
            return n->position.y;

        case TweenProp::Rotation:
            return n->rotation * 180.0 / TWEEN_PI;

        case TweenProp::ScaleX:
            return n->scale.x;

        case TweenProp::ScaleY:
            return n->scale.y;

        case TweenProp::Scale:
            return (n->scale.x + n->scale.y) * 0.5;

        case TweenProp::Alpha:
            return n->alpha;

        case TweenProp::Width:
            return n->w;

        case TweenProp::Height:
            return n->h;

        case TweenProp::None:
        default:
            return 0.0;
    }
}

inline void tweenSet(Node2D* n, TweenProp prop, double v) {
    if (!n) return;

    switch (prop) {
        case TweenProp::X:
            n->position.x = (float)v;
            break;

        case TweenProp::Y:
            n->position.y = (float)v;
            break;

        case TweenProp::Rotation:
            n->rotation = (float)(v * TWEEN_PI / 180.0);
            break;

        case TweenProp::ScaleX:
            n->scale.x = (float)v;
            break;

        case TweenProp::ScaleY:
            n->scale.y = (float)v;
            break;

        case TweenProp::Scale:
            n->scale.x = (float)v;
            n->scale.y = (float)v;
            break;

        case TweenProp::Alpha:
            n->alpha = (float)v;
            break;

        case TweenProp::Width:
            n->w = (float)v;
            break;

        case TweenProp::Height:
            n->h = (float)v;
            break;

        case TweenProp::None:
        default:
            break;
    }
}

struct Tween {
    int id = 0;

    std::string node;
    TweenProp prop = TweenProp::None;

    double from = 0.0;
    double to = 0.0;

    float duration = 0.0f;
    float elapsed = 0.0f;

    int easing = 0;

    bool loop = false;
    bool yoyo = false;

    bool hasFrom = false;
    bool active = true;

    std::string onComplete;
};

class TweenManager {
public:
    size_t count() const {
        return list_.size();
    }

    bool isActive(int id) const {
        for (const auto& t : list_) {
            if (t.id == id && t.active) return true;
        }
        return false;
    }

    int add(
        const std::string& node,
        const std::string& prop,
        double to,
        float duration,
        const std::string& easing = "linear",
        bool loop = false,
        bool yoyo = false,
        const std::string& onComplete = ""
    ) {
        TweenProp p = tweenPropFromString(prop);

        if (node.empty() || p == TweenProp::None) {
            return 0;
        }

        if (duration < 0.0f) duration = 0.0f;

        Tween t;
        t.id = nextId_++;
        t.node = node;
        t.prop = p;
        t.to = to;
        t.duration = duration;
        t.easing = tweenEaseFromString(easing);
        t.loop = loop;
        t.yoyo = yoyo;
        t.onComplete = onComplete;

        list_.push_back(t);

        return t.id;
    }

    void stop(int id) {
        for (size_t i = list_.size(); i-- > 0;) {
            if (list_[i].id == id) {
                list_.erase(list_.begin() + i);
            }
        }
    }

    void stopNode(const std::string& node) {
        for (size_t i = list_.size(); i-- > 0;) {
            if (list_[i].node == node) {
                list_.erase(list_.begin() + i);
            }
        }
    }

    void clear() {
        list_.clear();
        nextId_ = 1;
    }

    void update(
        float dt,
        Scene* scene,
        const std::function<void(const std::string&)>& onDone
    ) {
        std::vector<std::string> completed;

        if (!scene || !scene->root) {
            clear();
            return;
        }

        for (size_t i = list_.size(); i-- > 0;) {
            Tween& t = list_[i];

            if (!t.active || t.prop == TweenProp::None) {
                list_.erase(list_.begin() + i);
                continue;
            }

            Node* fn = scene->root->findNode(t.node);
            Node2D* n = dynamic_cast<Node2D*>(fn);

            if (!n) {
                list_.erase(list_.begin() + i);
                continue;
            }

            if (!t.hasFrom) {
                t.from = tweenGet(n, t.prop);
                t.hasFrom = true;
            }

            if (t.duration <= 0.0001f) {
                tweenSet(n, t.prop, t.to);

                if (!t.onComplete.empty()) {
                    completed.push_back(t.onComplete);
                }

                list_.erase(list_.begin() + i);
                continue;
            }

            t.elapsed += dt;

            float progress = t.elapsed / t.duration;

            if (progress >= 1.0f) {
                if (t.yoyo) {
                    std::swap(t.from, t.to);
                    t.elapsed = 0.0f;
                    progress = 0.0f;

                    if (!t.onComplete.empty()) {
                        completed.push_back(t.onComplete);
                    }
                } else if (t.loop) {
                    t.elapsed = 0.0f;
                    progress = 0.0f;

                    tweenSet(n, t.prop, t.from);

                    if (!t.onComplete.empty()) {
                        completed.push_back(t.onComplete);
                    }
                } else {
                    tweenSet(n, t.prop, t.to);

                    if (!t.onComplete.empty()) {
                        completed.push_back(t.onComplete);
                    }

                    list_.erase(list_.begin() + i);
                    continue;
                }
            }

            double e = tweenEase(t.easing, (double)progress);
            double v = t.from + (t.to - t.from) * e;

            tweenSet(n, t.prop, v);
        }

        if (onDone) {
            for (const auto& fn : completed) {
                onDone(fn);
            }
        }
    }

private:
    std::vector<Tween> list_;
    int nextId_ = 1;
};

inline TweenManager g_tweens;

} // namespace suka
