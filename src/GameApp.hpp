#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <set>
#include <algorithm>
#include <cmath>
#include <mutex>

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
        if (hubState_.selectedDir.empty() && !hubState_.games.empty())
            hubState_.selectedDir = hubState_.games.front().dir;
        input_.screenWidth = 1280.0f;
        input_.screenHeight = 720.0f;
        rebuildHub();
        appMode_ = AppMode::Hub;
        return true;
    }

    void submitText(const std::string& t)   { std::lock_guard<std::mutex> lk(dlgMtx_); textRes_ = t;   hasText_ = true; }
    void submitName(const std::string& t)   { std::lock_guard<std::mutex> lk(dlgMtx_); nameRes_ = t;   hasName_ = true; }
    void submitAction(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); actionRes_ = t; hasAction_ = true; }
    void submitNumber(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); numRes_ = t;    hasNum_ = true; }

    void feedMultiTouch(int phase, float x0, float y0, float x1, float y1) {
        if (appMode_ != AppMode::Editor || showCreate_ || showBg_) return;
        Scene* es = editor_ ? editor_->scene() : nullptr;
        if (!es) return;
        const float S = 0.46875f;
        float mx = (x0 + x1) / 2.0f, my = (y0 + y1) / 2.0f;
        if (phase == 1) { pinching_ = true; pinchMX_ = mx; pinchMY_ = my; return; }
        if (phase == 3) { pinching_ = false; return; }
        if (!pinching_) return;
        es->camX += (mx - pinchMX_) / S;
        es->camY += (my - pinchMY_) / S;
        pinchMX_ = mx; pinchMY_ = my;
    }

    void feedTouch(int action, float x, float y) {
        RawTouch t;
        if (action == 0) t.action = RawTouch::Action::Down;
        else if (action == 2) t.action = RawTouch::Action::Move;
        else t.action = RawTouch::Action::Up;
        t.x = x; t.y = y;
        Scene* cur = uiScene();
        if (!cur) return;

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

        if (appMode_ == AppMode::Editor && !showCreate_ && !showBg_) {
            Scene* es = editor_ ? editor_->scene() : nullptr;
            Node2D* g = (editor_ && editor_->selectedUi().empty()) ?
                        (editor_->selected() ? dynamic_cast<Node2D*>(editor_->selected()) : nullptr) : nullptr;

            if (g && es) {
                float scx, scy; proj(*es, g->position.x, g->position.y, scx, scy);
                if (t.action == RawTouch::Action::Down) {
                    if (pickParent_ && !pickChild_.empty()) {
                        float wx, wy; unproj(*es, x, y, wx, wy);
                        std::string hit = hitTest(es->root.get(), wx, wy);
                        if (!hit.empty() && hit != pickChild_) attachChildTo(pickChild_, hit);
                        pickParent_ = false; pickChild_.clear();
                        buildEditorPanels(); input_.setUi(&editorScene_.ui);
                        return;
                    }
                    float dx = x - scx, dy = y - scy;
                    float dist = std::sqrt(dx*dx + dy*dy);
                    if (manip_ == Manip::Rotate) {
                        if (std::fabs(dist - 70.0f) < 26.0f) { gizmoRot_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = g->rotation; return; }
                    } else if (manip_ == Manip::Scale) {
                        float hw = (g->w * g->scale.x) * 0.46875f / 2;
                        float hh = (g->h * g->scale.y) * 0.46875f / 2;
                        if (std::fabs(x - (scx + hw + 24)) < 28 && std::fabs(dy) < 28) { gizmoSclX_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSX_ = g->scale.x; return; }
                        if (std::fabs(y - (scy + hh + 24)) < 28 && std::fabs(dx) < 28) { gizmoSclY_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSY_ = g->scale.y; return; }
                    } else {
                        if (std::fabs(dy) < 16 && dx > 8 && dx < 64) { lockAxis_ = 1; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                        if (std::fabs(dx) < 16 && dy > 8 && dy < 64) { lockAxis_ = 2; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
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
                    if (!hit.empty()) { editor_->select(hit); dragNode_ = editor_->find2d(hit); dragging_ = (dragNode_ != nullptr); lockAxis_ = 0; startDragParent(es, hit); }
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
                    moveGroupButtons(es, dragNode_->name, nx - oldX, ny - oldY);   // GROUP: кнопки едут с группой
                }
            } else if (t.action == RawTouch::Action::Up) { dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; lockAxis_ = 0; }
        }
        touch_.onTouch(t, *cur, input_);
    }

    std::string stepFrame() {
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
        sx = 596 + (wx - sc.camX - 640) * 0.46875f;
        sy = 310 + (wy - sc.camY - 360) * 0.46875f;
    }
    void unproj(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        wx = 640 + sc.camX + (sx - 596) / 0.46875f;
        wy = 360 + sc.camY + (sy - 310) / 0.46875f;
    }
    void unprojGame(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        Node* cn = sc.root ? sc.root->findByType("Camera2D") : nullptr;
        if (cn) {
            Camera2D* cam = static_cast<Camera2D*>(cn);
            float z = cam->zoom > 0.01f ? cam->zoom : 1.0f;
            wx = (sx - 640) / z + cam->position.x;
            wy = (sy - 360) / z + cam->position.y;
        } else { wx = sx; wy = sy; }
    }
    std::string hitUi(Scene* es, float wx, float wy) {
        for (auto& b : es->ui)
            if (wx >= b.touch.rect.x && wx <= b.touch.rect.x + b.touch.rect.w &&
                wy >= b.touch.rect.y && wy <= b.touch.rect.y + b.touch.rect.h) return b.touch.id;
        return "";
    }

    // ---- мировая трансформация родителя (для честных хитбоксов и drag) ----
    void parentXf(Node* n, const std::string& name, float cx, float cy, float sx, float sy,
                  float& ox, float& oy, float& psx, float& psy, bool& found) {
        if (found) return;
        if (n->name == name) { ox = cx; oy = cy; psx = sx; psy = sy; found = true; return; }
        for (auto& ch : n->getChildren()) {
            Node2D* c2 = dynamic_cast<Node2D*>(ch.get());
            if (c2) parentXf(ch.get(), name, cx + c2->position.x * sx, cy + c2->position.y * sy, sx * c2->scale.x, sy * c2->scale.y, ox, oy, psx, psy, found);
            else parentXf(ch.get(), name, cx, cy, sx, sy, ox, oy, psx, psy, found);
        }
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
        if (child.rfind("UI:", 0) == 0) {          // GROUP: кнопка в группу
            UiButton* b = editor_->findUi(child.substr(3));
            if (b) b->group = parent;
            return;
        }
        if (!sc->root) return;
        Node* root = sc->root.get();
        if (child == parent) return;
        Node* cn = root->findNode(child);
        Node* pn = root->findNode(parent);
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

    // ---------------- HUB ----------------
    void rebuildHub() { hubState_.games = ProjectList::scan(); hubScene_ = buildHubLandscape(); input_.setUi(&hubScene_.ui); }
    Scene buildHubLandscape() {
        Theme& th = currentTheme();
        Scene s; s.name = "Hub"; s.root = std::make_unique<Node>(); s.root->name = "Hub";
        auto hdr = std::make_unique<Label>(); hdr->name = "ProjHdr"; hdr->text = "PROJECTS";
        hdr->fontSize = 22; hdr->color = th.ink; hdr->position = Vec2{14, 34}; s.root->addChild(std::move(hdr));
        for (size_t i = 0; i < hubState_.games.size(); ++i) {
            const auto& g = hubState_.games[i]; UiButton row; row.touch.id = "sel_" + g.dir;
            row.touch.rect = Rect{10, 70 + (float)i * 56, 240, 48};
            row.text = (hubState_.selectedDir == g.dir ? "* " : "  ") + g.dir; row.action = "sel:" + g.dir;
            row.color = (hubState_.selectedDir == g.dir) ? th.accent : th.button; s.ui.push_back(row);
        }
        if (!hubState_.selectedDir.empty()) {
            ProjectInfo info; ProjectLoader::load(PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/project.json", info);
            auto nm = std::make_unique<Label>(); nm->name = "SelName"; nm->text = info.name;
            nm->fontSize = 34; nm->color = th.ink; nm->position = Vec2{740, 120}; s.root->addChild(std::move(nm));
            auto sc = std::make_unique<Label>(); sc->name = "SelScene"; sc->text = "scene: " + info.mainScene;
            sc->fontSize = 20; sc->color = th.ink; sc->position = Vec2{740, 170}; s.root->addChild(std::move(sc));
            UiButton play; play.touch.id = "play"; play.touch.rect = Rect{740, 280, 150, 60}; play.text = "Play";
            play.action = "play:" + hubState_.selectedDir; play.color = th.accent; s.ui.push_back(play);
            UiButton edit; edit.touch.id = "edit"; edit.touch.rect = Rect{910, 280, 150, 60}; edit.text = "Edit";
            edit.action = "edit:" + hubState_.selectedDir; edit.color = th.button; s.ui.push_back(edit);
        } else {
            auto hint = std::make_unique<Label>(); hint->name = "Hint"; hint->text = "(select a project)";
            hint->fontSize = 24; hint->color = th.ink; hint->position = Vec2{740, 300}; s.root->addChild(std::move(hint));
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
        else if (a.kind == 1) enterGame(a.dir);
        else if (a.kind == 2) enterEditor(a.dir);
        input_.endFrame(); return out;
    }

    // ---------------- GAME ----------------
    bool enterGame(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) return false;
        project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";
        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) return false;
        scripts_.load(pi.rootPath);
        Scene* sc = sceneMgr_->current();
        UiButton close; close.touch.id = "close"; close.touch.rect = Rect{1180, 10, 90, 70}; close.text = "X"; close.action = "hub:"; close.color = parseColor("#D62828"); sc->ui.push_back(close);
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
        std::string out;
        if (ctx_.coinCollectedThisFrame) out += "SOUND coin\n";
        if (ctx_.jumpPressedThisFrame)   out += "SOUND jump\n";
        gameBackend_.begin(); Renderer renderer(gameBackend_); renderer.render(*sceneMgr_->current(), &ctx_); out += gameBackend_.str();
        input_.endFrame(); return out;
    }
    void runAction(const std::string& act) {
        Scene* sc = sceneMgr_->current(); if (!sc) return;
        const std::string pRestart = "restart_scene:", pChange = "change_scene:", pAdd = "add_var:", pSet = "set_var:", pHub = "hub:", pCall = "call:";
        if (act.rfind(pHub, 0) == 0) { appMode_ = AppMode::Hub; rebuildHub(); }
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

    // ---------------- EDITOR ----------------
    struct HierRow { int depth; std::string name; std::string type; };
    void collectHier(const Node& n, int depth, std::vector<HierRow>& out) {
        out.push_back({ depth, n.name, std::string(n.typeName()) });
        for (const auto& ch : n.getChildren()) collectHier(*ch, depth + 1, out);
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
            float cxw = ox + d->position.x * psx;
            float cyw = oy + d->position.y * psy;
            float hw = (d->w * d->scale.x * psx) / 2; if (hw < 28) hw = 28;
            float hh = (d->h * d->scale.y * psy) / 2; if (hh < 28) hh = 28;
            if (wx >= cxw - hw && wx <= cxw + hw && wy >= cyw - hh && wy <= cyw + hh) { if (bestBox.empty()) bestBox = d->name; }
            float dx = wx - cxw, dy = wy - cyw;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist < 45.0f && dist < bestDist) { bestDist = dist; bestNear = d->name; }
            for (const auto& ch : n->getChildren())
                collectHit(ch.get(), wx, wy, cxw, cyw, psx * d->scale.x, psy * d->scale.y, bestBox, bestNear, bestDist);
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
        showCreate_ = false; showBg_ = false; pendingText_ = false; pendingName_ = false; pendingAction_ = false; pendingNum_ = false;
        fsPath_ = ""; manip_ = Manip::Move; pinching_ = false; hierScroll_ = 0; pickParent_ = false;
        buildEditorPanels(); input_.setUi(&editorScene_.ui); touch_.resetJoystick(); appMode_ = AppMode::Editor; return true;
    }

    static std::string dash(int depth) { return depth > 0 ? std::string(depth, '-') + " " : ""; }

    void buildEditorPanels() {
        Theme& th = currentTheme(); const unsigned GODOT_ORANGE = 0xFF8800FFu;
        editorScene_ = Scene(); editorScene_.name = "Editor"; editorScene_.root = std::make_unique<Node>(); editorScene_.root->name = "EdRoot";
        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
            auto l = std::make_unique<Label>(); l->name = nm; l->text = txt; l->fontSize = fs; l->color = col; l->position = Vec2{x, y}; editorScene_.root->addChild(std::move(l));
        };
        auto fsBg = std::make_unique<Node2D>(); fsBg->name = "FsBg"; fsBg->shape = "square"; fsBg->color = dimColor(th.bg, 0.6f); fsBg->w = 284; fsBg->h = 320; fsBg->position = Vec2{150, 536}; editorScene_.root->addChild(std::move(fsBg));
        addLbl("TabScene", "Scene", 20, 8, 20, GODOT_ORANGE); addLbl("Tab2D", "2D", 110, 8, 20, th.ink); addLbl("Tab3D", "3D", 160, 8, 20, th.ink); addLbl("TabScr", "Script", 210, 8, 20, th.ink); addLbl("TabAss", "AssetLib", 300, 8, 20, th.ink);
        addLbl("DHdr", "Scene", 10, 40, 18, th.ink); addLbl("IHdr", "Inspector", 900, 40, 18, th.ink);

        const char* mlab[3] = { "POS","ROT","SCL" };
        Manip mval[3] = { Manip::Move, Manip::Rotate, Manip::Scale };
        for (int k = 0; k < 3; ++k) {
            UiButton b; b.touch.id = std::string("manipbtn")+std::to_string(k);
            b.touch.rect = Rect{1090 + (float)k * 60, 34, 56, 28};
            b.text = mlab[k]; b.action = std::string("manip:") + (k==0?"move":k==1?"rotate":"scale");
            b.color = (manip_ == mval[k]) ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        // TREE-FIX: строки с тире; кнопки группы выводятся дочерними под своим узлом
        std::vector<HierRow> hier;
        if (editor_ && editor_->scene() && editor_->scene()->root) collectHier(*editor_->scene()->root, 0, hier);
        struct Row { std::string text, action; bool sel; };
        std::vector<Row> rows;
        Node* selNode = editor_ ? editor_->selected() : nullptr; std::string sel = selNode ? selNode->name : std::string{};
        std::string selUi = editor_ ? editor_->selectedUi() : std::string{};
        Scene* esc = editor_ ? editor_->scene() : nullptr;
        for (auto& hr : hier) {
            Row r; r.text = dash(hr.depth) + hr.name + "   " + hr.type;
            r.action = "ed_select:" + hr.name; r.sel = (sel == hr.name); rows.push_back(r);
            if (esc) for (auto& ub : esc->ui) {
                if (ub.group == hr.name) {
                    Row br; br.text = dash(hr.depth + 1) + ub.touch.id + "   Button";
                    br.action = "ed_selectui:" + ub.touch.id; br.sel = (selUi == ub.touch.id); rows.push_back(br);
                }
            }
        }
        if (esc) for (auto& ub : esc->ui) {
            if (!ub.group.empty()) continue;
            Row r; r.text = "  " + ub.touch.id + "   Button";
            r.action = "ed_selectui:" + ub.touch.id; r.sel = (selUi == ub.touch.id); rows.push_back(r);
        }
        const int VIS = 10;
        int maxScroll = (int)rows.size() > VIS ? (int)rows.size() - VIS : 0;
        if (hierScroll_ < 0) hierScroll_ = 0;
        if (hierScroll_ > maxScroll) hierScroll_ = maxScroll;
        { UiButton b; b.touch.id="hup"; b.touch.rect=Rect{248,38,20,22}; b.text="^"; b.action="hier_up"; b.color=th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="hdn"; b.touch.rect=Rect{270,38,20,22}; b.text="v"; b.action="hier_dn"; b.color=th.button; editorScene_.ui.push_back(b); }
        for (int i = hierScroll_; i < (int)rows.size() && i < hierScroll_ + VIS; ++i) {
            UiButton b; b.touch.id = "h" + std::to_string(i);
            b.touch.rect = Rect{8, 64 + (float)(i - hierScroll_) * 30, 284, 28};
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
                addLbl("InCol", "Color " + colorToHex(ub->color), 900, 172, 16, th.ink);
                addLbl("InText", "Text: " + ub->text, 900, 196, 16, th.ink);
                addLbl("InAct", "Action: " + (ub->action.empty() ? std::string("(none)") : ub->action), 900, 220, 16, th.ink);
                addLbl("InAng", "Angle " + std::to_string((int)ub->angle), 900, 244, 16, th.ink);
                addLbl("InTex", "Texture: " + (ub->texture.empty() ? std::string("(none)") : ub->texture), 900, 268, 16, th.ink);
                addLbl("InGrp", "Group: " + (ub->group.empty() ? std::string("(none)") : ub->group), 900, 292, 16, th.ink);
                const char* nl[4] = { "X","Y","W","H" };
                const char* nk[4] = { "bx","by","bw","bh" };
                for (int k = 0; k < 4; ++k) {
                    UiButton b; b.touch.id = std::string("numbtn")+std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 62, 320, 58, 28};
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
                addLbl("InShp", "Shape  " + s->shape, 900, 220, 16, th.ink);
                addLbl("InCol", "Color  " + colorToHex(s->color), 900, 244, 16, th.ink);
                addLbl("InTex", "Texture: " + (s->texture.empty() ? std::string("(none)") : s->texture), 900, 268, 16, th.ink);
                addLbl("InAct", "Touch: " + (s->action.empty() ? std::string("(none)") : s->action), 900, 292, 16, th.ink);
                if (std::string(s->typeName()) == "Label") addLbl("InText", "Text: " + static_cast<Label*>(s)->text, 900, 316, 16, th.ink);
                const char* nl[6] = { "X","Y","ROT","SCL","W","H" };
                const char* na[6] = { "nx","ny","nrot","nscl","nw","nh" };
                for (int k = 0; k < 6; ++k) {
                    UiButton b; b.touch.id = std::string("numbtn")+std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 62, 344, 58, 28};
                    b.text = nl[k]; b.action = std::string("num:") + na[k];
                    b.color = th.button; editorScene_.ui.push_back(b);
                }
            } else addLbl("InNone", "(no selection)", 900, 92, 18, th.ink);
        }

        std::string sb = (editor_ && editor_->scene() && editor_->scene()->bgSet()) ? editor_->scene()->bg : std::string("(theme)");
        addLbl("InBg", "scene bg: " + sb, 900, 380, 16, th.ink);
        { UiButton b; b.touch.id = "bgbtn"; b.touch.rect = Rect{900, 402, 62, 30}; b.text = "BG"; b.action = "bg_open"; b.color = th.accent; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "actbtn"; b.touch.rect = Rect{966, 402, 62, 30}; b.text = "ACT"; b.action = "edit_action"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "texbtn"; b.touch.rect = Rect{1032, 402, 62, 30}; b.text = "T-"; b.action = "clear_tex"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "clnbtn"; b.touch.rect = Rect{1098, 402, 62, 30}; b.text = "DUP"; b.action = "ed_clone"; b.color = th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "parbtn"; b.touch.rect = Rect{1164, 402, 62, 30}; b.text = pickParent_ ? "PICK" : "PAR"; b.action = "ed_parent"; b.color = pickParent_ ? GODOT_ORANGE : th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id = "unpbtn"; b.touch.rect = Rect{1164, 436, 62, 30}; b.text = "UNP"; b.action = "ed_unparent"; b.color = th.button; editorScene_.ui.push_back(b); }

        const char* mv[4] = { "l","u","d","r" }; const char* mvTxt[4] = { "<","^","v",">" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("mv")+std::to_string(k); b.touch.rect = Rect{900+(float)k*58, 616, 54, 48}; b.text = mvTxt[k]; b.action = std::string("ed_move:")+mv[k]; b.color = th.button; editorScene_.ui.push_back(b); }

        float tx = 300;
        const char* shapes[4] = { "square","circle","diamond","triangle" }; const char* shTxt[4] = { "SQ","CI","DI","TR" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("sh")+std::to_string(k); b.touch.rect = Rect{tx,34,44,26}; tx+=46; b.text = shTxt[k]; b.action = std::string("ed_shape:")+shapes[k]; b.color = th.button; editorScene_.ui.push_back(b); }
        const char* cols[3] = { "#D62828","#2EC4B6","#F4EDE4" };
        for (int k = 0; k < 3; ++k) { UiButton b; b.touch.id = std::string("col")+std::to_string(k); b.touch.rect = Rect{tx,34,44,26}; tx+=46; b.text = ""; b.action = std::string("ed_color:")+cols[k]; b.color = parseColor(cols[k]); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="del"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="DEL"; b.action="ed_del"; b.color=parseColor("#D62828"); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="save"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="SAVE"; b.action="ed_save"; b.color=parseColor("#2E7D32"); editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="eback"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="<"; b.action="ed_back"; b.color=GODOT_ORANGE; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="plus"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="+"; b.action="create_open"; b.color=GODOT_ORANGE; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="txt"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="TXT"; b.action="edit_text"; b.color=th.button; editorScene_.ui.push_back(b); }
        { UiButton b; b.touch.id="scr"; b.touch.rect=Rect{tx,34,44,26}; tx+=46; b.text="SCR"; b.action="ed_scr"; b.color=th.button; editorScene_.ui.push_back(b); }

        if (showCreate_) {
            const char* ct[6] = { "Node2D","Node2D","Node2D","Node2D","Label","Sprite2D" };
            const char* cs[6] = { "square","circle","diamond","triangle","","" };
            const char* cl[6] = { "CUBE","CIRCLE","DIAMOND","TRIANGLE","TEXT","SPRITE" };
            for (int k = 0; k < 6; ++k) { UiButton b; b.touch.id = std::string("ct")+std::to_string(k); b.touch.rect = Rect{300+(float)k*66, 560, 62, 40}; b.text = cl[k]; b.action = std::string("create:")+ct[k]+":"+cs[k]; b.color = th.button; editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctcam"; b.touch.rect=Rect{300+6*66,560,62,40}; b.text="CAM"; b.action="create_cam"; b.color=th.accent; editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctlit"; b.touch.rect=Rect{300+7*66,560,62,40}; b.text="LIGHT"; b.action="create_light"; b.color=parseColor("#FFD700"); editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctgrp"; b.touch.rect=Rect{300+8*66,560,62,40}; b.text="GRP"; b.action="create_grp"; b.color=parseColor("#808080"); editorScene_.ui.push_back(b); }
        }
        if (showBg_) {
            const char* bgs[6] = { "#FFF3E0","#111111","#16213E","#2EC4B6","#D62828","#87CEEB" };
            for (int k = 0; k < 6; ++k) { UiButton b; b.touch.id = std::string("bgsw")+std::to_string(k); b.touch.rect = Rect{300+(float)k*84, 560, 80, 40}; b.text = ""; b.action = std::string("bg_set:")+bgs[k]; b.color = parseColor(bgs[k]); editorScene_.ui.push_back(b); }
            UiButton cl; cl.touch.id = "bgclr"; cl.touch.rect = Rect{300+6*84, 560, 80, 40}; cl.text = "CLR"; cl.action = "bg_clear"; cl.color = th.button; editorScene_.ui.push_back(cl);
        }

        addLbl("FsHdr", "FILES", 10, 384, 18, th.ink);
        std::string shown = fsPath_.empty() ? std::string("res/") : ("res/" + fsPath_);
        addLbl("FsPath", shown, 10, 406, 15, GODOT_ORANGE);
        float fy = 428; const float STEP = 28; int rowsN = 0; const int MAXROWS = 8;
        if (!fsPath_.empty()) {
            UiButton up; up.touch.id = "fsup"; up.touch.rect = Rect{8, fy, 284, STEP-2}; up.text = ".."; up.action = "fs_up"; up.color = th.button; editorScene_.ui.push_back(up); fy += STEP; ++rowsN;
        }
        std::string abs = project_.rootPath + "/" + fsPath_;
        std::vector<FileEntry> items = FileBrowser::list(abs);
        for (const auto& it : items) {
            if (rowsN >= MAXROWS) { addLbl("FsMore", "  ...", 10, fy, 15, th.ink); break; }
            UiButton fb; fb.touch.id = "fs" + std::to_string(rowsN); fb.touch.rect = Rect{8, fy, 284, STEP-2};
            std::string rel = fsPath_ + it.name;
            if (it.isDir) { fb.text = "/ " + it.name; fb.action = "fs_enter:" + it.name; fb.color = th.button; }
            else { fb.text = "  " + it.name; fb.action = "fs_pick:" + rel; fb.color = th.accent; }
            editorScene_.ui.push_back(fb);
            fy += STEP; ++rowsN;
        }
    }

    void processEditorActions() {
        if (!editor_) return;
        Node* sn = editor_->selected(); std::string sel = sn ? sn->name : std::string{};
        std::string selUi = editor_->selectedUi();
        UiButton* ub = selUi.empty() ? nullptr : editor_->findUi(selUi);
        bool changed = false;
        for (auto& b : editorScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action == "hier_up") { hierScroll_ -= 3; changed = true; }
            else if (b.action == "hier_dn") { hierScroll_ += 3; changed = true; }
            else if (b.action == "fs_up") {
                std::string tmp = fsPath_;
                while (!tmp.empty() && tmp.back() == '/') tmp.pop_back();
                size_t sl = tmp.find_last_of('/');
                fsPath_ = (sl == std::string::npos) ? std::string("") : tmp.substr(0, sl + 1);
                changed = true;
            }
            else if (b.action.rfind("fs_enter:", 0) == 0) { fsPath_ += b.action.substr(9) + "/"; changed = true; }
            else if (b.action.rfind("fs_pick:", 0) == 0) {
                std::string rel = b.action.substr(8);
                if (rel.size() > 5 && rel.compare(rel.size()-5, 5, ".json") == 0) {
                    if (sceneMgr_->restartScene(rel, resources_)) {
                        editor_->attach(sceneMgr_->current());
                        scripted_.clear(); showCreate_ = false; showBg_ = false; dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pinching_ = false;
                        ub = nullptr; sel.clear(); hierScroll_ = 0;
                        changed = true;
                    }
                } else {
                    if (ub) { ub->texture = rel; changed = true; }
                    else if (!sel.empty()) { editor_->setTexture(sel, rel); changed = true; }
                }
            }
            else if (b.action == "clear_tex") {
                if (ub) { ub->texture.clear(); changed = true; }
                else if (!sel.empty()) { editor_->setTexture(sel, std::string("")); changed = true; }
            }
            else if (b.action == "manip:move")   { manip_ = Manip::Move;   changed = true; }
            else if (b.action == "manip:rotate") { manip_ = Manip::Rotate; changed = true; }
            else if (b.action == "manip:scale")  { manip_ = Manip::Scale;  changed = true; }
            else if (b.action == "create_open") { showCreate_ = !showCreate_; showBg_ = false; changed = true; }
            else if (b.action == "bg_open") { showBg_ = !showBg_; showCreate_ = false; changed = true; }
            else if (b.action.rfind("bg_set:", 0) == 0) { if (editor_->scene()) editor_->scene()->bg = b.action.substr(7); showBg_ = false; changed = true; }
            else if (b.action == "bg_clear") { if (editor_->scene()) editor_->scene()->bg.clear(); showBg_ = false; changed = true; }
            else if (b.action == "edit_text") {
                if (ub) { pendingText_ = true; pendingTextCur_ = ub->text; }
                else if (sn && std::string(sn->typeName()) == "Label") { pendingText_ = true; pendingTextCur_ = static_cast<Label*>(sn)->text; }
            }
            else if (b.action == "edit_action") {
                pendingAction_ = true;
                pendingActionCur_ = ub ? ub->action : (sn ? static_cast<Node2D*>(sn)->action : std::string(""));
            }
            else if (b.action.rfind("num:", 0) == 0) {
                pendingNum_ = true; pendingNumKind_ = b.action.substr(4);
                Node2D* s2 = (!sel.empty()) ? editor_->find2d(sel) : nullptr;
                if (pendingNumKind_ == "nx") pendingNumCur_ = s2 ? std::to_string((int)s2->position.x) : "0";
                else if (pendingNumKind_ == "ny") pendingNumCur_ = s2 ? std::to_string((int)s2->position.y) : "0";
                else if (pendingNumKind_ == "nrot") pendingNumCur_ = s2 ? std::to_string((int)(s2->rotation * 57.2957795f)) : "0";
                else if (pendingNumKind_ == "nscl") pendingNumCur_ = s2 ? std::to_string((int)(s2->scale.x * 100)) : "100";
                else if (pendingNumKind_ == "nw") pendingNumCur_ = s2 ? std::to_string((int)s2->w) : "32";
                else if (pendingNumKind_ == "nh") pendingNumCur_ = s2 ? std::to_string((int)s2->h) : "32";
                else if (pendingNumKind_ == "bx") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.x) : "0";
                else if (pendingNumKind_ == "by") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.y) : "0";
                else if (pendingNumKind_ == "bw") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.w) : "100";
                else if (pendingNumKind_ == "bh") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.h) : "50";
            }
            else if (b.action == "ed_scr") { if (!sel.empty()) { attachScript(sel); changed = true; } }
            else if (b.action == "ed_clone") { if (!sel.empty()) { editor_->cloneSelected(sel + "_copy"); changed = true; } }
            else if (b.action == "ed_parent") {
                if (!selUi.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? ("UI:" + selUi) : std::string(""); changed = true; }
                else if (!sel.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? sel : std::string(""); changed = true; }
            }
            else if (b.action == "ed_unparent") {
                if (!selUi.empty()) { detachChild("UI:" + selUi); changed = true; }
                else if (!sel.empty()) { detachChild(sel); changed = true; }
            }
            else if (b.action.rfind("ed_selectui:", 0) == 0) {
                std::string id = b.action.substr(12);
                if (pickParent_ && !pickChild_.empty() && pickChild_ != id) { attachChildTo(pickChild_, id); pickParent_ = false; pickChild_.clear(); }
                else editor_->selectUi(id);
                changed = true;
            }
            else if (b.action.rfind("ed_select:", 0) == 0) {
                std::string nm = b.action.substr(10);
                if (pickParent_ && !pickChild_.empty() && nm != pickChild_) { attachChildTo(pickChild_, nm); pickParent_ = false; pickChild_.clear(); }
                else editor_->select(nm);
                changed = true;
            }
            else if (b.action == "create_cam") { std::string name = "Cam" + std::to_string(createCounter_++); editor_->addNode("Camera2D", name, 640, 360); editor_->select(name); showCreate_ = false; changed = true; }
            else if (b.action == "create_light") { std::string name = "Light" + std::to_string(createCounter_++); editor_->addNode("Light2D", name, 640, 360); editor_->select(name); showCreate_ = false; changed = true; }
            else if (b.action == "create_grp") { pendingName_ = true; pendingKind_ = 0; pendingType_ = "Node2D"; pendingShape_ = "none"; showCreate_ = false; changed = true; }
            else if (b.action.rfind("create:", 0) == 0) {
                std::string rest = b.action.substr(7); size_t c = rest.find(':');
                pendingName_ = true; pendingKind_ = 0;
                pendingType_ = rest.substr(0, c); pendingShape_ = (c == std::string::npos) ? "" : rest.substr(c + 1);
                showCreate_ = false; changed = true;
            }
            else if (b.action.rfind("ed_shape:", 0) == 0) { if (!sel.empty()) { editor_->setShape(sel, b.action.substr(9)); changed = true; } }
            else if (b.action.rfind("ed_color:", 0) == 0) {
                if (ub) { ub->color = parseColor(b.action.substr(9)); changed = true; }
                else if (!sel.empty()) { editor_->setColor(sel, b.action.substr(9)); changed = true; }
            }
            else if (b.action.rfind("ed_move:", 0) == 0) {
                std::string d = b.action.substr(8);
                if (ub) {
                    if (manip_ == Manip::Move) { float dx=(d=="l")?-16:(d=="r")?16:0; float dy=(d=="u")?-16:(d=="d")?16:0; ub->touch.rect.x+=dx; ub->touch.rect.y+=dy; }
                    else if (manip_ == Manip::Rotate) { float dr=(d=="l")?-15.0f:(d=="r")?15.0f:0.0f; ub->angle += dr; }
                    else if (manip_ == Manip::Scale) { float f=(d=="u")?1.1f:(d=="d")?(1.0f/1.1f):1.0f; ub->touch.rect.w*=f; ub->touch.rect.h*=f; }
                    changed = true;
                } else {
                    Node2D* n = (!sel.empty()) ? editor_->find2d(sel) : nullptr;
                    if (manip_ == Manip::Move) {
                        float dx=(d=="l")?-16:(d=="r")?16:0; float dy=(d=="u")?-16:(d=="d")?16:0;
                        editor_->moveSelected(dx, dy);
                        if (editor_->scene()) moveGroupButtons(editor_->scene(), sel, dx, dy);   // GROUP
                    }
                    else if (manip_ == Manip::Rotate && n) { float dr=(d=="l")?-15.0f:(d=="r")?15.0f:0.0f; n->rotation += dr * 3.14159265f / 180.0f; }
                    else if (manip_ == Manip::Scale && n) { float f=(d=="u")?1.1f:(d=="d")?(1.0f/1.1f):1.0f; n->scale.x*=f; n->scale.y*=f; }
                    changed = true;
                }
            }
            else if (b.action == "ed_del") {
                if (ub) { editor_->deleteUi(selUi); ub = nullptr; changed = true; }
                else if (!sel.empty()) { editor_->deleteNode(sel); sel.clear(); changed = true; }
            }
            else if (b.action == "ed_save") { editor_->save(project_.rootPath + "/scenes/main.json"); }
            else if (b.action == "ed_back") { appMode_ = AppMode::Hub; rebuildHub(); return; }
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
            if (!nm.empty() && pendingKind_ == 0) {
                editor_->addNode(pendingType_, nm, 640, 360);
                if (!pendingShape_.empty()) editor_->setShape(nm, pendingShape_);
                editor_->select(nm);
            }
            pendingName_ = false; showCreate_ = false;
        }
        if (ht) {
            std::string uid = editor_->selectedUi();
            if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) b->text = txt; }
            else { Node* s = editor_->selected(); if (s && std::string(s->typeName()) == "Label") static_cast<Label*>(s)->text = txt; }
        }
        if (ha) {
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
            if (pendingNumKind_ == "nx" && n2) { float dx = v - n2->position.x; n2->position.x = v; if (editor_->scene()) moveGroupButtons(editor_->scene(), n2->name, dx, 0); }
            else if (pendingNumKind_ == "ny" && n2) { float dy = v - n2->position.y; n2->position.y = v; if (editor_->scene()) moveGroupButtons(editor_->scene(), n2->name, 0, dy); }
            else if (pendingNumKind_ == "nrot" && n2) n2->rotation = v * 3.14159265f / 180.0f;
            else if (pendingNumKind_ == "nscl" && n2) { float f = v / 100.0f; if (f > 0.01f) { n2->scale.x = f; n2->scale.y = f; } }
            else if (pendingNumKind_ == "nw" && n2) n2->w = v;
            else if (pendingNumKind_ == "nh" && n2) n2->h = v;
            else if (pendingNumKind_ == "bx" && b) b->touch.rect.x = v;
            else if (pendingNumKind_ == "by" && b) b->touch.rect.y = v;
            else if (pendingNumKind_ == "bw" && b) b->touch.rect.w = v;
            else if (pendingNumKind_ == "bh" && b) b->touch.rect.h = v;
        }
        if (hn || ht || ha || hnum) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    void emitViewport(const Scene& sc, std::string& out) {
        const float VX0 = 300, VY0 = 64, VW = 592, VH = 492; const float CX = VX0 + VW/2, CY = VY0 + VH/2, S = 0.46875f;
        if (sc.bgSet()) out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|" + sc.bg + "|0\n";
        out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|#23232B|0\n";
        for (int gx = 0; gx <= 1280; gx += 64) { float px = CX + ((float)gx - 640 - sc.camX)*S; if (px < VX0 || px > VX0+VW) continue; out += "DRAW rect|" + std::to_string((int)px) + "|" + std::to_string((int)VY0) + "|1|" + std::to_string((int)VH) + "|#33333D|0\n"; }
        for (int gy = 0; gy <= 720; gy += 64) { float py = CY + ((float)gy - 360 - sc.camY)*S; if (py < VY0 || py > VY0+VH) continue; out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)py) + "|" + std::to_string((int)VW) + "|1|#33333D|0\n"; }
        emitNodePreview(sc.root.get(), CX, CY, S, VX0, VY0, VW, VH, sc.camX, sc.camY, out);
        for (auto& b : sc.ui) {
            float bcx = CX + (b.touch.rect.x + b.touch.rect.w/2 - sc.camX - 640)*S;
            float bcy = CY + (b.touch.rect.y + b.touch.rect.h/2 - sc.camY - 360)*S;
            if (bcx < VX0 || bcx > VX0+VW || bcy < VY0 || bcy > VY0+VH) continue;
            float bw = b.touch.rect.w*S, bh = b.touch.rect.h*S;
            out += "DRAW button|" + b.text + "|" + std::to_string((int)(bcx-bw/2)) + "|" + std::to_string((int)(bcy-bh/2)) + "|" + std::to_string((int)bw) + "|" + std::to_string((int)bh) + "|" + colorToHex(b.color) + "|" + std::to_string(b.angle) + "|" + resolveAssetPath(b.texture) + "\n";
        }
        Node* selN = editor_ ? editor_->selected() : nullptr;
        Node2D* g = (selN && (editor_->selectedUi().empty())) ? dynamic_cast<Node2D*>(selN) : nullptr;
        if (g) {
            float cx = CX + (g->position.x - sc.camX - 640)*S;
            float cy = CY + (g->position.y - sc.camY - 360)*S;
            if (manip_ == Manip::Move) {
                // ARROW-FIX: красная вправо, зелёная вниз (наконечники повёрнуты)
                out += "DRAW rect|" + std::to_string((int)cx) + "|" + std::to_string((int)(cy-2)) + "|56|4|#D62828|0\n";
                out += "DRAW shape|triangle|" + std::to_string((int)(cx+50)) + "|" + std::to_string((int)(cy-8)) + "|16|14|#D62828|90\n";
                out += "DRAW rect|" + std::to_string((int)(cx-2)) + "|" + std::to_string((int)cy) + "|4|56|#40C040|0\n";
                out += "DRAW shape|triangle|" + std::to_string((int)(cx-8)) + "|" + std::to_string((int)(cy+50)) + "|14|16|#40C040|180\n";
            } else if (manip_ == Manip::Rotate) {
                for (int k = 0; k < 24; ++k) {
                    float a = k * 6.28318f / 24.0f;
                    float px = cx + std::cos(a) * 70, py = cy + std::sin(a) * 70;
                    out += "DRAW rect|" + std::to_string((int)(px-3)) + "|" + std::to_string((int)(py-3)) + "|6|6|#FF8800|0\n";
                }
            } else {
                float hw = (g->w * g->scale.x)*S/2, hh = (g->h * g->scale.y)*S/2;
                out += "DRAW rect|" + std::to_string((int)cx) + "|" + std::to_string((int)(cy-1)) + "|" + std::to_string((int)(hw+24)) + "|2|#4CC9F0|0\n";
                out += "DRAW rect|" + std::to_string((int)(cx+hw+16)) + "|" + std::to_string((int)(cy-8)) + "|16|16|#4CC9F0|0\n";
                out += "DRAW rect|" + std::to_string((int)(cx-1)) + "|" + std::to_string((int)cy) + "|2|" + std::to_string((int)(hh+24)) + "|#4CC9F0|0\n";
                out += "DRAW rect|" + std::to_string((int)(cx-8)) + "|" + std::to_string((int)(cy+hh+16)) + "|16|16|#4CC9F0|0\n";
            }
        }
        if (pickParent_ && !pickChild_.empty()) {
            out += "DRAW rect|300|64|592|26|#FF8800|0\n";
            out += "DRAW text|PARENT FOR: " + pickChild_ + "  ->  tap object or row|306|68|16|#1A1A2E|0\n";
        }
    }
    void emitNodePreview(const Node* n, float CX, float CY, float S, float VX0, float VY0, float VW, float VH, float camX, float camY, std::string& out) {
        if (!n) return;
        std::string tn = std::string(n->typeName());
        if (tn == "Camera2D") {
            const Node2D* d = static_cast<const Node2D*>(n);
            float cx = CX + (d->position.x - camX - 640)*S, cy = CY + (d->position.y - camY - 360)*S;
            if (cx >= VX0 && cx <= VX0+VW && cy >= VY0 && cy <= VY0+VH) {
                out += "DRAW rect|" + std::to_string((int)(cx-14)) + "|" + std::to_string((int)(cy-10)) + "|28|20|#FFD700|0\n";
                out += "DRAW text|CAM|" + std::to_string((int)(cx-12)) + "|" + std::to_string((int)(cy+12)) + "|12|#FFD700|0\n";
            }
        }
        else if (tn != "Node") {
            const Node2D* d = static_cast<const Node2D*>(n);
            float ang = d->rotation * 57.2957795f;
            float sw = d->w * d->scale.x, sh = d->h * d->scale.y;
            float cx = CX + (d->position.x - camX - 640)*S, cy = CY + (d->position.y - camY - 360)*S;
            float w = sw*S, h = sh*S, rx = cx - w/2, ry = cy - h/2;
            if (!d->hasAppearance() && tn == "Node2D") {
                if (cx >= VX0 && cx <= VX0+VW && cy >= VY0 && cy <= VY0+VH) {
                    out += "DRAW rect|" + std::to_string((int)(cx-10)) + "|" + std::to_string((int)(cy-2)) + "|20|4|#808080|0\n";
                    out += "DRAW rect|" + std::to_string((int)(cx-2)) + "|" + std::to_string((int)(cy-10)) + "|4|20|#808080|0\n";
                    out += "DRAW text|" + d->name + "|" + std::to_string((int)(cx+12)) + "|" + std::to_string((int)(cy+4)) + "|12|#808080|0\n";
                }
            }
            bool vis = (rx >= VX0 && ry >= VY0 && rx + w <= VX0 + VW && ry + h <= VY0 + VH);
            if (vis) {
                if (tn == "Label") {
                    // TEXT-SCALE-FIX: шрифт растёт вместе со scale
                    int fs = (int)(14 * ((d->scale.x + d->scale.y) * 0.5f));
                    if (fs < 6) fs = 6;
                    out += "DRAW text|" + static_cast<const Label*>(d)->text + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string(fs) + "|" + colorToHex(d->color) + "|" + std::to_string(ang) + "\n";
                }
                else if (tn == "Sprite2D") out += "DRAW rect|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|#555555|" + std::to_string(ang) + "\n";
                else if (d->hasAppearance()) out += "DRAW shape|" + d->shape + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|" + colorToHex(d->color) + "|" + std::to_string(ang) + "\n";
            }
        }
        for (const auto& ch : n->getChildren()) emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, camX, camY, out);
    }

    std::string stepEditor() {
        if (!editor_ || !editor_->scene()) { appMode_ = AppMode::Hub; rebuildHub(); return ""; }
        consumeDialogResults();
        gameBackend_.begin(); Renderer gr(gameBackend_); gr.render(editorScene_, &ctx_); std::string out = gameBackend_.str();
        emitViewport(*editor_->scene(), out);
        processEditorActions();
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
    bool showCreate_ = false, showBg_ = false, dragging_ = false, pendingText_ = false, pinching_ = false;
    bool pendingName_ = false, pendingAction_ = false, pendingNum_ = false;
    int pendingKind_ = 0;
    std::string pendingNumKind_, pendingNumCur_;
    bool gizmoRot_ = false, gizmoSclX_ = false, gizmoSclY_ = false; int lockAxis_ = 0;
    float gizmoStartAngle_ = 0, gizmoStartRot_ = 0, gizmoStartDist_ = 1, gizmoStartSX_ = 1, gizmoStartSY_ = 1;
    bool pickParent_ = false; std::string pickChild_;
    std::string pendingType_, pendingShape_, pendingActionCur_;
    std::string pendingNodeAction_;
    float pinchMX_ = 0, pinchMY_ = 0;
    float dragOX_ = 0, dragOY_ = 0, dragPSX_ = 1, dragPSY_ = 1;
    int hierScroll_ = 0;
    Node2D* dragNode_ = nullptr; UiButton* dragUi_ = nullptr; int createCounter_ = 0; std::string pendingTextCur_;
    std::string fsPath_;
    std::mutex dlgMtx_;
    bool hasText_ = false, hasName_ = false, hasAction_ = false, hasNum_ = false;
    std::string textRes_, nameRes_, actionRes_, numRes_;
    ProjectInfo project_; std::string fontPath_; ResourceManager resources_; std::unique_ptr<SceneManager> sceneMgr_; InputManager input_; TouchProcessor touch_; StringRenderBackend gameBackend_; Context ctx_;
};

} // namespace suka
