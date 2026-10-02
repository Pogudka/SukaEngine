#pragma once

#include <string>
#include <vector>
#include <memory>

#include "Scene.hpp"
#include "Settings.hpp"
#include "Render.hpp"
#include "Project.hpp"
#include "ProjectOps.hpp"
#include "UiUtils.hpp"

namespace suka {

struct HubUiInput {
    const std::vector<ProjectEntry>& games;
    const std::string& selectedDir;
    const std::string& confirmDeleteDir;
};

inline Scene buildHubScene(const HubUiInput& in) {
    Theme& th = currentTheme();

    Scene s;
    s.name = "Hub";
    s.root = std::make_unique<Node>();
    s.root->name = "Hub";

    auto hdr = std::make_unique<Label>();
    hdr->name = "ProjHdr";
    hdr->text = "PROJECTS";
    hdr->fontSize = 22;
    hdr->color = th.ink;
    hdr->position = Vec2{14.0f, 34.0f};

    s.root->addChild(std::move(hdr));

    for (size_t i = 0; i < in.games.size(); ++i) {
        const auto& g = in.games[i];

        std::string disp = sanitizeLine(projectDisplayName(g.dir));

        UiButton row;
        row.touch.id = "sel_" + g.dir;
        row.touch.rect = Rect{10.0f, 70.0f + (float)i * 56.0f, 240.0f, 48.0f};
        row.text = std::string(in.selectedDir == g.dir ? "* " : "  ") + disp;
        row.action = "sel:" + g.dir;
        row.color = (in.selectedDir == g.dir) ? th.accent : th.button;

        s.ui.push_back(row);
    }

    if (!in.selectedDir.empty()) {
        ProjectInfo info;

        std::string pj = projectsDir() + in.selectedDir + "/project.json";

        if (!ProjectLoader::load(pj, info)) {
            info.name = in.selectedDir;
            info.mainScene = "scenes/main.json";
        }

        std::string dispName = sanitizeLine(info.name.empty() ? in.selectedDir : info.name);

        auto nm = std::make_unique<Label>();
        nm->name = "SelName";
        nm->text = dispName;
        nm->fontSize = 34;
        nm->color = th.ink;
        nm->position = Vec2{740.0f, 120.0f};

        s.root->addChild(std::move(nm));

        auto sc = std::make_unique<Label>();
        sc->name = "SelScene";
        sc->text = "scene: " + info.mainScene;
        sc->fontSize = 20;
        sc->color = th.ink;
        sc->position = Vec2{740.0f, 170.0f};

        s.root->addChild(std::move(sc));

        UiButton play;
        play.touch.id = "play";
        play.touch.rect = Rect{740.0f, 280.0f, 150.0f, 60.0f};
        play.text = "Play";
        play.action = "play:" + in.selectedDir;
        play.color = th.accent;

        s.ui.push_back(play);

        UiButton edit;
        edit.touch.id = "edit";
        edit.touch.rect = Rect{910.0f, 280.0f, 150.0f, 60.0f};
        edit.text = "Edit";
        edit.action = "edit:" + in.selectedDir;
        edit.color = th.button;

        s.ui.push_back(edit);

        std::string root = projectsDir() + in.selectedDir;
        bool svOn = projectWantsSave(root);

        UiButton sv;
        sv.touch.id = "toggle_save";
        sv.touch.rect = Rect{740.0f, 360.0f, 150.0f, 60.0f};
        sv.text = svOn ? "SAVE: ON" : "SAVE: OFF";
        sv.action = "save_toggle";
        sv.color = svOn ? parseColor("#2E7D32") : th.button;

        s.ui.push_back(sv);

        UiButton ren;
        ren.touch.id = "hub_rename";
        ren.touch.rect = Rect{910.0f, 360.0f, 150.0f, 60.0f};
        ren.text = "NAME";
        ren.action = "hub_rename";
        ren.color = th.button;

        s.ui.push_back(ren);

        bool confirming = (in.confirmDeleteDir == in.selectedDir);

        UiButton del;
        del.touch.id = "hub_delete";
        del.touch.rect = Rect{740.0f, 440.0f, 150.0f, 60.0f};
        del.text = confirming ? "SURE?" : "DELETE";
        del.action = confirming ? "hub_del_yes" : "hub_del";
        del.color = parseColor("#D62828");

        s.ui.push_back(del);

        UiButton rst;
        rst.touch.id = "hub_reset";
        rst.touch.rect = Rect{910.0f, 440.0f, 150.0f, 60.0f};
        rst.text = "RESET";
        rst.action = "hub_reset";
        rst.color = th.button;

        s.ui.push_back(rst);
    } else {
        auto hint = std::make_unique<Label>();
        hint->name = "Hint";
        hint->text = "(select a project)";
        hint->fontSize = 24;
        hint->color = th.ink;
        hint->position = Vec2{740.0f, 300.0f};

        s.root->addChild(std::move(hint));
    }

    UiButton nb;
    nb.touch.id = "new_project";
    nb.touch.rect = Rect{740.0f, 560.0f, 150.0f, 60.0f};
    nb.text = "+ NEW";
    nb.action = "new";
    nb.color = th.button;

    s.ui.push_back(nb);

    UiButton tb;
    tb.touch.id = "theme";
    tb.touch.rect = Rect{910.0f, 560.0f, 150.0f, 60.0f};
    tb.text = "Theme";
    tb.action = "theme";
    tb.color = th.accent;

    s.ui.push_back(tb);

    return s;
}

struct HubAct {
    int kind = 0;
    std::string dir;
};

inline HubAct processHubScene(Scene& hubScene) {
    HubAct a;

    for (auto& b : hubScene.ui) {
        if (!b.touch.pressEdge || b.action.empty()) {
            continue;
        }

        if (b.action == "new") {
            a.kind = 3;
        } else if (b.action == "theme") {
            a.kind = 5;
        } else if (b.action == "save_toggle") {
            a.kind = 6;
        } else if (b.action == "hub_rename") {
            a.kind = 7;
        } else if (b.action == "hub_del") {
            a.kind = 8;
        } else if (b.action == "hub_del_yes") {
            a.kind = 9;
        } else if (b.action == "hub_reset") {
            a.kind = 10;
        } else if (b.action.rfind("play:", 0) == 0) {
            a.kind = 1;
            a.dir = b.action.substr(5);
        } else if (b.action.rfind("edit:", 0) == 0) {
            a.kind = 2;
            a.dir = b.action.substr(5);
        } else if (b.action.rfind("sel:", 0) == 0) {
            a.kind = 4;
            a.dir = b.action.substr(4);
        }
    }

    return a;
}

} // namespace suka
