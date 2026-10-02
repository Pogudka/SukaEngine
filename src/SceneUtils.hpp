#pragma once

#include <string>
#include <cmath>

#include "Scene.hpp"

namespace suka {

inline int countNodes(const Node* n) {
    if (!n) return 0;

    int c = 1;

    for (auto& ch : n->getChildren()) {
        c += countNodes(ch.get());
    }

    return c;
}

inline bool nodeWorldRec(
    Node* n,
    const std::string& name,
    float ox,
    float oy,
    float orot,
    float osx,
    float osy,
    float& wx,
    float& wy,
    float& wr,
    float& wsx,
    float& wsy
) {
    Node2D* n2d = dynamic_cast<Node2D*>(n);

    if (!n2d) {
        for (auto& ch : n->getChildren()) {
            if (nodeWorldRec(ch.get(), name, ox, oy, orot, osx, osy, wx, wy, wr, wsx, wsy)) {
                return true;
            }
        }

        return false;
    }

    float cr = std::cos(orot);
    float sr = std::sin(orot);

    float cx = ox + (n2d->position.x * osx) * cr - (n2d->position.y * osy) * sr;
    float cy = oy + (n2d->position.x * osx) * sr + (n2d->position.y * osy) * cr;

    float crot = orot + n2d->rotation;
    float csx = osx * n2d->scale.x;
    float csy = osy * n2d->scale.y;

    if (n2d->name == name) {
        wx = cx;
        wy = cy;
        wr = crot;
        wsx = csx;
        wsy = csy;

        return true;
    }

    for (auto& ch : n2d->getChildren()) {
        if (nodeWorldRec(ch.get(), name, cx, cy, crot, csx, csy, wx, wy, wr, wsx, wsy)) {
            return true;
        }
    }

    return false;
}

inline bool nodeWorld(
    Scene* sc,
    const std::string& name,
    float& wx,
    float& wy,
    float& wr,
    float& wsx,
    float& wsy
) {
    if (!sc || !sc->root) return false;

    return nodeWorldRec(sc->root.get(), name, 0, 0, 0, 1, 1, wx, wy, wr, wsx, wsy);
}

inline bool parentTransform(
    Node* n,
    const std::string& name,
    float cx,
    float cy,
    float sx,
    float sy,
    float& ox,
    float& oy,
    float& psx,
    float& psy,
    bool& found
) {
    if (found) return true;

    if (n->name == name) {
        ox = cx;
        oy = cy;
        psx = sx;
        psy = sy;
        found = true;

        return true;
    }

    float ncx = cx;
    float ncy = cy;
    float nsx = sx;
    float nsy = sy;

    Node2D* n2 = dynamic_cast<Node2D*>(n);

    if (n2) {
        float cr = std::cos(n2->rotation);
        float sr = std::sin(n2->rotation);

        ncx = cx + (n2->position.x * sx) * cr - (n2->position.y * sy) * sr;
        ncy = cy + (n2->position.x * sx) * sr + (n2->position.y * sy) * cr;

        nsx = sx * n2->scale.x;
        nsy = sy * n2->scale.y;
    }

    for (auto& ch : n->getChildren()) {
        parentTransform(ch.get(), name, ncx, ncy, nsx, nsy, ox, oy, psx, psy, found);
    }

    return found;
}

inline void moveGroupButtons(Scene* es, const std::string& group, float dx, float dy) {
    if (!es) return;

    for (auto& b : es->ui) {
        if (b.group == group) {
            b.touch.rect.x += dx;
            b.touch.rect.y += dy;
        }
    }
}

inline void collectHit(
    const Node* n,
    float wx,
    float wy,
    float ox,
    float oy,
    float psx,
    float psy,
    std::string& bestBox,
    std::string& bestNear,
    float& bestDist
) {
    if (!n) return;

    std::string tn = std::string(n->typeName());

    if (tn != "Node" && tn != "Camera2D" && n->name.rfind("__", 0) != 0) {
        const Node2D* d = dynamic_cast<const Node2D*>(n);

        if (d) {
            float cxw = ox + d->position.x * psx;
            float cyw = oy + d->position.y * psy;

            float hw = (d->w * d->scale.x * psx) / 2;
            float hh = (d->h * d->scale.y * psy) / 2;

            if (hw < 28) hw = 28;
            if (hh < 28) hh = 28;

            if (wx >= cxw - hw && wx <= cxw + hw && wy >= cyw - hh && wy <= cyw + hh) {
                if (bestBox.empty()) bestBox = d->name;
            }

            float dx = wx - cxw;
            float dy = wy - cyw;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < 45.0f && dist < bestDist) {
                bestDist = dist;
                bestNear = d->name;
            }

            for (const auto& ch : n->getChildren()) {
                collectHit(
                    ch.get(),
                    wx,
                    wy,
                    cxw,
                    cyw,
                    psx * d->scale.x,
                    psy * d->scale.y,
                    bestBox,
                    bestNear,
                    bestDist
                );
            }

            return;
        }
    }

    for (const auto& ch : n->getChildren()) {
        collectHit(ch.get(), wx, wy, ox, oy, psx, psy, bestBox, bestNear, bestDist);
    }
}

inline std::string hitTest(const Node* n, float wx, float wy) {
    if (!n) return "";

    std::string bestBox;
    std::string bestNear;
    float bestDist = 1e9f;

    collectHit(n, wx, wy, 0, 0, 1, 1, bestBox, bestNear, bestDist);

    return bestBox.empty() ? bestNear : bestBox;
}

} // namespace suka
