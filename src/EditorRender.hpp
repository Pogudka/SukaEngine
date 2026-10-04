#pragma once

#include <string>
#include <cmath>

#include "Scene.hpp"
#include "Editor.hpp"
#include "Render.hpp"
#include "CommonTypes.hpp"
#include "SceneUtils.hpp"

namespace suka {

struct EditorRenderInput {
    Editor* editor;
    float edZoom;
    Manip manip;
    bool pickParent;
    const std::string& pickChild;
    const std::string& lastMsg;
    float fps;
    float viewW;
    float viewH;
    bool viewVert;    // ROT:PORT -> рамка камеры вертикальная
    float viewRatio;  // отношение сторон текущего окна (для расчёта вертикальной рамки)
};

static const float EDV_X0 = 300.0f;
static const float EDV_Y0 = 64.0f;
static const float EDV_W  = 592.0f;
static const float EDV_H  = 492.0f;
static const float EDV_CX = EDV_X0 + EDV_W * 0.5f;
static const float EDV_CY = EDV_Y0 + EDV_H * 0.5f;

inline void emitNodePreview(
    const Node* n,
    float CX, float CY, float S,
    float VX0, float VY0, float VW, float VH,
    float camX, float camY,
    float ox, float oy, float orot, float osx, float osy,
    std::string& out
) {
    if (!n) return;

    Node2D* n2d = dynamic_cast<Node2D*>(const_cast<Node*>(n));

    if (!n2d) {
        for (const auto& ch : n->getChildren()) {
            emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, camX, camY,
                            ox, oy, orot, osx, osy, out);
        }
        return;
    }

    float cr = std::cos(orot);
    float sr = std::sin(orot);

    float wx = ox + (n2d->position.x * osx) * cr - (n2d->position.y * osy) * sr;
    float wy = oy + (n2d->position.x * osx) * sr + (n2d->position.y * osy) * cr;

    float wrot = orot + n2d->rotation;
    float wsx = osx * n2d->scale.x;
    float wsy = osy * n2d->scale.y;

    std::string tn = std::string(n2d->typeName());

    float sx = CX + (wx - camX - 640) * S;
    float sy = CY + (wy - camY - 360) * S;

    float ang = wrot * 57.2957795f;
    unsigned colA = withAlpha(n2d->color, n2d->alpha);

    if (tn == "Camera2D") {
        out += "DRAW rect|" + std::to_string((int)(sx - 14)) + "|" +
               std::to_string((int)(sy - 10)) + "|28|20|#FFD700|0\n";
        out += "DRAW text|CAM|" + std::to_string((int)(sx - 12)) + "|" +
               std::to_string((int)(sy + 12)) + "|12|#FFD700|0\n";
    } else if (tn == "Light2D") {
        Light2D* li = static_cast<Light2D*>(n2d);
        float r = li->radius * ((wsx + wsy) * 0.5f) * S;
        out += "DRAW shape|glow|" + std::to_string((int)(sx - r)) + "|" +
               std::to_string((int)(sy - r)) + "|" +
               std::to_string((int)(2 * r)) + "|" +
               std::to_string((int)(2 * r)) + "|" +
               colorToHexA(colA) + "|0\n";
    } else if (tn == "Prefab2D") {
        float w = n2d->w * wsx * S;
        float h = n2d->h * wsy * S;
        if (w < 24) w = 24;
        if (h < 24) h = 24;
        out += "DRAW shape|diamond|" + std::to_string((int)(sx - w / 2)) + "|" +
               std::to_string((int)(sy - h / 2)) + "|" +
               std::to_string((int)w) + "|" + std::to_string((int)h) + "|" +
               colorToHexA(withAlpha(0x8E44ADFFu, n2d->alpha)) + "|" +
               std::to_string(ang) + "\n";
        out += "DRAW text|PREFAB|" + std::to_string((int)(sx - 24)) + "|" +
               std::to_string((int)(sy - 6)) + "|12|#FFFFFF|0\n";
        out += "DRAW text|" + n2d->name + "|" + std::to_string((int)(sx + w / 2 + 6)) + "|" +
               std::to_string((int)(sy + 4)) + "|12|#8E44AD|0\n";
        return;
    } else if (tn != "Node") {
        float w = n2d->w * wsx * S;
        float h = n2d->h * wsy * S;

        float rx = sx - w / 2;
        float ry = sy - h / 2;

        if (!n2d->hasAppearance() && tn == "Node2D") {
            out += "DRAW rect|" + std::to_string((int)(sx - 10)) + "|" +
                   std::to_string((int)(sy - 2)) + "|20|4|#808080|0\n";
            out += "DRAW rect|" + std::to_string((int)(sx - 2)) + "|" +
                   std::to_string((int)(sy - 10)) + "|4|20|#808080|0\n";
            out += "DRAW text|" + n2d->name + "|" +
                   std::to_string((int)(sx + 12)) + "|" +
                   std::to_string((int)(sy + 4)) + "|12|#808080|0\n";
        }

        if (tn == "Label") {
            int fs = (int)(static_cast<Label*>(n2d)->fontSize * ((wsx + wsy) * 0.5f) * S);
            if (fs < 6) fs = 6;
            out += "DRAW text|" + static_cast<Label*>(n2d)->text + "|" +
                   std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" +
                   std::to_string(fs) + "|" + colorToHexA(colA) + "|" +
                   std::to_string(ang) + "\n";
        } else if (tn == "Sprite2D") {
            out += "DRAW rect|" + std::to_string((int)rx) + "|" +
                   std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" +
                   std::to_string((int)h) + "|" +
                   colorToHexA(withAlpha(0x555555FFu, n2d->alpha)) + "|" +
                   std::to_string(ang) + "\n";
        } else if (n2d->hasAppearance()) {
            out += "DRAW shape|" + n2d->shape + "|" +
                   std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" +
                   std::to_string((int)w) + "|" + std::to_string((int)h) + "|" +
                   colorToHexA(colA) + "|" + std::to_string(ang) + "\n";
        }
    }

    for (const auto& ch : n2d->getChildren()) {
        emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, camX, camY,
                        wx, wy, wrot, wsx, wsy, out);
    }
}

inline void emitEditorViewport(const Scene& sc, std::string& out, const EditorRenderInput& in) {
    const float VX0 = EDV_X0, VY0 = EDV_Y0, VW = EDV_W, VH = EDV_H;
    const float CX = EDV_CX, CY = EDV_CY;

    const float S = 0.46875f * in.edZoom;

    if (sc.bgSet()) {
        out += "DRAW rect|" + std::to_string((int)VX0) + "|" +
               std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" +
               std::to_string((int)VH) + "|" + sc.bg + "|0\n";
    }

    out += "DRAW rect|" + std::to_string((int)VX0) + "|" +
           std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" +
           std::to_string((int)VH) + "|#23232B|0\n";

    // Бесконечная адаптивная сетка + оси мира
    {
        float wx0 = sc.camX + 640.0f + (VX0 - CX) / S;
        float wx1 = sc.camX + 640.0f + (VX0 + VW - CX) / S;
        float wy0 = sc.camY + 360.0f + (VY0 - CY) / S;
        float wy1 = sc.camY + 360.0f + (VY0 + VH - CY) / S;

        float step = 64.0f;
        while (step * S < 24.0f) step *= 2.0f;
        while (step * S > 192.0f && step > 4.0f) step *= 0.5f;

        for (float gx = std::floor(wx0 / step) * step; gx <= wx1; gx += step) {
            float px = CX + (gx - sc.camX - 640.0f) * S;
            out += "DRAW rect|" + std::to_string((int)px) + "|" +
                   std::to_string((int)VY0) + "|1|" + std::to_string((int)VH) +
                   "|#33333D|0\n";
        }
        for (float gy = std::floor(wy0 / step) * step; gy <= wy1; gy += step) {
            float py = CY + (gy - sc.camY - 360.0f) * S;
            out += "DRAW rect|" + std::to_string((int)VX0) + "|" +
                   std::to_string((int)py) + "|" + std::to_string((int)VW) +
                   "|1|#33333D|0\n";
        }

        float ax = CX + (0.0f - sc.camX - 640.0f) * S;
        if (ax >= VX0 && ax <= VX0 + VW) {
            out += "DRAW rect|" + std::to_string((int)ax) + "|" +
                   std::to_string((int)VY0) + "|2|" + std::to_string((int)VH) +
                   "|#4A4A5E|0\n";
        }
        float ay = CY + (0.0f - sc.camY - 360.0f) * S;
        if (ay >= VY0 && ay <= VY0 + VH) {
            out += "DRAW rect|" + std::to_string((int)VX0) + "|" +
                   std::to_string((int)ay) + "|" + std::to_string((int)VW) +
                   "|2|#4A4A5E|0\n";
        }
    }

    emitNodePreview(sc.root.get(), CX, CY, S, VX0, VY0, VW, VH,
                    sc.camX, sc.camY, 0, 0, 0, 1, 1, out);

    for (auto& b : sc.ui) {
        float bcx = CX + (b.touch.rect.x + b.touch.rect.w / 2 - sc.camX - 640) * S;
        float bcy = CY + (b.touch.rect.y + b.touch.rect.h / 2 - sc.camY - 360) * S;
        float bw = b.touch.rect.w * S;
        float bh = b.touch.rect.h * S;
        out += "DRAW button|" + b.text + "|" +
               std::to_string((int)(bcx - bw / 2)) + "|" +
               std::to_string((int)(bcy - bh / 2)) + "|" +
               std::to_string((int)bw) + "|" + std::to_string((int)bh) + "|" +
               colorToHexA(withAlpha(b.color, b.alpha)) + "|" +
               std::to_string(b.angle) + "|" + resolveAssetPath(b.texture) + "\n";
    }

    // ==== РАМКА КАМЕРЫ: при ROT:PORT показывает вертикальные пропорции ====
    {
        float vw = in.viewW > 1.0f ? in.viewW : 1280.0f;
        float vh = in.viewH > 1.0f ? in.viewH : 720.0f;
        if (in.viewVert) {
            float r = in.viewRatio > 0.01f ? in.viewRatio : 1.7777f;
            vw = vh / r;   // вертикальное окно: ширина меньше высоты
        }
        float cxw = vw * 0.5f, cyw = vh * 0.5f, z = 1.0f;
        bool hasCam = false;
        Node* camN = sc.root ? const_cast<Scene&>(sc).root->findByType("Camera2D") : nullptr;
        Camera2D* cam = camN ? static_cast<Camera2D*>(camN) : nullptr;
        if (cam) {
            hasCam = true;
            cxw = cam->position.x;
            cyw = cam->position.y;
            z = cam->zoom > 0.01f ? cam->zoom : 1.0f;
        }
        float fw = vw / z;
        float fh = vh / z;
        float x0w = cxw - fw * 0.5f;
        float y0w = cyw - fh * 0.5f;

        float sx0 = CX + (x0w - sc.camX - 640) * S;
        float sy0 = CY + (y0w - sc.camY - 360) * S;
        float sw = fw * S;
        float sh = fh * S;

        const char* col = hasCam ? "#FF8800" : "#7F8CA3";
        float t = 2.0f;

        out += std::string("DRAW rect|") + std::to_string((int)sx0) + "|" + std::to_string((int)sy0) + "|" + std::to_string((int)sw) + "|" + std::to_string((int)t) + "|" + col + "|0\n";
        out += std::string("DRAW rect|") + std::to_string((int)sx0) + "|" + std::to_string((int)(sy0 + sh - t)) + "|" + std::to_string((int)sw) + "|" + std::to_string((int)t) + "|" + col + "|0\n";
        out += std::string("DRAW rect|") + std::to_string((int)sx0) + "|" + std::to_string((int)sy0) + "|" + std::to_string((int)t) + "|" + std::to_string((int)sh) + "|" + col + "|0\n";
        out += std::string("DRAW rect|") + std::to_string((int)(sx0 + sw - t)) + "|" + std::to_string((int)sy0) + "|" + std::to_string((int)t) + "|" + std::to_string((int)sh) + "|" + col + "|0\n";

        float ck = 10.0f;
        out += std::string("DRAW rect|") + std::to_string((int)(sx0 - 1)) + "|" + std::to_string((int)(sy0 - 1)) + "|" + std::to_string((int)ck) + "|3|" + col + "|0\n";
        out += std::string("DRAW rect|") + std::to_string((int)(sx0 - 1)) + "|" + std::to_string((int)(sy0 - 1)) + "|3|" + std::to_string((int)ck) + "|" + col + "|0\n";
        out += std::string("DRAW rect|") + std::to_string((int)(sx0 + sw - ck + 1)) + "|" + std::to_string((int)(sy0 + sh - 2)) + "|" + std::to_string((int)ck) + "|3|" + col + "|0\n";
        out += std::string("DRAW rect|") + std::to_string((int)(sx0 + sw - 2)) + "|" + std::to_string((int)(sy0 + sh - ck + 1)) + "|3|" + std::to_string((int)ck) + "|" + col + "|0\n";

        std::string lbl = std::string("CAM ") + std::to_string((int)fw) + "x" + std::to_string((int)fh);
        if (hasCam) lbl += "  zoom " + std::to_string((int)(z * 100)) + "%";
        out += "DRAW text|" + lbl + "|" + std::to_string((int)(sx0 + 4)) + "|" + std::to_string((int)(sy0 - 16)) + "|12|" + col + "|0\n";
    }
}

inline void emitEditorGizmos(const Scene& sc, std::string& out, const EditorRenderInput& in) {
    const float VX0 = EDV_X0, VY0 = EDV_Y0, VW = EDV_W, VH = EDV_H;
    const float S = 0.46875f * in.edZoom;
    const float CX = EDV_CX, CY = EDV_CY;

    Node* selN = in.editor ? in.editor->selected() : nullptr;
    Node2D* g = (selN && in.editor && in.editor->selectedUi().empty())
        ? dynamic_cast<Node2D*>(selN)
        : nullptr;

    if (g && !g->locked) {
        float gwx, gwy, gwr, gsx, gsy;
        Scene* esc = const_cast<Scene*>(&sc);
        if (!nodeWorld(esc, g->name, gwx, gwy, gwr, gsx, gsy)) {
            gwx = g->position.x; gwy = g->position.y; gsx = gsy = 1;
        }
        float cx = CX + (gwx - sc.camX - 640) * S;
        float cy = CY + (gwy - sc.camY - 360) * S;

        if (in.manip == Manip::Move) {
            out += "DRAW rect|" + std::to_string((int)cx) + "|" + std::to_string((int)(cy - 2)) + "|56|4|#D62828|0\n";
            out += "DRAW shape|triangle|" + std::to_string((int)(cx + 50)) + "|" + std::to_string((int)(cy - 8)) + "|16|14|#D62828|90\n";
            out += "DRAW rect|" + std::to_string((int)(cx - 2)) + "|" + std::to_string((int)cy) + "|4|56|#40C040|0\n";
            out += "DRAW shape|triangle|" + std::to_string((int)(cx - 8)) + "|" + std::to_string((int)(cy + 50)) + "|14|16|#40C040|180\n";
        } else if (in.manip == Manip::Rotate) {
            for (int k = 0; k < 24; ++k) {
                float a = k * 6.28318f / 24.0f;
                float px = cx + std::cos(a) * 70;
                float py = cy + std::sin(a) * 70;
                out += "DRAW rect|" + std::to_string((int)(px - 3)) + "|" + std::to_string((int)(py - 3)) + "|6|6|#FF8800|0\n";
            }
        } else {
            float hw = (g->w * gsx) * S / 2;
            float hh = (g->h * gsy) * S / 2;
            out += "DRAW rect|" + std::to_string((int)cx) + "|" + std::to_string((int)(cy - 1)) + "|" + std::to_string((int)(hw + 24)) + "|2|#4CC9F0|0\n";
            out += "DRAW rect|" + std::to_string((int)(cx + hw + 16)) + "|" + std::to_string((int)(cy - 8)) + "|16|16|#4CC9F0|0\n";
            out += "DRAW rect|" + std::to_string((int)(cx - 1)) + "|" + std::to_string((int)cy) + "|2|" + std::to_string((int)(hh + 24)) + "|#4CC9F0|0\n";
            out += "DRAW rect|" + std::to_string((int)(cx - 8)) + "|" + std::to_string((int)(cy + hh + 16)) + "|16|16|#4CC9F0|0\n";
        }
    }

    if (in.editor) {
        std::string selUi2 = in.editor->selectedUi();
        UiButton* gb2 = selUi2.empty() ? nullptr : in.editor->findUi(selUi2);
        if (gb2 && in.manip == Manip::Rotate) {
            float bcx = CX + (gb2->touch.rect.x + gb2->touch.rect.w / 2 - sc.camX - 640) * S;
            float bcy = CY + (gb2->touch.rect.y + gb2->touch.rect.h / 2 - sc.camY - 360) * S;
            for (int k = 0; k < 24; ++k) {
                float a = k * 6.28318f / 24.0f;
                float px = bcx + std::cos(a) * 70;
                float py = bcy + std::sin(a) * 70;
                out += "DRAW rect|" + std::to_string((int)(px - 3)) + "|" + std::to_string((int)(py - 3)) + "|6|6|#FF8800|0\n";
            }
            out += "DRAW text|ang " + std::to_string((int)gb2->angle) + "|" +
                   std::to_string((int)(bcx + 80)) + "|" + std::to_string((int)(bcy - 10)) + "|14|#FF8800|0\n";
        }
    }

    if (in.pickParent && !in.pickChild.empty()) {
        out += "DRAW rect|300|64|592|26|#FF8800|0\n";
        out += "DRAW text|PARENT FOR: " + in.pickChild +
               "  ->  tap object or row|306|68|16|#1A1A2E|0\n";
    }

    if (!in.lastMsg.empty()) {
        out += "DRAW text|" + in.lastMsg + "   fps " +
               std::to_string((int)in.fps) + "|306|580|14|#FFD700|0\n";
    }
}

} // namespace suka
