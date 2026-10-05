#pragma once
#include "GameApp.hpp"

namespace suka {

inline void GameApp::startTransition(int type, const std::string& target, float duration) {
    if (target.empty()) return;
    if (type == 3) { performSceneChange(target); return; }
    if (transActive_) return;
    transActive_ = true; transType_ = type; transPhase_ = 0;
    transProgress_ = 0.0f; transDuration_ = duration > 0.05f ? duration : 0.4f;
    transTarget_ = target; transOffset_ = 0.0f; transAlpha_ = 0.0f;
}

inline void GameApp::performSceneChange(const std::string& target) {
    if (!sceneMgr_ || target.empty()) return;
    lua_stop_music();
    g_tweens.clear(); g_particles.clear();
    bool ok = sceneMgr_->restartScene(target, resources_);
    if (!ok) sceneMgr_->requestChange(target, true);
    touch_.resetJoystick();
    ensureGameButtons();
    applyProjFuncs();
}

inline void GameApp::updateTransition(float dt) {
    if (!transActive_) return;
    if (transDuration_ <= 0.001f) { clearTransition(); return; }
    float step = dt / transDuration_; if (step > 1.0f) step = 1.0f;
    transProgress_ += step;
    if (transPhase_ == 0) {
        if (transType_ == 0) { transAlpha_ = transProgress_; transOffset_ = 0.0f; }
        else if (transType_ == 1) { transAlpha_ = 0.0f; transOffset_ = -logicW_ * transProgress_; }
        else if (transType_ == 2) { transAlpha_ = 0.0f; transOffset_ = logicW_ * transProgress_; }
        if (transProgress_ >= 1.0f) {
            std::string target = transTarget_; transTarget_.clear();
            performSceneChange(target);
            if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); return; }
            transPhase_ = 1; transProgress_ = 0.0f;
        }
    } else {
        if (transType_ == 0) { transAlpha_ = 1.0f - transProgress_; transOffset_ = 0.0f; }
        else if (transType_ == 1) { transAlpha_ = 0.0f; transOffset_ = logicW_ * (1.0f - transProgress_); }
        else if (transType_ == 2) { transAlpha_ = 0.0f; transOffset_ = -logicW_ * (1.0f - transProgress_); }
        if (transProgress_ >= 1.0f) clearTransition();
    }
}

inline void GameApp::consumeLuaCmd() {
    LuaGameCmd cmd; bool any = false;
    { std::lock_guard<std::mutex> lk(g_luaCmdMtx);
      if (g_luaCmd.transition) { cmd = g_luaCmd; g_luaCmd = LuaGameCmd{}; any = true; } }
    if (!any) return;
    if (cmd.transition) startTransition(cmd.type, cmd.scene, cmd.duration);
}

inline void GameApp::ensureGameButtons() {
    Scene* sc = (sceneMgr_ && sceneMgr_->current()) ? sceneMgr_->current() : nullptr;
    if (!sc) return;
    input_.setUi(&sc->ui);
}

inline void GameApp::drainSoundCmds(std::string& out) {
    std::vector<std::string> q;
    { std::lock_guard<std::mutex> lk(g_soundMtx); q.swap(g_soundQ); }
    for (auto& c : q) {
        if (c == "MS") out += "MUSICSTOP\n";
        else if (c.rfind("S|", 0) == 0) out += "SOUND|" + c.substr(2) + "\n";
        else if (c.rfind("M|", 0) == 0) out += "MUSIC|" + c.substr(2) + "\n";
    }
}

inline void GameApp::drainCollideEvents() {
    std::vector<std::pair<std::string, std::string>> ev;
    { std::lock_guard<std::mutex> lk(g_collideMtx); ev.swap(g_collideEvents); }
    for (auto& p : ev) scripts_.callCollide(p.first, p.second);
}

inline void GameApp::consumeOverlayAction() {
    std::string a; bool hit = false;
    { std::lock_guard<std::mutex> lk(dlgMtx_);
      if (hasAction_ && !actionRes_.empty() && actionRes_.rfind("ov:", 0) == 0) {
          a = actionRes_.substr(3); hit = true;
          hasAction_ = false; actionRes_.clear();
      }
    }
    if (hit && !a.empty()) runAction(a);
}

inline bool GameApp::enterGame(const std::string& dir) {
    ProjectInfo pi;
    if (!ProjectLoader::load(projectsDir() + dir + "/project.json", pi)) return false;
    std::string root = pi.rootPath;
    std::string font = root + "/" + pi.defaultFont;
    if (!fileExists(font)) font = std::string(PROJECT_ROOT) + "/assets/fonts/Ubuntu-Regular.ttf";
    auto mgr = std::make_unique<SceneManager>(root, font);
    if (!mgr->restartScene(pi.mainScene, resources_)) return false;
    project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = font;
    sceneMgr_ = std::move(mgr);
    scripts_.load(root); g_tweens.clear(); g_particles.clear();
    ctx_ = Context(); g_luaLog.clear(); dbg_ = false;
    saveVarsEnabled_ = projectWantsSave(root); pendingLoadVars_ = saveVarsEnabled_; saveTimer_ = 0.0f;
    pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
    pendingSound_ = false; pendingSoundTarget_.clear();
    clearDialogResults();

    loadProjCamera(project_.rootPath);
    loadProjFuncs();
    logicW_ = projCamW_; logicH_ = projCamH_; orientVertical_ = projVertical_;
    applyScreenRatio();
    input_.screenWidth = logicW_; input_.screenHeight = logicH_;
    orientName_ = orientVertical_ ? "portrait" : "landscape";
    emitOrient_ = true;
    clearTransition();

    ensureGameButtons();
    applyProjFuncs();
    touch_.resetJoystick();
    appMode_ = AppMode::Game;

    transActive_ = true; transType_ = 0; transPhase_ = 1;
    transProgress_ = 0.0f; transDuration_ = 0.35f; transAlpha_ = 1.0f;
    transOffset_ = 0.0f; transTarget_.clear();
    return true;
}

inline std::string GameApp::stepGame() {
    consumeOverlayAction();
    applyScreenRatio();
    if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); return ""; }
    consumeLuaCmd();
    if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); return ""; }

    clearDialogResults();
    ctx_.coinCollectedThisFrame = false; ctx_.jumpPressedThisFrame = false; ctx_.input = input_.state();

    updateTransition(1.0f / 60.0f);
    ensureGameButtons();
    if (!sceneMgr_ || !sceneMgr_->current()) { clearTransition(); input_.endFrame(); return ""; }

    if (!transActive_ && !pendingNodeAction_.empty()) { runAction(pendingNodeAction_); pendingNodeAction_.clear(); }
    if (!transActive_) processUi();
    if (appMode_ != AppMode::Game) { input_.endFrame(); return ""; }

    if (!transActive_) {
        sceneMgr_->update(ctx_, 1.0 / 60.0, input_, resources_);
        scripts_.update(*sceneMgr_->current(), ctx_, 1.0 / 60.0, *sceneMgr_, ctx_.vars);
        if (sceneMgr_->current()) g_tweens.update(1.0f / 60.0f, sceneMgr_->current(), [this](const std::string& fn) {
            if (sceneMgr_ && sceneMgr_->current()) scripts_.callGlobal(fn, ctx_, *sceneMgr_, ctx_.vars, sceneMgr_->current());
        });
        suka::physicsUpdate(*sceneMgr_->current(), 1.0f / 60.0f);
        drainCollideEvents();
    }

    if (pendingLoadVars_) { if (saveVarsEnabled_) loadVars(); pendingLoadVars_ = false; }
    if (saveVarsEnabled_) { saveTimer_ += 1.0f / 60.0f; if (saveTimer_ >= 1.0f) { saveVars(); saveTimer_ = 0.0f; } }

    std::string out;
    drainSoundCmds(out);
    if (ctx_.coinCollectedThisFrame) out += "SOUND|coin\n";
    if (ctx_.jumpPressedThisFrame) out += "SOUND|jump\n";

    gameBackend_.begin(); Renderer renderer(gameBackend_); renderer.render(*sceneMgr_->current(), &ctx_);
    out += gameBackend_.str();

    if (!transActive_ && sceneMgr_ && sceneMgr_->current() && sceneMgr_->current()->root) {
        g_particles.update(1.0f / 60.0f);
        WorldXf ident; walkEmitters(*sceneMgr_->current()->root, ident, 1.0f / 60.0f);
        Scene* ps = sceneMgr_->current();
        bool camActive = false; float camX = 0, camY = 0, camZ = 1;
        Node* cn = ps->root->findByType("Camera2D");
        if (cn) { Camera2D* cam = static_cast<Camera2D*>(cn); camActive = true; camZ = cam->zoom > 0.01f ? cam->zoom : 1.0f; camX = cam->position.x; camY = cam->position.y; }
        float zf = camActive ? camZ : 1.0f;
        float hx = logicW_ * 0.5f, hy = logicH_ * 0.5f;
        const std::vector<Particle>& plist = g_particles.list();
        for (const Particle& pp : plist) {
            float t = pp.maxLife > 0.0f ? (1.0f - pp.life / pp.maxLife) : 1.0f;
            if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
            float sz = pp.size * (1.0f - t) + pp.sizeEnd * t; if (sz < 1.0f) sz = 1.0f; sz *= zf;
            float sx = camActive ? ((pp.x - camX) * camZ + hx) : pp.x;
            float sy = camActive ? ((pp.y - camY) * camZ + hy) : pp.y;
            float a = pp.maxLife > 0.0f ? (pp.life / pp.maxLife) : 0.0f;
            if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
            out += std::string("DRAW text|") + pp.glyph + "|" + std::to_string((int)sx) + "|" + std::to_string((int)sy) + "|" + std::to_string((int)sz) + "|" + colorToHexA(withAlpha(pp.color, a)) + "|" + std::to_string(pp.rot * 57.2957795f) + "\n";
        }
    }

    // Отладочные хитбоксы в игре (в логических координатах с учётом камеры).
    if (g_showBodies && sceneMgr_ && sceneMgr_->current()) {
        Scene* bs = sceneMgr_->current();
        float S2 = 1.0f, OX2 = 0.0f, OY2 = 0.0f;
        Node* cn2 = bs->root ? bs->root->findByType("Camera2D") : nullptr;
        if (cn2) {
            Camera2D* cm2 = static_cast<Camera2D*>(cn2);
            float z2 = cm2->zoom > 0.01f ? cm2->zoom : 1.0f;
            S2 = z2;
            OX2 = logicW_ * 0.5f - z2 * cm2->position.x;
            OY2 = logicH_ * 0.5f - z2 * cm2->position.y;
        }
        emitBodiesDebug(*bs, out, S2, OX2, OY2);
    }

    if (transActive_) out += "TRANS|" + std::to_string(transType_) + "|" + std::to_string(transPhase_) + "|" + std::to_string(transProgress_) + "\n";
    else out += "TRANS|0|0|0\n";

    if (dbg_ && sceneMgr_ && sceneMgr_->current() && sceneMgr_->current()->root) {
        nodeCount_ = countNodes(sceneMgr_->current()->root.get()); lastDraws_ = 0;
        for (size_t i = 0; i + 4 < out.size(); ++i) if (out[i] == 'D' && out[i + 1] == 'R' && out[i + 2] == 'A' && out[i + 3] == 'W') ++lastDraws_;
        out += "OVSTAT|fps " + std::to_string((int)fps_) + "  nodes " + std::to_string(nodeCount_) + "  draws " + std::to_string(lastDraws_) + "  parts " + std::to_string((int)g_particles.count()) + "  vars " + std::to_string((int)ctx_.vars.size()) + "  score " + std::to_string(ctx_.score) + "\n";
        size_t ln = g_luaLog.size(); int show = ln > 6 ? 6 : (int)ln;
        for (int i = 0; i < show; ++i) out += "OVLOG|" + g_luaLog[ln - show + i] + "\n";
    }

    out += "PROJ|" + project_.rootPath + "\n";
    out += "MODE|game\n";
    out += "RES|" + std::to_string((int)logicW_) + "|" + std::to_string((int)logicH_) + "\n";
    if (emitOrient_) { out += "ORIENT|" + orientName_ + "\n"; emitOrient_ = false; }
    input_.endFrame(); return out;
}

inline void GameApp::runAction(const std::string& act) {
    Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr;
    if (act == "dbg:") { dbg_ = !dbg_; return; }
    const std::string pReturn = "editor_return:";
    const std::string pRestart = "restart_scene:", pChange = "change_scene:", pAdd = "add_var:", pSet = "set_var:", pHub = "hub:", pCall = "call:", pClose = "close:";
    if (act == pClose) {
        if (saveVarsEnabled_) saveVars();
        clearTransition();
        lua_stop_music();
        if (playFromEditor_) { playFromEditor_ = false; enterEditor(lastEditorDir_); }
        else { playFromEditor_ = false; appMode_ = AppMode::Hub; pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear(); clearDialogResults(); rebuildHub(); }
        return;
    }
    if (act.rfind(pReturn, 0) == 0) {
        playFromEditor_ = false;
        if (saveVarsEnabled_) saveVars();
        clearTransition();
        lua_stop_music();
        enterEditor(lastEditorDir_);
        return;
    }
    if (act.rfind(pHub, 0) == 0) {
        if (saveVarsEnabled_) saveVars();
        playFromEditor_ = false;
        clearTransition();
        lua_stop_music();
        appMode_ = AppMode::Hub;
        pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
        clearDialogResults();
        rebuildHub();
    }
    else if (act.rfind(pRestart, 0) == 0) { startTransition(3, act.substr(pRestart.size()), 0.0f); }
    else if (act.rfind(pChange, 0) == 0) { startTransition(3, act.substr(pChange.size()), 0.0f); }
    else if (act.rfind(pCall, 0) == 0) { if (sc) scripts_.callGlobal(act.substr(pCall.size()), ctx_, *sceneMgr_, ctx_.vars, sc); }
    else if (act.rfind(pAdd, 0) == 0 || act.rfind(pSet, 0) == 0) {
        bool isAdd = act.rfind(pAdd, 0) == 0;
        std::string rest = act.substr(isAdd ? pAdd.size() : pSet.size());
        size_t c = rest.find(':');
        if (c != std::string::npos) { std::string name = rest.substr(0, c); double v = atof(rest.substr(c + 1).c_str()); if (isAdd) ctx_.vars[name] += v; else ctx_.vars[name] = v; }
    }
}

inline void GameApp::processUi() {
    Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr; if (!sc) return;
    for (auto& b : sc->ui) { if (!b.touch.pressEdge || b.action.empty()) continue; runAction(b.action); if (appMode_ != AppMode::Game || transActive_) return; }
}

} // namespace suka
