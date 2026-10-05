#pragma once
#include "GameApp.hpp"

namespace suka {

inline void GameApp::rebuildHub() {
    hubState_.games = ProjectList::scan();
    HubUiInput hin{ hubState_.games, hubState_.selectedDir, confirmDeleteDir_ };
    hubScene_ = buildHubScene(hin);
    input_.setUi(&hubScene_.ui);
}

inline std::string GameApp::stepHub() {
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
            projFuncs_.clear(); projSounds_.clear();
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
    // Звуковая очередь дренируется и в хабе: STOP/выход гасят музыку всегда.
    drainSoundCmds(out);
    out += "MODE|hub\n";
    out += "RES|1280|720\n";
    input_.endFrame(); return out;
}

} // namespace suka
