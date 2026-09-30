#pragma once

#include <string>
#include <vector>
#include <iostream>

#include "Scene.hpp"
#include "Project.hpp"

namespace suka {

struct HubState {
    std::vector<ProjectEntry> games;
    std::string selectedDir;
};

inline Scene buildHubScene(const HubState& hs) {
    Theme& th = currentTheme();

    Scene hub;
    hub.name = "Hub";
    hub.root = std::make_unique<Node>();
    hub.root->name = "Hub";

    auto title = std::make_unique<Label>();
    title->name = "HubTitle";
    title->text = "SukaEngine Hub";
    title->fontSize = 40;
    title->color = th.ink;
    title->position = Vec2{200, 60};
    hub.root->addChild(std::move(title));

    auto col = std::make_unique<Label>();
    col->name = "ProjectsHeader";
    col->text = "PROJECTS";
    col->fontSize = 24;
    col->color = th.ink;
    col->position = Vec2{30, 150};
    hub.root->addChild(std::move(col));

    for (size_t i = 0; i < hs.games.size(); ++i) {
        const auto& g = hs.games[i];
        UiButton row;
        row.touch.id = "sel_" + g.dir;
        row.touch.rect = Rect{20, 200 + (float)i * 90, 240, 80};
        row.text = (hs.selectedDir == g.dir ? "* " : "  ") + g.dir;
        row.action = "select_project:" + g.dir;
        row.color = (hs.selectedDir == g.dir) ? th.accent : th.button;
        hub.ui.push_back(row);
    }

    if (!hs.selectedDir.empty()) {
        ProjectInfo info;
        ProjectLoader::load(PROJECT_ROOT + "/projects/" + hs.selectedDir + "/project.json", info);

        auto nameL = std::make_unique<Label>();
        nameL->name = "SelName";
        nameL->text = info.name;
        nameL->fontSize = 32;
        nameL->color = th.ink;
        nameL->position = Vec2{320, 150};
        hub.root->addChild(std::move(nameL));

        auto sceneL = std::make_unique<Label>();
        sceneL->name = "SelScene";
        sceneL->text = "scene: " + info.mainScene;
        sceneL->fontSize = 20;
        sceneL->color = th.ink;
        sceneL->position = Vec2{320, 210};
        hub.root->addChild(std::move(sceneL));

        UiButton edit;
        edit.touch.id = "edit";
        edit.touch.rect = Rect{320, 300, 160, 80};
        edit.text = "Edit";
        edit.action = "edit_project:" + hs.selectedDir;
        edit.color = th.button;
        hub.ui.push_back(edit);

        UiButton play;
        play.touch.id = "play";
        play.touch.rect = Rect{500, 300, 160, 80};
        play.text = "Play";
        play.action = "play_project:" + hs.selectedDir;
        play.color = th.accent;
        hub.ui.push_back(play);
    }

    UiButton nb;
    nb.touch.id = "new_project";
    nb.touch.rect = Rect{20, 1100, 240, 80};
    nb.text = "+ NEW";
    nb.action = "new_project";
    nb.color = th.button;
    hub.ui.push_back(nb);

    UiButton tb;
    tb.touch.id = "theme";
    tb.touch.rect = Rect{500, 1100, 180, 80};
    tb.text = "Theme";
    tb.action = "cycle_theme";
    tb.color = th.accent;
    hub.ui.push_back(tb);

    return hub;
}

struct HubResult {
    int mode = 0; // 0 none, 1 play, 2 edit, 3 new, 4 select, 5 theme
    std::string dir;
};

inline HubResult processHub(Scene& hub) {
    HubResult r;
    for (auto& b : hub.ui) {
        if (!b.touch.pressEdge) continue;

        if (b.action == "new_project") {
            r.mode = 3;
        }
        else if (b.action == "cycle_theme") {
            r.mode = 5;
        }
        else if (b.action.rfind("play_project:", 0) == 0) {
            r.mode = 1;
            r.dir = b.action.substr(13);
        }
        else if (b.action.rfind("edit_project:", 0) == 0) {
            r.mode = 2;
            r.dir = b.action.substr(13);
        }
        else if (b.action.rfind("select_project:", 0) == 0) {
            r.mode = 4;
            r.dir = b.action.substr(15);
        }
    }
    return r;
}

inline std::string nextGameDir(const std::vector<ProjectEntry>& games) {
    for (int i = 1; i < 100; ++i) {
        std::string dir = "Game" + std::to_string(i);
        bool exists = false;
        for (const auto& g : games) if (g.dir == dir) exists = true;
        if (!exists) return dir;
    }
    return "GameX";
}

inline void printHubLayout(const HubState& hs) {
    Theme& th = currentTheme();

    const int LW = 26, RW = 40;
    auto sep = [&]() {
        std::cout << "+" << std::string(LW, '-') << "+" << std::string(RW, '-') << "+\n";
    };
    auto row = [&](const std::string& a, const std::string& b) {
        std::string pa = a.size() >= (size_t)LW ? a.substr(0, LW) : a + std::string(LW - a.size(), ' ');
        std::string pb = b.size() >= (size_t)RW ? b.substr(0, RW) : b + std::string(RW - b.size(), ' ');
        std::cout << "|" << pa << "|" << pb << "|\n";
    };

    sep();
    row(" PROJECTS", " Hub | theme: " + th.name);
    sep();

    size_t lines = hs.games.size() + 3;
    if (lines < 8) lines = 8;

    for (size_t i = 0; i < lines; ++i) {
        std::string left, right;
        if (i < hs.games.size()) {
            const auto& g = hs.games[i];
            left = std::string(hs.selectedDir == g.dir ? " [*] " : " [ ] ") + g.dir;
        } else if (i == hs.games.size() + 1) {
            left = " [+ NEW]";
        }

        if (!hs.selectedDir.empty()) {
            if (i == 0) right = " selected: " + hs.selectedDir;
            if (i == 2) right = "  [ Edit ]    [ Play ]";
        } else {
            if (i == 0) right = " (select a project)";
        }
        if (i == lines - 1) right = " [ Theme: " + th.name + " ]";
        row(left, right);
    }
    sep();
    std::cout << "  theme bg=" << colorToHex(th.bg)
              << " accent=" << colorToHex(th.accent)
              << " ink=" << colorToHex(th.ink) << "\n";
}

} // namespace suka