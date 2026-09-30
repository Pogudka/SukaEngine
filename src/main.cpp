#include <iostream>
#include <string>
#include <map>
#include <cstdlib>

#include "Core.hpp"
#include "Json.hpp"
#include "Settings.hpp"
#include "Input.hpp"
#include "Resources.hpp"
#include "Nodes.hpp"
#include "Scene.hpp"
#include "Project.hpp"
#include "Render.hpp"
#include "Touch.hpp"
#include "Sound.hpp"
#include "Editor.hpp"
#include "EditorUI.hpp"
#include "EditorCommands.hpp"
#include "Script.hpp"
#include "Hub.hpp"

using namespace suka;

std::string findUbuntuFont() {
    std::vector<std::string> paths = {
        PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf",
        "Ubuntu-Regular.ttf",
        "assets/fonts/Ubuntu-Regular.ttf",
        "/storage/emulated/0/SukaEngine/assets/fonts/Ubuntu-Regular.ttf",
        "/sdcard/SukaEngine/assets/fonts/Ubuntu-Regular.ttf",
        "/storage/emulated/0/Documents/SukaEngine/assets/fonts/Ubuntu-Regular.ttf"
    };

    for (const auto& path : paths) {
        if (fileExists(path)) return path;
    }
    return "";
}

void processUiActions(SceneManager& mgr, Context& ctx) {
    Scene* sc = mgr.current();
    if (!sc) return;

    const std::string prefixChange = "change_scene:";
    const std::string prefixRestart = "restart_scene:";
    const std::string prefixAdd = "add_var:";
    const std::string prefixSet = "set_var:";

    for (auto& b : sc->ui) {
        if (b.touch.pressEdge && !b.action.empty()) {
            if (b.action.rfind(prefixRestart, 0) == 0) {
                std::string target = b.action.substr(prefixRestart.size());
                std::cout << "  [UI] '" << b.touch.id << "' -> restart " << target << "\n";
                mgr.requestChange(target, true);
            }
            else if (b.action.rfind(prefixChange, 0) == 0) {
                std::string target = b.action.substr(prefixChange.size());
                std::cout << "  [UI] '" << b.touch.id << "' -> " << target << "\n";
                mgr.requestChange(target, false);
            }
            else if (b.action.rfind(prefixAdd, 0) == 0 || b.action.rfind(prefixSet, 0) == 0) {
                bool isAdd = b.action.rfind(prefixAdd, 0) == 0;
                std::string rest = b.action.substr(isAdd ? prefixAdd.size() : prefixSet.size());
                size_t c = rest.find(':');
                if (c != std::string::npos) {
                    std::string name = rest.substr(0, c);
                    double v = atof(rest.substr(c + 1).c_str());
                    if (isAdd) ctx.vars[name] += v;
                    else ctx.vars[name] = v;
                    std::cout << "  [Var] " << name << " = " << ctx.vars[name] << "\n";
                }
            }
        }
    }
}

void runGame(const std::string& dir) {
    ProjectInfo project;
    std::string projectPath = PROJECT_ROOT + "/projects/" + dir + "/project.json";

    if (!ProjectLoader::load(projectPath, project)) {
        std::cout << "[Hub] play failed: " << dir << "\n";
        return;
    }
    std::cout << "[OK] Проект загружен: " << project.name << "\n";

    std::string fontPath = project.rootPath + "/" + project.defaultFont;
    if (!fileExists(fontPath)) fontPath = findUbuntuFont();

    ResourceManager resources;
    SceneManager sceneMgr(project.rootPath, fontPath);

    if (!sceneMgr.restartScene(project.mainScene, resources)) {
        std::cout << "[NO] не удалось загрузить сцену\n";
        return;
    }

    InputManager input;
    input.screenWidth = 720.0f;
    input.screenHeight = 1280.0f;
    input.setUi(&sceneMgr.current()->ui);

    LogRenderBackend renderBackend;
    LogSoundBackend soundBackend;
    Renderer renderer(renderBackend);
    SoundManager sound(soundBackend);
    TouchProcessor touch;

    sound.load("coin", "assets/sounds/coin.wav");
    sound.load("jump", "assets/sounds/jump.wav");

    ScriptSystem scripts;
    scripts.load(project.rootPath);
    std::map<std::string, double> gameVars;

    Context ctx;
    const double dt = 1.0 / 60.0;

    for (int frame = 0; frame < 14; ++frame) {
        ctx.coinCollectedThisFrame = false;
        ctx.jumpPressedThisFrame = false;

        if (frame == 0)  touch.onTouch({RawTouch::Action::Down, 360, 760, 0},  *sceneMgr.current(), input);
        if (frame == 1)  touch.onTouch({RawTouch::Action::Up,   360, 760, 0},  *sceneMgr.current(), input);
        if (frame == 2)  touch.onTouch({RawTouch::Action::Down, 200, 900, 0},  *sceneMgr.current(), input);
        if (frame == 3)  touch.onTouch({RawTouch::Action::Move, 320, 900, 0},  *sceneMgr.current(), input);
        if (frame == 4)  touch.onTouch({RawTouch::Action::Down, 620, 1060, 1}, *sceneMgr.current(), input);
        if (frame == 5)  touch.onTouch({RawTouch::Action::Up,   620, 1060, 1}, *sceneMgr.current(), input);
        if (frame == 6)  touch.onTouch({RawTouch::Action::Down, 70,  50, 2},   *sceneMgr.current(), input);
        if (frame == 7)  touch.onTouch({RawTouch::Action::Down, 360, 750, 3},  *sceneMgr.current(), input);
        if (frame == 8)  touch.onTouch({RawTouch::Action::Up,   360, 750, 3},  *sceneMgr.current(), input);
        if (frame == 9)  touch.onTouch({RawTouch::Action::Down, 200, 900, 0},  *sceneMgr.current(), input);
        if (frame == 10) touch.onTouch({RawTouch::Action::Move, 320, 900, 0},  *sceneMgr.current(), input);

        ctx.input = input.state();

        processUiActions(sceneMgr, ctx);
        sceneMgr.update(ctx, dt, input, resources);

        Scene* sc = sceneMgr.current();
        if (sc) scripts.update(*sc, ctx, dt, sceneMgr, sound, gameVars);

        Player* player = nullptr;
        if (sc && sc->root) {
            Node* pn = sc->root->findNode("Player");
            if (pn && std::string(pn->typeName()) == "Player") {
                player = static_cast<Player*>(pn);
            }
        }

        std::cout << "frame " << frame
                  << " scene=" << (sc ? sc->name : std::string("-"));
        if (player) {
            std::cout << " player=(" << (int)player->position.x << ", " << (int)player->position.y << ")";
        }
        std::cout << "\n";

        std::cout << "  score=" << ctx.score << "\n";

        if (ctx.coinCollectedThisFrame) sound.play("coin");
        if (ctx.jumpPressedThisFrame)   sound.play("jump");

        if (sc) renderer.render(*sc, &ctx);

        input.printStatus();
        input.endFrame();
    }
}

void runEditor(const std::string& dir) {
    ProjectInfo project;
    std::string pp = PROJECT_ROOT + "/projects/" + dir + "/project.json";
    if (!ProjectLoader::load(pp, project)) {
        std::cout << "[Hub] edit failed: " << dir << "\n";
        return;
    }

    ResourceManager resources;
    std::string fontPath = project.rootPath + "/" + project.defaultFont;
    if (!fileExists(fontPath)) fontPath = findUbuntuFont();

    SceneManager sceneMgr(project.rootPath, fontPath);
    if (!sceneMgr.restartScene("scenes/main.json", resources)) {
        if (!sceneMgr.restartScene(project.mainScene, resources)) {
            std::cout << "[Hub] no scene to edit\n";
            return;
        }
    }

    Editor editor;
    editor.attach(sceneMgr.current());

    EditorUI ui;

    std::string cmdPath = PROJECT_ROOT + "/editor_commands.json";
    if (!EditorCommands::run(cmdPath, editor, project.rootPath)) {
        editor.select("Player");
    }

    ui.render(editor, project.rootPath);
}

int main() {
    std::cout << "-----------------------------\n";
    std::cout << ENGINE_NAME << " v" << ENGINE_VERSION << "\n";
    std::cout << "-----------------------------\n";

    loadSettings();
    std::cout << "[Settings] theme: " << currentTheme().name << "\n";

    TouchProcessor touch;

    HubState hs;
    hs.games = ProjectList::scan();
    Scene hub = buildHubScene(hs);
    InputManager hubInput;
    hubInput.setUi(&hub.ui);

    printHubLayout(hs);

    auto feed = [&](float x, float y) {
        touch.onTouch({RawTouch::Action::Down, x, y, 0}, hub, hubInput);
        touch.onTouch({RawTouch::Action::Up,   x, y, 0}, hub, hubInput);
        HubResult r = processHub(hub);
        hubInput.endFrame();
        return r;
    };

    auto rebuild = [&]() {
        hs.games = ProjectList::scan();
        hub = buildHubScene(hs);
        hubInput.setUi(&hub.ui);
        printHubLayout(hs);
    };

    // 1) переключить тему (кнопка Theme)
    HubResult r = feed(590, 1140);
    if (r.mode == 5) {
        cycleTheme();
        saveSettings();
        std::cout << "[Settings] theme switched to: " << currentTheme().name << "\n";
        rebuild();
    }

    // 2) + NEW
    r = feed(140, 1140);
    if (r.mode == 3) {
        std::string dir = nextGameDir(hs.games);
        ProjectCreator::createProject(dir, dir + " Game");
        rebuild();
    }

    // 3) выбрать строку 0
    r = feed(140, 240);
    if (r.mode == 4) {
        hs.selectedDir = r.dir;
        hub = buildHubScene(hs);
        hubInput.setUi(&hub.ui);
        printHubLayout(hs);
    }

    // 4) Edit
    r = feed(400, 340);
    if (r.mode == 2) {
        std::cout << "[Hub] opening editor: " << r.dir << "\n";
        runEditor(r.dir);
        printHubLayout(hs);
    }

    // 5) выбрать строку 1
    if (hs.games.size() > 1) {
        r = feed(140, 330);
        if (r.mode == 4) {
            hs.selectedDir = r.dir;
            hub = buildHubScene(hs);
            hubInput.setUi(&hub.ui);
            printHubLayout(hs);
        }
    }

    // 6) Play
    r = feed(580, 340);
    if (r.mode == 1) {
        std::cout << "[Hub] playing: " << r.dir << "\n";
        runGame(r.dir);
    }

    std::cout << "-----------------------------\n";
    std::cout << ENGINE_NAME << ": hub session finished\n";

    return 0;
}