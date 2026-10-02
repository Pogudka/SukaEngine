#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <set>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <chrono>
#include <fstream>
#include <cctype>

#include "Core.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "Render.hpp"
#include "Touch.hpp"
#include "Input.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Hub.hpp"
#include "Editor.hpp"
#include "Script.hpp"

namespace suka {

inline unsigned dimColor(unsigned c, float k) {
    unsigned r = (unsigned)(((c >> 24) & 255) * k);
    unsigned g = (unsigned)(((c >> 16) & 255) * k);
    unsigned b = (unsigned)(((c >> 8)  & 255) * k);
    unsigned a = (c & 255);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (r << 24) | (g << 16) | (b << 8) | a;
}
inline std::string sanitizeLine(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '|') o += '/';
        else if (c == '\t') o += "  ";
        else if ((unsigned char)c < 32) o += ' ';
        else o += c;
    }
    return o;
}
inline bool utf8Cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }
inline int utf8Prev(const std::string& s, int pos) {
    if (pos <= 0) return 0;
    --pos;
    while (pos > 0 && utf8Cont(s[pos])) --pos;
    return pos;
}
inline int utf8Next(const std::string& s, int pos) {
    if (pos >= (int)s.size()) return (int)s.size();
    ++pos;
    while (pos < (int)s.size() && utf8Cont(s[pos])) ++pos;
    return pos;
}
inline int utf8CpToByte(const std::string& s, int cp) {
    int p = 0;
    for (int i = 0; i < cp && p < (int)s.size(); ++i) p = utf8Next(s, p);
    return p;
}
inline int utf8ByteToCp(const std::string& s, int bytePos) {
    int c = 0, p = 0;
    while (p < bytePos && p < (int)s.size()) { p = utf8Next(s, p); ++c; }
    return c;
}
inline std::string rgbStr(unsigned c) {
    return std::to_string((c >> 24) & 255) + "," + std::to_string((c >> 16) & 255) + "," + std::to_string((c >> 8) & 255);
}
inline bool parseRgb(const std::string& s, unsigned& out) {
    int v[3]; int idx = 0; std::string num;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && isdigit((unsigned char)s[i])) { num += s[i]; continue; }
        if (!num.empty()) { if (idx < 3) v[idx++] = atoi(num.c_str()); num.clear(); }
    }
    if (idx < 3) return false;
    for (int k = 0; k < 3; ++k) { if (v[k] < 0) v[k] = 0; if (v[k] > 255) v[k] = 255; }
    out = ((unsigned)v[0] << 24) | ((unsigned)v[1] << 16) | ((unsigned)v[2] << 8) | 0xFFu;
    return true;
}

class GameApp {
public:
    enum class Mode { Console, String };
    enum class AppMode { Hub, Game, Editor };
    enum class Manip { Move, Rotate, Scale };

    bool init(Mode mode, const std::string& gameDir) {
        (void)mode;
        loadSettings();
        hubState_.games = ProjectList::scan();
        hubState_.selectedDir = gameDir;
        if (hubState_.selectedDir.empty() && !hubState_.games.empty()) hubState_.selectedDir = hubState_.games.front().dir;
        input_.screenWidth = 1280.0f; input_.screenHeight = 720.0f;
        rebuildHub(); appMode_ = AppMode::Hub;
        return true;
    }

    void submitText(const std::string& t)   { std::lock_guard<std::mutex> lk(dlgMtx_); textRes_ = t;   hasText_ = true; }
    void submitName(const std::string& t)   { std::lock_guard<std::mutex> lk(dlgMtx_); nameRes_ = t;   hasName_ = true; }
    void submitAction(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); actionRes_ = t; hasAction_ = true; }
    void submitNumber(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); numRes_ = t;    hasNum_ = true; }
    void submitScriptText(const std::string& t)   { std::lock_guard<std::mutex> lk(imeMtx_); imeTextQ_.push_back(t); }
    void submitScriptCompose(const std::string& t){ std::lock_guard<std::mutex> lk(imeMtx_); imeCompQ_.push_back(t); }
    void submitScriptFinish()                     { std::lock_guard<std::mutex> lk(imeMtx_); imeFinish_ = true; }
    void submitScriptKey(int k)                   { std::lock_guard<std::mutex> lk(imeMtx_); imeKeyQ_.push_back(k); }

    void feedMultiTouch(int phase, float x0, float y0, float x1, float y1) {
        if (appMode_ != AppMode::Editor || scriptMode_ || showCreate_) return;
        Scene* es = editor_ ? editor_->scene() : nullptr;
        if (!es) return;
        float mx = (x0 + x1) / 2.0f, my = (y0 + y1) / 2.0f;
        float dist = std::sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
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
                int colCp = (int)((x - 340) / 7.2f);
                const std::string& L = scriptLines_[line];
                int total = utf8ByteToCp(L, (int)L.size());
                if (colCp < 0) colCp = 0;
                if (colCp > total) colCp = total;
                curLine_ = line;
                curCol_ = utf8CpToByte(L, colCp);
                compAnchor_ = -1;
                imeWantOn_ = true; imeShown_ = true;
                imeChanged_ = true;
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
                } else lastMsg_ = hit.empty() ? "no target under tap" : "cannot attach to self";
                pickParent_ = false; pickChild_.clear();
                buildEditorPanels(); input_.setUi(&editorScene_.ui);
                return;
            }

            UiButton* gb = (!editor_->selectedUi().empty()) ? editor_->findUi(editor_->selectedUi()) : nullptr;
            if (gb && es) {
                float bcx, bcy; proj(*es, gb->touch.rect.x + gb->touch.rect.w/2, gb->touch.rect.y + gb->touch.rect.h/2, bcx, bcy);
                if (t.action == RawTouch::Action::Down) {
                    float dx = x - bcx, dy = y - bcy;
                    float dist = std::sqrt(dx*dx + dy*dy);
                    float R = 70.0f * Z;
                    if (manip_ == Manip::Rotate && std::fabs(dist - R) < 26.0f * Z) {
                        gizmoRotUi_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = gb->angle; return;
                    }
                } else if (t.action == RawTouch::Action::Move && gizmoRotUi_) {
                    float dx = x - bcx, dy = y - bcy;
                    gb->angle = gizmoStartRot_ + (std::atan2(dy, dx) - gizmoStartAngle_) * 57.2957795f;
                    return;
                } else if (t.action == RawTouch::Action::Up) { gizmoRotUi_ = false; }
            }

            Node2D* g = (editor_ && editor_->selectedUi().empty()) ?
                        (editor_->selected() ? dynamic_cast<Node2D*>(editor_->selected()) : nullptr) : nullptr;

            if (g && es) {
                float gwx, gwy, gwr, gsx, gsy;
                if (!nodeWorld(es, g->name, gwx, gwy, gwr, gsx, gsy)) { gwx = g->position.x; gwy = g->position.y; gsx = gsy = 1; }
                float scx, scy; proj(*es, gwx, gwy, scx, scy);
                if (t.action == RawTouch::Action::Down) {
                    if (!g->locked) {
                        float dx = x - scx, dy = y - scy;
                        float dist = std::sqrt(dx*dx + dy*dy);
                        if (manip_ == Manip::Rotate) {
                            float R = 70.0f * Z;
                            if (std::fabs(dist - R) < 26.0f * Z) { gizmoRot_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = g->rotation; return; }
                        } else if (manip_ == Manip::Scale) {
                            float hw = (g->w * gsx) * 0.46875f * Z / 2, hh = (g->h * gsy) * 0.46875f * Z / 2;
                            if (std::fabs(x - (scx + hw + 24*Z)) < 28*Z && std::fabs(dy) < 28*Z) { gizmoSclX_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSX_ = g->scale.x; return; }
                            if (std::fabs(y - (scy + hh + 24*Z)) < 28*Z && std::fabs(dx) < 28*Z) { gizmoSclY_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSY_ = g->scale.y; return; }
                        } else {
                            if (std::fabs(dy) < 16*Z && dx > 8*Z && dx < 64*Z) { lockAxis_ = 1; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                            if (std::fabs(dx) < 16*Z && dy > 8*Z && dy < 64*Z) { lockAxis_ = 2; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                        }
                    }
                }
                else if (t.action == RawTouch::Action::Move) {
                    if (gizmoRot_) { float dx = x - scx, dy = y - scy; g->rotation = gizmoStartRot_ + (std::atan2(dy, dx) - gizmoStartAngle_); return; }
                    if (gizmoSclX_ || gizmoSclY_) {
                        float dist = std::sqrt((x-scx)*(x-scx) + (y-scy)*(y-scy));
                        float f = dist / gizmoStartDist_; if (f < 0.05f) f = 0.05f;
                        if (gizmoSclX_) g->scale.x = gizmoStartSX_ * f;
                        if (gizmoSclY_) g->scale.y = gizmoStartSY_ * f;
                        return;
                    }
                }
                else if (t.action == RawTouch::Action::Up) { gizmoRot_ = false; gizmoSclX_ = false; gizmoSclY_ = false; lockAxis_ = 0; }
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
                    float dlx = (nx - oldX) / dragPSX_, dly = (ny - oldY) / dragPSY_;
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
        if (appMode_ == AppMode::Hub)    return stepHub();
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
    void proj(const Scene& sc, float wx, float wy, float& sx, float& sy) {
        float S = 0.46875f * edZoom_;
        sx = 596 + (wx - sc.camX - 640) * S;
        sy = 310 + (wy - sc.camY - 360) * S;
    }
    void unproj(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        float S = 0.46875f * edZoom_;
        wx = 640 + sc.camX + (sx - 596) / S;
        wy = 360 + sc.camY + (sy - 310) / S;
    }
    void unprojGame(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        Node* cn = sc.root ? sc.root->findByType("Camera2D") : nullptr;
        if (cn) { Camera2D* cam = static_cast<Camera2D*>(cn); float z = cam->zoom > 0.01f ? cam->zoom : 1.0f;
            wx = (sx - 640) / z + cam->position.x; wy = (sy - 360) / z + cam->position.y; }
        else { wx = sx; wy = sy; }
    }
    std::string hitUi(Scene* es, float wx, float wy) {
        for (auto& b : es->ui)
            if (wx >= b.touch.rect.x && wx <= b.touch.rect.x + b.touch.rect.w &&
                wy >= b.touch.rect.y && wy <= b.touch.rect.y + b.touch.rect.h) return b.touch.id;
        return "";
    }
    static int countNodes(const Node* n) { if (!n) return 0; int c = 1; for (auto& ch : n->getChildren()) c += countNodes(ch.get()); return c; }

    static bool nodeWorldRec(Node* n, const std::string& name, float ox, float oy, float orot, float osx, float osy,
                             float& wx, float& wy, float& wr, float& wsx, float& wsy) {
        Node2D* n2d = dynamic_cast<Node2D*>(n);
        if (!n2d) { for (auto& ch : n->getChildren()) if (nodeWorldRec(ch.get(), name, ox, oy, orot, osx, osy, wx, wy, wr, wsx, wsy)) return true; return false; }
        float cr = std::cos(orot), sr = std::sin(orot);
        float cx = ox + (n2d->position.x * osx) * cr - (n2d->position.y * osy) * sr;
        float cy = oy + (n2d->position.x * osx) * sr + (n2d->position.y * osy) * cr;
        float crot = orot + n2d->rotation;
        float csx = osx * n2d->scale.x, csy = osy * n2d->scale.y;
        if (n2d->name == name) { wx = cx; wy = cy; wr = crot; wsx = csx; wsy = csy; return true; }
        for (auto& ch : n2d->getChildren()) if (nodeWorldRec(ch.get(), name, cx, cy, crot, csx, csy, wx, wy, wr, wsx, wsy)) return true;
        return false;
    }
    static bool nodeWorld(Scene* sc, const std::string& name, float& wx, float& wy, float& wr, float& wsx, float& wsy) {
        if (!sc || !sc->root) return false;
        return nodeWorldRec(sc->root.get(), name, 0, 0, 0, 1, 1, wx, wy, wr, wsx, wsy);
    }
    void parentXf(Node* n, const std::string& name, float cx, float cy, float sx, float sy, float& ox, float& oy, float& psx, float& psy, bool& found) {
        if (found) return;
        if (n->name == name) { ox = cx; oy = cy; psx = sx; psy = sy; found = true; return; }
        float ncx = cx, ncy = cy, nsx = sx, nsy = sy;
        Node2D* n2 = dynamic_cast<Node2D*>(n);
        if (n2) {
            float cr = std::cos(n2->rotation), sr = std::sin(n2->rotation);
            ncx = cx + (n2->position.x * sx) * cr - (n2->position.y * sy) * sr;
            ncy = cy + (n2->position.x * sx) * sr + (n2->position.y * sy) * cr;
            nsx = sx * n2->scale.x; nsy = sy * n2->scale.y;
        }
        for (auto& ch : n->getChildren()) parentXf(ch.get(), name, ncx, ncy, nsx, nsy, ox, oy, psx, psy, found);
    }
    void startDragParent(Scene* es, const std::string& name) {
        dragOX_ = 0; dragOY_ = 0; dragPSX_ = 1; dragPSY_ = 1;
        if (es->root) { bool f = false; parentXf(es->root.get(), name, 0, 0, 1, 1, dragOX_, dragOY_, dragPSX_, dragPSY_, f); }
        if (dragPSX_ < 0.01f) dragPSX_ = 1; if (dragPSY_ < 0.01f) dragPSY_ = 1;
    }
    void moveGroupButtons(Scene* es, const std::string& group, float dx, float dy) {
        for (auto& b : es->ui) if (b.group == group) { b.touch.rect.x += dx; b.touch.rect.y += dy; }
    }
    void attachChildTo(const std::string& child, const std::string& parent) {
        if (!editor_ || !editor_->scene()) return;
        Scene* sc = editor_->scene();
        if (child.rfind("UI:", 0) == 0) { UiButton* b = editor_->findUi(child.substr(3)); if (b) b->group = parent; return; }
        if (!sc->root) return;
        Node* root = sc->root.get();
        if (child == parent) return;
        Node* cn = root->findNode(child); Node* pn = root->findNode(parent);
        if (!cn || !pn) return;
        if (cn->containsName(parent)) return;
        Node* owner = root->findParentOf(child);
        if (!owner || owner == pn) return;
        std::unique_ptr<Node> up = owner->takeChild(child);
        if (up) pn->addChild(std::move(up));
    }
    void detachChild(const std::string& child) {
        if (!editor_ || !editor_->scene()) return;
        Scene* sc = editor_->scene();
        if (child.rfind("UI:", 0) == 0) { UiButton* b = editor_->findUi(child.substr(3)); if (b) b->group.clear(); return; }
        if (!sc->root) return;
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
    void loadSnap(const std::string& rel) {
        if (sceneMgr_->restartScene(rel, resources_)) {
            editor_->attach(sceneMgr_->current());
            scripted_.clear(); dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pickParent_ = false;
            buildEditorPanels(); input_.setUi(&editorScene_.ui);
        }
    }
    void doUndo() {
        if (!editor_ || undoStack_.empty()) { lastMsg_ = "nothing to undo"; return; }
        std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";
        editor_->save(project_.rootPath + "/" + cur);
        redoStack_.push_back(cur);
        std::string rel = undoStack_.back(); undoStack_.pop_back();
        loadSnap(rel); lastMsg_ = "undo";
    }
    void doRedo() {
        if (!editor_ || redoStack_.empty()) { lastMsg_ = "nothing to redo"; return; }
        std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";
        editor_->save(project_.rootPath + "/" + cur);
        undoStack_.push_back(cur);
        std::string rel = redoStack_.back(); redoStack_.pop_back();
        loadSnap(rel); lastMsg_ = "redo";
    }

    void loadScript(const std::string& rel) {
        scriptPath_ = rel;
        std::string s = readFile(project_.rootPath + "/" + rel);
        scriptLines_.clear();
        std::string cur;
        for (char c : s) { if (c == '\n') { scriptLines_.push_back(cur); cur.clear(); } else cur += c; }
        scriptLines_.push_back(cur);
        if (scriptLines_.empty()) scriptLines_.push_back("");
        curLine_ = 0; curCol_ = 0; scriptScroll_ = 0; compAnchor_ = -1;
    }
    void saveScript() {
        if (scriptPath_.empty()) return;
        std::ofstream f(project_.rootPath + "/" + scriptPath_);
        for (size_t i = 0; i < scriptLines_.size(); ++i) { f << scriptLines_[i]; if (i + 1 < scriptLines_.size()) f << "\n"; }
        f.close();
        scripts_.load(project_.rootPath);
        lastMsg_ = "script saved: " + scriptPath_;
    }

    // SAVE-OPTION: чтение флага save_vars из project.json
    static bool projectWantsSave(const std::string& root) {
        std::string s = readFile(root + "/project.json");
        size_t p = s.find("\"save_vars\"");
        if (p == std::string::npos) return false;

        p = s.find(':', p + 11);
        if (p == std::string::npos) return false;
        ++p;

        while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
        if (p >= s.size()) return false;

        char c = s[p];
        if (c == 't' || c == 'T' || c == '1') return true;
        if (c == 'f' || c == 'F' || c == '0') return false;

        if (c == '"') {
            size_t e = s.find('"', p + 1);
            if (e == std::string::npos) return false;
            std::string v = s.substr(p + 1, e - p - 1);
            return v == "1" || v == "true" || v == "on" || v == "yes";
        }

        return false;
    }

    // SAVE-OPTION: запись/переключение save_vars в project.json
    static bool setProjectSaveFlag(const std::string& root, bool on) {
        std::string path = root + "/project.json";
        std::string s = readFile(path);
        if (s.empty()) return false;

        std::string val = on ? "true" : "false";
        size_t key = s.find("\"save_vars\"");

        if (key != std::string::npos) {
            size_t colon = s.find(':', key + 11);
            if (colon == std::string::npos) return false;

            size_t st = colon + 1;
            while (st < s.size() && std::isspace((unsigned char)s[st])) ++st;

            size_t en = st;
            if (en < s.size() && s[en] == '"') {
                en = s.find('"', en + 1);
                if (en == std::string::npos) return false;
                ++en;
            } else {
                while (en < s.size() &&
                       (std::isalnum((unsigned char)s[en]) || s[en] == '_' || s[en] == '.' || s[en] == '+' || s[en] == '-')) {
                    ++en;
                }
            }

            if (st == en) return false;
            s.replace(st, en - st, val);
        } else {
            size_t last = s.rfind('}');
            if (last == std::string::npos) return false;

            size_t ins = last;
            while (ins > 0 && std::isspace((unsigned char)s[ins - 1])) --ins;

            if (ins > 0 && s[ins - 1] != '{') s.insert(ins, ",\n  ");
            else s.insert(ins, "\n  ");

            s.insert(ins, "\"save_vars\": " + val);
        }

        std::ofstream f(path);
        if (!f.good()) return false;
        f << s;
        f.close();
        return true;
    }

    // SAVE-B4: переменные игры между запусками
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
        std::string s = readFile(path);
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t nl = s.find('\n', pos);
            std::string line;
            if (nl == std::string::npos) { line = s.substr(pos); pos = s.size() + 1; }
            else { line = s.substr(pos, nl - pos); pos = nl + 1; }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            double v = atof(line.substr(eq + 1).c_str());
            if (k == "__score__") ctx_.score = (int)v;
            else if (!k.empty()) ctx_.vars[k] = v;
        }
    }

    void scTypeChar(char c) {
        if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");
        std::string& L = scriptLines_[curLine_];
        if (c == '\n') {
            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
            std::string tail = L.substr(curCol_);
            L = L.substr(0, curCol_);
            scriptLines_.insert(scriptLines_.begin() + curLine_ + 1, tail);
            curLine_++; curCol_ = 0;
        } else {
            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
            L.insert(L.begin() + curCol_, c);
            curCol_++;
        }
        scClampView();
    }
    void scCompose(const std::string& text) {
        if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");
        std::string& L = scriptLines_[curLine_];
        if (compAnchor_ < 0 || compAnchor_ > (int)L.size()) compAnchor_ = curCol_;
        if (curCol_ > compAnchor_) { L.erase(L.begin() + compAnchor_, L.begin() + curCol_); curCol_ = compAnchor_; }
        for (char c : text) {
            if (c == '\n') c = ' ';
            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
            L.insert(L.begin() + curCol_, c);
            curCol_++;
        }
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
        if (curCol_ > 0) {
            int p = utf8Prev(L, curCol_);
            L.erase(L.begin() + p, L.begin() + curCol_);
            curCol_ = p;
        }
        else if (curLine_ > 0) {
            size_t prevLen = scriptLines_[curLine_ - 1].size();
            scriptLines_[curLine_ - 1] += L;
            scriptLines_.erase(scriptLines_.begin() + curLine_);
            curLine_--; curCol_ = (int)prevLen;
        }
        scClampView();
    }
    void scMove(int d) {
        compAnchor_ = -1;
        if (curLine_ >= (int)scriptLines_.size()) curLine_ = (int)scriptLines_.size() - 1;
        std::string& L = scriptLines_[curLine_];
        if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
        if (d < 0) {
            if (curCol_ > 0) curCol_ = utf8Prev(L, curCol_);
            else if (curLine_ > 0) { curLine_--; curCol_ = (int)scriptLines_[curLine_].size(); }
        } else {
            if (curCol_ < (int)L.size()) curCol_ = utf8Next(L, curCol_);
            else if (curLine_ + 1 < (int)scriptLines_.size()) { curLine_++; curCol_ = 0; }
        }
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

    void rebuildHub() { hubState_.games = ProjectList::scan(); hubScene_ = buildHubLandscape(); input_.setUi(&hubScene_.ui); }
    Scene buildHubLandscape() {
        Theme& th = currentTheme();
        Scene s; s.name = "Hub"; s.root = std::make_unique<Node>(); s.root->name = "Hub";
        auto hdr = std::make_unique<Label>(); hdr->name = "ProjHdr"; hdr->text = "PROJECTS"; hdr->fontSize = 22; hdr->color = th.ink; hdr->position = Vec2{14, 34}; s.root->addChild(std::move(hdr));
        for (size_t i = 0; i < hubState_.games.size(); ++i) {
            const auto& g = hubState_.games[i]; UiButton row; row.touch.id = "sel_" + g.dir;
            row.touch.rect = Rect{10, 70 + (float)i * 56, 240, 48};
            row.text = (hubState_.selectedDir == g.dir ? "* " : "  ") + g.dir; row.action = "sel:" + g.dir;
            row.color = (hubState_.selectedDir == g.dir) ? th.accent : th.button; s.ui.push_back(row);
        }
        if (!hubState_.selectedDir.empty()) {
            ProjectInfo info; ProjectLoader::load(PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/project.json", info);
            auto nm = std::make_unique<Label>(); nm->name = "SelName"; nm->text = info.name; nm->fontSize = 34; nm->color = th.ink; nm->position = Vec2{740, 120}; s.root->addChild(std::move(nm));
            auto sc = std::make_unique<Label>(); sc->name = "SelScene"; sc->text = "scene: " + info.mainScene; sc->fontSize = 20; sc->color = th.ink; sc->position = Vec2{740, 170}; s.root->addChild(std::move(sc));
            UiButton play; play.touch.id = "play"; play.touch.rect = Rect{740, 280, 150, 60}; play.text = "Play"; play.action = "play:" + hubState_.selectedDir; play.color = th.accent; s.ui.push_back(play);
            UiButton edit; edit.touch.id = "edit"; edit.touch.rect = Rect{910, 280, 150, 60}; edit.text = "Edit"; edit.action = "edit:" + hubState_.selectedDir; edit.color = th.button; s.ui.push_back(edit);

            std::string root = PROJECT_ROOT + "/projects/" + hubState_.selectedDir;
            bool svOn = projectWantsSave(root);
            UiButton sv; sv.touch.id = "toggle_save"; sv.touch.rect = Rect{740, 360, 150, 60};
            sv.text = svOn ? "SAVE: ON" : "SAVE: OFF";
            sv.action = "save_toggle";
            sv.color = svOn ? parseColor("#2E7D32") : th.button;
            s.ui.push_back(sv);
        } else {
            auto hint = std::make_unique<Label>(); hint->name = "Hint"; hint->text = "(select a project)"; hint->fontSize = 24; hint->color = th.ink; hint->position = Vec2{740, 300}; s.root->addChild(std::move(hint));
        }
        UiButton nb; nb.touch.id = "new_project"; nb.touch.rect = Rect{740, 560, 150, 60}; nb.text = "+ NEW"; nb.action = "new"; nb.color = th.button; s.ui.push_back(nb);
        UiButton tb; tb.touch.id = "theme"; tb.touch.rect = Rect{910, 560, 150, 60}; tb.text = "Theme"; tb.action = "theme"; tb.color = th.accent; s.ui.push_back(tb);
        return s;
    }
    struct HubAct { int kind = 0; std::string dir; };
    HubAct processHubLandscape() {
        HubAct a;
        for (auto& b : hubScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action == "new") a.kind = 3;
            else if (b.action == "theme") a.kind = 5;
            else if (b.action == "save_toggle") a.kind = 6;
            else if (b.action.rfind("play:", 0) == 0) { a.kind = 1; a.dir = b.action.substr(5); }
            else if (b.action.rfind("edit:", 0) == 0) { a.kind = 2; a.dir = b.action.substr(5); }
            else if (b.action.rfind("sel:", 0) == 0) { a.kind = 4; a.dir = b.action.substr(4); }
        }
        return a;
    }
    std::string stepHub() {
        gameBackend_.begin(); Renderer r(gameBackend_); r.render(hubScene_, nullptr); std::string out = gameBackend_.str();
        HubAct a = processHubLandscape();
        if (a.kind == 3) { std::string dir = nextGameDir(hubState_.games); ProjectCreator::createProject(dir, dir + " Game"); hubState_.selectedDir = dir; rebuildHub(); }
        else if (a.kind == 4) { hubState_.selectedDir = a.dir; rebuildHub(); }
        else if (a.kind == 5) { cycleTheme(); saveSettings(); rebuildHub(); }
        else if (a.kind == 6) {
            std::string root = PROJECT_ROOT + "/projects/" + hubState_.selectedDir;
            bool cur = projectWantsSave(root);
            setProjectSaveFlag(root, !cur);
            rebuildHub();
        }
        else if (a.kind == 1) enterGame(a.dir);
        else if (a.kind == 2) enterEditor(a.dir);
        input_.endFrame(); return out;
    }

    bool enterGame(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) return false;
        project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";
        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) return false;
        scripts_.load(pi.rootPath);
        ctx_ = Context();
        g_luaLog.clear();
        dbg_ = false;
        saveVarsEnabled_ = projectWantsSave(pi.rootPath);
        pendingLoadVars_ = saveVarsEnabled_;
        saveTimer_ = 0.0f;
        Scene* sc = sceneMgr_->current();
        UiButton close; close.touch.id = "close"; close.touch.rect = Rect{1180, 10, 90, 70}; close.text = "X"; close.action = "hub:"; close.color = parseColor("#D62828"); sc->ui.push_back(close);
        UiButton dbg; dbg.touch.id = "dbg"; dbg.touch.rect = Rect{1080, 10, 90, 70}; dbg.text = "DBG"; dbg.action = "dbg:"; dbg.color = parseColor("#808080"); sc->ui.push_back(dbg);
        input_.setUi(&sc->ui); touch_.resetJoystick(); appMode_ = AppMode::Game; return true;
    }
    std::string stepGame() {
        if (!sceneMgr_ || !sceneMgr_->current()) return "";
        ctx_.coinCollectedThisFrame = false; ctx_.jumpPressedThisFrame = false; ctx_.input = input_.state();
        if (!pendingNodeAction_.empty()) { runAction(pendingNodeAction_); pendingNodeAction_.clear(); }
        processUi();
        if (appMode_ != AppMode::Game) return "";
        sceneMgr_->update(ctx_, 1.0 / 60.0, input_, resources_);
        scripts_.update(*sceneMgr_->current(), ctx_, 1.0 / 60.0, *sceneMgr_, ctx_.vars);

        if (pendingLoadVars_) {
            if (saveVarsEnabled_) loadVars();
            pendingLoadVars_ = false;
        }

        if (saveVarsEnabled_) {
            saveTimer_ += 1.0f / 60.0f;
            if (saveTimer_ >= 1.0f) {
                saveVars();
                saveTimer_ = 0.0f;
            }
        }

        std::string out;
        if (ctx_.coinCollectedThisFrame) out += "SOUND coin\n";
        if (ctx_.jumpPressedThisFrame)   out += "SOUND jump\n";
        gameBackend_.begin(); Renderer renderer(gameBackend_); renderer.render(*sceneMgr_->current(), &ctx_); out += gameBackend_.str();
        if (dbg_) {
            nodeCount_ = countNodes(sceneMgr_->current()->root.get());
            lastDraws_ = 0; for (size_t i = 0; i + 4 < out.size(); ++i) if (out[i]=='D' && out[i+1]=='R' && out[i+2]=='A' && out[i+3]=='W') ++lastDraws_;
            out += "DRAW text|fps " + std::to_string((int)fps_) + "  nodes " + std::to_string(nodeCount_) + "  draws " + std::to_string(lastDraws_) + "|20|100|18|#FFD700|0\n";
            out += "DRAW text|vars " + std::to_string((int)ctx_.vars.size()) + "  score " + std::to_string(ctx_.score) + "|20|124|18|#FFD700|0\n";
            size_t ln = g_luaLog.size(); int show = ln > 4 ? 4 : (int)ln;
            for (int i = 0; i < show; ++i)
                out += "DRAW text|" + g_luaLog[ln - show + i] + "|20|" + std::to_string(148 + i*20) + "|16|#87CEEB|0\n";
        }
        input_.endFrame(); return out;
    }
    void runAction(const std::string& act) {
        Scene* sc = sceneMgr_->current(); if (!sc) return;
        if (act == "dbg:") { dbg_ = !dbg_; return; }
        const std::string pRestart = "restart_scene:", pChange = "change_scene:", pAdd = "add_var:", pSet = "set_var:", pHub = "hub:", pCall = "call:";
        if (act.rfind(pHub, 0) == 0) {
            if (saveVarsEnabled_) saveVars();
            appMode_ = AppMode::Hub;
            rebuildHub();
        }
        else if (act.rfind(pRestart, 0) == 0) sceneMgr_->requestChange(act.substr(pRestart.size()), true);
        else if (act.rfind(pChange, 0) == 0)  sceneMgr_->requestChange(act.substr(pChange.size()), false);
        else if (act.rfind(pCall, 0) == 0) scripts_.callGlobal(act.substr(pCall.size()), ctx_, *sceneMgr_, ctx_.vars);
        else if (act.rfind(pAdd, 0) == 0 || act.rfind(pSet, 0) == 0) {
            bool isAdd = act.rfind(pAdd, 0) == 0;
            std::string rest = act.substr(isAdd ? pAdd.size() : pSet.size());
            size_t c = rest.find(':');
            if (c != std::string::npos) { std::string name = rest.substr(0, c); double v = atof(rest.substr(c + 1).c_str()); if (isAdd) ctx_.vars[name] += v; else ctx_.vars[name] = v; }
        }
    }
    void processUi() {
        Scene* sc = sceneMgr_->current(); if (!sc) return;
        for (auto& b : sc->ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            runAction(b.action);
            if (appMode_ != AppMode::Game) return;
        }
    }

    std::string hitTest(const Node* n, float wx, float wy) {
        if (!n) return "";
        std::string bestBox, bestNear; float bestDist = 1e9f;
        collectHit(n, wx, wy, 0, 0, 1, 1, bestBox, bestNear, bestDist);
        return bestBox.empty() ? bestNear : bestBox;
    }
    void collectHit(const Node* n, float wx, float wy, float ox, float oy, float psx, float psy,
                    std::string& bestBox, std::string& bestNear, float& bestDist) const {
        if (!n) return;
        std::string tn = std::string(n->typeName());
        if (tn != "Node" && tn != "Camera2D" && n->name.rfind("__", 0) != 0) {
            const Node2D* d = static_cast<const Node2D*>(n);
            float cxw = ox + d->position.x * psx, cyw = oy + d->position.y * psy;
            float hw = (d->w * d->scale.x * psx) / 2; if (hw < 28) hw = 28;
            float hh = (d->h * d->scale.y * psy) / 2; if (hh < 28) hh = 28;
            if (wx >= cxw - hw && wx <= cxw + hw && wy >= cyw - hh && wy <= cyw + hh) { if (bestBox.empty()) bestBox = d->name; }
            float dx = wx - cxw, dy = wy - cyw;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist < 45.0f && dist < bestDist) { bestDist = dist; bestNear = d->name; }
            for (const auto& ch : n->getChildren()) collectHit(ch.get(), wx, wy, cxw, cyw, psx * d->scale.x, psy * d->scale.y, bestBox, bestNear, bestDist);
            return;
        }
        for (const auto& ch : n->getChildren()) collectHit(ch.get(), wx, wy, ox, oy, psx, psy, bestBox, bestNear, bestDist);
    }

    void attachScript(const std::string& name) {
        std::string rel = "scripts/" + name + ".lua";
        ProjectCreator::createScript(project_.rootPath, rel, name);
        scripts_.load(project_.rootPath);
        scripted_.insert(name);
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";
        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene("scenes/main.json", resources_))
            if (!sceneMgr_->restartScene(pi.mainScene, resources_)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        editor_ = std::make_unique<Editor>(); editor_->attach(sceneMgr_->current());
        showCreate_ = false; pendingText_ = false; pendingName_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
        fsPath_ = ""; manip_ = Manip::Move; pinching_ = false; hierScroll_ = 0; fsScroll_ = 0; pickParent_ = false; lastMsg_.clear();
        edZoom_ = 1.0f; undoStack_.clear(); redoStack_.clear(); clipboard_.reset();
        scriptMode_ = false; scriptPath_.clear(); scriptLines_.clear(); compAnchor_ = -1; imeShown_ = false;
        pendingLoadVars_ = false; saveVarsEnabled_ = false; saveTimer_ = 0.0f;
        buildEditorPanels(); input_.setUi(&editorScene_.ui); touch_.resetJoystick(); appMode_ = AppMode::Editor; return true;
    }

    static std::string dash(int depth) { return depth > 0 ? std::string(depth, '-') + " " : ""; }
    bool hasUiGroup(Scene* esc, const std::string& name) {
        if (!esc) return false;
        for (auto& ub : esc->ui) if (ub.group == name) return true;
        return false;
    }

    struct EdRow { std::string text, action; bool sel; bool hasKids; bool open; std::string name; };

    void buildTreeRows(Scene* esc, Node* n, int depth, const std::string& sel, const std::string& selUi, std::vector<EdRow>& rows) {
        for (auto& ch : n->getChildren()) {
            std::string nm = ch->name;
            bool kids = ch->childCount() > 0 || hasUiGroup(esc, nm);
            bool open = collapsed_.count(nm) == 0;
            Node2D* ch2d = dynamic_cast<Node2D*>(ch.get());
            std::string lockMark = (ch2d && ch2d->locked) ? " [L]" : "";
            EdRow r;
            r.text = dash(depth) + (kids ? (open ? "[-] " : "[+] ") : "    ") + nm + lockMark + "   " + std::string(ch->typeName());
            r.action = "ed_select:" + nm;
            r.sel = (sel == nm); r.hasKids = kids; r.open = open; r.name = nm;
            rows.push_back(r);
            if (kids && open) {
                buildTreeRows(esc, ch.get(), depth + 1, sel, selUi, rows);
                for (auto& ub : esc->ui) {
                    if (ub.group == nm) {
                        EdRow br; br.text = dash(depth + 1) + "    " + ub.touch.id + "   Button";
                        br.action = "ed_selectui:" + ub.touch.id; br.sel = (selUi == ub.touch.id);
                        br.hasKids = false; br.open = false; br.name = "";
                        rows.push_back(br);
                    }
                }
            }
        }
    }

    void buildEditorPanels() {
        Theme& th = currentTheme(); const unsigned GODOT_ORANGE = 0xFF8800FFu;
        editorScene_ = Scene(); editorScene_.name = "Editor"; editorScene_.root = std::make_unique<Node>(); editorScene_.root->name = "EdRoot";
        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
            auto l = std::make_unique<Label>(); l->name = nm; l->text = txt; l->fontSize = fs; l->color = col; l->position = Vec2{x, y}; editorScene_.root->addChild(std::move(l));
        };
        { UiButton b; b.touch.id="tab_scene"; b.touch.rect=Rect{10,4,80,26}; b.text="Scene"; b.action="tab_scene"; b.color=scriptMode_?th.button:GODOT_ORANGE; editorScene_.ui.push_back(b); }
        addLbl("Tab2D", "2D", 110, 8, 20, th.ink); addLbl("Tab3D", "3D", 160, 8, 20, th.ink);
        { UiButton b; b.touch.id="tab_scripts"; b.touch.rect=Rect{200,4,90,26}; b.text="Scripts"; b.action="tab_scripts"; b.color=scriptMode_?GODOT_ORANGE:th.button; editorScene_.ui.push_back(b); }
        addLbl("TabAss", "AssetLib", 300, 8, 20, th.ink);

        if (scriptMode_) { buildScriptPanels(th); return; }

        auto fsBg = std::make_unique<Node2D>(); fsBg->name = "FsBg"; fsBg->shape = "square"; fsBg->color = dimColor(th.bg, 0.6f); fsBg->w = 284; fsBg->h = 320; fsBg->position = Vec2{150, 536}; editorScene_.root->addChild(std::move(fsBg));
        addLbl("DHdr", "Scene", 10, 40, 18, th.ink); addLbl("IHdr", "Inspector", 900, 40, 18, th.ink);
        addLbl("ZoomLbl", "zoom " + std::to_string((int)(edZoom_ * 100)) + "%", 380, 8, 16, th.ink);

        const char* mlab[3] = { "POS","ROT","SCL" };
        Manip mval[3] = { Manip::Move, Manip::Rotate, Manip::Scale };
        for (int k = 0; k < 3; ++k) {
            UiButton b; b.touch.id = std::string("manipbtn")+std::to_string(k);
            b.touch.rect = Rect{1090 + (float)k * 60, 34, 56, 28};
            b.text = mlab[k]; b.action = std::string("manip:") + (k==0?"move":k==1?"rotate":"scale");
            b.color = (manip_ == mval[k]) ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        Scene* esc = editor_ ? editor_->scene() : nullptr;
        Node* selNode = editor_ ? editor_->selected() : nullptr; std::string sel = selNode ? selNode->name : std::string{};
        std::string selUi = editor_ ? editor_->selectedUi() : std::string{};

        std::vector<EdRow> rows;
        if (esc && esc->root) buildTreeRows(esc, esc->root.get(), 0, sel, selUi, rows);
        if (esc) for (auto& ub : esc->ui) {
            if (!ub.group.empty()) continue;
            EdRow r; r.text = "    " + ub.touch.id + "   Button";
            r.action = "ed_selectui:" + ub.touch.id; r.sel = (selUi == ub.touch.id);
            r.hasKids = false; r.open = false; r.name = "";
            rows.push_back(r);
        }

        const int VIS = 10;
        int maxScroll = (int)rows.size() > VIS ? (int)rows.size() - VIS : 0;
        if (hierScroll_ < 0) hierScroll_ = 0;
        if (hierScroll_ > maxScroll) hierScroll_ = maxScroll;
        { UiButton b; b.touch.id="hup"; b.touch.rect=Rect{248,38,20,22}; b.text="^"; b.action="hier_up"; b.color=th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="hdn"; b.touch.rect=Rect{270,38,20,22}; b.text="v"; b.action="hier_dn"; b.color=th.button; editorScene_.ui.push_back(b); }
        for (int i = hierScroll_; i < (int)rows.size() && i < hierScroll_ + VIS; ++i) {
            float y = 64 + (float)(i - hierScroll_) * 30;
            if (rows[i].hasKids) {
                UiButton f; f.touch.id = "fold" + std::to_string(i);
                f.touch.rect = Rect{8, y, 26, 28};
                f.text = rows[i].open ? "-" : "+";
                f.action = "fold:" + rows[i].name; f.color = th.accent;
                editorScene_.ui.push_back(f);
            }
            UiButton b; b.touch.id = "h" + std::to_string(i);
            float rx = rows[i].hasKids ? 36.0f : 8.0f;
            float rw = rows[i].hasKids ? 256.0f : 284.0f;
            b.touch.rect = Rect{rx, y, rw, 28};
            b.text = rows[i].text; b.action = rows[i].action;
            b.color = rows[i].sel ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        if (!selUi.empty()) {
            UiButton* ub = editor_->findUi(selUi);
            if (ub) {
                addLbl("InName", ub->touch.id, 900, 64, 22, GODOT_ORANGE); addLbl("InType", "Button", 900, 92, 16, th.ink);
                addLbl("InPos", "Pos  (" + std::to_string((int)ub->touch.rect.x) + ", " + std::to_string((int)ub->touch.rect.y) + ")", 900, 124, 16, th.ink);
                addLbl("InSiz", "Size (" + std::to_string((int)ub->touch.rect.w) + ", " + std::to_string((int)ub->touch.rect.h) + ")", 900, 148, 16, th.ink);
                addLbl("InCol", "Color " + colorToHex(ub->color) + " (" + rgbStr(ub->color) + ") A" + std::to_string((int)(ub->alpha * 100)) + "%", 900, 172, 16, th.ink);
                addLbl("InText", "Text: " + ub->text, 900, 196, 16, th.ink);
                addLbl("InAct", "Action: " + (ub->action.empty() ? std::string("(none)") : ub->action), 900, 220, 16, th.ink);
                addLbl("InAng", "Angle " + std::to_string((int)ub->angle), 900, 244, 16, th.ink);
                addLbl("InTex", "Texture: " + (ub->texture.empty() ? std::string("(none)") : ub->texture), 900, 268, 16, th.ink);
                addLbl("InGrp", "Group: " + (ub->group.empty() ? std::string("(none)") : ub->group), 900, 292, 16, th.ink);
                const char* nl[6] = { "X","Y","W","H","R","A" };
                const char* nk[6] = { "bx","by","bw","bh","bang","balpha" };
                for (int k = 0; k < 6; ++k) {
                    UiButton b; b.touch.id = std::string("numbtn")+std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 46, 320, 44, 28};
                    b.text = nl[k]; b.action = std::string("num:") + nk[k];
                    b.color = th.button; editorScene_.ui.push_back(b);
                }
            }
        } else {
            Node2D* s = (editor_ && !sel.empty()) ? editor_->find2d(sel) : nullptr;
            if (s) {
                addLbl("InName", s->name, 900, 64, 22, GODOT_ORANGE); addLbl("InType", std::string(s->typeName()), 900, 92, 16, th.ink);
                addLbl("InPos", "Position  (" + std::to_string((int)s->position.x) + ", " + std::to_string((int)s->position.y) + ")", 900, 124, 16, th.ink);
                addLbl("InRot", "Rotation  " + std::to_string((int)(s->rotation * 57.2957795f)), 900, 148, 16, th.ink);
                addLbl("InScl", "Scale  (" + std::to_string((int)(s->scale.x*100)) + "%, " + std::to_string((int)(s->scale.y*100)) + "%)", 900, 172, 16, th.ink);
                addLbl("InSiz", "Size  (" + std::to_string((int)s->w) + ", " + std::to_string((int)s->h) + ")", 900, 196, 16, th.ink);
                addLbl("InLck", "Locked  " + std::string(s->locked ? "YES" : "no"), 900, 220, 16, s->locked ? parseColor("#D62828") : th.ink);
                addLbl("InAlp", "Alpha  " + std::to_string((int)(s->alpha * 100)) + "%", 900, 244, 16, th.ink);
                addLbl("InShp", "Shape  " + s->shape, 900, 268, 16, th.ink);
                addLbl("InCol", "Color  " + colorToHex(s->color) + "  (" + rgbStr(s->color) + ")", 900, 292, 16, th.ink);
                addLbl("InTex", "Texture: " + (s->texture.empty() ? std::string("(none)") : s->texture), 900, 316, 16, th.ink);
                addLbl("InAct", "Touch: " + (s->action.empty() ? std::string("(none)") : s->action), 900, 340, 16, th.ink);
                if (std::string(s->typeName()) == "Label") addLbl("InText", "Text: " + static_cast<Label*>(s)->text, 900, 364, 16, th.ink);
                const char* nl[7] = { "X","Y","ROT","SCL","W","H","A" };
                const char* na[7] = { "nx","ny","nrot","nscl","nw","nh","nalpha" };
                for (int k = 0; k < 7; ++k) {
                    UiButton b; b.touch.id = std::string("numbtn")+std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 46, 392, 44, 28};
                    b.text = nl[k]; b.action = std::string("num:") + na[k];
                    b.color = th.button; editorScene_.ui.push_back(b);
                }
            } else addLbl("InNone", "(no selection)", 900, 92, 18, th.ink);
        }

        {
            size_t ln = g_luaLog.size();
            int show = ln > 5 ? 5 : (int)ln;
            if (show == 0) addLbl("LogNone", "(lua log empty)", 900, 520, 13, th.ink);
            for (int i = 0; i < show; ++i) {
                std::string nm = "LogLn" + std::to_string(i);
                addLbl(nm.c_str(), g_luaLog[ln - show + i], 900, 520 + i*16, 13, parseColor("#87CEEB"));
            }
        }

        std::string sb = (editor_ && editor_->scene() && editor_->scene()->bgSet()) ? editor_->scene()->bg : std::string("(theme)");
        addLbl("InBg", "scene bg: " + sb, 900, 426, 16, th.ink);
        { UiButton b; b.touch.id = "lckbtn"; b.touch.rect = Rect{900, 448, 44, 30}; b.text = "LCK"; b.action = "ed_lock"; b.color = parseColor("#D62828"); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "bgbtn"; b.touch.rect = Rect{948, 448, 44, 30}; b.text = "BG"; b.action = "bg_rgb"; b.color = th.accent; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "actbtn"; b.touch.rect = Rect{996, 448, 44, 30}; b.text = "ACT"; b.action = "edit_action"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "texbtn"; b.touch.rect = Rect{1044, 448, 44, 30}; b.text = "T-"; b.action = "clear_tex"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "clnbtn"; b.touch.rect = Rect{1092, 448, 44, 30}; b.text = "DUP"; b.action = "ed_clone"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "parbtn"; b.touch.rect = Rect{1140, 448, 44, 30}; b.text = pickParent_ ? "PICK" : "PAR"; b.action = "ed_parent"; b.color = pickParent_ ? GODOT_ORANGE : th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "unpbtn"; b.touch.rect = Rect{900, 482, 44, 30}; b.text = "UNP"; b.action = "ed_unparent"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "cpybtn"; b.touch.rect = Rect{948, 482, 44, 30}; b.text = "CPY"; b.action = "ed_copy"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "pstbtn"; b.touch.rect = Rect{996, 482, 44, 30}; b.text = "PST"; b.action = "ed_paste"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "undbtn"; b.touch.rect = Rect{1044, 482, 44, 30}; b.text = "UND"; b.action = "ed_undo"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "redbtn"; b.touch.rect = Rect{1092, 482, 44, 30}; b.text = "RED"; b.action = "ed_redo"; b.color = th.button; editorScene_.ui.push_back(b); }

        const char* mv[4] = { "l","u","d","r" }; const char* mvTxt[4] = { "<","^","v",">" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("mv")+std::to_string(k); b.touch.rect = Rect{900+(float)k*58, 616, 54, 48}; b.text = mvTxt[k]; b.action = std::string("ed_move:")+mv[k]; b.color = th.button; editorScene_.ui.push_back(b); }

        float tx = 300;
        const char* shapes[4] = { "square","circle","diamond","triangle" }; const char* shTxt[4] = { "SQ","CI","DI","TR" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("sh")+std::to_string(k); b.touch.rect = Rect{tx,34,44,26}; tx+=46; b.text = shTxt[k]; b.action = std::string("ed_shape:")+shapes[k]; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="colbtn"; b.touch.rect=Rect{tx,34,54,26}; tx+=56; b.text="RGB"; b.action="col_rgb"; b.color=th.accent; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="del"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="DEL"; b.action="ed_del"; b.color=parseColor("#D62828"); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="save"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="SAVE"; b.action="ed_save"; b.color=parseColor("#2E7D32"); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="eback"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="<"; b.action="ed_back"; b.color=GODOT_ORANGE; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="plus"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="+"; b.action="create_open"; b.color=GODOT_ORANGE; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="txt"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="TXT"; b.action="edit_text"; b.color=th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="scr"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="SCR"; b.action="ed_scr"; b.color=th.button; editorScene_.ui.push_back(b); }

        if (showCreate_) {
            const char* ct[10] = { "Node2D","Node2D","Node2D","Node2D","Label","Sprite2D","","","","" };
            const char* cs[10] = { "square","circle","diamond","triangle","","","","","","" };
            const char* cl[10] = { "CUBE","CIRCLE","DIAMOND","TRIANGLE","TEXT","SPRITE","CAM","LIGHT","GRP","BTN" };
            for (int k = 0; k < 6; ++k) { UiButton b; b.touch.id = std::string("ct")+std::to_string(k); b.touch.rect = Rect{300+(float)k*58, 560, 54, 40}; b.text = cl[k]; b.action = std::string("create:")+ct[k]+":"+cs[k]; b.color = th.button; editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctcam"; b.touch.rect=Rect{300+6*58,560,54,40}; b.text="CAM"; b.action="create_cam"; b.color=th.accent; editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctlit"; b.touch.rect=Rect{300+7*58,560,54,40}; b.text="LIGHT"; b.action="create_light"; b.color=parseColor("#FFD700"); editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctgrp"; b.touch.rect=Rect{300+8*58,560,54,40}; b.text="GRP"; b.action="create_grp"; b.color=parseColor("#808080"); editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctbtn"; b.touch.rect=Rect{300+9*58,560,54,40}; b.text="BTN"; b.action="create_btn"; b.color=parseColor("#2EC4B6"); editorScene_.ui.push_back(b); }
        }

        addLbl("FsHdr", "FILES", 10, 384, 18, th.ink);
        { UiButton b; b.touch.id="fsu"; b.touch.rect=Rect{248,382,20,22}; b.text="^"; b.action="fscroll_up"; b.color=th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="fsd"; b.touch.rect=Rect{270,382,20,22}; b.text="v"; b.action="fscroll_dn"; b.color=th.button; editorScene_.ui.push_back(b); }
        struct FsRow { std::string text, action; unsigned col; };
        std::vector<FsRow> frows;
        if (!fsPath_.empty()) frows.push_back({ "..", "fs_up", th.button });
        std::string abs = project_.rootPath + "/" + fsPath_;
        for (const auto& it : FileBrowser::list(abs)) {
            if (it.name.rfind("snap_", 0) == 0) continue;
            if (it.name == "save.vars") continue;
            FsRow r;
            r.text = (it.isDir ? "/ " : "  ") + it.name;
            r.action = it.isDir ? ("fs_enter:" + it.name) : ("fs_pick:" + fsPath_ + it.name);
            r.col = it.isDir ? th.button : th.accent;
            frows.push_back(r);
        }
        const int FMAXROWS = 8;
        int fmax = (int)frows.size() > FMAXROWS ? (int)frows.size() - FMAXROWS : 0;
        if (fsScroll_ < 0) fsScroll_ = 0;
        if (fsScroll_ > fmax) fsScroll_ = fmax;
        std::string shown = fsPath_.empty() ? std::string("res/") : ("res/" + fsPath_);
        addLbl("FsPath", shown + "  (" + std::to_string((int)frows.size()) + ")", 10, 406, 15, GODOT_ORANGE);
        float fy = 428; const float STEP = 28;
        for (int i = fsScroll_; i < (int)frows.size() && i < fsScroll_ + FMAXROWS; ++i) {
            UiButton fb; fb.touch.id = "fs" + std::to_string(i); fb.touch.rect = Rect{8, fy, 284, STEP-2};
            fb.text = frows[i].text; fb.action = frows[i].action; fb.color = frows[i].col;
            editorScene_.ui.push_back(fb);
            fy += STEP;
        }
    }

    void buildScriptPanels(Theme& th) {
        const unsigned GODOT_ORANGE = 0xFF8800FFu;
        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
            auto l = std::make_unique<Label>(); l->name = nm; l->text = txt; l->fontSize = fs; l->color = col; l->position = Vec2{x, y}; editorScene_.root->addChild(std::move(l));
        };
        const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
        addLbl("ScHdr", "SCRIPTS", 10, 40, 18, th.ink);
        std::vector<FileEntry> ls = FileBrowser::list(project_.rootPath + "/scripts");
        int li = 0;
        for (auto& it : ls) {
            if (it.isDir) continue;
            if (it.name.size() < 4 || it.name.compare(it.name.size()-4, 4, ".lua") != 0) continue;
            if (li >= 11) break;
            UiButton b; b.touch.id = "scf" + std::to_string(li);
            b.touch.rect = Rect{8, 64 + (float)li * 28, 284, 26};
            b.text = "  " + it.name; b.action = "scrfile:" + it.name;
            b.color = (scriptPath_ == "scripts/" + it.name) ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b); ++li;
        }
        { UiButton b; b.touch.id="scnew"; b.touch.rect=Rect{8, 64 + (float)li * 28, 284, 26}; b.text="+ NEW SCRIPT"; b.action="snew"; b.color=th.accent; editorScene_.ui.push_back(b); }

        auto edbg = std::make_unique<Node2D>(); edbg->name = "__scbg"; edbg->shape = "square";
        edbg->color = 0x101018FF; edbg->w = VW; edbg->h = VH;
        edbg->position = Vec2{VX0 + VW/2, VY0 + VH/2};
        editorScene_.root->addChild(std::move(edbg));

        { UiButton b; b.touch.id="ssave"; b.touch.rect=Rect{300,34,70,26}; b.text="SAVE"; b.action="ssave"; b.color=parseColor("#2E7D32"); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="sclose"; b.touch.rect=Rect{374,34,70,26}; b.text="SCENE"; b.action="tab_scene"; b.color=GODOT_ORANGE; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="kbtog"; b.touch.rect=Rect{448,34,80,26}; b.text=imeShown_?"KB OFF":"KB ON"; b.action="kb_toggle"; b.color=imeShown_?th.button:th.accent; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="scu"; b.touch.rect=Rect{860,70,26,26}; b.text="^"; b.action="scup"; b.color=th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="scd"; b.touch.rect=Rect{860,100,26,26}; b.text="v"; b.action="scdn"; b.color=th.button; editorScene_.ui.push_back(b); }

        const int LINES = 24; const float LH = 19;
        int smax = (int)scriptLines_.size() > LINES ? (int)scriptLines_.size() - LINES : 0;
        if (scriptScroll_ < 0) scriptScroll_ = 0;
        if (scriptScroll_ > smax) scriptScroll_ = smax;
        for (int i = scriptScroll_; i < (int)scriptLines_.size() && i < scriptScroll_ + LINES; ++i) {
            float y = 70 + (float)(i - scriptScroll_) * LH;
            std::string num = std::to_string(i + 1);
            while (num.size() < 3) num = " " + num;
            std::string nmN = "ScN" + std::to_string(i);
            addLbl(nmN.c_str(), num, 306, y, 14, parseColor("#667089"));
            std::string txt = sanitizeLine(scriptLines_[i]);
            if (txt.size() > 68) txt = txt.substr(0, 68);
            std::string nmC = "ScC" + std::to_string(i);
            addLbl(nmC.c_str(), txt, 340, y, 14, i == curLine_ ? parseColor("#FFD700") : parseColor("#D8E0F0"));
        }
        if (curLine_ >= scriptScroll_ && curLine_ < scriptScroll_ + LINES) {
            int cps = utf8ByteToCp(scriptLines_[curLine_], curCol_);
            auto cur = std::make_unique<Node2D>(); cur->name = "__sccur"; cur->shape = "square";
            cur->color = 0x4CC9F0FF; cur->w = 3; cur->h = 16;
            float cxp = 340 + cps * 8.0f;
            float cyp = 70 + (float)(curLine_ - scriptScroll_) * LH + 8;
            cur->position = Vec2{cxp + 1, cyp};
            editorScene_.root->addChild(std::move(cur));
        }

        addLbl("ScInfo", scriptPath_.empty() ? "(no script)" : scriptPath_, 900, 64, 16, GODOT_ORANGE);
        addLbl("ScInfo2", "lines " + std::to_string((int)scriptLines_.size()) + "   cur " + std::to_string(curLine_+1) + ":" + std::to_string(utf8ByteToCp(scriptLines_.size() ? scriptLines_[curLine_] : std::string(""), curCol_)), 900, 88, 14, th.ink);
        addLbl("ScInfo3", "tap line = cursor + keyboard", 900, 110, 14, th.ink);
        addLbl("ScInfo4", "SAVE writes file + reloads scripts", 900, 132, 14, th.ink);
    }

    void processEditorActions() {
        if (!editor_) return;
        Node* sn = editor_->selected(); std::string sel = sn ? sn->name : std::string{};
        std::string selUi = editor_->selectedUi();
        UiButton* ub = selUi.empty() ? nullptr : editor_->findUi(selUi);
        Node2D* s2 = (!sel.empty()) ? editor_->find2d(sel) : nullptr;
        bool lk = (s2 != nullptr) && s2->locked;
        bool changed = false;
        for (auto& b : editorScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action == "kb_toggle") {
                if (imeShown_) { imeWantOff_ = true; imeShown_ = false; }
                else { imeWantOn_ = true; imeShown_ = true; }
                changed = true;
            }
            else if (b.action == "tab_scripts") {
                if (!scriptMode_) {
                    scriptMode_ = true;
                    if (scriptPath_.empty()) loadScript("scripts/main.lua");
                    imeWantOn_ = true; imeShown_ = true;
                    changed = true;
                }
            }
            else if (b.action == "tab_scene") {
                if (scriptMode_) { scriptMode_ = false; imeWantOff_ = true; imeShown_ = false; changed = true; }
            }
            else if (b.action.rfind("scrfile:", 0) == 0) { loadScript("scripts/" + b.action.substr(8)); imeChanged_ = true; changed = true; }
            else if (b.action == "ssave") { saveScript(); changed = true; }
            else if (b.action == "scup") { scriptScroll_ -= 3; imeChanged_ = true; changed = true; }
            else if (b.action == "scdn") { scriptScroll_ += 3; imeChanged_ = true; changed = true; }
            else if (b.action == "snew") { pendingName_ = true; pendingKind_ = 2; changed = true; }
            else if (scriptMode_) { continue; }
            else if (b.action == "col_rgb" && !lk) {
                pendingRgb_ = 1; pendingText_ = true;
                pendingTextCur_ = ub ? rgbStr(ub->color) : (s2 ? rgbStr(s2->color) : std::string("255,255,255"));
            }
            else if (b.action == "bg_rgb") {
                pendingRgb_ = 2; pendingText_ = true;
                unsigned bc = (editor_->scene() && editor_->scene()->bgSet()) ? parseColor(editor_->scene()->bg) : currentTheme().bg;
                pendingTextCur_ = rgbStr(bc);
            }
            else if (b.action == "fscroll_up") { fsScroll_ -= 3; changed = true; }
            else if (b.action == "fscroll_dn") { fsScroll_ += 3; changed = true; }
            else if (b.action == "ed_lock") {
                if (s2) { pushUndo(); s2->locked = !s2->locked; lastMsg_ = s2->locked ? "locked " + sel : "unlocked " + sel; changed = true; }
            }
            else if (b.action == "ed_undo") { doUndo(); }
            else if (b.action == "ed_redo") { doRedo(); }
            else if (b.action == "ed_copy") { if (s2) { clipboard_ = s2->cloneNode(); lastMsg_ = "copied " + sel; } }
            else if (b.action == "ed_paste") {
                if (clipboard_ && editor_->scene() && editor_->scene()->root) {
                    pushUndo();
                    auto cp = clipboard_->cloneNode();
                    std::string nm = cp->name + "_c" + std::to_string(clipCounter_++);
                    cp->name = nm;
                    Node2D* raw = dynamic_cast<Node2D*>(cp.get());
                    if (raw) raw->position.x += 40;
                    editor_->scene()->root->addChild(std::move(cp));
                    editor_->select(nm);
                    lastMsg_ = "pasted " + nm; changed = true;
                } else lastMsg_ = "clipboard empty";
            }
            else if (b.action.rfind("fold:", 0) == 0) { std::string nm = b.action.substr(5); if (collapsed_.count(nm)) collapsed_.erase(nm); else collapsed_.insert(nm); changed = true; }
            else if (b.action == "hier_up") { hierScroll_ -= 3; changed = true; }
            else if (b.action == "hier_dn") { hierScroll_ += 3; changed = true; }
            else if (b.action == "fs_up") {
                std::string tmp = fsPath_;
                while (!tmp.empty() && tmp.back() == '/') tmp.pop_back();
                size_t sl = tmp.find_last_of('/');
                fsPath_ = (sl == std::string::npos) ? std::string("") : tmp.substr(0, sl + 1);
                fsScroll_ = 0;
                changed = true;
            }
            else if (b.action.rfind("fs_enter:", 0) == 0) { fsPath_ += b.action.substr(9) + "/"; fsScroll_ = 0; changed = true; }
            else if (b.action.rfind("fs_pick:", 0) == 0) {
                std::string rel = b.action.substr(8);
                if (rel.size() > 5 && rel.compare(rel.size()-5, 5, ".json") == 0) {
                    if (sceneMgr_->restartScene(rel, resources_)) {
                        editor_->attach(sceneMgr_->current());
                        scripted_.clear(); showCreate_ = false; dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pinching_ = false;
                        ub = nullptr; sel.clear(); s2 = nullptr; lk = false; hierScroll_ = 0;
                        undoStack_.clear(); redoStack_.clear();
                        changed = true;
                    }
                } else if (!lk) {
                    pushUndo();
                    if (ub) { ub->texture = rel; changed = true; }
                    else if (!sel.empty()) { editor_->setTexture(sel, rel); changed = true; }
                }
            }
            else if (b.action == "clear_tex" && !lk) {
                pushUndo();
                if (ub) { ub->texture.clear(); changed = true; }
                else if (!sel.empty()) { editor_->setTexture(sel, std::string("")); changed = true; }
            }
            else if (b.action == "manip:move")   { manip_ = Manip::Move;   changed = true; }
            else if (b.action == "manip:rotate") { manip_ = Manip::Rotate; changed = true; }
            else if (b.action == "manip:scale")  { manip_ = Manip::Scale;  changed = true; }
            else if (b.action == "create_open") { showCreate_ = !showCreate_; changed = true; }
            else if (b.action == "edit_text" && !lk) {
                pendingRgb_ = 0;
                if (ub) { pendingText_ = true; pendingTextCur_ = ub->text; }
                else if (sn && std::string(sn->typeName()) == "Label") { pendingText_ = true; pendingTextCur_ = static_cast<Label*>(sn)->text; }
            }
            else if (b.action == "edit_action" && !lk) {
                pendingAction_ = true;
                pendingActionCur_ = ub ? ub->action : (s2 ? s2->action : std::string(""));
            }
            else if (b.action.rfind("num:", 0) == 0 && !lk) {
                pendingNum_ = true; pendingNumKind_ = b.action.substr(4);
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
            }
            else if (b.action == "ed_scr" && !lk) { if (!sel.empty()) { attachScript(sel); changed = true; } }
            else if (b.action == "ed_clone" && !lk) { pushUndo(); if (!sel.empty()) { editor_->cloneSelected(sel + "_copy"); changed = true; } }
            else if (b.action == "ed_parent" && !lk) {
                if (!selUi.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? ("UI:" + selUi) : std::string(""); changed = true; }
                else if (!sel.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? sel : std::string(""); changed = true; }
            }
            else if (b.action == "ed_unparent" && !lk) { pushUndo(); if (!selUi.empty()) { detachChild("UI:" + selUi); changed = true; } else if (!sel.empty()) { detachChild(sel); changed = true; } }
            else if (b.action.rfind("ed_selectui:", 0) == 0) {
                std::string id = b.action.substr(12);
                if (pickParent_ && !pickChild_.empty() && pickChild_ != id && pickChild_ != ("UI:" + id)) { pushUndo(); attachChildTo(pickChild_, id); pickParent_ = false; pickChild_.clear(); }
                else editor_->selectUi(id);
                changed = true;
            }
            else if (b.action.rfind("ed_select:", 0) == 0) {
                std::string nm = b.action.substr(10);
                if (pickParent_ && !pickChild_.empty() && nm != pickChild_) { pushUndo(); attachChildTo(pickChild_, nm); pickParent_ = false; pickChild_.clear(); }
                else editor_->select(nm);
                changed = true;
            }
            else if (b.action == "create_cam") { pushUndo(); std::string name = "Cam" + std::to_string(createCounter_++); editor_->addNode("Camera2D", name, 640, 360); editor_->select(name); showCreate_ = false; changed = true; }
            else if (b.action == "create_light") { pushUndo(); std::string name = "Light" + std::to_string(createCounter_++); editor_->addNode("Light2D", name, 640, 360); editor_->select(name); showCreate_ = false; changed = true; }
            else if (b.action == "create_grp") { pendingName_ = true; pendingKind_ = 0; pendingType_ = "Node2D"; pendingShape_ = "none"; showCreate_ = false; changed = true; }
            else if (b.action == "create_btn") { pendingName_ = true; pendingKind_ = 1; showCreate_ = false; changed = true; }
            else if (b.action.rfind("create:", 0) == 0) {
                std::string rest = b.action.substr(7); size_t c = rest.find(':');
                pendingName_ = true; pendingKind_ = 0;
                pendingType_ = rest.substr(0, c); pendingShape_ = (c == std::string::npos) ? "" : rest.substr(c + 1);
                showCreate_ = false; changed = true;
            }
            else if (b.action.rfind("ed_shape:", 0) == 0 && !lk) { pushUndo(); if (!sel.empty()) { editor_->setShape(sel, b.action.substr(9)); changed = true; } }
            else if (b.action.rfind("ed_move:", 0) == 0 && !lk) {
                std::string d = b.action.substr(8);
                pushUndo();
                if (ub) {
                    if (manip_ == Manip::Move) { float dx=(d=="l")?-16:(d=="r")?16:0; float dy=(d=="u")?-16:(d=="d")?16:0; ub->touch.rect.x+=dx; ub->touch.rect.y+=dy; }
                    else if (manip_ == Manip::Rotate) { float dr=(d=="l")?-15.0f:(d=="r")?15.0f:0.0f; ub->angle += dr; }
                    else if (manip_ == Manip::Scale) { float f=(d=="u")?1.1f:(d=="d")?(1.0f/1.1f):1.0f; ub->touch.rect.w*=f; ub->touch.rect.h*=f; }
                    changed = true;
                } else {
                    if (manip_ == Manip::Move) {
                        float dx=(d=="l")?-16:(d=="r")?16:0; float dy=(d=="u")?-16:(d=="d")?16:0;
                        editor_->moveSelected(dx, dy);
                        if (editor_->scene()) moveGroupButtons(editor_->scene(), sel, dx, dy);
                    }
                    else if (manip_ == Manip::Rotate && s2) { float dr=(d=="l")?-15.0f:(d=="r")?15.0f:0.0f; s2->rotation += dr * 3.14159265f / 180.0f; }
                    else if (manip_ == Manip::Scale && s2) { float f=(d=="u")?1.1f:(d=="d")?(1.0f/1.1f):1.0f; s2->scale.x*=f; s2->scale.y*=f; }
                    changed = true;
                }
            }
            else if (b.action == "ed_del" && !lk) {
                pushUndo();
                if (ub) { editor_->deleteUi(selUi); ub = nullptr; changed = true; }
                else if (!sel.empty()) { editor_->deleteNode(sel); sel.clear(); s2 = nullptr; changed = true; }
            }
            else if (b.action == "ed_save") { editor_->save(project_.rootPath + "/scenes/main.json"); lastMsg_ = "saved"; }
            else if (b.action == "ed_back") { if (scriptMode_) { scriptMode_ = false; imeWantOff_ = true; imeShown_ = false; } appMode_ = AppMode::Hub; rebuildHub(); return; }
        }
        if (changed) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    void consumeDialogResults() {
        std::string txt, nm, act, num; bool ht = false, hn = false, ha = false, hnum = false;
        {
            std::lock_guard<std::mutex> lk(dlgMtx_);
            ht = hasText_; hn = hasName_; ha = hasAction_; hnum = hasNum_;
            txt = textRes_; nm = nameRes_; act = actionRes_; num = numRes_;
            hasText_ = hasName_ = hasAction_ = hasNum_ = false;
        }
        if (!editor_) return;
        if (hn) {
            if (pendingKind_ == 2) {
                if (!nm.empty()) {
                    std::string rel = "scripts/" + nm + ".lua";
                    std::ofstream f(project_.rootPath + "/" + rel);
                    f << "-- " << nm << "\nfunction on_start()\nend\n\nfunction on_update(dt)\nend\n";
                    f.close();
                    loadScript(rel);
                    lastMsg_ = "script created: " + rel;
                }
            } else {
                pushUndo();
                if (!nm.empty() && pendingKind_ == 0) {
                    editor_->addNode(pendingType_, nm, 640, 360);
                    if (!pendingShape_.empty()) editor_->setShape(nm, pendingShape_);
                    editor_->select(nm);
                    lastMsg_ = "created " + nm;
                } else if (!nm.empty() && pendingKind_ == 1) {
                    editor_->addUi(nm, nm, 580, 335, 120, 50, std::string(""), parseColor("#808080"));
                    editor_->selectUi(nm);
                    lastMsg_ = "created button " + nm;
                }
            }
            pendingName_ = false; showCreate_ = false;
        }
        if (ht) {
            if (pendingRgb_ == 1) {
                unsigned c = 0;
                if (parseRgb(txt, c)) {
                    pushUndo();
                    std::string uid = editor_->selectedUi();
                    if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) b->color = c; }
                    else { Node* s = editor_->selected(); Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr; if (n2) n2->color = c; }
                } else lastMsg_ = "bad rgb, need r,g,b";
                pendingRgb_ = 0;
            } else if (pendingRgb_ == 2) {
                unsigned c = 0;
                if (parseRgb(txt, c) && editor_->scene()) { pushUndo(); editor_->scene()->bg = colorToHex(c); }
                else lastMsg_ = "bad rgb, need r,g,b";
                pendingRgb_ = 0;
            } else {
                pushUndo();
                std::string uid = editor_->selectedUi();
                if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) b->text = txt; }
                else { Node* s = editor_->selected(); if (s && std::string(s->typeName()) == "Label") static_cast<Label*>(s)->text = txt; }
            }
        }
        if (ha) {
            pushUndo();
            std::string uid = editor_->selectedUi();
            if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) b->action = act; }
            else { Node* s = editor_->selected(); Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr; if (n2) n2->action = act; }
        }
        if (hnum) {
            float v = (float)atof(num.c_str());
            std::string uid = editor_->selectedUi();
            UiButton* b = uid.empty() ? nullptr : editor_->findUi(uid);
            Node* s = editor_->selected();
            Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr;
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
            if (b) {
                if (pendingNumKind_ == "bx") b->touch.rect.x = v;
                else if (pendingNumKind_ == "by") b->touch.rect.y = v;
                else if (pendingNumKind_ == "bw") b->touch.rect.w = v;
                else if (pendingNumKind_ == "bh") b->touch.rect.h = v;
                else if (pendingNumKind_ == "bang") b->angle = v;
                else if (pendingNumKind_ == "balpha") { float a = v / 100.0f; if (a < 0) a = 0; if (a > 1) a = 1; b->alpha = a; }
            }
        }
        if (hn || ht || ha || hnum) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    void emitViewport(const Scene& sc, std::string& out) {
        const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
        const float S = 0.46875f * edZoom_;
        const float CX = VX0 + VW/2, CY = VY0 + VH/2;
        if (sc.bgSet()) out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|" + sc.bg + "|0\n";
        out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|#23232B|0\n";
        for (int gx = 0; gx <= 2560; gx += 64) { float px = CX + ((float)gx - 640 - sc.camX)*S; if (px < VX0 || px > VX0+VW) continue; out += "DRAW rect|" + std::to_string((int)px) + "|" + std::to_string((int)VY0) + "|1|" + std::to_string((int)VH) + "|#33333D|0\n"; }
        for (int gy = 0; gy <= 1440; gy += 64) { float py = CY + ((float)gy - 360 - sc.camY)*S; if (py < VY0 || py > VY0+VH) continue; out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)py) + "|" + std::to_string((int)VW) + "|1|#33333D|0\n"; }
        emitNodePreview(sc.root.get(), CX, CY, S, VX0, VY0, VW, VH, sc.camX, sc.camY, 0, 0, 0, 1, 1, out);
        for (auto& b : sc.ui) {
            float bcx = CX + (b.touch.rect.x + b.touch.rect.w/2 - sc.camX - 640)*S;
            float bcy = CY + (b.touch.rect.y + b.touch.rect.h/2 - sc.camY - 360)*S;
            if (bcx < VX0 || bcx > VX0+VW || bcy < VY0 || bcy > VY0+VH) continue;
            float bw = b.touch.rect.w*S, bh = b.touch.rect.h*S;
            out += "DRAW button|" + b.text + "|" + std::to_string((int)(bcx-bw/2)) + "|" + std::to_string((int)(bcy-bh/2)) + "|" + std::to_string((int)bw) + "|" + std::to_string((int)bh) + "|" + colorToHexA(withAlpha(b.color, b.alpha)) + "|" + std::to_string(b.angle) + "|" + resolveAssetPath(b.texture) + "\n";
        }
        Node* selN = editor_ ? editor_->selected() : nullptr;
        Node2D* g = (selN && (editor_->selectedUi().empty())) ? dynamic_cast<Node2D*>(selN) : nullptr;
        if (g && !g->locked) {
            float gwx, gwy, gwr, gsx, gsy;
            Scene* esc = const_cast<Scene*>(&sc);
            if (!nodeWorld(esc, g->name, gwx, gwy, gwr, gsx, gsy)) { gwx = g->position.x; gwy = g->position.y; gsx = gsy = 1; }
            float cx = CX + (gwx - sc.camX - 640)*S;
            float cy = CY + (gwy - sc.camY - 360)*S;
            float Z = edZoom_;
            if (manip_ == Manip::Move) {
                out += "DRAW rect|" + std::to_string((int)cx) + "|" + std::to_string((int)(cy-2)) + "|" + std::to_string((int)(56*Z)) + "|4|#D62828|0\n";
                out += "DRAW shape|triangle|" + std::to_string((int)(cx+50*Z)) + "|" + std::to_string((int)(cy-8*Z)) + "|" + std::to_string((int)(16*Z)) + "|" + std::to_string((int)(14*Z)) + "|#D62828|90\n";
                out += "DRAW rect|" + std::to_string((int)(cx-2)) + "|" + std::to_string((int)cy) + "|4|" + std::to_string((int)(56*Z)) + "|#40C040|0\n";
                out += "DRAW shape|triangle|" + std::to_string((int)(cx-8*Z)) + "|" + std::to_string((int)(cy+50*Z)) + "|" + std::to_string((int)(14*Z)) + "|" + std::to_string((int)(16*Z)) + "|#40C040|180\n";
            } else if (manip_ == Manip::Rotate) {
                for (int k = 0; k < 24; ++k) {
                    float a = k * 6.28318f / 24.0f;
                    float px = cx + std::cos(a) * 70*Z, py = cy + std::sin(a) * 70*Z;
                    out += "DRAW rect|" + std::to_string((int)(px-3)) + "|" + std::to_string((int)(py-3)) + "|6|6|#FF8800|0\n";
                }
            } else {
                float hw = (g->w * gsx)*S/2, hh = (g->h * gsy)*S/2;
                out += "DRAW rect|" + std::to_string((int)cx) + "|" + std::to_string((int)(cy-1)) + "|" + std::to_string((int)(hw+24*Z)) + "|2|#4CC9F0|0\n";
                out += "DRAW rect|" + std::to_string((int)(cx+hw+16*Z)) + "|" + std::to_string((int)(cy-8*Z)) + "|16|16|#4CC9F0|0\n";
                out += "DRAW rect|" + std::to_string((int)(cx-1)) + "|" + std::to_string((int)cy) + "|2|" + std::to_string((int)(hh+24*Z)) + "|#4CC9F0|0\n";
                out += "DRAW rect|" + std::to_string((int)(cx-8*Z)) + "|" + std::to_string((int)(cy+hh+16*Z)) + "|16|16|#4CC9F0|0\n";
            }
        }
        {
            std::string selUi2 = editor_ ? editor_->selectedUi() : std::string("");
            UiButton* gb2 = selUi2.empty() ? nullptr : editor_->findUi(selUi2);
            if (gb2 && manip_ == Manip::Rotate) {
                float bcx = CX + (gb2->touch.rect.x + gb2->touch.rect.w/2 - sc.camX - 640)*S;
                float bcy = CY + (gb2->touch.rect.y + gb2->touch.rect.h/2 - sc.camY - 360)*S;
                float Z = edZoom_;
                for (int k = 0; k < 24; ++k) {
                    float a = k * 6.28318f / 24.0f;
                    float px = bcx + std::cos(a) * 70*Z, py = bcy + std::sin(a) * 70*Z;
                    out += "DRAW rect|" + std::to_string((int)(px-3)) + "|" + std::to_string((int)(py-3)) + "|6|6|#FF8800|0\n";
                }
                out += "DRAW text|ang " + std::to_string((int)gb2->angle) + "|" + std::to_string((int)(bcx+80)) + "|" + std::to_string((int)(bcy-10)) + "|14|#FF8800|0\n";
            }
        }
        if (pickParent_ && !pickChild_.empty()) {
            out += "DRAW rect|300|64|592|26|#FF8800|0\n";
            out += "DRAW text|PARENT FOR: " + pickChild_ + "  ->  tap object or row|306|68|16|#1A1A2E|0\n";
        }
        if (!lastMsg_.empty()) out += "DRAW text|" + lastMsg_ + "   fps " + std::to_string((int)fps_) + "|306|580|14|#FFD700|0\n";
    }

    void emitNodePreview(const Node* n, float CX, float CY, float S, float VX0, float VY0, float VW, float VH,
                         float camX, float camY, float ox, float oy, float orot, float osx, float osy,
                         std::string& out) {
        if (!n) return;
        Node2D* n2d = dynamic_cast<Node2D*>(const_cast<Node*>(n));
        if (!n2d) { for (const auto& ch : n->getChildren()) emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, camX, camY, ox, oy, orot, osx, osy, out); return; }
        float cr = std::cos(orot), sr = std::sin(orot);
        float wx = ox + (n2d->position.x * osx) * cr - (n2d->position.y * osy) * sr;
        float wy = oy + (n2d->position.x * osx) * sr + (n2d->position.y * osy) * cr;
        float wrot = orot + n2d->rotation;
        float wsx = osx * n2d->scale.x, wsy = osy * n2d->scale.y;
        std::string tn = std::string(n2d->typeName());
        float sx = CX + (wx - camX - 640)*S, sy = CY + (wy - camY - 360)*S;
        float ang = wrot * 57.2957795f;
        unsigned colA = withAlpha(n2d->color, n2d->alpha);
        if (tn == "Camera2D") {
            if (sx >= VX0 && sx <= VX0+VW && sy >= VY0 && sy <= VY0+VH) {
                out += "DRAW rect|" + std::to_string((int)(sx-14)) + "|" + std::to_string((int)(sy-10)) + "|28|20|#FFD700|0\n";
                out += "DRAW text|CAM|" + std::to_string((int)(sx-12)) + "|" + std::to_string((int)(sy+12)) + "|12|#FFD700|0\n";
            }
        }
        else if (tn == "Light2D") {
            Light2D* li = static_cast<Light2D*>(n2d);
            float r = li->radius * ((wsx + wsy) * 0.5f) * S;
            if (sx >= VX0 && sx <= VX0+VW && sy >= VY0 && sy <= VY0+VH)
                out += "DRAW shape|glow|" + std::to_string((int)(sx-r)) + "|" + std::to_string((int)(sy-r)) + "|" + std::to_string((int)(2*r)) + "|" + std::to_string((int)(2*r)) + "|" + colorToHexA(colA) + "|0\n";
        }
        else if (tn != "Node") {
            float w = n2d->w * wsx * S, h = n2d->h * wsy * S;
            float rx = sx - w/2, ry = sy - h/2;
            if (!n2d->hasAppearance() && tn == "Node2D") {
                if (sx >= VX0 && sx <= VX0+VW && sy >= VY0 && sy <= VY0+VH) {
                    out += "DRAW rect|" + std::to_string((int)(sx-10)) + "|" + std::to_string((int)(sy-2)) + "|20|4|#808080|0\n";
                    out += "DRAW rect|" + std::to_string((int)(sx-2)) + "|" + std::to_string((int)(sy-10)) + "|4|20|#808080|0\n";
                    out += "DRAW text|" + n2d->name + "|" + std::to_string((int)(sx+12)) + "|" + std::to_string((int)(sy+4)) + "|12|#808080|0\n";
                }
            }
            bool vis = (rx >= VX0 && ry >= VY0 && rx + w <= VX0 + VW && ry + h <= VY0 + VH);
            if (vis) {
                if (tn == "Label") {
                    int fs = (int)(static_cast<Label*>(n2d)->fontSize * ((wsx + wsy) * 0.5f) * S);
                    if (fs < 6) fs = 6;
                    out += "DRAW text|" + static_cast<Label*>(n2d)->text + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string(fs) + "|" + colorToHexA(colA) + "|" + std::to_string(ang) + "\n";
                }
                else if (tn == "Sprite2D") out += "DRAW rect|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|" + colorToHexA(withAlpha(0x555555FFu, n2d->alpha)) + "|" + std::to_string(ang) + "\n";
                else if (n2d->hasAppearance()) out += "DRAW shape|" + n2d->shape + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|" + colorToHexA(colA) + "|" + std::to_string(ang) + "\n";
            }
        }
        for (const auto& ch : n2d->getChildren())
            emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, camX, camY, wx, wy, wrot, wsx, wsy, out);
    }

    std::string stepEditor() {
        if (!editor_ || !editor_->scene()) { appMode_ = AppMode::Hub; rebuildHub(); return ""; }
        consumeDialogResults();
        if (scriptMode_) imeApply();
        gameBackend_.begin(); Renderer gr(gameBackend_); gr.render(editorScene_, &ctx_); std::string out = gameBackend_.str();
        if (!scriptMode_) emitViewport(*editor_->scene(), out);
        processEditorActions();
        if (scriptMode_ && imeChanged_) { buildEditorPanels(); input_.setUi(&editorScene_.ui); imeChanged_ = false; }
        if (imeWantOn_)  { out += "IME_ON\n";  imeWantOn_ = false; }
        if (imeWantOff_) { out += "IME_OFF\n"; imeWantOff_ = false; }
        if (pendingText_ && appMode_ == AppMode::Editor) { out += "REQ_TEXT|" + pendingTextCur_ + "\n"; pendingText_ = false; }
        if (pendingName_ && appMode_ == AppMode::Editor) { out += "REQ_NAME|Object\n"; }
        if (pendingAction_ && appMode_ == AppMode::Editor) { out += "REQ_ACTION|" + pendingActionCur_ + "\n"; pendingAction_ = false; }
        if (pendingNum_ && appMode_ == AppMode::Editor) { out += "REQ_NUM|" + pendingNumCur_ + "\n"; pendingNum_ = false; }
        input_.endFrame(); return out;
    }

    AppMode appMode_ = AppMode::Hub;
    HubState hubState_; Scene hubScene_; Scene editorScene_; std::unique_ptr<Editor> editor_;
    ScriptSystem scripts_; std::set<std::string> scripted_;
    Manip manip_ = Manip::Move;
    bool showCreate_ = false, dragging_ = false, pendingText_ = false, pinching_ = false;
    bool pendingName_ = false, pendingAction_ = false, pendingNum_ = false;
    int pendingKind_ = 0;
    int pendingRgb_ = 0;
    std::string pendingNumKind_, pendingNumCur_;
    bool gizmoRot_ = false, gizmoSclX_ = false, gizmoSclY_ = false; bool gizmoRotUi_ = false; int lockAxis_ = 0;
    float gizmoStartAngle_ = 0, gizmoStartRot_ = 0, gizmoStartDist_ = 1, gizmoStartSX_ = 1, gizmoStartSY_ = 1;
    bool pickParent_ = false; std::string pickChild_;
    std::string pendingType_, pendingShape_, pendingActionCur_;
    std::string pendingNodeAction_;
    std::string lastMsg_;
    float edZoom_ = 1.0f;
    float pinchDist0_ = 0, pinchZoom0_ = 1.0f, pinchAX_ = 0, pinchAY_ = 0;
    float dragOX_ = 0, dragOY_ = 0, dragPSX_ = 1, dragPSY_ = 1;
    int hierScroll_ = 0;
    int fsScroll_ = 0;
    std::set<std::string> collapsed_;
    std::vector<std::string> undoStack_, redoStack_; int snapCounter_ = 0;
    std::unique_ptr<Node> clipboard_; int clipCounter_ = 0;
    bool dbg_ = false; float fps_ = 0; long long lastMs_ = 0; int nodeCount_ = 0, lastDraws_ = 0;
    bool scriptMode_ = false; std::string scriptPath_;
    std::vector<std::string> scriptLines_; int curLine_ = 0, curCol_ = 0, scriptScroll_ = 0;
    int compAnchor_ = -1;
    bool imeWantOn_ = false, imeWantOff_ = false, imeChanged_ = false, imeShown_ = false;
    std::mutex imeMtx_; std::vector<std::string> imeTextQ_, imeCompQ_; std::vector<int> imeKeyQ_; bool imeFinish_ = false;
    Node2D* dragNode_ = nullptr; UiButton* dragUi_ = nullptr; int createCounter_ = 0; std::string pendingTextCur_;
    std::string fsPath_;
    std::mutex dlgMtx_;
    bool hasText_ = false, hasName_ = false, hasAction_ = false, hasNum_ = false;
    std::string textRes_, nameRes_, actionRes_, numRes_;
    bool pendingLoadVars_ = false;
    bool saveVarsEnabled_ = false;
    float saveTimer_ = 0.0f;
    ProjectInfo project_; std::string fontPath_; ResourceManager resources_; std::unique_ptr<SceneManager> sceneMgr_; InputManager input_; TouchProcessor touch_; StringRenderBackend gameBackend_; Context ctx_;
};

} // namespace suka
