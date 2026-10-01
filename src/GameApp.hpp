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

    void feedTouch(int action, float x, float y) {
        RawTouch t;
        if (action == 0) t.action = RawTouch::Action::Down;
        else if (action == 2) t.action = RawTouch::Action::Move;
        else t.action = RawTouch::Action::Up;
        t.x = x; t.y = y;

        Scene* cur = uiScene();
        if (cur) touch_.onTouch(t, *cur, input_);
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
    void rebuildHub() {
        hubState_.games = ProjectList::scan();
        hubScene_ = buildHubLandscape();
        input_.setUi(&hubScene_.ui);
    }

    Scene buildHubLandscape() {
        Theme& th = currentTheme();
        Scene s;
        s.name = "Hub";
        s.root = std::make_unique<Node>();
        s.root->name = "Hub";

        auto hdr = std::make_unique<Label>();
        hdr->name = "ProjHdr"; hdr->text = "PROJECTS";
        hdr->fontSize = 26; hdr->color = th.ink; hdr->position = Vec2{14, 40};
        s.root->addChild(std::move(hdr));

        for (size_t i = 0; i < hubState_.games.size(); ++i) {
            const auto& g = hubState_.games[i];
            UiButton row;
            row.touch.id = "sel_" + g.dir;
            row.touch.rect = Rect{10, 90 + (float)i * 80, 300, 70};
            row.text = (hubState_.selectedDir == g.dir ? "* " : "  ") + g.dir;
            row.action = "sel:" + g.dir;
            row.color = (hubState_.selectedDir == g.dir) ? th.accent : th.button;
            s.ui.push_back(row);
        }

        if (!hubState_.selectedDir.empty()) {
            ProjectInfo info;
            ProjectLoader::load(PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/project.json", info);

            auto nm = std::make_unique<Label>();
            nm->name = "SelName"; nm->text = info.name;
            nm->fontSize = 40; nm->color = th.ink; nm->position = Vec2{760, 120};
            s.root->addChild(std::move(nm));

            auto sc = std::make_unique<Label>();
            sc->name = "SelScene"; sc->text = "scene: " + info.mainScene;
            sc->fontSize = 22; sc->color = th.ink; sc->position = Vec2{760, 180};
            s.root->addChild(std::move(sc));

            UiButton play; play.touch.id = "play";
            play.touch.rect = Rect{820, 300, 200, 110}; play.text = "Play";
            play.action = "play:" + hubState_.selectedDir; play.color = th.accent;
            s.ui.push_back(play);

            UiButton edit; edit.touch.id = "edit";
            edit.touch.rect = Rect{1040, 300, 200, 110}; edit.text = "Edit";
            edit.action = "edit:" + hubState_.selectedDir; edit.color = th.button;
            s.ui.push_back(edit);
        } else {
            auto hint = std::make_unique<Label>();
            hint->name = "Hint"; hint->text = "(select a project)";
            hint->fontSize = 28; hint->color = th.ink; hint->position = Vec2{760, 320};
            s.root->addChild(std::move(hint));
        }

        UiButton nb; nb.touch.id = "new_project";
        nb.touch.rect = Rect{820, 560, 200, 100}; nb.text = "+ NEW";
        nb.action = "new"; nb.color = th.button; s.ui.push_back(nb);

        UiButton tb; tb.touch.id = "theme";
        tb.touch.rect = Rect{1040, 560, 200, 100}; tb.text = "Theme";
        tb.action = "theme"; tb.color = th.accent; s.ui.push_back(tb);

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
        gameBackend_.begin();
        Renderer r(gameBackend_);
        r.render(hubScene_, nullptr);
        std::string out = gameBackend_.str();

        HubAct a = processHubLandscape();
        if (a.kind == 3) {
            std::string dir = nextGameDir(hubState_.games);
            ProjectCreator::createProject(dir, dir + " Game");
            hubState_.selectedDir = dir;
            rebuildHub();
        } else if (a.kind == 4) {
            hubState_.selectedDir = a.dir;
            rebuildHub();
        } else if (a.kind == 5) {
            cycleTheme(); saveSettings(); rebuildHub();
        } else if (a.kind == 1) {
            enterGame(a.dir);
        } else if (a.kind == 2) {
            enterEditor(a.dir);
        }

        input_.endFrame();
        return out;
    }

    // ---------------- GAME ----------------
    bool enterGame(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) return false;
        project_ = pi;
        fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";

        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) return false;

        input_.setUi(&sceneMgr_->current()->ui);
        touch_.resetJoystick();
        appMode_ = AppMode::Game;
        return true;
    }

    std::string stepGame() {
        if (!sceneMgr_ || !sceneMgr_->current()) return "";

        ctx_.coinCollectedThisFrame = false;
        ctx_.jumpPressedThisFrame = false;
        ctx_.input = input_.state();

        processUi();
        sceneMgr_->update(ctx_, 1.0 / 60.0, input_, resources_);

        std::string out;
        if (ctx_.coinCollectedThisFrame) out += "SOUND coin\n";
        if (ctx_.jumpPressedThisFrame)   out += "SOUND jump\n";

        gameBackend_.begin();
        Renderer renderer(gameBackend_);
        renderer.render(*sceneMgr_->current(), &ctx_);
        out += gameBackend_.str();

        input_.endFrame();
        return out;
    }

    void processUi() {
        Scene* sc = sceneMgr_->current();
        if (!sc) return;
        const std::string pRestart = "restart_scene:";
        const std::string pChange  = "change_scene:";
        const std::string pAdd     = "add_var:";
        const std::string pSet     = "set_var:";

        for (auto& b : sc->ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;
            if (b.action.rfind(pRestart, 0) == 0) {
                sceneMgr_->requestChange(b.action.substr(pRestart.size()), true);
            } else if (b.action.rfind(pChange, 0) == 0) {
                sceneMgr_->requestChange(b.action.substr(pChange.size()), false);
            } else if (b.action.rfind(pAdd, 0) == 0 || b.action.rfind(pSet, 0) == 0) {
                bool isAdd = b.action.rfind(pAdd, 0) == 0;
                std::string rest = b.action.substr(isAdd ? pAdd.size() : pSet.size());
                size_t c = rest.find(':');
                if (c != std::string::npos) {
                    std::string name = rest.substr(0, c);
                    double v = atof(rest.substr(c + 1).c_str());
                    if (isAdd) ctx_.vars[name] += v; else ctx_.vars[name] = v;
                }
            }
        }
    }

    // ---------------- EDITOR ----------------
    void collectHier(const Node& n, std::vector<std::pair<std::string,std::string>>& out) {
        out.push_back({ n.name, std::string(n.typeName()) });
        for (const auto& ch : n.getChildren()) collectHier(*ch, out);
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) {
            appMode_ = AppMode::Hub; rebuildHub(); return false;
        }
        project_ = pi;
        fontPath_ = pi.rootPath + "/" + pi.defaultFont;
        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";

        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene("scenes/main.json", resources_))
            if (!sceneMgr_->restartScene(pi.mainScene, resources_)) {
                appMode_ = AppMode::Hub; rebuildHub(); return false;
            }

        editor_ = std::make_unique<Editor>();
        editor_->attach(sceneMgr_->current());
        buildEditorPanels();
        input_.setUi(&editorScene_.ui);
        touch_.resetJoystick();
        appMode_ = AppMode::Editor;
        return true;
    }

    void buildEditorPanels() {
        Theme& th = currentTheme();
        editorScene_ = Scene();
        editorScene_.name = "Editor";
        editorScene_.root = std::make_unique<Node>();
        editorScene_.root->name = "EdRoot";

        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs) {
            auto l = std::make_unique<Label>();
            l->name = nm; l->text = txt; l->fontSize = fs; l->color = th.ink;
            l->position = Vec2{x, y};
            editorScene_.root->addChild(std::move(l));
        };

        addLbl("EHdr", "HIERARCHY", 14, 40, 24);
        addLbl("SHdr", "SCENE", 360, 40, 24);
        addLbl("IHdr", "INSPECTOR", 720, 40, 24);

        std::vector<std::pair<std::string,std::string>> hier;
        if (editor_ && editor_->scene() && editor_->scene()->root)
            collectHier(*editor_->scene()->root, hier);
        std::string sel = editor_ ? editor_->selected() : std::string();

        for (size_t i = 0; i < hier.size(); ++i) {
            UiButton b;
            b.touch.id = "h" + std::to_string(i);
            b.touch.rect = Rect{10, 80 + (float)i * 64, 300, 56};
            b.text = (sel == hier[i].first ? "* " : "  ") + hier[i].first + "  " + hier[i].second;
            b.action = "ed_select:" + hier[i].first;
            b.color = (sel == hier[i].first) ? th.accent : th.button;
            editorScene_.ui.push_back(b);
        }

        Node2D* s = (editor_ && !sel.empty()) ? editor_->find2d(sel) : nullptr;
        if (s) {
            addLbl("InName", "name: " + s->name, 720, 80, 22);
            addLbl("InType", "type: " + std::string(s->typeName()), 720, 116, 22);
            addLbl("InPos", "pos: (" + std::to_string((int)s->position.x) + ", " + std::to_string((int)s->position.y) + ")", 720, 152, 22);
            addLbl("InShape", "shape: " + s->shape, 720, 188, 22);
            addLbl("InColor", "color: " + colorToHex(s->color), 720, 224, 22);
        }

        const char* shapes[4] = { "square", "circle", "diamond", "triangle" };
        const char* shTxt[4]  = { "SQ", "CI", "DI", "TR" };
        for (int k = 0; k < 4; ++k) {
            UiButton b; b.touch.id = std::string("sh") + k;
            b.touch.rect = Rect{720 + k * 130, 300, 120, 64};
            b.text = shTxt[k]; b.action = std::string("ed_shape:") + shapes[k]; b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        const char* cols[3] = { "#D62828", "#2EC4B6", "#F4EDE4" };
        for (int k = 0; k < 3; ++k) {
            UiButton b; b.touch.id = std::string("col") + k;
            b.touch.rect = Rect{720 + k * 130, 380, 120, 64};
            b.text = ""; b.action = std::string("ed_color:") + cols[k]; b.color = parseColor(cols[k]);
            editorScene_.ui.push_back(b);
        }

        const char* mv[4] = { "l", "u", "d", "r" };
        const char* mvTxt[4] = { "<", "^", "v", ">" };
        for (int k = 0; k < 4; ++k) {
            UiButton b; b.touch.id = std::string("mv") + k;
            b.touch.rect = Rect{720 + k * 130, 460, 120, 64};
            b.text = mvTxt[k]; b.action = std::string("ed_move:") + mv[k]; b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        UiButton del; del.touch.id = "del"; del.touch.rect = Rect{720, 540, 120, 64};
        del.text = "DEL"; del.action = "ed_del"; del.color = parseColor("#D62828"); editorScene_.ui.push_back(del);

        UiButton addC; addC.touch.id = "addc"; addC.touch.rect = Rect{850, 540, 160, 64};
        addC.text = "+COIN"; addC.action = "ed_add_coin"; addC.color = th.button; editorScene_.ui.push_back(addC);

        UiButton save; save.touch.id = "save"; save.touch.rect = Rect{1030, 540, 120, 64};
        save.text = "SAVE"; save.action = "ed_save"; save.color = parseColor("#2E7D32"); editorScene_.ui.push_back(save);

        UiButton back; back.touch.id = "eback"; back.touch.rect = Rect{1170, 540, 100, 64};
        back.text = "<"; back.action = "ed_back"; back.color = th.accent; editorScene_.ui.push_back(back);
    }

    void processEditorActions() {
        if (!editor_) return;
        std::string sel = editor_->selected();
        bool changed = false;

        for (auto& b : editorScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            if (b.action.rfind("ed_select:", 0) == 0) {
                editor_->select(b.action.substr(10)); changed = true;
            }
            else if (b.action.rfind("ed_shape:", 0) == 0) {
                if (!sel.empty()) { editor_->setShape(sel, b.action.substr(9)); changed = true; }
            }
            else if (b.action.rfind("ed_color:", 0) == 0) {
                if (!sel.empty()) { editor_->setColor(sel, parseColor(b.action.substr(9))); changed = true; }
            }
            else if (b.action.rfind("ed_move:", 0) == 0) {
                std::string d = b.action.substr(8);
                float dx = (d == "l") ? -16 : (d == "r") ? 16 : 0;
                float dy = (d == "u") ? -16 : (d == "d") ? 16 : 0;
                editor_->moveSelected(dx, dy); changed = true;
            }
            else if (b.action == "ed_del") {
                if (!sel.empty()) { editor_->deleteNode(sel); changed = true; }
            }
            else if (b.action == "ed_add_coin") {
                static int cn = 100;
                editor_->addNode("Coin", "Coin" + std::to_string(cn++), 640, 360); changed = true;
            }
            else if (b.action == "ed_save") {
                editor_->save(project_.rootPath + "/scenes/main_edited.json");
            }
            else if (b.action == "ed_back") {
                appMode_ = AppMode::Hub; rebuildHub(); return;
            }
        }

        if (changed) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
    }

    std::string stepEditor() {
        if (!editor_ || !editor_->scene()) { appMode_ = AppMode::Hub; rebuildHub(); return ""; }

        previewBackend_.begin();
        Renderer pr(previewBackend_);
        pr.render(*editor_->scene(), nullptr);

        gameBackend_.begin();
        Renderer gr(gameBackend_);
        gr.render(editorScene_, &ctx_);

        std::string out = previewBackend_.str() + gameBackend_.str();
        processEditorActions();
        input_.endFrame();
        return out;
    }

    // ---------------- members ----------------
    AppMode appMode_ = AppMode::Hub;
    HubState hubState_;
    Scene hubScene_;
    Scene editorScene_;
    std::unique_ptr<Editor> editor_;

    ProjectInfo project_;
    std::string fontPath_;
    ResourceManager resources_;
    std::unique_ptr<SceneManager> sceneMgr_;
    InputManager input_;
    TouchProcessor touch_;
    StringRenderBackend gameBackend_;
    StringRenderBackend previewBackend_;
    Context ctx_;
};

} // namespace suka
