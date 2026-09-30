#pragma once

#include <string>
#include <memory>

#include "Core.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "Render.hpp"
#include "Touch.hpp"
#include "Input.hpp"
#include "Resources.hpp"
#include "Settings.hpp"

namespace suka {

class GameApp {
public:
    enum class Mode { Console, String };

    bool init(Mode mode, const std::string& gameDir) {
        mode_ = mode;
        loadSettings();

        std::string projectPath = PROJECT_ROOT + "/projects/" + gameDir + "/project.json";
        if (!ProjectLoader::load(projectPath, project_)) return false;

        fontPath_ = project_.rootPath + "/" + project_.defaultFont;
        if (!fileExists(fontPath_)) {
            fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";
        }

        sceneMgr_ = std::make_unique<SceneManager>(project_.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(project_.mainScene, resources_)) return false;

        input_.screenWidth = 720.0f;
        input_.screenHeight = 1280.0f;
        input_.setUi(&sceneMgr_->current()->ui);

        return true;
    }

    void feedTouch(int action, float x, float y) {
        RawTouch t;
        if (action == 0) t.action = RawTouch::Action::Down;
        else if (action == 2) t.action = RawTouch::Action::Move;
        else t.action = RawTouch::Action::Up;
        t.x = x;
        t.y = y;
        if (sceneMgr_ && sceneMgr_->current()) {
            touch_.onTouch(t, *sceneMgr_->current(), input_);
        }
    }

    std::string stepFrame() {
        if (!sceneMgr_ || !sceneMgr_->current()) return "";

        ctx_.coinCollectedThisFrame = false;
        ctx_.jumpPressedThisFrame = false;

        ctx_.input = input_.state();

        processUi();
        sceneMgr_->update(ctx_, 1.0 / 60.0, input_, resources_);

        std::string out;
        if (ctx_.coinCollectedThisFrame) out += "SOUND coin\n";
        if (ctx_.jumpPressedThisFrame)   out += "SOUND jump\n";

        strBackend_.begin();
        Renderer renderer(strBackend_);
        renderer.render(*sceneMgr_->current());
        out += strBackend_.str();

        input_.endFrame();
        return out;
    }

    SceneManager& sceneMgr() { return *sceneMgr_; }
    ProjectInfo& project() { return project_; }

private:
    void processUi() {
        Scene* sc = sceneMgr_->current();
        if (!sc) return;

        const std::string prefixChange = "change_scene:";
        const std::string prefixRestart = "restart_scene:";

        for (auto& b : sc->ui) {
            if (b.touch.pressEdge && !b.action.empty()) {
                if (b.action.rfind(prefixRestart, 0) == 0) {
                    sceneMgr_->requestChange(b.action.substr(prefixRestart.size()), true);
                }
                else if (b.action.rfind(prefixChange, 0) == 0) {
                    sceneMgr_->requestChange(b.action.substr(prefixChange.size()), false);
                }
            }
        }
    }

    Mode mode_ = Mode::String;
    ProjectInfo project_;
    std::string fontPath_;
    ResourceManager resources_;
    std::unique_ptr<SceneManager> sceneMgr_;
    InputManager input_;
    TouchProcessor touch_;
    StringRenderBackend strBackend_;
    Context ctx_;
};

} // namespace suka