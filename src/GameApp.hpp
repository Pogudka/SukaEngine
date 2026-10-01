#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <set>
#include <algorithm>
#include <cmath>

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

    void setNodeText(const std::string& t) {
        if (!editor_) return;
        Node* sn = editor_->selected();
        if (sn && std::string(sn->typeName()) == "Label")
            static_cast<Label*>(sn)->text = t;
    }

    // PAN: двухпальцевый обзор превью. phase 1=down 2=move 3=up
    void feedMultiTouch(int phase, float x0, float y0, float x1, float y1) {
        if (appMode_ != AppMode::Editor || showCreate_ || showBg_) return;
        Scene* es = editor_ ? editor_->scene() : nullptr;
        if (!es) return;
        const float S = 0.46875f;
        float mx = (x0 + x1) / 2.0f, my = (y0 + y1) / 2.0f;
        if (phase == 1) { pinching_ = true; pinchMX_ = mx; pinchMY_ = my; return; }
        if (phase == 3) { pinching_ = false; return; }
        if (!pinching_) return;
        es->camX += (mx - pinchMX_) / S;            // тянем карту за середину двух пальцев
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
        if (appMode_ == AppMode::Editor && !showCreate_ && !showBg_) {
            const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
            const float CX = VX0 + VW / 2, CY = VY0 + VH / 2, S = 0.46875f;
            bool inVP = (x >= VX0 && x <= VX0 + VW && y >= VY0 && y <= VY0 + VH);
            Scene* es = editor_ ? editor_->scene() : nullptr;
            if (t.action == RawTouch::Action::Down && inVP && es && es->root) {
                float wx = 640 + es->camX + (x - CX) / S;   // PAN: учёт смещения обзора
                float wy = 360 + es->camY + (y - CY) / S;
                std::string hit = hitTest(es->root.get(), wx, wy);
                if (!hit.empty()) { editor_->select(hit); dragNode_ = editor_->find2d(hit); dragging_ = (dragNode_ != nullptr); }
            } else if (t.action == RawTouch::Action::Move && dragging_ && dragNode_ && es) {
                dragNode_->position.x = 640 + es->camX + (x - CX) / S;
                dragNode_->position.y = 360 + es->camY + (y - CY) / S;
            } else if (t.action == RawTouch::Action::Up) { dragging_ = false; dragNode_ = nullptr; }
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
        project_ = pi; fontPath_ = pi.rootPath + "/" + pi.defaultFont;
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

    void processUi() {
        Scene* sc = sceneMgr_->current(); if (!sc) return;
        const std::string pRestart = "restart_scene:", pChange = "change_scene:", pAdd = "add_var:", pSet = "set_var:", pHub = "hub:";
        for (auto& b : sc->ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action.rfind(pHub, 0) == 0) { appMode_ = AppMode::Hub; rebuildHub(); return; }
            else if (b.action.rfind(pRestart, 0) == 0) sceneMgr_->requestChange(b.action.substr(pRestart.size()), true);
            else if (b.action.rfind(pChange, 0) == 0)  sceneMgr_->requestChange(b.action.substr(pChange.size()), false);
            else if (b.action.rfind(pAdd, 0) == 0 || b.action.rfind(pSet, 0) == 0) {
                bool isAdd = b.action.rfind(pAdd, 0) == 0;
                std::string rest = b.action.substr(isAdd ? pAdd.size() : pSet.size());
                size_t c = rest.find(':');
                if (c != std::string::npos) { std::string name = rest.substr(0, c); double v = atof(rest.substr(c + 1).c_str()); if (isAdd) ctx_.vars[name] += v; else ctx_.vars[name] = v; }
            }
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
        collectHit(n, wx, wy, bestBox, bestNear, bestDist);
        if (!bestBox.empty()) return bestBox;
        return bestNear;
    }
    void collectHit(const Node* n, float wx, float wy, std::string& bestBox, std::string& bestNear, float& bestDist) const {
        if (!n) return;
        std::string tn = std::string(n->typeName());
        if (tn != "Node" && tn != "Camera2D" && n->name.rfind("__", 0) != 0) {
            const Node2D* d = static_cast<const Node2D*>(n);
            float hw = d->w > 0 ? d->w / 2 : 24; if (hw < 28) hw = 28;
            float hh = d->h > 0 ? d->h / 2 : 24; if (hh < 28) hh = 28;
            if (wx >= d->position.x - hw && wx <= d->position.x + hw &&
                wy >= d->position.y - hh && wy <= d->position.y + hh) {
                if (bestBox.empty()) bestBox = d->name;
            }
            float dx = wx - d->position.x, dy = wy - d->position.y;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist < 45.0f && dist < bestDist) { bestDist = dist; bestNear = d->name; }
        }
        for (const auto& ch : n->getChildren()) collectHit(ch.get(), wx, wy, bestBox, bestNear, bestDist);
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
        project_ = pi; fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";
        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene("scenes/main.json", resources_))
            if (!sceneMgr_->restartScene(pi.mainScene, resources_)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        editor_ = std::make_unique<Editor>(); editor_->attach(sceneMgr_->current());
        showCreate_ = false; showBg_ = false; pendingText_ = false; fsPath_ = ""; manip_ = Manip::Move; pinching_ = false;
        buildEditorPanels(); input_.setUi(&editorScene_.ui); touch_.resetJoystick(); appMode_ = AppMode::Editor; return true;
    }

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

        std::vector<HierRow> hier;
        if (editor_ && editor_->scene() && editor_->scene()->root) collectHier(*editor_->scene()->root, 0, hier);
        Node* selNode = editor_ ? editor_->selected() : nullptr; std::string sel = selNode ? selNode->name : std::string{};
        for (size_t i = 0; i < hier.size(); ++i) {
            if (i >= 10) { addLbl("HierMore", "  ...", 8, 64 + (float)i * 30, 16, th.ink); break; }
            std::string pad(hier[i].depth * 2, ' '); UiButton b; b.touch.id = "h" + std::to_string(i);
            b.touch.rect = Rect{8, 64 + (float)i * 30, 284, 28}; b.text = pad + hier[i].name + "   " + hier[i].type; b.action = "ed_select:" + hier[i].name;
            b.color = (sel == hier[i].name) ? GODOT_ORANGE : th.button; editorScene_.ui.push_back(b);
        }

        Node2D* s = (editor_ && !sel.empty()) ? editor_->find2d(sel) : nullptr;
        if (s) {
            addLbl("InName", s->name, 900, 64, 22, GODOT_ORANGE); addLbl("InType", std::string(s->typeName()), 900, 92, 16, th.ink);
            addLbl("InTrHdr", "Transform", 900, 124, 18, th.ink);
            addLbl("InPos", "Position  (" + std::to_string((int)s->position.x) + ", " + std::to_string((int)s->position.y) + ")", 900, 150, 16, th.ink);
            addLbl("InRot", "Rotation  " + std::to_string((int)(s->rotation * 57.2957795f)), 900, 174, 16, th.ink);
            addLbl("InScl", "Scale  (" + std::to_string((int)(s->scale.x*100)) + "%, " + std::to_string((int)(s->scale.y*100)) + "%)", 900, 198, 16, th.ink);
            addLbl("InShp", "Shape  " + s->shape, 900, 222, 16, th.ink);
            addLbl("InCol", "Color  " + colorToHex(s->color), 900, 246, 16, th.ink);
            if (std::string(s->typeName()) == "Label") addLbl("InText", "Text: " + static_cast<Label*>(s)->text, 900, 270, 16, th.ink);
            if (scripted_.count(sel)) addLbl("InScript", "script: scripts/" + sel + ".lua", 900, 294, 16, GODOT_ORANGE);
        } else addLbl("InNone", "(no selection)", 900, 92, 18, th.ink);

        std::string sb = (editor_ && editor_->scene() && editor_->scene()->bgSet()) ? editor_->scene()->bg : std::string("(theme)");
        addLbl("InBg", "scene bg: " + sb, 900, 320, 16, th.ink);
        { UiButton b; b.touch.id = "bgbtn"; b.touch.rect = Rect{900, 344, 120, 30}; b.text = "BG"; b.action = "bg_open"; b.color = th.accent; editorScene_.ui.push_back(b); }

        const char* mv[4] = { "l","u","d","r" }; const char* mvTxt[4] = { "<","^","v",">" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("mv")+std::to_string(k); b.touch.rect = Rect{900+(float)k*48, 640, 44, 32}; b.text = mvTxt[k]; b.action = std::string("ed_move:")+mv[k]; b.color = th.button; editorScene_.ui.push_back(b); }

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
            // CAM/LIGHT: 8 кнопок в полосе 300..892
            const char* ct[6] = { "Node2D","Node2D","Node2D","Node2D","Label","Sprite2D" };
            const char* cs[6] = { "square","circle","diamond","triangle","","" };
            const char* cl[6] = { "CUBE","CIRCLE","DIAMOND","TRIANGLE","TEXT","SPRITE" };
            for (int k = 0; k < 6; ++k) { UiButton b; b.touch.id = std::string("ct")+std::to_string(k); b.touch.rect = Rect{300+(float)k*74, 560, 70, 40}; b.text = cl[k]; b.action = std::string("create:")+ct[k]+":"+cs[k]; b.color = th.button; editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctcam"; b.touch.rect=Rect{300+6*74,560,70,40}; b.text="CAM"; b.action="create_cam"; b.color=th.accent; editorScene_.ui.push_back(b); }
            { UiButton b; b.touch.id="ctlit"; b.touch.rect=Rect{300+7*74,560,70,40}; b.text="LIGHT"; b.action="create_light"; b.color=parseColor("#FFD700"); editorScene_.ui.push_back(b); }
        }
        if (showBg_) {
            const char* bgs[6] = { "#FFF3E0","#111111","#16213E","#2EC4B6","#D62828","#87CEEB" };
            for (int k = 0; k < 6; ++k) { UiButton b; b.touch.id = std::string("bgsw")+std::to_string(k); b.touch.rect = Rect{300+(float)k*84, 560, 80, 40}; b.text = ""; b.action = std::string("bg_set:")+bgs[k]; b.color = parseColor(bgs[k]); editorScene_.ui.push_back(b); }
            UiButton cl; cl.touch.id = "bgclr"; cl.touch.rect = Rect{300+6*84, 560, 80, 40}; cl.text = "CLR"; cl.action = "bg_clear"; cl.color = th.button; editorScene_.ui.push_back(cl);
        }

        addLbl("FsHdr", "FILES", 10, 384, 18, th.ink);
        std::string shown = fsPath_.empty() ? std::string("res/") : ("res/" + fsPath_);
        addLbl("FsPath", shown, 10, 406, 15, GODOT_ORANGE);
        float fy = 428; const float STEP = 28; int rows = 0; const int MAXROWS = 8;
        if (!fsPath_.empty()) {
            UiButton up; up.touch.id = "fsup"; up.touch.rect = Rect{8, fy, 284, STEP-2}; up.text = ".."; up.action = "fs_up"; up.color = th.button; editorScene_.ui.push_back(up); fy += STEP; ++rows;
        }
        std::string abs = project_.rootPath + "/" + fsPath_;
        std::vector<FileEntry> items = FileBrowser::list(abs);
        for (const auto& it : items) {
            if (rows >= MAXROWS) { addLbl("FsMore", "  ...", 10, fy, 15, th.ink); break; }
            std::string rel = fsPath_ + it.name;
            if (it.isDir) {
                UiButton b; b.touch.id = "fsd" + std::to_string(rows); b.touch.rect = Rect{8, fy, 284, STEP-2};
                b.text = "/ " + it.name; b.action = "fs_enter:" + it.name; b.color = th.button; editorScene_.ui.push_back(b);
            } else if (it.name.size() > 5 && it.name.compare(it.name.size()-5, 5, ".json") == 0) {
                UiButton b; b.touch.id = "fsf" + std::to_string(rows); b.touch.rect = Rect{8, fy, 284, STEP-2};
                b.text = "  " + it.name; b.action = "fs_open:" + rel; b.color = th.accent; editorScene_.ui.push_back(b);
            } else {
                addLbl(("fsx"+std::to_string(rows)).c_str(), "  " + it.name, 10, fy+4, 15, th.ink);
            }
            fy += STEP; ++rows;
        }
    }

    void processEditorActions() {
        if (!editor_) return;
        Node* sn = editor_->selected(); std::string sel = sn ? sn->name : std::string{}; bool changed = false;
        for (auto& b : editorScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action == "fs_up") {
                std::string tmp = fsPath_;
                while (!tmp.empty() && tmp.back() == '/') tmp.pop_back();
                size_t sl = tmp.find_last_of('/');
                fsPath_ = (sl == std::string::npos) ? std::string("") : tmp.substr(0, sl + 1);
                changed = true;
            }
            else if (b.action.rfind("fs_enter:", 0) == 0) { fsPath_ += b.action.substr(9) + "/"; changed = true; }
            else if (b.action.rfind("fs_open:", 0) == 0) {
                std::string rel = b.action.substr(8);
                if (sceneMgr_->restartScene(rel, resources_)) {
                    editor_->attach(sceneMgr_->current());
                    scripted_.clear(); showCreate_ = false; showBg_ = false; dragging_ = false; dragNode_ = nullptr; pinching_ = false;
                    changed = true;
                }
            }
            else if (b.action == "manip:move")   { manip_ = Manip::Move;   changed = true; }
            else if (b.action == "manip:rotate") { manip_ = Manip::Rotate; changed = true; }
            else if (b.action == "manip:scale")  { manip_ = Manip::Scale;  changed = true; }
            else if (b.action == "create_open") { showCreate_ = !showCreate_; showBg_ = false; changed = true; }
            else if (b.action == "bg_open") { showBg_ = !showBg_; showCreate_ = false; changed = true; }
            else if (b.action.rfind("bg_set:", 0) == 0) { if (editor_->scene()) editor_->scene()->bg = b.action.substr(7); showBg_ = false; changed = true; }
            else if (b.action == "bg_clear") { if (editor_->scene()) editor_->scene()->bg.clear(); showBg_ = false; changed = true; }
            else if (b.action == "edit_text") { if (sn && std::string(sn->typeName()) == "Label") { pendingText_ = true; pendingTextCur_ = static_cast<Label*>(sn)->text; } }
            else if (b.action == "ed_scr") { if (!sel.empty()) { attachScript(sel); changed = true; } }
            else if (b.action == "create_cam") {                       // CAM: реальная нода Camera2D
                std::string name = "Cam" + std::to_string(createCounter_++);
                editor_->addNode("Camera2D", name, 640, 360); editor_->select(name); showCreate_ = false; changed = true;
            }
            else if (b.action == "create_light") {                     // LIGHT: жёлтый круг-маркер (см. оговорку)
                std::string name = "Light" + std::to_string(createCounter_++);
                editor_->addNode("Node2D", name, 640, 360); editor_->setShape(name, "circle"); editor_->setColor(name, "#FFD700"); editor_->select(name); showCreate_ = false; changed = true;
            }
            else if (b.action.rfind("create:", 0) == 0) {
                std::string rest = b.action.substr(7); size_t c = rest.find(':');
                std::string type = rest.substr(0, c); std::string shape = (c == std::string::npos) ? "" : rest.substr(c + 1);
                std::string name = "Obj" + std::to_string(createCounter_++);
                editor_->addNode(type, name, 640, 360); if (!shape.empty()) editor_->setShape(name, shape); editor_->select(name); showCreate_ = false; changed = true;
            }
            else if (b.action.rfind("ed_select:", 0) == 0) { editor_->select(b.action.substr(10)); changed = true; }
            else if (b.action.rfind("ed_shape:", 0) == 0) { if (!sel.empty()) { editor_->setShape(sel, b.action.substr(9)); changed = true; } }
            else if (b.action.rfind("ed_color:", 0) == 0) { if (!sel.empty()) { editor_->setColor(sel, b.action.substr(9)); changed = true; } }
            else if (b.action.rfind("ed_move:", 0) == 0) {
                std::string d = b.action.substr(8);
                Node2D* n = (!sel.empty()) ? editor_->find2d(sel) : nullptr;
                if (manip_ == Manip::Move) {
                    float dx = (d=="l")?-16:(d=="r")?16:0;
                    float dy = (d=="u")?-16:(d=="d")?16:0;
                    editor_->moveSelected(dx, dy);
                } else if (manip_ == Manip::Rotate && n) {
                    float dr = (d=="l")?-15.0f:(d=="r")?15.0f:0.0f;
                    n->rotation += dr * 3.14159265f / 180.0f;
                } else if (manip_ == Manip::Scale && n) {
                    float f = (d=="u")?1.1f:(d=="d")?(1.0f/1.1f):1.0f;
                    n->scale.x *= f; n->scale.y *= f;
                }
                changed = true;
            }
            else if (b.action == "ed_del") { if (!sel.empty()) { editor_->deleteNode(sel); changed = true; } }
            else if (b.action == "ed_save") { editor_->save(project_.rootPath + "/scenes/main.json"); }
            else if (b.action == "ed_back") { appMode_ = AppMode::Hub; rebuildHub(); return; }
        }
        if (changed) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    void emitViewport(const Scene& sc, std::string& out) {
        const float VX0 = 300, VY0 = 64, VW = 592, VH = 492; const float CX = VX0 + VW/2, CY = VY0 + VH/2, S = 0.46875f;
        if (sc.bgSet()) out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|" + sc.bg + "\n";
        out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|#23232B\n";
        for (int gx = 0; gx <= 1280; gx += 64) { float px = CX + ((float)gx - 640 - sc.camX)*S; if (px < VX0 || px > VX0+VW) continue; out += "DRAW rect|" + std::to_string((int)px) + "|" + std::to_string((int)VY0) + "|1|" + std::to_string((int)VH) + "|#33333D\n"; }   // PAN: сетка едет с камерой
        for (int gy = 0; gy <= 720; gy += 64) { float py = CY + ((float)gy - 360 - sc.camY)*S; if (py < VY0 || py > VY0+VH) continue; out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)py) + "|" + std::to_string((int)VW) + "|1|#33333D\n"; }
        emitNodePreview(sc.root.get(), CX, CY, S, VX0, VY0, VW, VH, sc.camX, sc.camY, out);
    }
    void emitNodePreview(const Node* n, float CX, float CY, float S, float VX0, float VY0, float VW, float VH, float camX, float camY, std::string& out) {
        if (!n) return;
        std::string tn = std::string(n->typeName());
        if (tn == "Camera2D") {                                          // CAM: маркер камеры в превью
            const Node2D* d = static_cast<const Node2D*>(n);
            float cx = CX + (d->position.x - camX - 640)*S, cy = CY + (d->position.y - camY - 360)*S;
            if (cx >= VX0 && cx <= VX0+VW && cy >= VY0 && cy <= VY0+VH) {
                out += "DRAW rect|" + std::to_string((int)(cx-14)) + "|" + std::to_string((int)(cy-10)) + "|28|20|#FFD700\n";
                out += "DRAW text|CAM|" + std::to_string((int)(cx-12)) + "|" + std::to_string((int)(cy+12)) + "|12|#FFD700\n";
            }
        }
        else if (tn != "Node") {
            const Node2D* d = static_cast<const Node2D*>(n);
            float cx = CX + (d->position.x - camX - 640)*S, cy = CY + (d->position.y - camY - 360)*S;   // PAN: смещение обзора
            float w = d->w*S, h = d->h*S, rx = cx - w/2, ry = cy - h/2;
            bool vis = (rx >= VX0 && ry >= VY0 && rx + w <= VX0 + VW && ry + h <= VY0 + VH);
            if (vis) {
                if (tn == "Label") out += "DRAW text|" + static_cast<const Label*>(d)->text + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|14|" + colorToHex(d->color) + "\n";
                else if (tn == "Sprite2D") out += "DRAW rect|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|#555555\n";
                else if (d->hasAppearance()) out += "DRAW shape|" + d->shape + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|" + colorToHex(d->color) + "\n";
            }
        }
        for (const auto& ch : n->getChildren()) emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, camX, camY, out);
    }

    std::string stepEditor() {
        if (!editor_ || !editor_->scene()) { appMode_ = AppMode::Hub; rebuildHub(); return ""; }
        gameBackend_.begin(); Renderer gr(gameBackend_); gr.render(editorScene_, &ctx_); std::string out = gameBackend_.str();
        emitViewport(*editor_->scene(), out);
        processEditorActions();
        if (pendingText_ && appMode_ == AppMode::Editor) { out += "REQ_TEXT|" + pendingTextCur_ + "\n"; pendingText_ = false; }
        input_.endFrame(); return out;
    }

    AppMode appMode_ = AppMode::Hub;
    HubState hubState_; Scene hubScene_; Scene editorScene_; std::unique_ptr<Editor> editor_;
    ScriptSystem scripts_; std::set<std::string> scripted_;
    Manip manip_ = Manip::Move;
    bool showCreate_ = false, showBg_ = false, dragging_ = false, pendingText_ = false, pinching_ = false;   // PAN
    float pinchMX_ = 0, pinchMY_ = 0;                                                                          // PAN
    Node2D* dragNode_ = nullptr; int createCounter_ = 0; std::string pendingTextCur_;
    std::string fsPath_;
    ProjectInfo project_; std::string fontPath_; ResourceManager resources_; std::unique_ptr<SceneManager> sceneMgr_; InputManager input_; TouchProcessor touch_; StringRenderBackend gameBackend_; Context ctx_;
};

} // namespace suka
