#pragma once

#include <string>
#include <memory>
#include <vector>
#include <set>
#include <cmath>
#include <mutex>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <algorithm>

#include "Core.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "Particles.hpp"
#include "Render.hpp"
#include "Touch.hpp"
#include "Input.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Hub.hpp"
#include "Editor.hpp"
#include "Script.hpp"
#include "Tween.hpp"

#include "CommonTypes.hpp"
#include "UiUtils.hpp"
#include "ProjectOps.hpp"
#include "SceneUtils.hpp"
#include "HubUI.hpp"
#include "EditorUI.hpp"
#include "EditorRender.hpp"
#include "LuaGameApi.hpp"

namespace suka {

static void walkEmitters(Node& n, const WorldXf& parent, float dt) {
    Node2D* n2 = dynamic_cast<Node2D*>(&n);
    if (!n2) { for (const auto& c : n.getChildren()) walkEmitters(*c, parent, dt); return; }
    WorldXf w = parent.child(n2->position, n2->rotation, n2->scale.x, n2->scale.y);
    if (std::string(n2->typeName()) == "Particle2D") {
        Particle2D* pe = static_cast<Particle2D*>(n2);
        if (pe->emitting) {
            SpawnOpts o;
            float avgScale = (w.sx + w.sy) * 0.5f;
            if (avgScale < 0.01f) avgScale = 0.01f;
            o.vx = pe->vx * avgScale; o.vy = pe->vy * avgScale; o.spread = pe->spread * avgScale;
            o.gravity = pe->gravity;
            o.life = pe->life; o.lifeSpread = pe->lifeSpread;
            o.size = pe->size * avgScale; o.sizeEnd = pe->sizeEnd * avgScale;
            o.drag = pe->drag; o.color = pe->color; o.rot = w.rot;
            for (int i = 0; i < 8; ++i) o.glyph[i] = 0;
            size_t gn = pe->glyph.size(); if (gn > 7) gn = 7;
            for (size_t i = 0; i < gn; ++i) o.glyph[i] = pe->glyph[i]; o.glyph[gn] = 0;
            int toSpawn = 0;
            if (pe->burstPending) { toSpawn += pe->burst; pe->burstPending = false; }
            if (pe->rate > 0.0f) { pe->acc += pe->rate * dt; int wh = (int)pe->acc; pe->acc -= wh; toSpawn += wh; }
            if (toSpawn > 0) g_particles.spawn(w.x, w.y, toSpawn, o);
        } else { pe->acc = 0.0f; }
    }
    for (const auto& c : n2->getChildren()) walkEmitters(*c, w, dt);
}

static void projEditor(float wx, float wy, float& sx, float& sy, float zoom, float camX, float camY) {
    float S = 0.46875f * zoom;
    sx = 596 + (wx - camX - 640) * S;
    sy = 310 + (wy - camY - 360) * S;
}

static void drawParticlePreviewTree(Node& n, const WorldXf& parent, std::string& out,
                                     float zoom, float camX, float camY, const std::string& sel) {
    Node2D* n2 = dynamic_cast<Node2D*>(&n);
    if (!n2) { for (const auto& c : n.getChildren()) drawParticlePreviewTree(*c, parent, out, zoom, camX, camY, sel); return; }
    WorldXf w = parent.child(n2->position, n2->rotation, n2->scale.x, n2->scale.y);
    if (std::string(n2->typeName()) == "Particle2D") {
        Particle2D* pe = static_cast<Particle2D*>(n2);
        bool isSel = (sel == n2->name);
        float avgScale = (w.sx + w.sy) * 0.5f; if (avgScale < 0.01f) avgScale = 0.01f;
        float base = pe->size * avgScale; if (base < 8.0f) base = 8.0f;
        unsigned col = pe->color;
        float alpha = isSel ? 1.0f : 0.55f;
        float sx = 0, sy = 0; projEditor(w.x, w.y, sx, sy, zoom, camX, camY);
        std::string markerGlyph = isSel ? std::string("\xe2\x97\x89") : pe->glyph;
        float markerSize = isSel ? base * 0.75f : base * 0.45f;
        out += std::string("DRAW text|") + markerGlyph + "|" + std::to_string((int)sx) + "|" + std::to_string((int)sy) + "|" + std::to_string((int)markerSize) + "|" + colorToHexA(withAlpha(col, alpha)) + "|" + std::to_string(w.rot * 57.2957795f) + "\n";
        if (isSel) {
            for (int i = 0; i < 12; ++i) {
                float ang = w.rot + (float)i * 0.5235987756f;
                float rad = base * (0.75f + (float)(i % 3) * 0.28f);
                float px = w.x + std::cos(ang) * rad;
                float py = w.y + std::sin(ang) * rad;
                float psx = 0, psy = 0; projEditor(px, py, psx, psy, zoom, camX, camY);
                float psz = base * 0.32f; if (psz < 4.0f) psz = 4.0f;
                out += std::string("DRAW text|") + pe->glyph + "|" + std::to_string((int)psx) + "|" + std::to_string((int)psy) + "|" + std::to_string((int)psz) + "|" + colorToHexA(withAlpha(col, 0.75f)) + "|" + std::to_string(w.rot * 57.2957795f) + "\n";
            }
        }
    }
    for (const auto& c : n2->getChildren()) drawParticlePreviewTree(*c, w, out, zoom, camX, camY, sel);
}

static void drawTexturePreviewTree(Node& n, const WorldXf& parent, std::string& out,
                                    float zoom, float camX, float camY) {
    Node2D* n2 = dynamic_cast<Node2D*>(&n);
    if (!n2) { for (const auto& c : n.getChildren()) drawTexturePreviewTree(*c, parent, out, zoom, camX, camY); return; }
    WorldXf w = parent.child(n2->position, n2->rotation, n2->scale.x, n2->scale.y);
    if (std::string(n2->typeName()) == "Prefab2D") return;
    Sprite2D* sp = dynamic_cast<Sprite2D*>(n2);
    std::string tex = !n2->texture.empty() ? n2->texture : (sp ? sp->texturePath : std::string());
    if (!tex.empty()) {
        float bw = sp ? sp->size.x : n2->w;
        float bh = sp ? sp->size.y : n2->h;
        float ww = bw * w.sx * zoom;
        float hh = bh * w.sy * zoom;
        float sx = 0, sy = 0;
        projEditor(w.x, w.y, sx, sy, zoom, camX, camY);
        out += "DRAW tex|" + resolveAssetPath(tex) + "|"
             + std::to_string((int)(sx - ww / 2)) + "|" + std::to_string((int)(sy - hh / 2)) + "|"
             + std::to_string((int)ww) + "|" + std::to_string((int)hh) + "|"
             + std::to_string(w.rot * 57.2957795f) + "\n";
    }
    for (const auto& c : n2->getChildren()) drawTexturePreviewTree(*c, w, out, zoom, camX, camY);
}

static std::vector<std::string> splitPipe(const std::string& s) {
    std::vector<std::string> v; size_t start = 0;
    while (true) {
        size_t p = s.find('|', start);
        if (p == std::string::npos) { v.push_back(s.substr(start)); break; }
        v.push_back(s.substr(start, p - start)); start = p + 1;
    }
    return v;
}
static std::string joinPipe(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) { if (i) out += '|'; out += v[i]; }
    return out;
}
static std::string shiftDrawLineX(const std::string& line, float dx) {
    if (line.rfind("DRAW ", 0) != 0) return line;
    std::string body = line.substr(5);
    size_t tp = body.find('|');
    if (tp == std::string::npos) return line;
    std::string type = body.substr(0, tp);
    int xIndex = -1;
    if (type == "text") xIndex = 2;
    else if (type == "rect") xIndex = 1;
    else if (type == "shape") xIndex = 2;
    else if (type == "button") xIndex = 2;
    else if (type == "tex") xIndex = 2;
    else return line;
    std::vector<std::string> parts = splitPipe(body);
    if ((int)parts.size() <= xIndex) return line;
    float x = (float)std::atof(parts[xIndex].c_str()) + dx;
    parts[xIndex] = std::to_string(x);
    return "DRAW " + joinPipe(parts);
}
static void shiftOutputX(std::string& out, float dx) {
    if (std::fabs(dx) < 0.01f) return;
    std::string res; res.reserve(out.size());
    size_t pos = 0;
    while (pos < out.size()) {
        size_t nl = out.find('\n', pos);
        std::string line;
        if (nl == std::string::npos) { line = out.substr(pos); pos = out.size(); }
        else { line = out.substr(pos, nl - pos); pos = nl + 1; }
        res += shiftDrawLineX(line, dx);
        if (nl != std::string::npos) res += '\n';
    }
    out.swap(res);
}

// Camera-panel hit zones in editor logical coords (1280x720). Top-right of viewport.
static const float CAM_PX0 = 792.0f, CAM_PX1 = 888.0f;
static const float CAM_BTN_W_Y0 = 66.0f,  CAM_BTN_W_Y1 = 90.0f;
static const float CAM_BTN_H_Y0 = 92.0f,  CAM_BTN_H_Y1 = 116.0f;
static const float CAM_BTN_O_Y0 = 118.0f, CAM_BTN_O_Y1 = 142.0f;

class GameApp {
public:
    using Manip = suka::Manip;
    enum class Mode { Console, String };
    enum class AppMode { Hub, Game, Editor };

    bool init(Mode mode, const std::string& gameDir) {
        (void)mode;
        loadSettings();
        hubState_.games = ProjectList::scan();
        hubState_.selectedDir = gameDir;
        if (hubState_.selectedDir.empty() && !hubState_.games.empty()) hubState_.selectedDir = hubState_.games.front().dir;
        logicW_ = 1280.0f; logicH_ = 720.0f;
        input_.screenWidth = logicW_; input_.screenHeight = logicH_;
        orientVertical_ = false; emitOrient_ = false; orientName_ = "landscape";
        projCamW_ = 1280.0f; projCamH_ = 720.0f; projVertical_ = false;
        clearTransition();
        rebuildHub();
        appMode_ = AppMode::Hub;
        return true;
    }

    void submitText(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); textRes_ = t; hasText_ = true; }
    void submitName(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); nameRes_ = t; hasName_ = true; }
    void submitAction(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); actionRes_ = t; hasAction_ = true; }
    void submitNumber(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); numRes_ = t; hasNum_ = true; }
    void submitScriptText(const std::string& t) { std::lock_guard<std::mutex> lk(imeMtx_); imeTextQ_.push_back(t); }
    void submitScriptCompose(const std::string& t) { std::lock_guard<std::mutex> lk(imeMtx_); imeCompQ_.push_back(t); }
    void submitScriptFinish() { std::lock_guard<std::mutex> lk(imeMtx_); imeFinish_ = true; }
    void submitScriptKey(int k) { std::lock_guard<std::mutex> lk(imeMtx_); imeKeyQ_.push_back(k); }

    void submitImportFile(const std::string& category, const std::string& relativePath) {
        if (category.empty() || relativePath.empty()) return;
        pendingImportCategory_.clear();
        lastMsg_ = "imported " + category + ": " + relativePath;
        buildEditorPanels();
    }

    void feedMultiTouch(int phase, float x0, float y0, float x1, float y1) {
        if (appMode_ != AppMode::Editor || scriptMode_ || showCreate_) return;
        Scene* es = editor_ ? editor_->scene() : nullptr;
        if (!es) return;
        float mx = (x0 + x1) / 2.0f, my = (y0 + y1) / 2.0f;
        float dist = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (phase == 1) {
            pinching_ = true; pinchDist0_ = dist; pinchZoom0_ = edZoom_;
            float S = 0.46875f * edZoom_;
            pinchAX_ = 640 + es->camX + (mx - 596) / S;
            pinchAY_ = 360 + es->camY + (my - 310) / S;
            return;
        }
        if (phase == 3) { pinching_ = false; return; }
        if (!pinching_) return;
        float z = pinchZoom0_;
        if (pinchDist0_ > 4 && dist > 4) {
            z = pinchZoom0_ * (dist / pinchDist0_);
            if (z < 0.4f) z = 0.4f; if (z > 3.0f) z = 3.0f;
        }
        float S = 0.46875f * z;
        es->camX = pinchAX_ - 640 - (mx - 596) / S;
        es->camY = pinchAY_ - 360 - (my - 310) / S;
        edZoom_ = z;
    }

    void feedTouch(int action, float x, float y) {
        if (action == 9) {
            if (appMode_ == AppMode::Editor && scriptMode_ && editor_) {
                int line = (int)x; int col = (int)y;
                if (line < 0) line = 0;
                if (line >= (int)scriptLines_.size()) line = (int)scriptLines_.size() - 1;
                const std::string& L = scriptLines_[line];
                if (col < 0) col = 0;
                if (col > (int)L.size()) col = (int)L.size();
                while (col > 0 && col < (int)L.size() && ((unsigned char)L[col] & 0xC0) == 0x80) --col;
                curLine_ = line; curCol_ = col; compAnchor_ = -1;
                imeWantOn_ = true; imeShown_ = true; imeChanged_ = true;
            }
            return;
        }

        if (appMode_ == AppMode::Game && transActive_) return;

        RawTouch t;
        if (action == 0) t.action = RawTouch::Action::Down;
        else if (action == 2) t.action = RawTouch::Action::Move;
        else t.action = RawTouch::Action::Up;
        t.x = x; t.y = y;
        Scene* cur = uiScene();
        if (!cur) return;

        if (appMode_ == AppMode::Editor && scriptMode_) {
            if (t.action == RawTouch::Action::Down && x >= 300 && x <= 850 && y >= 64 && y <= 556) {
                const float LH = 19;
                int line = scriptScroll_ + (int)((y - 70) / LH);
                if (line < 0) line = 0;
                if (line >= (int)scriptLines_.size()) line = (int)scriptLines_.size() - 1;
                int colCp = (int)((x - 340) / 8.0f);
                const std::string& L = scriptLines_[line];
                int total = utf8ByteToCp(L, (int)L.size());
                if (colCp < 0) colCp = 0; if (colCp > total) colCp = total;
                curLine_ = line; curCol_ = utf8CpToByte(L, colCp);
                compAnchor_ = -1;
                imeWantOn_ = true; imeShown_ = true; imeChanged_ = true;
            }
            touch_.onTouch(t, *cur, input_);
            return;
        }

        if (appMode_ == AppMode::Game && t.action == RawTouch::Action::Down && sceneMgr_ && sceneMgr_->current()) {
            Scene* gs = sceneMgr_->current();
            float wx, wy; unprojGame(*gs, x, y, wx, wy);
            std::string hit = hitTest(gs->root.get(), wx, wy);
            if (!hit.empty()) {
                Node* fn = gs->root->findNode(hit);
                Node2D* n = fn ? dynamic_cast<Node2D*>(fn) : nullptr;
                if (n && !n->action.empty()) { pendingNodeAction_ = n->action; return; }
            }
        }

        if (appMode_ == AppMode::Editor && !scriptMode_ && !showCreate_) {
            // Camera panel has priority over viewport dragging (top-right corner).
            if (t.action == RawTouch::Action::Down && !showSettings_ && !showPrefabs_ && !showAssets_
                && x >= CAM_PX0 && x <= CAM_PX1) {
                if (y >= CAM_BTN_W_Y0 && y <= CAM_BTN_W_Y1) {
                    pendingNum_ = true; pendingNumKind_ = "camw";
                    pendingNumCur_ = std::to_string((int)projCamW_);
                    return;
                }
                if (y >= CAM_BTN_H_Y0 && y <= CAM_BTN_H_Y1) {
                    pendingNum_ = true; pendingNumKind_ = "camh";
                    pendingNumCur_ = std::to_string((int)projCamH_);
                    return;
                }
                if (y >= CAM_BTN_O_Y0 && y <= CAM_BTN_O_Y1) {
                    std::swap(projCamW_, projCamH_);
                    projVertical_ = (projCamH_ > projCamW_);
                    saveProjCamera();
                    return;
                }
            }

            Scene* es = editor_ ? editor_->scene() : nullptr;
            float Z = edZoom_;

            if (t.action == RawTouch::Action::Down && pickParent_ && !pickChild_.empty() && es && es->root) {
                float wx, wy; unproj(*es, x, y, wx, wy);
                std::string hit = hitTest(es->root.get(), wx, wy);
                if (!hit.empty() && hit != pickChild_) {
                    pushUndo(); attachChildTo(pickChild_, hit);
                    lastMsg_ = "attached " + pickChild_ + " -> " + hit;
                    if (pickChild_.rfind("UI:", 0) == 0) editor_->selectUi(pickChild_.substr(3));
                    else editor_->select(pickChild_);
                } else {
                    lastMsg_ = hit.empty() ? "no target under tap" : "cannot attach to self";
                }
                pickParent_ = false; pickChild_.clear();
                buildEditorPanels(); input_.setUi(&editorScene_.ui);
                return;
            }

            UiButton* gb = (editor_ && !editor_->selectedUi().empty()) ? editor_->findUi(editor_->selectedUi()) : nullptr;
            if (gb && es) {
                float bcx, bcy;
                proj(*es, gb->touch.rect.x + gb->touch.rect.w / 2, gb->touch.rect.y + gb->touch.rect.h / 2, bcx, bcy);
                if (t.action == RawTouch::Action::Down) {
                    float dx = x - bcx, dy = y - bcy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (manip_ == Manip::Rotate && std::fabs(dist - 70.0f) < 26.0f) {
                        gizmoRotUi_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = gb->angle; return;
                    }
                } else if (t.action == RawTouch::Action::Move && gizmoRotUi_) {
                    float dx = x - bcx, dy = y - bcy;
                    gb->angle = gizmoStartRot_ + (std::atan2(dy, dx) - gizmoStartAngle_) * 57.2957795f; return;
                } else if (t.action == RawTouch::Action::Up) { gizmoRotUi_ = false; }
            }

            Node2D* g = (editor_ && editor_->selectedUi().empty()) ? (editor_->selected() ? dynamic_cast<Node2D*>(editor_->selected()) : nullptr) : nullptr;
            if (g && es) {
                float gwx, gwy, gwr, gsx, gsy;
                if (!nodeWorld(es, g->name, gwx, gwy, gwr, gsx, gsy)) { gwx = g->position.x; gwy = g->position.y; gsx = gsy = 1; }
                float scx, scy; proj(*es, gwx, gwy, scx, scy);
                if (t.action == RawTouch::Action::Down) {
                    if (!g->locked) {
                        float dx = x - scx, dy = y - scy;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        if (manip_ == Manip::Rotate) {
                            if (std::fabs(dist - 70.0f) < 26.0f) { gizmoRot_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = g->rotation; return; }
                        } else if (manip_ == Manip::Scale) {
                            float hw = (g->w * gsx) * 0.46875f * Z / 2;
                            float hh = (g->h * gsy) * 0.46875f * Z / 2;
                            if (std::fabs(x - (scx + hw + 24)) < 28 && std::fabs(dy) < 28) { gizmoSclX_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSX_ = g->scale.x; return; }
                            if (std::fabs(y - (scy + hh + 24)) < 28 && std::fabs(dx) < 28) { gizmoSclY_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSY_ = g->scale.y; return; }
                        } else {
                            if (std::fabs(dy) < 16 && dx > 8 && dx < 64) { lockAxis_ = 1; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                            if (std::fabs(dx) < 16 && dy > 8 && dy < 64) { lockAxis_ = 2; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                        }
                    }
                } else if (t.action == RawTouch::Action::Move) {
                    if (gizmoRot_) { g->rotation = gizmoStartRot_ + (std::atan2(y - scy, x - scx) - gizmoStartAngle_); return; }
                    if (gizmoSclX_ || gizmoSclY_) {
                        float dist = std::sqrt((x - scx) * (x - scx) + (y - scy) * (y - scy));
                        float f = dist / gizmoStartDist_; if (f < 0.05f) f = 0.05f;
                        if (gizmoSclX_) g->scale.x = gizmoStartSX_ * f;
                        if (gizmoSclY_) g->scale.y = gizmoStartSY_ * f;
                        return;
                    }
                } else if (t.action == RawTouch::Action::Up) { gizmoRot_ = false; gizmoSclX_ = false; gizmoSclY_ = false; lockAxis_ = 0; }
            }

            const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
            bool inVP = (x >= VX0 && x <= VX0 + VW && y >= VY0 && y <= VY0 + VH);
            if (t.action == RawTouch::Action::Down && inVP && es) {
                float wx, wy; unproj(*es, x, y, wx, wy);
                std::string uiHit = hitUi(es, wx, wy);
                if (!uiHit.empty()) { editor_->selectUi(uiHit); dragUi_ = editor_->findUi(uiHit); dragging_ = (dragUi_ != nullptr); lockAxis_ = 0; }
                else if (es->root) {
                    std::string hit = hitTest(es->root.get(), wx, wy);
                    if (!hit.empty()) {
                        editor_->select(hit);
                        Node2D* hitN = editor_->find2d(hit);
                        if (hitN && !hitN->locked) { dragNode_ = hitN; dragging_ = true; lockAxis_ = 0; startDragParent(es, hit); }
                        else { dragNode_ = nullptr; dragging_ = false; }
                    }
                }
            } else if (t.action == RawTouch::Action::Move && dragging_ && es) {
                float wx, wy; unproj(*es, x, y, wx, wy);
                if (dragUi_) { dragUi_->touch.rect.x = wx - dragUi_->touch.rect.w / 2; dragUi_->touch.rect.y = wy - dragUi_->touch.rect.h / 2; }
                else if (dragNode_) {
                    float oldX = dragOX_ + dragNode_->position.x * dragPSX_;
                    float oldY = dragOY_ + dragNode_->position.y * dragPSY_;
                    float nx, ny;
                    if (lockAxis_ == 1) { nx = wx; ny = oldY; }
                    else if (lockAxis_ == 2) { nx = oldX; ny = wy; }
                    else { nx = wx; ny = wy; }
                    float dlx = (nx - oldX) / dragPSX_;
                    float dly = (ny - oldY) / dragPSY_;
                    dragNode_->position.x += dlx; dragNode_->position.y += dly;
                    moveGroupButtons(es, dragNode_->name, nx - oldX, ny - oldY);
                }
            } else if (t.action == RawTouch::Action::Up) { dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; lockAxis_ = 0; }
        }

        touch_.onTouch(t, *cur, input_);
    }

    std::string stepFrame() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (lastMs_ > 0) { float dt = (ms - lastMs_) / 1000.0f; if (dt > 0.0001f) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f; }
        lastMs_ = ms;
        if (appMode_ == AppMode::Hub) return stepHub();
        if (appMode_ == AppMode::Editor) return stepEditor();
        return stepGame();
    }

    SceneManager& sceneMgr() { return *sceneMgr_; }
    ProjectInfo& project() { return project_; }

private:
    Scene* uiScene() {
        if (appMode_ == AppMode::Hub) return &hubScene_;
        if (appMode_ == AppMode::Editor) return &editorScene_;
        return (sceneMgr_ && sceneMgr_->current()) ? sceneMgr_->current() : nullptr;
    }

    // ---- project camera settings (editor_camera.json) ----
    static double jsonNum(const std::string& s, const std::string& key, double def) {
        size_t p = s.find(key);
        if (p == std::string::npos) return def;
        p += key.size();
        while (p < s.size() && (s[p] == ':' || s[p] == ' ' || s[p] == '\t')) ++p;
        const char* c = s.c_str() + p;
        char* end = nullptr;
        double v = std::strtod(c, &end);
        if (end == c) return def;
        return v;
    }
    static bool jsonBool(const std::string& s, const std::string& key, bool def) {
        size_t p = s.find(key);
        if (p == std::string::npos) return def;
        p += key.size();
        while (p < s.size() && (s[p] == ':' || s[p] == ' ' || s[p] == '\t')) ++p;
        if (s.compare(p, 4, "true") == 0) return true;
        if (s.compare(p, 5, "false") == 0) return false;
        return def;
    }
    void loadProjCamera(const std::string& root) {
        projCamW_ = 1280.0f; projCamH_ = 720.0f; projVertical_ = false;
        if (root.empty()) return;
        std::string path = root + "/editor_camera.json";
        if (!fileExists(path)) return;
        std::string s = readFile(path);
        double w = jsonNum(s, "\"w\"", 1280.0);
        double h = jsonNum(s, "\"h\"", 720.0);
        if (w < 160.0) w = 160.0; if (w > 2160.0) w = 2160.0;
        if (h < 160.0) h = 160.0; if (h > 2160.0) h = 2160.0;
        projCamW_ = (float)w; projCamH_ = (float)h;
        projVertical_ = jsonBool(s, "\"vertical\"", projCamH_ > projCamW_);
    }
    void saveProjCamera() {
        if (project_.rootPath.empty()) return;
        std::ofstream f(project_.rootPath + "/editor_camera.json");
        if (!f.good()) return;
        f << "{\"w\":" << (int)projCamW_ << ",\"h\":" << (int)projCamH_
          << ",\"vertical\":" << (projVertical_ ? "true" : "false") << "}\n";
        f.close();
    }

    void clearTransition() {
        transActive_ = false; transType_ = 0; transPhase_ = 0;
        transProgress_ = 0.0f; transDuration_ = 0.4f;
        transOffset_ = 0.0f; transAlpha_ = 0.0f; transTarget_.clear();
    }

    void startTransition(int type, const std::string& target, float duration) {
        if (target.empty()) return;
        if (type == 3) { performSceneChange(target); return; }
        if (transActive_) return;
        transActive_ = true; transType_ = type; transPhase_ = 0;
        transProgress_ = 0.0f; transDuration_ = duration > 0.05f ? duration : 0.4f;
        transTarget_ = target; transOffset_ = 0.0f; transAlpha_ = 0.0f;
    }

    void performSceneChange(const std::string& target) {
        if (!sceneMgr_ || target.empty()) return;
        g_tweens.clear(); g_particles.clear();
        bool ok = sceneMgr_->restartScene(target, resources_);
        if (!ok) sceneMgr_->requestChange(target, true);
        touch_.resetJoystick();
        ensureGameButtons();
    }

    void updateTransition(float dt) {
        if (!transActive_) return;
        if (transDuration_ <= 0.001f) { clearTransition(); return; }
        float step = dt / transDuration_; if (step > 1.0f) step = 1.0f;
        transProgress_ += step;
        if (transPhase_ == 0) {
            if (transType_ == 0) { transAlpha_ = transProgress_; transOffset_ = 0.0f; }
            else if (transType_ == 1) { transAlpha_ = 0.0f; transOffset_ = -logicW_ * transProgress_; }
            else if (transType_ == 2) { transAlpha_ = 0.0f; transOffset_ = logicW_ * transProgress_; }
            if (transProgress_ >= 1.0f) {
                std::string target = transTarget_; transTarget_.clear();
                performSceneChange(target);
                if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); return; }
                transPhase_ = 1; transProgress_ = 0.0f;
            }
        } else {
            if (transType_ == 0) { transAlpha_ = 1.0f - transProgress_; transOffset_ = 0.0f; }
            else if (transType_ == 1) { transAlpha_ = 0.0f; transOffset_ = logicW_ * (1.0f - transProgress_); }
            else if (transType_ == 2) { transAlpha_ = 0.0f; transOffset_ = -logicW_ * (1.0f - transProgress_); }
            if (transProgress_ >= 1.0f) clearTransition();
        }
    }

    void consumeLuaCmd() {
        LuaGameCmd cmd; bool any = false;
        { std::lock_guard<std::mutex> lk(g_luaCmdMtx);
          if (g_luaCmd.transition) { cmd = g_luaCmd; g_luaCmd = LuaGameCmd{}; any = true; } }
        if (!any) return;
        if (cmd.transition) startTransition(cmd.type, cmd.scene, cmd.duration);
    }

    void ensureGameButtons() {
        Scene* sc = (sceneMgr_ && sceneMgr_->current()) ? sceneMgr_->current() : nullptr;
        if (!sc) return;
        bool hasClose = false, hasDbg = false;
        for (auto& b : sc->ui) {
            if (b.touch.id == "close") { hasClose = true; b.touch.rect = Rect{logicW_ - 100.0f, 10.0f, 90.0f, 70.0f}; b.text = "X"; b.action = playFromEditor_ ? "editor_return:" : "hub:"; b.color = parseColor("#D62828"); }
            else if (b.touch.id == "dbg") { hasDbg = true; b.touch.rect = Rect{logicW_ - 200.0f, 10.0f, 90.0f, 70.0f}; b.text = "DBG"; b.action = "dbg:"; b.color = parseColor("#808080"); }
        }
        if (!hasClose) { UiButton c; c.touch.id = "close"; c.touch.rect = Rect{logicW_ - 100.0f, 10.0f, 90.0f, 70.0f}; c.text = "X"; c.action = playFromEditor_ ? "editor_return:" : "hub:"; c.color = parseColor("#D62828"); sc->ui.push_back(c); }
        if (!hasDbg) { UiButton d; d.touch.id = "dbg"; d.touch.rect = Rect{logicW_ - 200.0f, 10.0f, 90.0f, 70.0f}; d.text = "DBG"; d.action = "dbg:"; d.color = parseColor("#808080"); sc->ui.push_back(d); }
        input_.setUi(&sc->ui);
    }

    void proj(const Scene& sc, float wx, float wy, float& sx, float& sy) { float S = 0.46875f * edZoom_; sx = 596 + (wx - sc.camX - 640) * S; sy = 310 + (wy - sc.camY - 360) * S; }
    void unproj(const Scene& sc, float sx, float sy, float& wx, float& wy) { float S = 0.46875f * edZoom_; wx = 640 + sc.camX + (sx - 596) / S; wy = 360 + sc.camY + (sy - 310) / S; }
    void unprojGame(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        float hx = logicW_ * 0.5f, hy = logicH_ * 0.5f;
        Node* cn = sc.root ? sc.root->findByType("Camera2D") : nullptr;
        if (cn) { Camera2D* cam = static_cast<Camera2D*>(cn); float z = cam->zoom > 0.01f ? cam->zoom : 1.0f; wx = (sx - hx) / z + cam->position.x; wy = (sy - hy) / z + cam->position.y; }
        else { wx = sx; wy = sy; }
    }

    std::string hitUi(Scene* es, float wx, float wy) {
        if (!es) return "";
        for (auto& b : es->ui) if (wx >= b.touch.rect.x && wx <= b.touch.rect.x + b.touch.rect.w && wy >= b.touch.rect.y && wy <= b.touch.rect.y + b.touch.rect.h) return b.touch.id;
        return "";
    }

    void startDragParent(Scene* es, const std::string& name) {
        dragOX_ = 0; dragOY_ = 0; dragPSX_ = 1; dragPSY_ = 1;
        if (es && es->root) { bool found = false; parentTransform(es->root.get(), name, 0, 0, 1, 1, dragOX_, dragOY_, dragPSX_, dragPSY_, found); }
        if (dragPSX_ < 0.01f) dragPSX_ = 1; if (dragPSY_ < 0.01f) dragPSY_ = 1;
    }

    void setNodeTexture(const std::string& name, const std::string& rel) {
        editor_->setTexture(name, rel);
        Node2D* n = editor_->find2d(name);
        if (!n) return;
        Sprite2D* sp = dynamic_cast<Sprite2D*>(n);
        if (sp) sp->texturePath = rel;
        if (!rel.empty()) n->shape = "none";
    }

    void attachChildTo(const std::string& child, const std::string& parent) {
        if (!editor_ || !editor_->scene()) return;
        Scene* sc = editor_->scene();
        if (child.rfind("UI:", 0) == 0) { UiButton* b = editor_->findUi(child.substr(3)); if (b) b->group = parent; return; }
        if (!sc || !sc->root) return;
        Node* root = sc->root.get();
        if (child == parent) return;
        Node* cn = root->findNode(child); Node* pn = root->findNode(parent);
        if (!cn || !pn) return; if (cn->containsName(parent)) return;
        Node* owner = root->findParentOf(child);
        if (!owner || owner == pn) return;
        std::unique_ptr<Node> up = owner->takeChild(child);
        if (up) pn->addChild(std::move(up));
    }

    void detachChild(const std::string& child) {
        if (!editor_ || !editor_->scene()) return;
        Scene* sc = editor_->scene();
        if (child.rfind("UI:", 0) == 0) { UiButton* b = editor_->findUi(child.substr(3)); if (b) b->group.clear(); return; }
        if (!sc || !sc->root) return;
        Node* root = sc->root.get();
        Node* owner = root->findParentOf(child);
        if (!owner || owner == root) return;
        std::unique_ptr<Node> up = owner->takeChild(child);
        if (up) root->addChild(std::move(up));
    }

    void pushUndo() {
        if (!editor_ || !editor_->scene()) return;
        std::string rel = "snap_" + std::to_string(snapCounter_++) + ".json";
        editor_->save(project_.rootPath + "/" + rel);
        undoStack_.push_back(rel);
        if (undoStack_.size() > 12) undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }

    bool loadSnap(const std::string& rel) {
        if (!sceneMgr_ || !editor_) return false;
        if (sceneMgr_->restartScene(rel, resources_)) {
            editor_->attach(sceneMgr_->current());
            editor_->setProjectRoot(project_.rootPath);
            scripted_.clear();
            dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pickParent_ = false; pickChild_.clear();
            buildEditorPanels(); input_.setUi(&editorScene_.ui);
            return true;
        }
        return false;
    }

    bool doUndo() {
        if (!editor_ || undoStack_.empty()) { lastMsg_ = "nothing to undo"; return false; }
        std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";
        editor_->save(project_.rootPath + "/" + cur); redoStack_.push_back(cur);
        std::string rel = undoStack_.back(); undoStack_.pop_back();
        bool ok = loadSnap(rel); lastMsg_ = ok ? "undo" : "undo failed"; return ok;
    }
    bool doRedo() {
        if (!editor_ || redoStack_.empty()) { lastMsg_ = "nothing to redo"; return false; }
        std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";
        editor_->save(project_.rootPath + "/" + cur); undoStack_.push_back(cur);
        std::string rel = redoStack_.back(); redoStack_.pop_back();
        bool ok = loadSnap(rel); lastMsg_ = ok ? "redo" : "redo failed"; return ok;
    }

    void loadScript(const std::string& rel) {
        scriptPath_ = rel; std::string s = readFile(project_.rootPath + "/" + rel);
        scriptLines_.clear(); std::string cur;
        for (char c : s) { if (c == '\n') { scriptLines_.push_back(cur); cur.clear(); } else cur += c; }
        scriptLines_.push_back(cur);
        if (scriptLines_.empty()) scriptLines_.push_back("");
        curLine_ = 0; curCol_ = 0; scriptScroll_ = 0; compAnchor_ = -1;
    }
    void saveScript() {
        if (scriptPath_.empty()) return;
        std::ofstream f(project_.rootPath + "/" + scriptPath_);
        for (size_t i = 0; i < scriptLines_.size(); ++i) { f << scriptLines_[i]; if (i + 1 < scriptLines_.size()) f << "\n"; }
        f.close(); scripts_.load(project_.rootPath); lastMsg_ = "script saved: " + scriptPath_;
    }
    void attachScript(const std::string& name) {
        std::string rel = "scripts/" + name + ".lua";
        ProjectCreator::createScript(project_.rootPath, rel, name);
        scripts_.load(project_.rootPath); scripted_.insert(name);
    }

    void saveVars() {
        if (project_.rootPath.empty()) return;
        std::ofstream f(project_.rootPath + "/save.vars");
        if (!f.good()) return;
        f << "__score__=" << ctx_.score << "\n";
        for (const auto& kv : ctx_.vars) f << kv.first << "=" << kv.second << "\n";
        f.close();
    }
    void loadVars() {
        if (project_.rootPath.empty()) return;
        std::string path = project_.rootPath + "/save.vars";
        if (!fileExists(path)) return;
        std::string s = readFile(path); size_t pos = 0;
        while (pos <= s.size()) {
            size_t nl = s.find('\n', pos); std::string line;
            if (nl == std::string::npos) { line = s.substr(pos); pos = s.size() + 1; }
            else { line = s.substr(pos, nl - pos); pos = nl + 1; }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq); double v = atof(line.substr(eq + 1).c_str());
            if (k == "__score__") ctx_.score = (int)v;
            else if (!k.empty()) ctx_.vars[k] = v;
        }
    }

    void clearDialogResults() {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        hasText_ = false; hasName_ = false; hasAction_ = false; hasNum_ = false;
    }
    bool takeNameResult(std::string& out) {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        if (!hasName_) return false; out = nameRes_; hasName_ = false; return true;
    }

    void scTypeChar(char c) {
        if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");
        std::string& L = scriptLines_[curLine_];
        if (c == '\n') {
            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
            std::string tail = L.substr(curCol_); L = L.substr(0, curCol_);
            scriptLines_.insert(scriptLines_.begin() + curLine_ + 1, tail);
            curLine_++; curCol_ = 0;
        } else { if (curCol_ > (int)L.size()) curCol_ = (int)L.size(); L.insert(L.begin() + curCol_, c); curCol_++; }
        scClampView();
    }
    void scCompose(const std::string& text) {
        if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");
        std::string& L = scriptLines_[curLine_];
        if (compAnchor_ < 0 || compAnchor_ > (int)L.size()) compAnchor_ = curCol_;
        if (curCol_ > compAnchor_) { L.erase(L.begin() + compAnchor_, L.begin() + curCol_); curCol_ = compAnchor_; }
        for (char c : text) { if (c == '\n') c = ' '; if (curCol_ > (int)L.size()) curCol_ = (int)L.size(); L.insert(L.begin() + curCol_, c); curCol_++; }
        scClampView();
    }
    void scCommit(const std::string& text) {
        if (compAnchor_ >= 0) {
            std::string& L = scriptLines_[curLine_];
            if (compAnchor_ <= (int)L.size() && curCol_ > compAnchor_) { L.erase(L.begin() + compAnchor_, L.begin() + curCol_); curCol_ = compAnchor_; }
            compAnchor_ = -1;
        }
        for (char c : text) scTypeChar(c);
    }
    void scFinish() { compAnchor_ = -1; }
    void scBackspace() {
        compAnchor_ = -1;
        if (curLine_ >= (int)scriptLines_.size()) return;
        std::string& L = scriptLines_[curLine_];
        if (curCol_ > 0) { int p = utf8Prev(L, curCol_); L.erase(L.begin() + p, L.begin() + curCol_); curCol_ = p; }
        else if (curLine_ > 0) { size_t prevLen = scriptLines_[curLine_ - 1].size(); scriptLines_[curLine_ - 1] += L; scriptLines_.erase(scriptLines_.begin() + curLine_); curLine_--; curCol_ = (int)prevLen; }
        scClampView();
    }
    void scMove(int d) {
        compAnchor_ = -1;
        if (curLine_ >= (int)scriptLines_.size()) curLine_ = (int)scriptLines_.size() - 1;
        std::string& L = scriptLines_[curLine_];
        if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
        if (d < 0) { if (curCol_ > 0) curCol_ = utf8Prev(L, curCol_); else if (curLine_ > 0) { curLine_--; curCol_ = (int)scriptLines_[curLine_].size(); } }
        else { if (curCol_ < (int)L.size()) curCol_ = utf8Next(L, curCol_); else if (curLine_ + 1 < (int)scriptLines_.size()) { curLine_++; curCol_ = 0; } }
        scClampView();
    }
    void scClampView() {
        const int LINES = 24;
        if (curLine_ < scriptScroll_) scriptScroll_ = curLine_;
        if (curLine_ >= scriptScroll_ + LINES) scriptScroll_ = curLine_ - LINES + 1;
        if (scriptScroll_ < 0) scriptScroll_ = 0;
    }
    void imeApply() {
        std::vector<std::string> tq, cq; std::vector<int> kq; bool fin = false;
        { std::lock_guard<std::mutex> lk(imeMtx_); tq.swap(imeTextQ_); cq.swap(imeCompQ_); kq.swap(imeKeyQ_); fin = imeFinish_; imeFinish_ = false; }
        if (tq.empty() && cq.empty() && kq.empty() && !fin) return;
        for (auto& s : cq) scCompose(s);
        for (auto& s : tq) scCommit(s);
        if (fin) scFinish();
        for (int k : kq) {
            if (k == 67) scBackspace();
            else if (k == 66) { compAnchor_ = -1; scTypeChar('\n'); }
            else if (k == 21) scMove(-1);
            else if (k == 22) scMove(1);
        }
        imeChanged_ = true;
    }

    void rebuildHub() {
        hubState_.games = ProjectList::scan();
        HubUiInput hin{ hubState_.games, hubState_.selectedDir, confirmDeleteDir_ };
        hubScene_ = buildHubScene(hin);
        input_.setUi(&hubScene_.ui);
    }

    EditorUiInput makeEditorUiInput() {
        return EditorUiInput{
            editor_.get(), scriptMode_, edZoom_, manip_, pickParent_, showCreate_, showAssets_, showSettings_, showPrefabs_, showFiles_,
            hierScroll_, fsScroll_, assetScroll_, scriptScroll_, prefabScroll_,
            collapsed_, fsPath_, project_.rootPath, pendingDeleteFile_, scriptPath_, scriptLines_,
            curLine_, curCol_, imeShown_, g_luaLog
        };
    }
    EditorRenderInput makeEditorRenderInput() { return EditorRenderInput{ editor_.get(), edZoom_, manip_, pickParent_, pickChild_, lastMsg_, fps_ }; }
    void buildEditorPanels() { editorScene_ = buildEditorScene(makeEditorUiInput()); input_.setUi(&editorScene_.ui); }

    std::string stepHub() {
        logicW_ = 1280.0f; logicH_ = 720.0f; orientVertical_ = false; emitOrient_ = false;
        input_.screenWidth = logicW_; input_.screenHeight = logicH_;
        clearTransition();
        if (!pendingNewProject_ && !pendingHubRename_) clearDialogResults();
        std::string nm;
        if (pendingNewProject_) {
            if (takeNameResult(nm)) {
                pendingNewProject_ = false;
                if (!nm.empty()) {
                    std::string dir = uniqueProjectDir(sanitizeProjectDirName(nm), hubState_.games);
                    std::string disp = safeProjectDisplayName(nm);
                    ProjectCreator::createProject(dir, disp);
                    hubState_.selectedDir = dir;
                }
                rebuildHub();
            }
        } else if (pendingHubRename_) {
            if (takeNameResult(nm)) {
                pendingHubRename_ = false;
                if (!nm.empty() && !pendingHubDir_.empty()) { std::string root = projectsDir() + pendingHubDir_; setProjectDisplayName(root, nm); }
                pendingHubDir_.clear(); pendingHubCurrentName_.clear(); rebuildHub();
            }
        }
        gameBackend_.begin(); Renderer r(gameBackend_); r.render(hubScene_, nullptr);
        std::string out = gameBackend_.str();
        HubAct a = processHubScene(hubScene_);
        if (a.kind == 3) { if (!pendingNewProject_) { pendingNewProject_ = true; pendingHubRename_ = false; confirmDeleteDir_.clear(); } }
        else if (a.kind == 4) { hubState_.selectedDir = a.dir; confirmDeleteDir_.clear(); rebuildHub(); }
        else if (a.kind == 5) { cycleTheme(); saveSettings(); confirmDeleteDir_.clear(); rebuildHub(); }
        else if (a.kind == 6) { std::string root = projectsDir() + hubState_.selectedDir; bool cur = projectWantsSave(root); setProjectSaveFlag(root, !cur); confirmDeleteDir_.clear(); rebuildHub(); }
        else if (a.kind == 7) {
            if (!hubState_.selectedDir.empty()) {
                pendingHubDir_ = hubState_.selectedDir;
                pendingHubCurrentName_ = sanitizeLine(projectDisplayName(pendingHubDir_));
                pendingHubRename_ = true; pendingNewProject_ = false; confirmDeleteDir_.clear();
            }
        } else if (a.kind == 8) { confirmDeleteDir_ = hubState_.selectedDir; rebuildHub(); }
        else if (a.kind == 9) {
            if (!hubState_.selectedDir.empty() && confirmDeleteDir_ == hubState_.selectedDir) {
                std::string oldDir = hubState_.selectedDir;
                std::string root = projectsDir() + oldDir;
                removePathRecursive(root);
                sceneMgr_.reset(); editor_.reset();
                hubState_.games = ProjectList::scan();
                bool stillExists = false;
                for (const auto& g : hubState_.games) if (g.dir == oldDir) { stillExists = true; break; }
                if (!stillExists) { if (!hubState_.games.empty()) hubState_.selectedDir = hubState_.games.front().dir; else hubState_.selectedDir.clear(); }
                confirmDeleteDir_.clear(); rebuildHub();
            }
        } else if (a.kind == 10) {
            if (!hubState_.selectedDir.empty()) { std::string sv = projectsDir() + hubState_.selectedDir + "/save.vars"; std::remove(sv.c_str()); }
            confirmDeleteDir_.clear(); rebuildHub();
        } else if (a.kind == 1) { confirmDeleteDir_.clear(); pendingHubRename_ = false; pendingNewProject_ = false; playFromEditor_ = false; if (!enterGame(a.dir)) rebuildHub(); }
        else if (a.kind == 2) { confirmDeleteDir_.clear(); pendingHubRename_ = false; pendingNewProject_ = false; if (!enterEditor(a.dir)) rebuildHub(); }
        if (appMode_ == AppMode::Hub) {
            if (pendingNewProject_) out += "REQ_NAME|Project\n";
            else if (pendingHubRename_) out += "REQ_NAME|" + pendingHubCurrentName_ + "\n";
        }
        out += "RES|1280|720\n";
        input_.endFrame(); return out;
    }

    bool enterGame(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(projectsDir() + dir + "/project.json", pi)) return false;
        std::string root = pi.rootPath;
        std::string font = root + "/" + pi.defaultFont;
        if (!fileExists(font)) font = std::string(PROJECT_ROOT) + "/assets/fonts/Ubuntu-Regular.ttf";
        auto mgr = std::make_unique<SceneManager>(root, font);
        if (!mgr->restartScene(pi.mainScene, resources_)) return false;
        project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = font;
        sceneMgr_ = std::move(mgr);
        scripts_.load(root); g_tweens.clear(); g_particles.clear();
        ctx_ = Context(); g_luaLog.clear(); dbg_ = false;
        saveVarsEnabled_ = projectWantsSave(root); pendingLoadVars_ = saveVarsEnabled_; saveTimer_ = 0.0f;
        pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
        clearDialogResults();

        loadProjCamera(project_.rootPath);
        logicW_ = projCamW_; logicH_ = projCamH_; orientVertical_ = projVertical_;
        input_.screenWidth = logicW_; input_.screenHeight = logicH_;
        emitOrient_ = true; orientName_ = orientVertical_ ? "portrait" : "landscape";
        clearTransition();

        ensureGameButtons();
        touch_.resetJoystick();
        appMode_ = AppMode::Game;

        transActive_ = true; transType_ = 0; transPhase_ = 1;
        transProgress_ = 0.0f; transDuration_ = 0.35f; transAlpha_ = 1.0f;
        transOffset_ = 0.0f; transTarget_.clear();
        return true;
    }

    std::string stepGame() {
        if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); return ""; }
        consumeLuaCmd();
        if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); return ""; }

        clearDialogResults();
        ctx_.coinCollectedThisFrame = false; ctx_.jumpPressedThisFrame = false; ctx_.input = input_.state();

        updateTransition(1.0f / 60.0f);
        ensureGameButtons();
        if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); input_.endFrame(); return ""; }

        if (!transActive_ && !pendingNodeAction_.empty()) { runAction(pendingNodeAction_); pendingNodeAction_.clear(); }
        if (!transActive_) processUi();
        if (appMode_ != AppMode::Game) { input_.endFrame(); return ""; }

        if (!transActive_) {
            sceneMgr_->update(ctx_, 1.0 / 60.0, input_, resources_);
            scripts_.update(*sceneMgr_->current(), ctx_, 1.0 / 60.0, *sceneMgr_, ctx_.vars);
            if (sceneMgr_->current()) g_tweens.update(1.0f / 60.0f, sceneMgr_->current(), [this](const std::string& fn) {
                if (sceneMgr_ && sceneMgr_->current()) scripts_.callGlobal(fn, ctx_, *sceneMgr_, ctx_.vars, sceneMgr_->current());
            });
        }

        if (pendingLoadVars_) { if (saveVarsEnabled_) loadVars(); pendingLoadVars_ = false; }
        if (saveVarsEnabled_) { saveTimer_ += 1.0f / 60.0f; if (saveTimer_ >= 1.0f) { saveVars(); saveTimer_ = 0.0f; } }

        std::string out;
        if (ctx_.coinCollectedThisFrame) out += "SOUND coin\n";
        if (ctx_.jumpPressedThisFrame) out += "SOUND jump\n";

        gameBackend_.begin(); Renderer renderer(gameBackend_); renderer.render(*sceneMgr_->current(), &ctx_);
        out += gameBackend_.str();

        if (!transActive_ && sceneMgr_ && sceneMgr_->current() && sceneMgr_->current()->root) {
            g_particles.update(1.0f / 60.0f);
            WorldXf ident; walkEmitters(*sceneMgr_->current()->root, ident, 1.0f / 60.0f);
            Scene* ps = sceneMgr_->current();
            bool camActive = false; float camX = 0, camY = 0, camZ = 1;
            Node* cn = ps->root->findByType("Camera2D");
            if (cn) { Camera2D* cam = static_cast<Camera2D*>(cn); camActive = true; camZ = cam->zoom > 0.01f ? cam->zoom : 1.0f; camX = cam->position.x; camY = cam->position.y; }
            float zf = camActive ? camZ : 1.0f;
            float hx = logicW_ * 0.5f, hy = logicH_ * 0.5f;
            const std::vector<Particle>& plist = g_particles.list();
            for (const Particle& pp : plist) {
                float t = pp.maxLife > 0.0f ? (1.0f - pp.life / pp.maxLife) : 1.0f;
                if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                float sz = pp.size * (1.0f - t) + pp.sizeEnd * t; if (sz < 1.0f) sz = 1.0f; sz *= zf;
                float sx = camActive ? ((pp.x - camX) * camZ + hx) : pp.x;
                float sy = camActive ? ((pp.y - camY) * camZ + hy) : pp.y;
                float a = pp.maxLife > 0.0f ? (pp.life / pp.maxLife) : 0.0f;
                if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
                out += std::string("DRAW text|") + pp.glyph + "|" + std::to_string((int)sx) + "|" + std::to_string((int)sy) + "|" + std::to_string((int)sz) + "|" + colorToHexA(withAlpha(pp.color, a)) + "|" + std::to_string(pp.rot * 57.2957795f) + "\n";
            }
        }

        if (std::fabs(transOffset_) > 0.01f) shiftOutputX(out, transOffset_);
        if (transAlpha_ > 0.001f) {
            int ai = (int)(transAlpha_ * 255.0f + 0.5f); if (ai < 0) ai = 0; if (ai > 255) ai = 255;
            char ab[3]; std::snprintf(ab, sizeof(ab), "%02X", (unsigned)ai);
            out += "DRAW rect|0|0|" + std::to_string((int)logicW_) + "|" + std::to_string((int)logicH_) + "|#";
            out += ab; out += "000000|0\n";
        }

        if (dbg_ && sceneMgr_ && sceneMgr_->current() && sceneMgr_->current()->root) {
            nodeCount_ = countNodes(sceneMgr_->current()->root.get()); lastDraws_ = 0;
            for (size_t i = 0; i + 4 < out.size(); ++i) if (out[i] == 'D' && out[i + 1] == 'R' && out[i + 2] == 'A' && out[i + 3] == 'W') ++lastDraws_;
            out += "DRAW text|fps " + std::to_string((int)fps_) + "  nodes " + std::to_string(nodeCount_) + "  draws " + std::to_string(lastDraws_) + "  parts " + std::to_string((int)g_particles.count()) + "|20|100|18|#FFD700|0\n";
            out += "DRAW text|vars " + std::to_string((int)ctx_.vars.size()) + "  score " + std::to_string(ctx_.score) + "|20|124|18|#FFD700|0\n";
            size_t ln = g_luaLog.size(); int show = ln > 4 ? 4 : (int)ln;
            for (int i = 0; i < show; ++i) out += "DRAW text|" + g_luaLog[ln - show + i] + "|20|" + std::to_string(148 + i * 20) + "|16|#87CEEB|0\n";
        }

        out += "RES|" + std::to_string((int)logicW_) + "|" + std::to_string((int)logicH_) + "\n";
        if (emitOrient_) { out += "ORIENT|" + orientName_ + "\n"; emitOrient_ = false; }
        input_.endFrame(); return out;
    }

    void runAction(const std::string& act) {
        Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr; if (!sc) return;
        if (act == "dbg:") { dbg_ = !dbg_; return; }
        const std::string pReturn = "editor_return:";
        const std::string pRestart = "restart_scene:", pChange = "change_scene:", pAdd = "add_var:", pSet = "set_var:", pHub = "hub:", pCall = "call:";
        if (act.rfind(pReturn, 0) == 0) {
            playFromEditor_ = false;
            if (saveVarsEnabled_) saveVars();
            clearTransition();
            enterEditor(lastEditorDir_);
            return;
        }
        if (act.rfind(pHub, 0) == 0) {
            if (saveVarsEnabled_) saveVars();
            playFromEditor_ = false;
            clearTransition();
            appMode_ = AppMode::Hub;
            pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
            clearDialogResults();
            rebuildHub();
        }
        else if (act.rfind(pRestart, 0) == 0) { startTransition(3, act.substr(pRestart.size()), 0.0f); }
        else if (act.rfind(pChange, 0) == 0) { startTransition(3, act.substr(pChange.size()), 0.0f); }
        else if (act.rfind(pCall, 0) == 0) scripts_.callGlobal(act.substr(pCall.size()), ctx_, *sceneMgr_, ctx_.vars, sc);
        else if (act.rfind(pAdd, 0) == 0 || act.rfind(pSet, 0) == 0) {
            bool isAdd = act.rfind(pAdd, 0) == 0;
            std::string rest = act.substr(isAdd ? pAdd.size() : pSet.size());
            size_t c = rest.find(':');
            if (c != std::string::npos) { std::string name = rest.substr(0, c); double v = atof(rest.substr(c + 1).c_str()); if (isAdd) ctx_.vars[name] += v; else ctx_.vars[name] = v; }
        }
    }

    void processUi() {
        Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr; if (!sc) return;
        for (auto& b : sc->ui) { if (!b.touch.pressEdge || b.action.empty()) continue; runAction(b.action); if (appMode_ != AppMode::Game || transActive_) return; }
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(projectsDir() + dir + "/project.json", pi)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        std::string root = pi.rootPath;
        std::string font = root + "/" + pi.defaultFont;
        if (!fileExists(font)) font = std::string(PROJECT_ROOT) + "/assets/fonts/Ubuntu-Regular.ttf";
        auto mgr = std::make_unique<SceneManager>(root, font);
        if (!mgr->restartScene("scenes/main.json", resources_)) if (!mgr->restartScene(pi.mainScene, resources_)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = font; sceneMgr_ = std::move(mgr);
        lastEditorDir_ = dir;
        editor_ = std::make_unique<Editor>();
        editor_->attach(sceneMgr_->current());
        editor_->setProjectRoot(project_.rootPath);
        showCreate_ = false; showAssets_ = false; showSettings_ = false; showPrefabs_ = false; showFiles_ = true;
        assetScroll_ = 0; prefabScroll_ = 0;
        pendingDeleteFile_.clear();
        pendingText_ = false; pendingName_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
        pendingSceneSave_ = false; pendingPrefabSave_ = false;
        fsPath_ = ""; manip_ = Manip::Move; pinching_ = false; hierScroll_ = 0; fsScroll_ = 0;
        pickParent_ = false; pickChild_.clear(); lastMsg_.clear(); edZoom_ = 1.0f;
        undoStack_.clear(); redoStack_.clear(); clipboard_.reset();
        scriptMode_ = false; scriptPath_.clear(); scriptLines_.clear(); compAnchor_ = -1; imeShown_ = false;
        pendingLoadVars_ = false; saveVarsEnabled_ = false; saveTimer_ = 0.0f;
        pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
        clearDialogResults(); g_tweens.clear(); g_particles.clear();
        loadProjCamera(project_.rootPath);
        logicW_ = 1280.0f; logicH_ = 720.0f; orientVertical_ = false; emitOrient_ = false;
        input_.screenWidth = logicW_; input_.screenHeight = logicH_;
        clearTransition();
        buildEditorPanels(); input_.setUi(&editorScene_.ui); touch_.resetJoystick();
        appMode_ = AppMode::Editor;
        return true;
    }

    int applyEditorAction(const std::string& act) {
        if (!editor_) return 0;
        auto rebuild = [&]() { buildEditorPanels(); input_.setUi(&editorScene_.ui); };
        bool allowedInScript = act == "kb_toggle" || act == "tab_scene" || act == "ssave" || act == "scup" || act == "scdn" || act == "snew" || act.rfind("scrfile:", 0) == 0;
        if (scriptMode_ && !allowedInScript) return 0;
        if (act == "kb_toggle") { if (imeShown_) { imeWantOff_ = true; imeShown_ = false; } else { imeWantOn_ = true; imeShown_ = true; } rebuild(); return 1; }
        if (act == "tab_scripts") {
            if (!scriptMode_) { scriptMode_ = true; if (scriptPath_.empty()) loadScript("scripts/main.lua"); imeWantOn_ = true; imeShown_ = true; rebuild(); return 1; } return 0;
        }
        if (act == "tab_scene") { if (scriptMode_) { scriptMode_ = false; imeWantOff_ = true; imeShown_ = false; rebuild(); return 1; } return 0; }
        if (act.rfind("scrfile:", 0) == 0) { loadScript("scripts/" + act.substr(8)); imeChanged_ = true; rebuild(); return 1; }
        if (act == "ssave") { saveScript(); rebuild(); return 1; }
        if (act == "scup") { scriptScroll_ -= 3; imeChanged_ = true; rebuild(); return 1; }
        if (act == "scdn") { scriptScroll_ += 3; imeChanged_ = true; rebuild(); return 1; }
        if (act == "snew") { pendingName_ = true; pendingKind_ = 2; rebuild(); return 1; }
        if (scriptMode_) return 0;

        Node* sn = editor_->selected();
        std::string sel = sn ? sn->name : std::string(); std::string selUi = editor_->selectedUi();
        UiButton* ub = selUi.empty() ? nullptr : editor_->findUi(selUi);
        Node2D* s2 = (!sel.empty()) ? editor_->find2d(sel) : nullptr;
        bool lk = (s2 != nullptr) && s2->locked;

        if (act == "ed_play") {
            editor_->save(project_.rootPath + "/scenes/main.json");
            std::string pd = projectsDir();
            std::string dir = project_.rootPath;
            if (dir.rfind(pd, 0) == 0) dir = dir.substr(pd.size());
            pendingName_ = false; pendingText_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
            pendingSceneSave_ = false; pendingPrefabSave_ = false;
            clearDialogResults();
            playFromEditor_ = true;
            if (!enterGame(dir)) { playFromEditor_ = false; lastMsg_ = "play failed"; rebuild(); return 0; }
            return 2;
        }
        if (act == "save_as_prefab") {
            if (sel.empty() || !s2) { lastMsg_ = "select a node first"; return 0; }
            if (std::string(s2->typeName()) == "Prefab2D") { lastMsg_ = "already a prefab"; return 0; }
            if (lk) { lastMsg_ = "unlock node first"; return 0; }
            pendingPrefabSave_ = true; pendingText_ = true; pendingTextCur_ = sel; return 0;
        }
        if (act == "prefabs_open") { showPrefabs_ = true; prefabScroll_ = 0; rebuild(); return 1; }
        if (act == "prefabs_close") { showPrefabs_ = false; rebuild(); return 1; }
        if (act == "prefabs_up") { prefabScroll_ -= 3; rebuild(); return 1; }
        if (act == "prefabs_dn") { prefabScroll_ += 3; rebuild(); return 1; }
        if (act.rfind("prefab_add:", 0) == 0) {
            std::string rel = act.substr(11);
            pushUndo();
            std::string name = "PF" + std::to_string(createCounter_++);
            Node2D* base = editor_->addNode("Prefab2D", name, 640, 360);
            Prefab2D* pf = base ? static_cast<Prefab2D*>(base) : nullptr;
            if (pf) { pf->sourcePath = rel; pf->instantiate(project_.rootPath, fontPath_, editor_->scene()); editor_->select(name); lastMsg_ = "added " + rel; }
            else { lastMsg_ = "insert failed"; }
            showPrefabs_ = false; rebuild(); return 1;
        }
        if (act == "prefab_reload") {
            Prefab2D* pf = s2 ? dynamic_cast<Prefab2D*>(s2) : nullptr;
            if (pf && !pf->sourcePath.empty()) { pushUndo(); pf->instantiate(project_.rootPath, fontPath_, editor_->scene()); lastMsg_ = "reloaded " + pf->sourcePath; rebuild(); return 1; }
            return 0;
        }
        if (act == "save_scene_as") { pendingSceneSave_ = true; pendingText_ = true; pendingTextCur_ = "main"; return 0; }
        if (act == "files_open") { showFiles_ = !showFiles_; fsScroll_ = 0; pendingDeleteFile_.clear(); rebuild(); return 1; }
        if (act == "fscroll_up") { fsScroll_ -= 3; rebuild(); return 1; }
        if (act == "fscroll_dn") { fsScroll_ += 3; rebuild(); return 1; }
        if (act.rfind("fs_del_ask:", 0) == 0) { pendingDeleteFile_ = act.substr(11); rebuild(); return 1; }
        if (act.rfind("fs_del_yes:", 0) == 0) {
            std::string rel = act.substr(11);
            std::string full = project_.rootPath + "/" + rel;
            if (pendingDeleteFile_ == rel && fileExists(full)) {
                if (std::remove(full.c_str()) == 0) lastMsg_ = "deleted " + rel; else lastMsg_ = "delete failed: " + rel;
            } else { lastMsg_ = "delete cancelled"; }
            pendingDeleteFile_.clear(); rebuild(); return 1;
        }
        if (act == "create_particle" || act == "create:Particle2D:none") { pushUndo(); std::string name = "Emitter" + std::to_string(createCounter_++); editor_->addNode("Particle2D", name, 640, 360); editor_->select(name); showCreate_ = false; rebuild(); return 1; }
        Particle2D* p2 = dynamic_cast<Particle2D*>(s2);
        if (act.rfind("view:", 0) == 0) { if (lk || sel.empty() || !p2) return 0; pushUndo(); std::string preset = act.substr(5); if (editor_->setEmitterPreset(sel, preset)) { p2->emitting = true; p2->burstPending = true; lastMsg_ = "view " + preset + " -> " + sel; rebuild(); return 1; } rebuild(); return 0; }
        if (act == "ponoff") { if (!lk && p2) { pushUndo(); p2->emitting = !p2->emitting; if (p2->emitting) p2->burstPending = true; lastMsg_ = p2->emitting ? ("emitting ON: " + sel) : ("emitting OFF: " + sel); rebuild(); return 1; } return 0; }
        if (act == "pcolor") { if (!lk && p2) { pendingRgb_ = 3; pendingText_ = true; pendingTextCur_ = rgbStr(p2->color); } return 0; }
        if (act.rfind("pnum:", 0) == 0) {
            if (!lk && p2) {
                pendingNum_ = true; pendingNumKind_ = act.substr(5);
                if (pendingNumKind_ == "rate") pendingNumCur_ = std::to_string((int)p2->rate);
                else if (pendingNumKind_ == "life") pendingNumCur_ = std::to_string(p2->life);
                else if (pendingNumKind_ == "size") pendingNumCur_ = std::to_string((int)p2->size);
                else if (pendingNumKind_ == "spread") pendingNumCur_ = std::to_string((int)p2->spread);
                else if (pendingNumKind_ == "gravity") pendingNumCur_ = std::to_string((int)p2->gravity);
                else pendingNumCur_ = "0";
            }
            return 0;
        }
        if (act == "settings_open") { showSettings_ = !showSettings_; if (showSettings_) showCreate_ = false; rebuild(); return 1; }
        if (act == "settings_close") { showSettings_ = false; rebuild(); return 1; }
        if (act.rfind("import_category:", 0) == 0) {
            std::string category = act.substr(16);
            if (category == "fonts" || category == "sprites") pendingImportCategory_ = category;
            else lastMsg_ = category + ": coming soon";
            return 0;
        }
        if (act == "col_rgb") { if (!lk) { pendingRgb_ = 1; pendingText_ = true; pendingTextCur_ = ub ? rgbStr(ub->color) : (s2 ? rgbStr(s2->color) : std::string("255,255,255")); } return 0; }
        if (act == "bg_rgb") { pendingRgb_ = 2; pendingText_ = true; unsigned bc = (editor_->scene() && editor_->scene()->bgSet()) ? parseColor(editor_->scene()->bg) : currentTheme().bg; pendingTextCur_ = rgbStr(bc); return 0; }
        if (act == "assets_open") { showAssets_ = !showAssets_; assetScroll_ = 0; rebuild(); return 1; }
        if (act == "assets_up") { assetScroll_ -= 3; rebuild(); return 1; }
        if (act == "assets_dn") { assetScroll_ += 3; rebuild(); return 1; }
        if (act.rfind("tex_pick:", 0) == 0) {
            if (!showAssets_) return 0;
            std::string img = act.substr(9); std::string rel = "assets/" + img;
            if (s2) { if (lk) return 0; pushUndo(); setNodeTexture(sel, rel); lastMsg_ = "tex " + img + " -> " + sel; }
            else if (ub) { pushUndo(); ub->texture = rel; lastMsg_ = "tex " + img + " -> button " + selUi; }
            else { pushUndo(); std::string name = "Sprite" + std::to_string(createCounter_++); editor_->addNode("Sprite2D", name, 640, 360); setNodeTexture(name, rel); editor_->select(name); lastMsg_ = "sprite " + name + " <- " + img; }
            showAssets_ = false; rebuild(); return 1;
        }
        if (act == "ed_lock") { if (s2) { pushUndo(); s2->locked = !s2->locked; lastMsg_ = s2->locked ? "locked " + sel : "unlocked " + sel; rebuild(); return 1; } return 0; }
        if (act == "ed_undo") return doUndo() ? 1 : 0;
        if (act == "ed_redo") return doRedo() ? 1 : 0;
        if (act == "ed_copy") { if (s2) { clipboard_ = s2->cloneNode(); lastMsg_ = "copied " + sel; } return 0; }
        if (act == "ed_paste") {
            if (clipboard_ && editor_->scene() && editor_->scene()->root) { pushUndo(); auto cp = clipboard_->cloneNode(); std::string nm = cp->name + "_c" + std::to_string(clipCounter_++); cp->name = nm; Node2D* raw = dynamic_cast<Node2D*>(cp.get()); if (raw) raw->position.x += 40; editor_->scene()->root->addChild(std::move(cp)); editor_->select(nm); lastMsg_ = "pasted " + nm; rebuild(); return 1; }
            lastMsg_ = "clipboard empty"; return 0;
        }
        if (act.rfind("fold:", 0) == 0) { std::string nm = act.substr(5); if (collapsed_.count(nm)) collapsed_.erase(nm); else collapsed_.insert(nm); rebuild(); return 1; }
        if (act == "hier_up") { hierScroll_ -= 3; rebuild(); return 1; }
        if (act == "hier_dn") { hierScroll_ += 3; rebuild(); return 1; }
        if (act == "fs_up") { std::string tmp = fsPath_; while (!tmp.empty() && tmp.back() == '/') tmp.pop_back(); size_t sl = tmp.find_last_of('/'); fsPath_ = (sl == std::string::npos) ? std::string("") : tmp.substr(0, sl + 1); fsScroll_ = 0; pendingDeleteFile_.clear(); rebuild(); return 1; }
        if (act.rfind("fs_enter:", 0) == 0) { fsPath_ += act.substr(9) + "/"; fsScroll_ = 0; pendingDeleteFile_.clear(); rebuild(); return 1; }
        if (act.rfind("fs_pick:", 0) == 0) {
            std::string rel = act.substr(8);
            if (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".prf") == 0) { lastMsg_ = "prefab: use PF ADD to insert"; rebuild(); return 0; }
            if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".json") == 0) {
                if (sceneMgr_ && sceneMgr_->restartScene(rel, resources_)) {
                    editor_->attach(sceneMgr_->current());
                    editor_->setProjectRoot(project_.rootPath);
                    scripted_.clear();
                    showCreate_ = false; showAssets_ = false; showSettings_ = false; showPrefabs_ = false; showFiles_ = true;
                    assetScroll_ = 0; dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pinching_ = false;
                    pickParent_ = false; pickChild_.clear(); hierScroll_ = 0;
                    pendingDeleteFile_.clear(); undoStack_.clear(); redoStack_.clear();
                    lastMsg_ = "loaded " + rel; rebuild(); return 1;
                }
                return 0;
            }
            if (!lk) { pushUndo(); if (ub) { ub->texture = rel; rebuild(); return 1; } if (!sel.empty()) { setNodeTexture(sel, rel); rebuild(); return 1; } }
            return 0;
        }
        if (act == "clear_tex") {
            if (!lk) {
                pushUndo();
                if (ub) { ub->texture.clear(); rebuild(); return 1; }
                if (!sel.empty()) {
                    editor_->setTexture(sel, std::string());
                    Node2D* n = editor_->find2d(sel);
                    Sprite2D* sp = n ? dynamic_cast<Sprite2D*>(n) : nullptr;
                    if (sp) sp->texturePath.clear();
                    if (n) n->shape = "square";
                    rebuild(); return 1;
                }
            }
            return 0;
        }
        if (act == "manip:move") { manip_ = Manip::Move; rebuild(); return 1; }
        if (act == "manip:rotate") { manip_ = Manip::Rotate; rebuild(); return 1; }
        if (act == "manip:scale") { manip_ = Manip::Scale; rebuild(); return 1; }
        if (act == "create_open") { showCreate_ = !showCreate_; rebuild(); return 1; }
        if (act == "edit_text") { if (!lk) { pendingRgb_ = 0; if (ub) { pendingText_ = true; pendingTextCur_ = ub->text; } else if (sn && std::string(sn->typeName()) == "Label") { pendingText_ = true; pendingTextCur_ = static_cast<Label*>(sn)->text; } } return 0; }
        if (act == "edit_action") { if (!lk) { pendingAction_ = true; pendingActionCur_ = ub ? ub->action : (s2 ? s2->action : std::string("")); } return 0; }
        if (act.rfind("num:", 0) == 0) {
            if (!lk) {
                pendingNum_ = true; pendingNumKind_ = act.substr(4);
                if (pendingNumKind_ == "nx") pendingNumCur_ = s2 ? std::to_string((int)s2->position.x) : "0";
                else if (pendingNumKind_ == "ny") pendingNumCur_ = s2 ? std::to_string((int)s2->position.y) : "0";
                else if (pendingNumKind_ == "nrot") pendingNumCur_ = s2 ? std::to_string((int)(s2->rotation * 57.2957795f)) : "0";
                else if (pendingNumKind_ == "nscl") pendingNumCur_ = s2 ? std::to_string((int)(s2->scale.x * 100)) : "100";
                else if (pendingNumKind_ == "nw") pendingNumCur_ = s2 ? std::to_string((int)s2->w) : "32";
                else if (pendingNumKind_ == "nh") pendingNumCur_ = s2 ? std::to_string((int)s2->h) : "32";
                else if (pendingNumKind_ == "nalpha") pendingNumCur_ = s2 ? std::to_string((int)(s2->alpha * 100)) : "100";
                else if (pendingNumKind_ == "bx") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.x) : "0";
                else if (pendingNumKind_ == "by") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.y) : "0";
                else if (pendingNumKind_ == "bw") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.w) : "100";
                else if (pendingNumKind_ == "bh") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.h) : "50";
                else if (pendingNumKind_ == "bang") pendingNumCur_ = ub ? std::to_string((int)ub->angle) : "0";
                else if (pendingNumKind_ == "balpha") pendingNumCur_ = ub ? std::to_string((int)(ub->alpha * 100)) : "100";
                else pendingNumCur_ = "0";
            }
            return 0;
        }
        if (act == "ed_scr") { if (!lk && !sel.empty()) { attachScript(sel); rebuild(); return 1; } return 0; }
        if (act == "ed_clone") {
            if (!lk && !sel.empty()) {
                pushUndo();
                Node2D* cl = editor_->cloneSelected(sel + "_copy");
                Prefab2D* cpf = cl ? dynamic_cast<Prefab2D*>(cl) : nullptr;
                if (cpf) cpf->instantiate(project_.rootPath, fontPath_, editor_->scene());
                rebuild(); return 1;
            }
            return 0;
        }
        if (act == "ed_parent") {
            if (!lk) {
                if (!selUi.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? ("UI:" + selUi) : std::string(); rebuild(); return 1; }
                if (!sel.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? sel : std::string(); rebuild(); return 1; }
            }
            return 0;
        }
        if (act == "ed_unparent") { if (!lk) { if (!selUi.empty()) { pushUndo(); detachChild("UI:" + selUi); rebuild(); return 1; } if (!sel.empty()) { pushUndo(); detachChild(sel); rebuild(); return 1; } } return 0; }
        if (act.rfind("ed_selectui:", 0) == 0) { std::string id = act.substr(12); if (pickParent_ && !pickChild_.empty() && pickChild_ != id && pickChild_ != ("UI:" + id)) { pushUndo(); attachChildTo(pickChild_, id); pickParent_ = false; pickChild_.clear(); } else editor_->selectUi(id); rebuild(); return 1; }
        if (act.rfind("ed_select:", 0) == 0) { std::string nm = act.substr(10); if (pickParent_ && !pickChild_.empty() && nm != pickChild_) { pushUndo(); attachChildTo(pickChild_, nm); pickParent_ = false; pickChild_.clear(); } else editor_->select(nm); rebuild(); return 1; }
        if (act == "create_cam") { pushUndo(); std::string name = "Cam" + std::to_string(createCounter_++); editor_->addNode("Camera2D", name, 640, 360); editor_->select(name); showCreate_ = false; rebuild(); return 1; }
        if (act == "create_light") { pushUndo(); std::string name = "Light" + std::to_string(createCounter_++); editor_->addNode("Light2D", name, 640, 360); editor_->select(name); showCreate_ = false; rebuild(); return 1; }
        if (act == "create_grp") { pendingName_ = true; pendingKind_ = 0; pendingType_ = "Node2D"; pendingShape_ = "none"; showCreate_ = false; rebuild(); return 1; }
        if (act == "create_btn") { pendingName_ = true; pendingKind_ = 1; showCreate_ = false; rebuild(); return 1; }
        if (act.rfind("create:", 0) == 0) { std::string rest = act.substr(7); size_t c = rest.find(':'); pendingName_ = true; pendingKind_ = 0; pendingType_ = rest.substr(0, c); pendingShape_ = (c == std::string::npos) ? "" : rest.substr(c + 1); showCreate_ = false; rebuild(); return 1; }
        if (act.rfind("ed_shape:", 0) == 0) { if (!lk && !sel.empty()) { pushUndo(); editor_->setShape(sel, act.substr(9)); rebuild(); return 1; } return 0; }
        if (act.rfind("ed_move:", 0) == 0) {
            if (lk) return 0;
            std::string d = act.substr(8);
            if (ub) { pushUndo(); if (manip_ == Manip::Move) { float dx = (d == "l") ? -16 : (d == "r") ? 16 : 0; float dy = (d == "u") ? -16 : (d == "d") ? 16 : 0; ub->touch.rect.x += dx; ub->touch.rect.y += dy; } else if (manip_ == Manip::Rotate) { float dr = (d == "l") ? -15.0f : (d == "r") ? 15.0f : 0.0f; ub->angle += dr; } else if (manip_ == Manip::Scale) { float f = (d == "u") ? 1.1f : (d == "d") ? (1.0f / 1.1f) : 1.0f; ub->touch.rect.w *= f; ub->touch.rect.h *= f; } rebuild(); return 1; }
            if (s2) { pushUndo(); if (manip_ == Manip::Move) { float dx = (d == "l") ? -16 : (d == "r") ? 16 : 0; float dy = (d == "u") ? -16 : (d == "d") ? 16 : 0; editor_->moveSelected(dx, dy); if (editor_->scene()) moveGroupButtons(editor_->scene(), sel, dx, dy); } else if (manip_ == Manip::Rotate) { float dr = (d == "l") ? -15.0f : (d == "r") ? 15.0f : 0.0f; s2->rotation += dr * 3.14159265f / 180.0f; } else if (manip_ == Manip::Scale) { float f = (d == "u") ? 1.1f : (d == "d") ? (1.0f / 1.1f) : 1.0f; s2->scale.x *= f; s2->scale.y *= f; } rebuild(); return 1; }
            return 0;
        }
        if (act == "ed_del") { if (lk) return 0; if (ub) { pushUndo(); editor_->deleteUi(selUi); rebuild(); return 1; } if (!sel.empty()) { pushUndo(); editor_->deleteNode(sel); rebuild(); return 1; } return 0; }
        if (act == "ed_save") { editor_->save(project_.rootPath + "/scenes/main.json"); lastMsg_ = "saved"; return 0; }
        if (act == "ed_back") {
            if (scriptMode_) { scriptMode_ = false; imeWantOff_ = true; imeShown_ = false; }
            pendingName_ = false; pendingText_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
            pendingSceneSave_ = false; pendingPrefabSave_ = false;
            clearDialogResults();
            pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
            playFromEditor_ = false;
            clearTransition();
            appMode_ = AppMode::Hub; rebuildHub();
            return 2;
        }
        return 0;
    }

    void processEditorActions() {
        if (!editor_) return;
        std::vector<std::string> pressed; pressed.reserve(editorScene_.ui.size());
        for (const auto& b : editorScene_.ui) if (b.touch.pressEdge && !b.action.empty()) pressed.push_back(b.action);
        for (const auto& act : pressed) { int r = applyEditorAction(act); if (r == 2) break; }
    }

    void consumeDialogResults() {
        std::string txt, nm, act, num;
        bool ht = false, hn = false, ha = false, hnum = false;
        { std::lock_guard<std::mutex> lk(dlgMtx_); ht = hasText_; hn = hasName_; ha = hasAction_; hnum = hasNum_; txt = textRes_; nm = nameRes_; act = actionRes_; num = numRes_; hasText_ = false; hasName_ = false; hasAction_ = false; hasNum_ = false; }
        if (!editor_) return;

        if (ht && pendingSceneSave_) {
            std::string name = sanitizeProjectDirName(txt);
            if (!name.empty()) { editor_->setProjectRoot(project_.rootPath); if (editor_->saveScene(name)) lastMsg_ = "saved scenes/" + name + ".json"; else lastMsg_ = "save scene failed"; }
            pendingSceneSave_ = false; buildEditorPanels(); input_.setUi(&editorScene_.ui); return;
        }
        if (ht && pendingPrefabSave_) {
            std::string name = sanitizeProjectDirName(txt);
            if (!name.empty()) { editor_->setProjectRoot(project_.rootPath); if (editor_->saveAsPrefab(name)) lastMsg_ = "saved prefabs/" + name + ".prf"; else lastMsg_ = "prefab save failed"; }
            pendingPrefabSave_ = false; buildEditorPanels(); input_.setUi(&editorScene_.ui); return;
        }

        if (hn) {
            if (pendingKind_ == 2) {
                if (!nm.empty()) {
                    std::string safe = sanitizeProjectDirName(nm);
                    if (safe.size() >= 4 && safe.compare(safe.size() - 4, 4, ".lua") == 0) safe = safe.substr(0, safe.size() - 4);
                    if (safe.empty()) safe = "script";
                    std::string rel = "scripts/" + safe + ".lua";
                    ProjectCreator::createScript(project_.rootPath, rel, safe); loadScript(rel);
                    lastMsg_ = "script created: " + rel;
                }
            } else if (!nm.empty()) {
                pushUndo();
                if (pendingKind_ == 0) { editor_->addNode(pendingType_, nm, 640, 360); if (!pendingShape_.empty()) editor_->setShape(nm, pendingShape_); editor_->select(nm); lastMsg_ = "created " + nm; }
                else if (pendingKind_ == 1) { editor_->addUi(nm, nm, 580, 335, 120, 50, std::string(), parseColor("#808080")); editor_->selectUi(nm); lastMsg_ = "created button " + nm; }
            }
            pendingName_ = false; showCreate_ = false;
        }
        if (ht) {
            if (pendingRgb_ == 1) { unsigned c = 0; if (parseRgb(txt, c)) { pushUndo(); std::string uid = editor_->selectedUi(); if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) b->color = c; } else { Node* s = editor_->selected(); Particle2D* pep = dynamic_cast<Particle2D*>(s); if (pep) pep->color = c; else { Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr; if (n2) n2->color = c; } } } else lastMsg_ = "bad rgb, need r,g,b"; pendingRgb_ = 0; }
            else if (pendingRgb_ == 2) { unsigned c = 0; if (parseRgb(txt, c) && editor_->scene()) { pushUndo(); editor_->scene()->bg = colorToHex(c); } else lastMsg_ = "bad rgb, need r,g,b"; pendingRgb_ = 0; }
            else if (pendingRgb_ == 3) { unsigned c = 0; if (parseRgb(txt, c)) { pushUndo(); Particle2D* pe = dynamic_cast<Particle2D*>(editor_->selected()); if (pe) pe->color = c; } else lastMsg_ = "bad rgb, need r,g,b"; pendingRgb_ = 0; }
            else { std::string uid = editor_->selectedUi(); if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) { pushUndo(); b->text = txt; } } else { Node* s = editor_->selected(); if (s && std::string(s->typeName()) == "Label") { pushUndo(); static_cast<Label*>(s)->text = txt; } } }
        }
        if (ha) {
            std::string uid = editor_->selectedUi();
            if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) { pushUndo(); b->action = act; } }
            else { Node* s = editor_->selected(); Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr; if (n2) { pushUndo(); n2->action = act; } }
        }
        if (hnum) {
            if (pendingNumKind_ == "camw" || pendingNumKind_ == "camh") {
                float v = (float)atof(num.c_str());
                if (v < 160.0f) v = 160.0f; if (v > 2160.0f) v = 2160.0f;
                if (pendingNumKind_ == "camw") projCamW_ = v; else projCamH_ = v;
                projVertical_ = (projCamH_ > projCamW_);
                saveProjCamera();
                pendingNumKind_.clear();
            } else {
                float v = (float)atof(num.c_str());
                std::string uid = editor_->selectedUi();
                UiButton* b = uid.empty() ? nullptr : editor_->findUi(uid);
                Node* s = editor_->selected(); Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr;
                bool lk2 = (n2 != nullptr) && n2->locked;
                if (!lk2 && (n2 || b)) pushUndo();
                if (!lk2 && n2) {
                    if (pendingNumKind_ == "nx") { float dx = v - n2->position.x; n2->position.x = v; if (editor_->scene()) moveGroupButtons(editor_->scene(), n2->name, dx, 0); }
                    else if (pendingNumKind_ == "ny") { float dy = v - n2->position.y; n2->position.y = v; if (editor_->scene()) moveGroupButtons(editor_->scene(), n2->name, 0, dy); }
                    else if (pendingNumKind_ == "nrot") n2->rotation = v * 3.14159265f / 180.0f;
                    else if (pendingNumKind_ == "nscl") { float f = v / 100.0f; if (f > 0.01f) { n2->scale.x = f; n2->scale.y = f; } }
                    else if (pendingNumKind_ == "nw") n2->w = v;
                    else if (pendingNumKind_ == "nh") n2->h = v;
                    else if (pendingNumKind_ == "nalpha") { float a = v / 100.0f; if (a < 0) a = 0; if (a > 1) a = 1; n2->alpha = a; }
                }
                Particle2D* pe = dynamic_cast<Particle2D*>(s);
                if (!lk2 && pe) {
                    if (pendingNumKind_ == "rate") pe->rate = v;
                    else if (pendingNumKind_ == "life") { pe->life = v; if (pe->life < 0.05f) pe->life = 0.05f; }
                    else if (pendingNumKind_ == "size") { pe->size = v; if (pe->size < 1.0f) pe->size = 1.0f; }
                    else if (pendingNumKind_ == "spread") pe->spread = v;
                    else if (pendingNumKind_ == "gravity") pe->gravity = v;
                }
                if (b) {
                    if (pendingNumKind_ == "bx") b->touch.rect.x = v;
                    else if (pendingNumKind_ == "by") b->touch.rect.y = v;
                    else if (pendingNumKind_ == "bw") b->touch.rect.w = v;
                    else if (pendingNumKind_ == "bh") b->touch.rect.h = v;
                    else if (pendingNumKind_ == "bang") b->angle = v;
                    else if (pendingNumKind_ == "balpha") { float a = v / 100.0f; if (a < 0) a = 0; if (a > 1) a = 1; b->alpha = a; }
                }
            }
        }
        if (hn || ht || ha || hnum) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    std::string stepEditor() {
        if (!editor_ || !editor_->scene()) {
            pendingName_ = false; pendingText_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
            pendingSceneSave_ = false; pendingPrefabSave_ = false;
            clearDialogResults(); pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
            clearTransition();
            appMode_ = AppMode::Hub; rebuildHub(); return "";
        }
        consumeDialogResults();
        if (scriptMode_) imeApply();
        if (dragging_ || gizmoRot_ || gizmoSclX_ || gizmoSclY_ || gizmoRotUi_) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }

        gameBackend_.begin(); Renderer gr(gameBackend_); gr.render(editorScene_, &ctx_);
        std::string out = gameBackend_.str();

        if (scriptMode_) {
            const int LINES = 24;
            for (int i = scriptScroll_; i < (int)scriptLines_.size() && i < scriptScroll_ + LINES; ++i) {
                float y = 70 + (float)(i - scriptScroll_) * 19;
                std::string txt = sanitizeLine(scriptLines_[i]);
                if (txt.size() > 68) txt = txt.substr(0, 68);
                out += "DRAW codeline|" + std::to_string(i) + "|" + txt + "|" + std::to_string((int)y) + "|14|" +
                       (i == curLine_ ? "#FFD700" : "#D8E0F0") + "\n";
            }
            if (curLine_ >= scriptScroll_ && curLine_ < scriptScroll_ + LINES && curLine_ < (int)scriptLines_.size()) {
                std::string shown = sanitizeLine(scriptLines_[curLine_]);
                if (shown.size() > 68) shown = shown.substr(0, 68);
                int cut = curCol_; if (cut < 0) cut = 0; if (cut > (int)shown.size()) cut = (int)shown.size();
                while (cut > 0 && cut < (int)shown.size() && ((unsigned char)shown[cut] & 0xC0) == 0x80) --cut;
                std::string pref = shown.substr(0, cut);
                float y = 70 + (float)(curLine_ - scriptScroll_) * 19;
                out += "DRAW caret|" + pref + "|340|" + std::to_string((int)y) + "|16|#4CC9F0\n";
            }
        }

        if (!scriptMode_ && !showSettings_ && !showPrefabs_) {
            out += "DRAW clipon\n";
            emitEditorViewport(*editor_->scene(), out, makeEditorRenderInput());
            Scene* es = editor_->scene();
            if (es && es->root) {
                std::string sel = editor_->selected() ? editor_->selected()->name : std::string();
                WorldXf ident;
                drawParticlePreviewTree(*es->root, ident, out, edZoom_, es->camX, es->camY, sel);
                drawTexturePreviewTree(*es->root, ident, out, edZoom_, es->camX, es->camY);
            }
            emitEditorGizmos(*editor_->scene(), out, makeEditorRenderInput());
            out += "DRAW clipoff\n";

            // Camera panel overlay (top-right of viewport). Drawn here, handled in feedTouch.
            std::string wTxt = "W " + std::to_string((int)projCamW_);
            std::string hTxt = "H " + std::to_string((int)projCamH_);
            std::string oTxt = projVertical_ ? "ROT:PORT" : "ROT:LAND";
            out += "DRAW rect|" + std::to_string((int)CAM_PX0) + "|" + std::to_string((int)CAM_BTN_W_Y0) + "|" + std::to_string((int)(CAM_PX1 - CAM_PX0)) + "|" + std::to_string((int)(CAM_BTN_W_Y1 - CAM_BTN_W_Y0)) + "|#2A2F4AFF|0\n";
            out += "DRAW text|" + wTxt + "|" + std::to_string((int)CAM_PX0 + 6) + "|" + std::to_string((int)CAM_BTN_W_Y0 + 2) + "|14|#D8E0F0|0\n";
            out += "DRAW rect|" + std::to_string((int)CAM_PX0) + "|" + std::to_string((int)CAM_BTN_H_Y0) + "|" + std::to_string((int)(CAM_PX1 - CAM_PX0)) + "|" + std::to_string((int)(CAM_BTN_H_Y1 - CAM_BTN_H_Y0)) + "|#2A2F4AFF|0\n";
            out += "DRAW text|" + hTxt + "|" + std::to_string((int)CAM_PX0 + 6) + "|" + std::to_string((int)CAM_BTN_H_Y0 + 2) + "|14|#D8E0F0|0\n";
            out += "DRAW rect|" + std::to_string((int)CAM_PX0) + "|" + std::to_string((int)CAM_BTN_O_Y0) + "|" + std::to_string((int)(CAM_PX1 - CAM_PX0)) + "|" + std::to_string((int)(CAM_BTN_O_Y1 - CAM_BTN_O_Y0)) + "|#3A2F5AFF|0\n";
            out += "DRAW text|" + oTxt + "|" + std::to_string((int)CAM_PX0 + 6) + "|" + std::to_string((int)CAM_BTN_O_Y0 + 2) + "|14|#FFD700|0\n";
        }

        processEditorActions();
        if (appMode_ != AppMode::Editor) return "";
        if (scriptMode_ && imeChanged_) { buildEditorPanels(); input_.setUi(&editorScene_.ui); imeChanged_ = false; }
        if (imeWantOn_) { out += "IME_ON\n"; imeWantOn_ = false; }
        if (imeWantOff_) { out += "IME_OFF\n"; imeWantOff_ = false; }
        if (pendingText_ && appMode_ == AppMode::Editor) { out += "REQ_TEXT|" + pendingTextCur_ + "\n"; pendingText_ = false; }
        if (pendingName_ && appMode_ == AppMode::Editor) out += "REQ_NAME|Object\n";
        if (pendingAction_ && appMode_ == AppMode::Editor) { out += "REQ_ACTION|" + pendingActionCur_ + "\n"; pendingAction_ = false; }
        if (pendingNum_ && appMode_ == AppMode::Editor) { out += "REQ_NUM|" + pendingNumCur_ + "\n"; pendingNum_ = false; }
        if (!pendingImportCategory_.empty() && appMode_ == AppMode::Editor) { out += "REQ_IMPORT|" + pendingImportCategory_ + "|" + project_.rootPath + "\n"; pendingImportCategory_.clear(); }
        out += "RES|1280|720\n";
        input_.endFrame(); return out;
    }

    AppMode appMode_ = AppMode::Hub;
    HubState hubState_;
    Scene hubScene_;
    Scene editorScene_;
    std::unique_ptr<Editor> editor_;
    ScriptSystem scripts_;
    std::set<std::string> scripted_;
    Manip manip_ = Manip::Move;
    bool showCreate_ = false;
    bool showAssets_ = false;
    bool showSettings_ = false;
    bool showPrefabs_ = false;
    bool showFiles_ = true;
    bool playFromEditor_ = false;
    std::string lastEditorDir_;
    std::string pendingDeleteFile_;
    bool dragging_ = false;
    bool pendingText_ = false;
    bool pinching_ = false;
    bool pendingName_ = false;
    bool pendingAction_ = false;
    bool pendingNum_ = false;
    bool pendingSceneSave_ = false;
    bool pendingPrefabSave_ = false;
    int pendingKind_ = 0;
    int pendingRgb_ = 0;
    std::string pendingNumKind_;
    std::string pendingNumCur_;
    bool gizmoRot_ = false;
    bool gizmoSclX_ = false;
    bool gizmoSclY_ = false;
    bool gizmoRotUi_ = false;
    int lockAxis_ = 0;
    float gizmoStartAngle_ = 0;
    float gizmoStartRot_ = 0;
    float gizmoStartDist_ = 1;
    float gizmoStartSX_ = 1;
    float gizmoStartSY_ = 1;
    bool pickParent_ = false;
    std::string pickChild_;
    std::string pendingType_;
    std::string pendingShape_;
    std::string pendingActionCur_;
    std::string pendingNodeAction_;
    std::string lastMsg_;
    float edZoom_ = 1.0f;
    float pinchDist0_ = 0;
    float pinchZoom0_ = 1.0f;
    float pinchAX_ = 0;
    float pinchAY_ = 0;
    float dragOX_ = 0;
    float dragOY_ = 0;
    float dragPSX_ = 1;
    float dragPSY_ = 1;
    int hierScroll_ = 0;
    int fsScroll_ = 0;
    int assetScroll_ = 0;
    int prefabScroll_ = 0;
    std::set<std::string> collapsed_;
    std::vector<std::string> undoStack_;
    std::vector<std::string> redoStack_;
    int snapCounter_ = 0;
    std::unique_ptr<Node> clipboard_;
    int clipCounter_ = 0;
    bool dbg_ = false;
    float fps_ = 0;
    long long lastMs_ = 0;
    int nodeCount_ = 0;
    int lastDraws_ = 0;
    bool scriptMode_ = false;
    std::string scriptPath_;
    std::vector<std::string> scriptLines_;
    int curLine_ = 0;
    int curCol_ = 0;
    int scriptScroll_ = 0;
    int compAnchor_ = -1;
    bool imeWantOn_ = false;
    bool imeWantOff_ = false;
    bool imeChanged_ = false;
    bool imeShown_ = false;
    std::mutex imeMtx_;
    std::vector<std::string> imeTextQ_;
    std::vector<std::string> imeCompQ_;
    std::vector<int> imeKeyQ_;
    bool imeFinish_ = false;
    Node2D* dragNode_ = nullptr;
    UiButton* dragUi_ = nullptr;
    int createCounter_ = 0;
    std::string pendingTextCur_;
    std::string fsPath_;
    std::string pendingImportCategory_;
    std::mutex dlgMtx_;
    bool hasText_ = false;
    bool hasName_ = false;
    bool hasAction_ = false;
    bool hasNum_ = false;
    std::string textRes_;
    std::string nameRes_;
    std::string actionRes_;
    std::string numRes_;
    bool pendingLoadVars_ = false;
    bool saveVarsEnabled_ = false;
    bool pendingNewProject_ = false;
    bool pendingHubRename_ = false;
    std::string pendingHubDir_;
    std::string pendingHubCurrentName_;
    std::string confirmDeleteDir_;
    float saveTimer_ = 0.0f;
    ProjectInfo project_;
    std::string fontPath_;
    ResourceManager resources_;
    std::unique_ptr<SceneManager> sceneMgr_;
    InputManager input_;
    TouchProcessor touch_;
    StringRenderBackend gameBackend_;
    Context ctx_;

    float logicW_ = 1280.0f;
    float logicH_ = 720.0f;
    bool orientVertical_ = false;
    bool emitOrient_ = false;
    std::string orientName_ = "landscape";

    float projCamW_ = 1280.0f;
    float projCamH_ = 720.0f;
    bool projVertical_ = false;

    bool transActive_ = false;
    int transType_ = 0;
    int transPhase_ = 0;
    float transProgress_ = 0.0f;
    float transDuration_ = 0.4f;
    float transOffset_ = 0.0f;
    float transAlpha_ = 0.0f;
    std::string transTarget_;
};

} // namespace suka
