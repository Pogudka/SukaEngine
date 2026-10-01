#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>

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

namespace suka {

class GameApp {
public:
    enum class Mode { Console, String };
    enum class AppMode { Hub, Game, Editor };

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

    // вызывается из JNI, когда пользователь ввёл текст в диалоге
    void setNodeText(const std::string& t) {
        if (!editor_) return;
        Node* sn = editor_->selected();
        if (sn && std::string(sn->typeName()) == "Label")
            static_cast<Label*>(sn)->text = t;
    }

    void feedTouch(int action, float x, float y) {
        RawTouch t;
        if (action == 0) t.action = RawTouch::Action::Down;
        else if (action == 2) t.action = RawTouch::Action::Move;
        else t.action = RawTouch::Action::Up;
        t.x = x; t.y = y;
        Scene* cur = uiScene();
        if (!cur) return;
        if (appMode_ == AppMode::Editor && !showCreate_) {
            const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
            const float CX = VX0 + VW / 2, CY = VY0 + VH / 2, S = 0.46875f;
            bool inVP = (x >= VX0 && x <= VX0 + VW && y >= VY0 && y <= VY0 + VH);
            if (t.action == RawTouch::Action::Down && inVP) {
                float wx = 640 + (x - CX) / S, wy = 360 + (y - CY) / S;
                std::string hit = hitTest(cur->root.get(), wx, wy);
                if (!hit.empty()) { editor_->select(hit); dragNode_ = editor_->find2d(hit); dragging_ = (dragNode_ != nullptr); }
            } else if (t.action == RawTouch::Action::Move && dragging_ && dragNode_) {
                dragNode_->position.x = 640 + (x - CX) / S;
                dragNode_->position.y = 360 + (y - CY) / S;
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
        std::string tn = std::string(n->typeName());
        if (tn != "Node" && tn != "Camera2D") {
            const Node2D* d = static_cast<const Node2D*>(n);
            float hw = d->w > 0 ? d->w / 2 : 24, hh = d->h > 0 ? d->h / 2 : 24;
            if (wx >= d->position.x - hw && wx <= d->position.x + hw && wy >= d->position.y - hh && wy <= d->position.y + hh) return d->name;
        }
        for (const auto& ch : n->getChildren()) { std::string h = hitTest(ch.get(), wx, wy); if (!h.empty()) return h; }
        return "";
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        project_ = pi; fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";
        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
        editor_ = std::make_unique<Editor>(); editor_->attach(sceneMgr_->current());
        showCreate_ = false; pendingText_ = false;
        buildEditorPanels(); input_.setUi(&editorScene_.ui); touch_.resetJoystick(); appMode_ = AppMode::Editor; return true;
    }

    void buildEditorPanels() {
        Theme& th = currentTheme(); const unsigned GODOT_ORANGE = 0xFF8800FFu;
        editorScene_ = Scene(); editorScene_.name = "Editor"; editorScene_.root = std::make_unique<Node>(); editorScene_.root->name = "EdRoot";
        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
            auto l = std::make_unique<Label>(); l->name = nm; l->text = txt; l->fontSize = fs; l->color = col; l->position = Vec2{x, y}; editorScene_.root->addChild(std::move(l));
        };
        auto fsBg = std::make_unique<Node2D>(); fsBg->name = "FsBg"; fsBg->shape = "square"; fsBg->color = 0xFF0E0E16u; fsBg->w = 280; fsBg->h = 130; fsBg->position = Vec2{150, 623}; editorScene_.root->addChild(std::move(fsBg));
        addLbl("TabScene", "Scene", 20, 8, 20, GODOT_ORANGE); addLbl("Tab2D", "2D", 110, 8, 20, th.ink); addLbl("Tab3D", "3D", 160, 8, 20, th.ink); addLbl("TabScr", "Script", 210, 8, 20, th.ink); addLbl("TabAss", "AssetLib", 300, 8, 20, th.ink);
        addLbl("DHdr", "Scene", 10, 40, 18, th.ink); addLbl("IHdr", "Inspector", 900, 40, 18, th.ink);
        std::vector<HierRow> hier;
        if (editor_ && editor_->scene() && editor_->scene()->root) collectHier(*editor_->scene()->root, 0, hier);
        Node* selNode = editor_ ? editor_->selected() : nullptr; std::string sel = selNode ? selNode->name : std::string{};
        for (size_t i = 0; i < hier.size(); ++i) {
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
        } else addLbl("InNone", "(no selection)", 900, 92, 18, th.ink);

        const char* mv[4] = { "l","u","d","r" }; const char* mvTxt[4] = { "<","^","v",">" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("mv")+std::to_string(k); b.touch.rect = Rect{900+(float)k*48, 300, 44, 32}; b.text = mvTxt[k]; b.action = std::string("ed_move:")+mv[k]; b.color = th.button; editorScene_.ui.push_back(b); }
        const char* shapes[4] = { "square","circle","diamond","triangle" }; const char* shTxt[4] = { "SQ","CI","DI","TR" };
        for (int k = 0; k < 4; ++k) { UiButton b; b.touch.id = std::string("sh")+std::to_string(k); b.touch.rect = Rect{300+(float)k*56, 34, 52, 26}; b.text = shTxt[k]; b.action = std::string("ed_shape:")+shapes[k]; b.color = th.button; editorScene_.ui.push_back(b); }
        const char* cols[3] = { "#D62828","#2EC4B6","#F4EDE4" };
        for (int k = 0; k < 3; ++k) { UiButton b; b.touch.id = std::string("col")+std::to_string(k); b.touch.rect = Rect{524+(float)k*56, 34, 52, 26}; b.text = ""; b.action = std::string("ed_color:")+cols[k]; b.color = parseColor(cols[k]); editorScene_.ui.push_back(b); }
        UiButton del; del.touch.id="del"; del.touch.rect=Rect{692,34,52,26}; del.text="DEL"; del.action="ed_del"; del.color=parseColor("#D62828"); editorScene_.ui.push_back(del);
        UiButton save; save.touch.id="save"; save.touch.rect=Rect{748,34,60,26}; save.text="SAVE"; save.action="ed_save"; save.color=parseColor("#2E7D32"); editorScene_.ui.push_back(save);
        UiButton back; back.touch.id="eback"; back.touch.rect=Rect{812,34,44,26}; back.text="<"; back.action="ed_back"; back.color=GODOT_ORANGE; editorScene_.ui.push_back(back);
        UiButton plus; plus.touch.id="plus"; plus.touch.rect=Rect{860,34,36,26}; plus.text="+"; plus.action="create_open"; plus.color=GODOT_ORANGE; editorScene_.ui.push_back(plus);
        UiButton txt; txt.touch.id="txt"; txt.touch.rect=Rect{900,34,44,26}; txt.text="TXT"; txt.action="edit_text"; txt.color=th.button; editorScene_.ui.push_back(txt);

        if (showCreate_) {
            const char* ct[6] = { "Node2D","Node2D","Node2D","Node2D","Label","Sprite2D" };
            const char* cs[6] = { "square","circle","diamond","triangle","","" };
            const char* cl[6] = { "CUBE","CIRCLE","DIAMOND","TRIANGLE","TEXT","SPRITE" };
            for (int k = 0; k < 6; ++k) { UiButton b; b.touch.id = std::string("ct")+std::to_string(k); b.touch.rect = Rect{470, 150+(float)k*50, 240, 44}; b.text = cl[k]; b.action = std::string("create:")+ct[k]+":"+cs[k]; b.color = th.button; editorScene_.ui.push_back(b); }
        }
        addLbl("FsHdr", "FileSystem", 10, 566, 18, th.ink); addLbl("Fs1", "res/", 10, 592, 16, th.ink); addLbl("Fs2", "  scenes/", 10, 614, 16, th.ink); addLbl("Fs3", "  assets/", 10, 636, 16, th.ink); addLbl("Fs4", "  scripts/", 10, 658, 16, th.ink);
    }

    void processEditorActions() {
        if (!editor_) return;
        Node* sn = editor_->selected(); std::string sel = sn ? sn->name : std::string{}; bool changed = false;
        for (auto& b : editorScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action == "create_open") { showCreate_ = !showCreate_; changed = true; }
            else if (b.action == "edit_text") {
                if (sn && std::string(sn->typeName()) == "Label") { pendingText_ = true; pendingTextCur_ = static_cast<Label*>(sn)->text; }
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
            else if (b.action.rfind("ed_move:", 0) == 0) { std::string d = b.action.substr(8); float dx = (d=="l")?-16:(d=="r")?16:0; float dy = (d=="u")?-16:(d=="d")?16:0; editor_->moveSelected(dx, dy); changed = true; }
            else if (b.action == "ed_del") { if (!sel.empty()) { editor_->deleteNode(sel); changed = true; } }
            else if (b.action == "ed_save") { editor_->save(project_.rootPath + "/" + project_.mainScene); }
            else if (b.action == "ed_back") { appMode_ = AppMode::Hub; rebuildHub(); return; }
        }
        if (changed) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    void emitViewport(const Scene& sc, std::string& out) {
        const float VX0 = 300, VY0 = 64, VW = 592, VH = 492; const float CX = VX0 + VW/2, CY = VY0 + VH/2, S = 0.46875f;
        out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)VY0) + "|" + std::to_string((int)VW) + "|" + std::to_string((int)VH) + "|#23232B\n";
        for (int gx = 0; gx <= 1280; gx += 64) { float px = CX + ((float)gx - 640)*S; if (px < VX0 || px > VX0+VW) continue; out += "DRAW rect|" + std::to_string((int)px) + "|" + std::to_string((int)VY0) + "|1|" + std::to_string((int)VH) + "|#33333D\n"; }
        for (int gy = 0; gy <= 720; gy += 64) { float py = CY + ((float)gy - 360)*S; if (py < VY0 || py > VY0+VH) continue; out += "DRAW rect|" + std::to_string((int)VX0) + "|" + std::to_string((int)py) + "|" + std::to_string((int)VW) + "|1|#33333D\n"; }
        emitNodePreview(sc.root.get(), CX, CY, S, VX0, VY0, VW, VH, out);
    }

    void emitNodePreview(const Node* n, float CX, float CY, float S, float VX0, float VY0, float VW, float VH, std::string& out) {
        if (!n) return;
        std::string tn = std::string(n->typeName());
        if (tn != "Node" && tn != "Camera2D") {
            const Node2D* d = static_cast<const Node2D*>(n);
            float cx = CX + (d->position.x - 640)*S, cy = CY + (d->position.y - 360)*S, w = d->w*S, h = d->h*S, rx = cx - w/2, ry = cy - h/2;
            bool vis = !(rx+w < VX0 || rx > VX0+VW || ry+h < VY0 || ry > VY0+VH);
            if (vis) {
                if (tn == "Label") out += "DRAW text|" + static_cast<const Label*>(d)->text + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|14|" + colorToHex(d->color) + "\n";
                else if (tn == "Sprite2D") out += "DRAW rect|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|#555555\n";
                else if (d->hasAppearance()) out += "DRAW shape|" + d->shape + "|" + std::to_string((int)rx) + "|" + std::to_string((int)ry) + "|" + std::to_string((int)w) + "|" + std::to_string((int)h) + "|" + colorToHex(d->color) + "\n";
            }
        }
        for (const auto& ch : n->getChildren()) emitNodePreview(ch.get(), CX, CY, S, VX0, VY0, VW, VH, out);
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
    bool showCreate_ = false, dragging_ = false, pendingText_ = false; Node2D* dragNode_ = nullptr; int createCounter_ = 0; std::string pendingTextCur_;
    ProjectInfo project_; std::string fontPath_; ResourceManager resources_; std::unique_ptr<SceneManager> sceneMgr_; InputManager input_; TouchProcessor touch_; StringRenderBackend gameBackend_; Context ctx_;
};

} // namespace suka
