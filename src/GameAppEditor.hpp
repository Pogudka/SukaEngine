#pragma once
#include "GameApp.hpp"

namespace suka {

inline void GameApp::submitImportFile(const std::string& category, const std::string& relativePath) {
    if (category.empty() || relativePath.empty()) return;
    pendingImportCategory_.clear();
    lastMsg_ = "imported " + category + ": " + relativePath;
    buildEditorPanels();
}

inline void GameApp::attachChildTo(const std::string& child, const std::string& parent) {
    if (!editor_ || !editor_->scene()) return;
    Scene* sc = editor_->scene();
    if (child.rfind("UI:", 0) == 0) { UiButton* b = editor_->findUi(child.substr(3)); if (b) b->group = parent; return; }
    if (!sc || !sc->root) return;
    Node* root = sc->root.get();
    if (child == parent) return;
    Node* cn = root->findNode(child); Node* pn = root->findNode(parent);
    if (!cn || !pn) return; if (cn->containsName(parent)) return;
    Node* owner = root->findParentOf(child);
    if (!owner || owner == pn) return;
    std::unique_ptr<Node> up = owner->takeChild(child);
    if (up) pn->addChild(std::move(up));
}

inline void GameApp::detachChild(const std::string& child) {
    if (!editor_ || !editor_->scene()) return;
    Scene* sc = editor_->scene();
    if (child.rfind("UI:", 0) == 0) { UiButton* b = editor_->findUi(child.substr(3)); if (b) b->group.clear(); return; }
    if (!sc || !sc->root) return;
    Node* root = sc->root.get();
    Node* owner = root->findParentOf(child);
    if (!owner || owner == root) return;
    std::unique_ptr<Node> up = owner->takeChild(child);
    if (up) root->addChild(std::move(up));
}

inline void GameApp::pushUndo() {
    if (!editor_ || !editor_->scene()) return;
    std::string rel = "snap_" + std::to_string(snapCounter_++) + ".json";
    editor_->save(project_.rootPath + "/" + rel);
    undoStack_.push_back(rel);
    if (undoStack_.size() > 12) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

inline bool GameApp::loadSnap(const std::string& rel) {
    if (!sceneMgr_ || !editor_) return false;
    if (sceneMgr_->restartScene(rel, resources_)) {
        editor_->attach(sceneMgr_->current());
        editor_->setProjectRoot(project_.rootPath);
        scripted_.clear();
        dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pickParent_ = false; pickChild_.clear();
        buildEditorPanels(); input_.setUi(&editorScene_.ui);
        return true;
    }
    return false;
}

inline bool GameApp::doUndo() {
    if (!editor_ || undoStack_.empty()) { lastMsg_ = "nothing to undo"; return false; }
    std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";
    editor_->save(project_.rootPath + "/" + cur); redoStack_.push_back(cur);
    std::string rel = undoStack_.back(); undoStack_.pop_back();
    bool ok = loadSnap(rel); lastMsg_ = ok ? "undo" : "undo failed"; return ok;
}
inline bool GameApp::doRedo() {
    if (!editor_ || redoStack_.empty()) { lastMsg_ = "nothing to redo"; return false; }
    std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";
    editor_->save(project_.rootPath + "/" + cur); undoStack_.push_back(cur);
    std::string rel = redoStack_.back(); redoStack_.pop_back();
    bool ok = loadSnap(rel); lastMsg_ = ok ? "redo" : "redo failed"; return ok;
}

inline void GameApp::loadScript(const std::string& rel) {
    scriptPath_ = rel; std::string s = readFile(project_.rootPath + "/" + rel);
    scriptLines_.clear(); std::string cur;
    for (char c : s) { if (c == '\n') { scriptLines_.push_back(cur); cur.clear(); } else cur += c; }
    scriptLines_.push_back(cur);
    if (scriptLines_.empty()) scriptLines_.push_back("");
    curLine_ = 0; curCol_ = 0; scriptScroll_ = 0; compAnchor_ = -1;
}
inline void GameApp::saveScript() {
    if (scriptPath_.empty()) return;
    std::ofstream f(project_.rootPath + "/" + scriptPath_);
    for (size_t i = 0; i < scriptLines_.size(); ++i) { f << scriptLines_[i]; if (i + 1 < scriptLines_.size()) f << "\n"; }
    f.close(); scripts_.load(project_.rootPath); lastMsg_ = "script saved: " + scriptPath_;
}
inline void GameApp::attachScript(const std::string& name) {
    std::string rel = "scripts/" + name + ".lua";
    ProjectCreator::createScript(project_.rootPath, rel, name);
    scripts_.load(project_.rootPath); scripted_.insert(name);
}

inline void GameApp::saveVars() {
    if (project_.rootPath.empty()) return;
    std::ofstream f(project_.rootPath + "/save.vars");
    if (!f.good()) return;
    f << "__score__=" << ctx_.score << "\n";
    for (const auto& kv : ctx_.vars) f << kv.first << "=" << kv.second << "\n";
    f.close();
}
inline void GameApp::loadVars() {
    if (project_.rootPath.empty()) return;
    std::string path = project_.rootPath + "/save.vars";
    if (!fileExists(path)) return;
    std::string s = readFile(path); size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos); std::string line;
        if (nl == std::string::npos) { line = s.substr(pos); pos = s.size() + 1; }
        else { line = s.substr(pos, nl - pos); pos = nl + 1; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq); double v = atof(line.substr(eq + 1).c_str());
        if (k == "__score__") ctx_.score = (int)v;
        else if (!k.empty()) ctx_.vars[k] = v;
    }
}

inline void GameApp::scTypeChar(char c) {
    if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");
    std::string& L = scriptLines_[curLine_];
    if (c == '\n') {
        if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
        std::string tail = L.substr(curCol_); L = L.substr(0, curCol_);
        scriptLines_.insert(scriptLines_.begin() + curLine_ + 1, tail);
        curLine_++; curCol_ = 0;
    } else { if (curCol_ > (int)L.size()) curCol_ = (int)L.size(); L.insert(L.begin() + curCol_, c); curCol_++; }
    scClampView();
}
inline void GameApp::scCompose(const std::string& text) {
    if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");
    std::string& L = scriptLines_[curLine_];
    if (compAnchor_ < 0 || compAnchor_ > (int)L.size()) compAnchor_ = curCol_;
    if (curCol_ > compAnchor_) { L.erase(L.begin() + compAnchor_, L.begin() + curCol_); curCol_ = compAnchor_; }
    for (char c : text) { if (c == '\n') c = ' '; if (curCol_ > (int)L.size()) curCol_ = (int)L.size(); L.insert(L.begin() + curCol_, c); curCol_++; }
    scClampView();
}
inline void GameApp::scCommit(const std::string& text) {
    if (compAnchor_ >= 0) {
        std::string& L = scriptLines_[curLine_];
        if (compAnchor_ <= (int)L.size() && curCol_ > compAnchor_) { L.erase(L.begin() + compAnchor_, L.begin() + curCol_); curCol_ = compAnchor_; }
        compAnchor_ = -1;
    }
    for (char c : text) scTypeChar(c);
}
inline void GameApp::scBackspace() {
    compAnchor_ = -1;
    if (curLine_ >= (int)scriptLines_.size()) return;
    std::string& L = scriptLines_[curLine_];
    if (curCol_ > 0) { int p = utf8Prev(L, curCol_); L.erase(L.begin() + p, L.begin() + curCol_); curCol_ = p; }
    else if (curLine_ > 0) { size_t prevLen = scriptLines_[curLine_ - 1].size(); scriptLines_[curLine_ - 1] += L; scriptLines_.erase(scriptLines_.begin() + curLine_); curLine_--; curCol_ = (int)prevLen; }
    scClampView();
}
inline void GameApp::scMove(int d) {
    compAnchor_ = -1;
    if (curLine_ >= (int)scriptLines_.size()) curLine_ = (int)scriptLines_.size() - 1;
    std::string& L = scriptLines_[curLine_];
    if (curCol_ > (int)L.size()) curCol_ = (int)L.size();
    if (d < 0) { if (curCol_ > 0) curCol_ = utf8Prev(L, curCol_); else if (curLine_ > 0) { curLine_--; curCol_ = (int)scriptLines_[curLine_].size(); } }
    else { if (curCol_ < (int)L.size()) curCol_ = utf8Next(L, curCol_); else if (curLine_ + 1 < (int)scriptLines_.size()) { curLine_++; curCol_ = 0; } }
    scClampView();
}
inline void GameApp::scClampView() {
    const int LINES = 24;
    if (curLine_ < scriptScroll_) scriptScroll_ = curLine_;
    if (curLine_ >= scriptScroll_ + LINES) scriptScroll_ = curLine_ - LINES + 1;
    if (scriptScroll_ < 0) scriptScroll_ = 0;
}
inline void GameApp::imeApply() {
    std::vector<std::string> tq, cq; std::vector<int> kq; bool fin = false;
    { std::lock_guard<std::mutex> lk(imeMtx_); tq.swap(imeTextQ_); cq.swap(imeCompQ_); kq.swap(imeKeyQ_); fin = imeFinish_; imeFinish_ = false; }
    if (tq.empty() && cq.empty() && kq.empty() && !fin) return;
    for (auto& s : cq) scCompose(s);
    for (auto& s : tq) scCommit(s);
    if (fin) scFinish();
    for (int k : kq) {
        if (k == 67) scBackspace();
        else if (k == 66) { compAnchor_ = -1; scTypeChar('\n'); }
        else if (k == 21) scMove(-1);
        else if (k == 22) scMove(1);
    }
    imeChanged_ = true;
}

inline void GameApp::addFuncToSelected(const std::string& fn) {
    Node* sn = editor_ ? editor_->selected() : nullptr;
    Node2D* s2 = sn ? dynamic_cast<Node2D*>(sn) : nullptr;
    if (!s2) { funcMsg_ = "Select an object first"; funcMsgTimer_ = 150; return; }
    if (s2->locked) { funcMsg_ = "Unlock the object first"; funcMsgTimer_ = 150; return; }
    std::string cur = funcsOf(s2->name);
    if (cur.find(fn) == std::string::npos) { if (!cur.empty()) cur += ","; cur += fn; }
    projFuncs_[s2->name] = cur;
    saveProjFuncs();
    lastMsg_ = "func " + fn + " -> " + s2->name;
}
inline void GameApp::clearFuncsSelected() {
    Node* sn = editor_ ? editor_->selected() : nullptr;
    if (!sn) { funcMsg_ = "Select an object first"; funcMsgTimer_ = 150; return; }
    projFuncs_.erase(sn->name);
    saveProjFuncs();
    lastMsg_ = "funcs cleared: " + sn->name;
}
inline void GameApp::loadProjFuncs() {
    projFuncs_.clear(); projSounds_.clear();
    if (project_.rootPath.empty()) return;
    std::string path = project_.rootPath + "/funcs.txt";
    if (!fileExists(path)) return;
    std::string s = readFile(path); size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos); std::string line;
        if (nl == std::string::npos) { line = s.substr(pos); pos = s.size() + 1; }
        else { line = s.substr(pos, nl - pos); pos = nl + 1; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("SND:", 0) == 0) {
            std::string rest = line.substr(4);
            size_t eq = rest.find('=');
            if (eq == std::string::npos) continue;
            std::string nm = rest.substr(0, eq);
            std::string val = rest.substr(eq + 1);
            SoundDef sd;
            size_t p1 = val.find('|');
            if (p1 == std::string::npos) { sd.snd = val; }
            else {
                sd.snd = val.substr(0, p1);
                std::string flags = val.substr(p1 + 1);
                sd.loop = flags.find("loop") != std::string::npos;
                sd.autoplay = flags.find("auto") != std::string::npos;
            }
            projSounds_[nm] = sd;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        projFuncs_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}
inline void GameApp::saveProjFuncs() {
    if (project_.rootPath.empty()) return;
    std::ofstream f(project_.rootPath + "/funcs.txt");
    if (!f.good()) return;
    for (auto& kv : projFuncs_) f << kv.first << "=" << kv.second << "\n";
    for (auto& kv : projSounds_) {
        f << "SND:" << kv.first << "=" << kv.second.snd
          << "|" << (kv.second.loop ? "loop" : "-")
          << "|" << (kv.second.autoplay ? "auto" : "-") << "\n";
    }
    f.close();
}
inline void GameApp::applyProjFuncs() {
    Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr;
    if (!sc || !sc->root) return;
    for (auto& kv : projFuncs_) {
        Node* n = sc->root->findNode(kv.first);
        if (!n) continue;
        const std::string& f = kv.second;
        bool stat = f.find("staticbody") != std::string::npos;
        bool rig  = f.find("rigidbody")  != std::string::npos;
        if (stat) bodyAdd(kv.first, true, 1.0f);
        else if (rig) bodyAdd(kv.first, false, 1.0f);
        else continue;
        Body* b = bodyGet(kv.first);
        if (b) {
            if (f.find("nogravity") != std::string::npos) b->gravity = false;
            if (f.find("bouncy")    != std::string::npos) b->restitution = 0.6f;
        }
    }
    for (auto& kv : projSounds_) {
        Node* n = sc->root->findNode(kv.first);
        if (!n) continue;
        if (kv.second.autoplay && !kv.second.snd.empty()) {
            if (kv.second.loop) lua_play_music(kv.second.snd, true);
            else lua_play_sound(kv.second.snd);
        }
    }
}

inline bool GameApp::enterEditor(const std::string& dir) {
    ProjectInfo pi;
    if (!ProjectLoader::load(projectsDir() + dir + "/project.json", pi)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
    std::string root = pi.rootPath;
    std::string font = root + "/" + pi.defaultFont;
    if (!fileExists(font)) font = std::string(PROJECT_ROOT) + "/assets/fonts/Ubuntu-Regular.ttf";
    auto mgr = std::make_unique<SceneManager>(root, font);
    if (!mgr->restartScene("scenes/main.json", resources_)) if (!mgr->restartScene(pi.mainScene, resources_)) { appMode_ = AppMode::Hub; rebuildHub(); return false; }
    project_ = pi; g_projectRoot = project_.rootPath; fontPath_ = font; sceneMgr_ = std::move(mgr);
    lastEditorDir_ = dir;
    editor_ = std::make_unique<Editor>();
    editor_->attach(sceneMgr_->current());
    editor_->setProjectRoot(project_.rootPath);
    showCreate_ = false; showAssets_ = false; showSettings_ = false; showPrefabs_ = false; showFiles_ = true;
    showFuncs_ = false; funcMsgTimer_ = 0;
    pendingSound_ = false; pendingSoundTarget_.clear();
    assetScroll_ = 0; prefabScroll_ = 0;
    pendingDeleteFile_.clear();
    pendingText_ = false; pendingName_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
    pendingSceneSave_ = false; pendingPrefabSave_ = false;
    fsPath_ = ""; manip_ = Manip::Move; pinching_ = false; hierScroll_ = 0; fsScroll_ = 0;
    pickParent_ = false; pickChild_.clear(); lastMsg_.clear(); edZoom_ = 1.0f;
    undoStack_.clear(); redoStack_.clear(); clipboard_.reset();
    scriptMode_ = false; scriptPath_.clear(); scriptLines_.clear(); compAnchor_ = -1; imeShown_ = false;
    pendingLoadVars_ = false; saveVarsEnabled_ = false; saveTimer_ = 0.0f;
    pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
    clearDialogResults(); g_tweens.clear(); g_particles.clear();
    loadProjCamera(project_.rootPath);
    loadProjFuncs();
    logicW_ = 1280.0f; logicH_ = 720.0f; orientVertical_ = false; emitOrient_ = false;
    input_.screenWidth = logicW_; input_.screenHeight = logicH_;
    clearTransition();
    buildEditorPanels(); input_.setUi(&editorScene_.ui); touch_.resetJoystick();
    appMode_ = AppMode::Editor;
    return true;
}

inline int GameApp::applyEditorAction(const std::string& act) {
    if (!editor_) return 0;
    auto rebuild = [&]() { buildEditorPanels(); input_.setUi(&editorScene_.ui); };
    bool allowedInScript = act == "kb_toggle" || act == "tab_scene" || act == "ssave" || act == "scup" || act == "scdn" || act == "snew" || act.rfind("scrfile:", 0) == 0;
    if (scriptMode_ && !allowedInScript) return 0;
    if (act == "kb_toggle") { if (imeShown_) { imeWantOff_ = true; imeShown_ = false; } else { imeWantOn_ = true; imeShown_ = true; } rebuild(); return 1; }
    if (act == "tab_scripts") {
        if (!scriptMode_) { scriptMode_ = true; if (scriptPath_.empty()) loadScript("scripts/main.lua"); imeWantOn_ = true; imeShown_ = true; rebuild(); return 1; } return 0;
    }
    if (act == "tab_scene") { if (scriptMode_) { scriptMode_ = false; imeWantOff_ = true; imeShown_ = false; rebuild(); return 1; } return 0; }
    if (act.rfind("scrfile:", 0) == 0) { loadScript("scripts/" + act.substr(8)); imeChanged_ = true; rebuild(); return 1; }
    if (act == "ssave") { saveScript(); rebuild(); return 1; }
    if (act == "scup") { scriptScroll_ -= 3; imeChanged_ = true; rebuild(); return 1; }
    if (act == "scdn") { scriptScroll_ += 3; imeChanged_ = true; rebuild(); return 1; }
    if (act == "snew") { pendingName_ = true; pendingKind_ = 2; rebuild(); return 1; }
    if (act == "funcs_open") { showFuncs_ = !showFuncs_; rebuild(); return 1; }
    if (scriptMode_) return 0;

    Node* sn = editor_->selected();
    std::string sel = sn ? sn->name : std::string(); std::string selUi = editor_->selectedUi();
    UiButton* ub = selUi.empty() ? nullptr : editor_->findUi(selUi);
    Node2D* s2 = (!sel.empty()) ? editor_->find2d(sel) : nullptr;
    bool lk = (s2 != nullptr) && s2->locked;

    if (act == "ed_play") {
        editor_->save(project_.rootPath + "/scenes/main.json");
        std::string pd = projectsDir();
        std::string dir = project_.rootPath;
        if (dir.rfind(pd, 0) == 0) dir = dir.substr(pd.size());
        pendingName_ = false; pendingText_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
        pendingSceneSave_ = false; pendingPrefabSave_ = false;
        pendingSound_ = false; pendingSoundTarget_.clear();
        clearDialogResults();
        playFromEditor_ = true;
        if (!enterGame(dir)) { playFromEditor_ = false; lastMsg_ = "play failed"; rebuild(); return 0; }
        return 2;
    }
    if (act == "save_as_prefab") {
        if (sel.empty() || !s2) { lastMsg_ = "select a node first"; return 0; }
        if (std::string(s2->typeName()) == "Prefab2D") { lastMsg_ = "already a prefab"; return 0; }
        if (lk) { lastMsg_ = "unlock node first"; return 0; }
        pendingPrefabSave_ = true; pendingText_ = true; pendingTextCur_ = sel; return 0;
    }
    if (act == "prefabs_open") { showPrefabs_ = true; prefabScroll_ = 0; rebuild(); return 1; }
    if (act == "prefabs_close") { showPrefabs_ = false; rebuild(); return 1; }
    if (act == "prefabs_up") { prefabScroll_ -= 3; rebuild(); return 1; }
    if (act == "prefabs_dn") { prefabScroll_ += 3; rebuild(); return 1; }
    if (act.rfind("prefab_add:", 0) == 0) {
        std::string rel = act.substr(11);
        pushUndo();
        std::string name = "PF" + std::to_string(createCounter_++);
        Node2D* base = editor_->addNode("Prefab2D", name, 640, 360);
        Prefab2D* pf = base ? static_cast<Prefab2D*>(base) : nullptr;
        if (pf) { pf->sourcePath = rel; pf->instantiate(project_.rootPath, fontPath_, editor_->scene()); editor_->select(name); lastMsg_ = "added " + rel; }
        else { lastMsg_ = "insert failed"; }
        showPrefabs_ = false; rebuild(); return 1;
    }
    if (act == "prefab_reload") {
        Prefab2D* pf = s2 ? dynamic_cast<Prefab2D*>(s2) : nullptr;
        if (pf && !pf->sourcePath.empty()) { pushUndo(); pf->instantiate(project_.rootPath, fontPath_, editor_->scene()); lastMsg_ = "reloaded " + pf->sourcePath; rebuild(); return 1; }
        return 0;
    }
    if (act == "save_scene_as") { pendingSceneSave_ = true; pendingText_ = true; pendingTextCur_ = "main"; return 0; }
    if (act == "files_open") { showFiles_ = !showFiles_; fsScroll_ = 0; pendingDeleteFile_.clear(); rebuild(); return 1; }
    if (act == "fscroll_up") { fsScroll_ -= 3; rebuild(); return 1; }
    if (act == "fscroll_dn") { fsScroll_ += 3; rebuild(); return 1; }
    if (act.rfind("fs_del_ask:", 0) == 0) { pendingDeleteFile_ = act.substr(11); rebuild(); return 1; }
    if (act.rfind("fs_del_yes:", 0) == 0) {
        std::string rel = act.substr(11);
        std::string full = project_.rootPath + "/" + rel;
        if (pendingDeleteFile_ == rel && fileExists(full)) {
            if (std::remove(full.c_str()) == 0) lastMsg_ = "deleted " + rel; else lastMsg_ = "delete failed: " + rel;
        } else { lastMsg_ = "delete cancelled"; }
        pendingDeleteFile_.clear(); rebuild(); return 1;
    }
    if (act == "create_particle" || act == "create:Particle2D:none") { pushUndo(); std::string name = "Emitter" + std::to_string(createCounter_++); editor_->addNode("Particle2D", name, 640, 360); editor_->select(name); showCreate_ = false; rebuild(); return 1; }
    if (act == "create_sound") {
        pushUndo();
        std::string name = "Sound" + std::to_string(createCounter_++);
        Node2D* bn = editor_->addNode("Node2D", name, 640, 360);
        if (bn) { bn->shape = "none"; bn->w = 32; bn->h = 32; }
        SoundDef sd;
        projSounds_[name] = sd;
        saveProjFuncs();
        editor_->select(name);
        showCreate_ = false;
        lastMsg_ = "created sound object " + name;
        rebuild(); return 1;
    }
    Particle2D* p2 = dynamic_cast<Particle2D*>(s2);
    if (act.rfind("view:", 0) == 0) { if (lk || sel.empty() || !p2) return 0; pushUndo(); std::string preset = act.substr(5); if (editor_->setEmitterPreset(sel, preset)) { p2->emitting = true; p2->burstPending = true; lastMsg_ = "view " + preset + " -> " + sel; rebuild(); return 1; } rebuild(); return 0; }
    if (act == "ponoff") { if (!lk && p2) { pushUndo(); p2->emitting = !p2->emitting; if (p2->emitting) p2->burstPending = true; lastMsg_ = p2->emitting ? ("emitting ON: " + sel) : ("emitting OFF: " + sel); rebuild(); return 1; } return 0; }
    if (act == "pcolor") { if (!lk && p2) { pendingRgb_ = 3; pendingText_ = true; pendingTextCur_ = rgbStr(p2->color); } return 0; }
    if (act.rfind("pnum:", 0) == 0) {
        if (!lk && p2) {
            pendingNum_ = true; pendingNumKind_ = act.substr(5);
            if (pendingNumKind_ == "rate") pendingNumCur_ = std::to_string((int)p2->rate);
            else if (pendingNumKind_ == "life") pendingNumCur_ = std::to_string(p2->life);
            else if (pendingNumKind_ == "size") pendingNumCur_ = std::to_string((int)p2->size);
            else if (pendingNumKind_ == "spread") pendingNumCur_ = std::to_string((int)p2->spread);
            else if (pendingNumKind_ == "gravity") pendingNumCur_ = std::to_string((int)p2->gravity);
            else pendingNumCur_ = "0";
        }
        return 0;
    }
    if (act == "settings_open") { showSettings_ = !showSettings_; if (showSettings_) showCreate_ = false; rebuild(); return 1; }
    if (act == "settings_close") { showSettings_ = false; rebuild(); return 1; }
    if (act.rfind("import_category:", 0) == 0) {
        std::string category = act.substr(16);
        if (category == "fonts" || category == "sprites" || category == "sounds") pendingImportCategory_ = category;
        else lastMsg_ = category + ": unavailable";
        return 0;
    }
    if (act == "col_rgb") { if (!lk) { pendingRgb_ = 1; pendingText_ = true; pendingTextCur_ = ub ? rgbStr(ub->color) : (s2 ? rgbStr(s2->color) : std::string("255,255,255")); } return 0; }
    if (act == "bg_rgb") { pendingRgb_ = 2; pendingText_ = true; unsigned bc = (editor_->scene() && editor_->scene()->bgSet()) ? parseColor(editor_->scene()->bg) : currentTheme().bg; pendingTextCur_ = rgbStr(bc); return 0; }
    if (act == "assets_open") { showAssets_ = !showAssets_; assetScroll_ = 0; rebuild(); return 1; }
    if (act == "assets_up") { assetScroll_ -= 3; rebuild(); return 1; }
    if (act == "assets_dn") { assetScroll_ += 3; rebuild(); return 1; }
    if (act.rfind("tex_pick:", 0) == 0) {
        if (!showAssets_) return 0;
        std::string img = act.substr(9); std::string rel = "assets/" + img;
        if (s2) { if (lk) return 0; pushUndo(); setNodeTexture(sel, rel); lastMsg_ = "tex " + img + " -> " + sel; }
        else if (ub) { pushUndo(); ub->texture = rel; lastMsg_ = "tex " + img + " -> button " + selUi; }
        else { pushUndo(); std::string name = "Sprite" + std::to_string(createCounter_++); editor_->addNode("Sprite2D", name, 640, 360); setNodeTexture(name, rel); editor_->select(name); lastMsg_ = "sprite " + name + " <- " + img; }
        showAssets_ = false; rebuild(); return 1;
    }
    if (act == "ed_lock") { if (s2) { pushUndo(); s2->locked = !s2->locked; lastMsg_ = s2->locked ? "locked " + sel : "unlocked " + sel; rebuild(); return 1; } return 0; }
    if (act == "ed_undo") return doUndo() ? 1 : 0;
    if (act == "ed_redo") return doRedo() ? 1 : 0;
    if (act == "ed_copy") { if (s2) { clipboard_ = s2->cloneNode(); lastMsg_ = "copied " + sel; } return 0; }
    if (act == "ed_paste") {
        if (clipboard_ && editor_->scene() && editor_->scene()->root) { pushUndo(); auto cp = clipboard_->cloneNode(); std::string nm = cp->name + "_c" + std::to_string(clipCounter_++); cp->name = nm; Node2D* raw = dynamic_cast<Node2D*>(cp.get()); if (raw) raw->position.x += 40; editor_->scene()->root->addChild(std::move(cp)); editor_->select(nm); lastMsg_ = "pasted " + nm; rebuild(); return 1; }
        lastMsg_ = "clipboard empty"; return 0;
    }
    if (act.rfind("fold:", 0) == 0) { std::string nm = act.substr(5); if (collapsed_.count(nm)) collapsed_.erase(nm); else collapsed_.insert(nm); rebuild(); return 1; }
    if (act == "hier_up") { hierScroll_ -= 3; rebuild(); return 1; }
    if (act == "hier_dn") { hierScroll_ += 3; rebuild(); return 1; }
    if (act == "fs_up") { std::string tmp = fsPath_; while (!tmp.empty() && tmp.back() == '/') tmp.pop_back(); size_t sl = tmp.find_last_of('/'); fsPath_ = (sl == std::string::npos) ? std::string("") : tmp.substr(0, sl + 1); fsScroll_ = 0; pendingDeleteFile_.clear(); rebuild(); return 1; }
    if (act.rfind("fs_enter:", 0) == 0) { fsPath_ += act.substr(9) + "/"; fsScroll_ = 0; pendingDeleteFile_.clear(); rebuild(); return 1; }
    if (act.rfind("fs_pick:", 0) == 0) {
        std::string rel = act.substr(8);
        if (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".prf") == 0) { lastMsg_ = "prefab: use PF ADD to insert"; rebuild(); return 0; }
        if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".json") == 0) {
            if (sceneMgr_ && sceneMgr_->restartScene(rel, resources_)) {
                editor_->attach(sceneMgr_->current());
                editor_->setProjectRoot(project_.rootPath);
                scripted_.clear();
                showCreate_ = false; showAssets_ = false; showSettings_ = false; showPrefabs_ = false; showFiles_ = true;
                assetScroll_ = 0; dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; pinching_ = false;
                pickParent_ = false; pickChild_.clear(); hierScroll_ = 0;
                pendingDeleteFile_.clear(); undoStack_.clear(); redoStack_.clear();
                lastMsg_ = "loaded " + rel; rebuild(); return 1;
            }
            return 0;
        }
        {
            size_t dpos = rel.find_last_of('.');
            if (dpos != std::string::npos) {
                std::string ex = rel.substr(dpos + 1);
                for (auto& ch2 : ex) ch2 = (char)std::tolower((unsigned char)ch2);
                if (ex == "ogg" || ex == "mp3" || ex == "wav") {
                    Node* sn3 = editor_ ? editor_->selected() : nullptr;
                    if (sn3 && projSounds_.count(sn3->name)) {
                        std::string base = rel.substr(0, dpos);
                        size_t sl2 = base.find_last_of('/');
                        if (sl2 != std::string::npos) base = base.substr(sl2 + 1);
                        projSounds_[sn3->name].snd = base;
                        saveProjFuncs();
                        lastMsg_ = "sound " + base + " -> " + sn3->name;
                        rebuild(); return 1;
                    }
                    lastMsg_ = "audio file: select a Sound object or use SET";
                    rebuild(); return 0;
                }
            }
        }
        if (!lk) { pushUndo(); if (ub) { ub->texture = rel; rebuild(); return 1; } if (!sel.empty()) { setNodeTexture(sel, rel); rebuild(); return 1; } }
        return 0;
    }
    if (act == "clear_tex") {
        if (!lk) {
            pushUndo();
            if (ub) { ub->texture.clear(); rebuild(); return 1; }
            if (!sel.empty()) {
                editor_->setTexture(sel, std::string());
                Node2D* n = editor_->find2d(sel);
                Sprite2D* sp = n ? dynamic_cast<Sprite2D*>(n) : nullptr;
                if (sp) sp->texturePath.clear();
                if (n) n->shape = "square";
                rebuild(); return 1;
            }
        }
        return 0;
    }
    if (act == "manip:move") { manip_ = Manip::Move; rebuild(); return 1; }
    if (act == "manip:rotate") { manip_ = Manip::Rotate; rebuild(); return 1; }
    if (act == "manip:scale") { manip_ = Manip::Scale; rebuild(); return 1; }
    if (act == "create_open") { showCreate_ = !showCreate_; rebuild(); return 1; }
    if (act == "edit_text") { if (!lk) { pendingRgb_ = 0; if (ub) { pendingText_ = true; pendingTextCur_ = ub->text; } else if (sn && std::string(sn->typeName()) == "Label") { pendingText_ = true; pendingTextCur_ = static_cast<Label*>(sn)->text; } } return 0; }
    if (act == "edit_action") { if (!lk) { pendingAction_ = true; pendingActionCur_ = ub ? ub->action : (s2 ? s2->action : std::string("")); } return 0; }
    if (act.rfind("num:", 0) == 0) {
        if (!lk) {
            pendingNum_ = true; pendingNumKind_ = act.substr(4);
            if (pendingNumKind_ == "nx") pendingNumCur_ = s2 ? std::to_string((int)s2->position.x) : "0";
            else if (pendingNumKind_ == "ny") pendingNumCur_ = s2 ? std::to_string((int)s2->position.y) : "0";
            else if (pendingNumKind_ == "nrot") pendingNumCur_ = s2 ? std::to_string((int)(s2->rotation * 57.2957795f)) : "0";
            else if (pendingNumKind_ == "nscl") pendingNumCur_ = s2 ? std::to_string((int)(s2->scale.x * 100)) : "100";
            else if (pendingNumKind_ == "nw") pendingNumCur_ = s2 ? std::to_string((int)s2->w) : "32";
            else if (pendingNumKind_ == "nh") pendingNumCur_ = s2 ? std::to_string((int)s2->h) : "32";
            else if (pendingNumKind_ == "nalpha") pendingNumCur_ = s2 ? std::to_string((int)(s2->alpha * 100)) : "100";
            else if (pendingNumKind_ == "bx") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.x) : "0";
            else if (pendingNumKind_ == "by") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.y) : "0";
            else if (pendingNumKind_ == "bw") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.w) : "100";
            else if (pendingNumKind_ == "bh") pendingNumCur_ = ub ? std::to_string((int)ub->touch.rect.h) : "50";
            else if (pendingNumKind_ == "bang") pendingNumCur_ = ub ? std::to_string((int)ub->angle) : "0";
            else if (pendingNumKind_ == "balpha") pendingNumCur_ = ub ? std::to_string((int)(ub->alpha * 100)) : "100";
            else if (pendingNumKind_ == "camzoom") {
                Node* cn = editor_->scene() && editor_->scene()->root ? editor_->scene()->root->findByType("Camera2D") : nullptr;
                Camera2D* cm = cn ? static_cast<Camera2D*>(cn) : nullptr;
                pendingNumCur_ = cm ? std::to_string((int)(cm->zoom * 100)) : "100";
            }
            else pendingNumCur_ = "0";
        }
        return 0;
    }
    if (act == "ed_scr") { if (!lk && !sel.empty()) { attachScript(sel); rebuild(); return 1; } return 0; }
    if (act == "ed_clone") {
        if (!lk && !sel.empty()) {
            pushUndo();
            Node2D* cl = editor_->cloneSelected(sel + "_copy");
            Prefab2D* cpf = cl ? dynamic_cast<Prefab2D*>(cl) : nullptr;
            if (cpf) cpf->instantiate(project_.rootPath, fontPath_, editor_->scene());
            rebuild(); return 1;
        }
        return 0;
    }
    if (act == "ed_parent") {
        if (!lk) {
            if (!selUi.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? ("UI:" + selUi) : std::string(); rebuild(); return 1; }
            if (!sel.empty()) { pickParent_ = !pickParent_; pickChild_ = pickParent_ ? sel : std::string(); rebuild(); return 1; }
        }
        return 0;
    }
    if (act == "ed_unparent") { if (!lk) { if (!selUi.empty()) { pushUndo(); detachChild("UI:" + selUi); rebuild(); return 1; } if (!sel.empty()) { pushUndo(); detachChild(sel); rebuild(); return 1; } } return 0; }
    if (act.rfind("ed_selectui:", 0) == 0) { std::string id = act.substr(12); if (pickParent_ && !pickChild_.empty() && pickChild_ != id && pickChild_ != ("UI:" + id)) { pushUndo(); attachChildTo(pickChild_, id); pickParent_ = false; pickChild_.clear(); } else editor_->selectUi(id); rebuild(); return 1; }
    if (act.rfind("ed_select:", 0) == 0) { std::string nm = act.substr(10); if (pickParent_ && !pickChild_.empty() && nm != pickChild_) { pushUndo(); attachChildTo(pickChild_, nm); pickParent_ = false; pickChild_.clear(); } else editor_->select(nm); rebuild(); return 1; }
    if (act == "create_cam") { pushUndo(); std::string name = "Cam" + std::to_string(createCounter_++); editor_->addNode("Camera2D", name, 640, 360); editor_->select(name); showCreate_ = false; rebuild(); return 1; }
    if (act == "create_light") { pushUndo(); std::string name = "Light" + std::to_string(createCounter_++); editor_->addNode("Light2D", name, 640, 360); editor_->select(name); showCreate_ = false; rebuild(); return 1; }
    if (act == "create_grp") { pendingName_ = true; pendingKind_ = 0; pendingType_ = "Node2D"; pendingShape_ = "none"; showCreate_ = false; rebuild(); return 1; }
    if (act == "create_btn") { pendingName_ = true; pendingKind_ = 1; showCreate_ = false; rebuild(); return 1; }
    if (act.rfind("create:", 0) == 0) { std::string rest = act.substr(7); size_t c = rest.find(':'); pendingName_ = true; pendingKind_ = 0; pendingType_ = rest.substr(0, c); pendingShape_ = (c == std::string::npos) ? "" : rest.substr(c + 1); showCreate_ = false; rebuild(); return 1; }
    if (act.rfind("ed_shape:", 0) == 0) { if (!lk && !sel.empty()) { pushUndo(); editor_->setShape(sel, act.substr(9)); rebuild(); return 1; } return 0; }
    if (act.rfind("ed_move:", 0) == 0) {
        if (lk) return 0;
        std::string d = act.substr(8);
        if (ub) { pushUndo(); if (manip_ == Manip::Move) { float dx = (d == "l") ? -16 : (d == "r") ? 16 : 0; float dy = (d == "u") ? -16 : (d == "d") ? 16 : 0; ub->touch.rect.x += dx; ub->touch.rect.y += dy; } else if (manip_ == Manip::Rotate) { float dr = (d == "l") ? -15.0f : (d == "r") ? 15.0f : 0.0f; ub->angle += dr; } else if (manip_ == Manip::Scale) { float f = (d == "u") ? 1.1f : (d == "d") ? (1.0f / 1.1f) : 1.0f; ub->touch.rect.w *= f; ub->touch.rect.h *= f; } rebuild(); return 1; }
        if (s2) { pushUndo(); if (manip_ == Manip::Move) { float dx = (d == "l") ? -16 : (d == "r") ? 16 : 0; float dy = (d == "u") ? -16 : (d == "d") ? 16 : 0; editor_->moveSelected(dx, dy); if (editor_->scene()) moveGroupButtons(editor_->scene(), sel, dx, dy); } else if (manip_ == Manip::Rotate) { float dr = (d == "l") ? -15.0f : (d == "r") ? 15.0f : 0.0f; s2->rotation += dr * 3.14159265f / 180.0f; } else if (manip_ == Manip::Scale) { float f = (d == "u") ? 1.1f : (d == "d") ? (1.0f / 1.1f) : 1.0f; s2->scale.x *= f; s2->scale.y *= f; } rebuild(); return 1; }
        return 0;
    }
    if (act == "ed_del") { if (lk) return 0; if (ub) { pushUndo(); editor_->deleteUi(selUi); rebuild(); return 1; } if (!sel.empty()) { pushUndo(); projSounds_.erase(sel); saveProjFuncs(); editor_->deleteNode(sel); rebuild(); return 1; } return 0; }
    if (act == "ed_save") { editor_->save(project_.rootPath + "/scenes/main.json"); lastMsg_ = "saved"; return 0; }
    if (act == "ed_back") {
        if (scriptMode_) { scriptMode_ = false; imeWantOff_ = true; imeShown_ = false; }
        pendingName_ = false; pendingText_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
        pendingSceneSave_ = false; pendingPrefabSave_ = false;
        pendingSound_ = false; pendingSoundTarget_.clear();
        clearDialogResults();
        pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
        playFromEditor_ = false;
        clearTransition();
        lua_stop_music();
        appMode_ = AppMode::Hub; rebuildHub();
        return 2;
    }
    return 0;
}

inline void GameApp::processEditorActions() {
    if (!editor_) return;
    std::vector<std::string> pressed; pressed.reserve(editorScene_.ui.size());
    for (const auto& b : editorScene_.ui) if (b.touch.pressEdge && !b.action.empty()) pressed.push_back(b.action);
    for (const auto& act : pressed) { int r = applyEditorAction(act); if (r == 2) break; }
}

inline void GameApp::consumeDialogResults() {
    std::string txt, nm, act, num;
    bool ht = false, hn = false, ha = false, hnum = false;
    { std::lock_guard<std::mutex> lk(dlgMtx_); ht = hasText_; hn = hasName_; ha = hasAction_; hnum = hasNum_; txt = textRes_; nm = nameRes_; act = actionRes_; num = numRes_; hasText_ = false; hasName_ = false; hasAction_ = false; hasNum_ = false; }
    if (!editor_) return;
    if (ha && (act.rfind("sys:", 0) == 0 || act.rfind("ov:", 0) == 0)) ha = false;

    if (ht && pendingSound_) {
        std::string base = txt;
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        auto it = projSounds_.find(pendingSoundTarget_);
        if (it != projSounds_.end()) { it->second.snd = base; saveProjFuncs(); lastMsg_ = "sound " + base + " -> " + pendingSoundTarget_; }
        pendingSound_ = false; pendingSoundTarget_.clear();
        buildEditorPanels(); input_.setUi(&editorScene_.ui);
        return;
    }
    if (ht && pendingSceneSave_) {
        std::string name = sanitizeProjectDirName(txt);
        if (!name.empty()) { editor_->setProjectRoot(project_.rootPath); if (editor_->saveScene(name)) lastMsg_ = "saved scenes/" + name + ".json"; else lastMsg_ = "save scene failed"; }
        pendingSceneSave_ = false; buildEditorPanels(); input_.setUi(&editorScene_.ui); return;
    }
    if (ht && pendingPrefabSave_) {
        std::string name = sanitizeProjectDirName(txt);
        if (!name.empty()) { editor_->setProjectRoot(project_.rootPath); if (editor_->saveAsPrefab(name)) lastMsg_ = "saved prefabs/" + name + ".prf"; else lastMsg_ = "prefab save failed"; }
        pendingPrefabSave_ = false; buildEditorPanels(); input_.setUi(&editorScene_.ui); return;
    }

    if (hn) {
        if (pendingKind_ == 2) {
            if (!nm.empty()) {
                std::string safe = sanitizeProjectDirName(nm);
                if (safe.size() >= 4 && safe.compare(safe.size() - 4, 4, ".lua") == 0) safe = safe.substr(0, safe.size() - 4);
                if (safe.empty()) safe = "script";
                std::string rel = "scripts/" + safe + ".lua";
                ProjectCreator::createScript(project_.rootPath, rel, safe); loadScript(rel);
                lastMsg_ = "script created: " + rel;
            }
        } else if (!nm.empty()) {
            pushUndo();
            if (pendingKind_ == 0) { editor_->addNode(pendingType_, nm, 640, 360); if (!pendingShape_.empty()) editor_->setShape(nm, pendingShape_); editor_->select(nm); lastMsg_ = "created " + nm; }
            else if (pendingKind_ == 1) { editor_->addUi(nm, nm, 580, 335, 120, 50, std::string(), parseColor("#808080")); editor_->selectUi(nm); lastMsg_ = "created button " + nm; }
        }
        pendingName_ = false; showCreate_ = false;
    }
    if (ht) {
        if (pendingRgb_ == 1) { unsigned c = 0; if (parseRgb(txt, c)) { pushUndo(); std::string uid = editor_->selectedUi(); if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) b->color = c; } else { Node* s = editor_->selected(); Particle2D* pep = dynamic_cast<Particle2D*>(s); if (pep) pep->color = c; else { Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr; if (n2) n2->color = c; } } } else lastMsg_ = "bad rgb, need r,g,b"; pendingRgb_ = 0; }
        else if (pendingRgb_ == 2) { unsigned c = 0; if (parseRgb(txt, c) && editor_->scene()) { pushUndo(); editor_->scene()->bg = colorToHex(c); } else lastMsg_ = "bad rgb, need r,g,b"; pendingRgb_ = 0; }
        else if (pendingRgb_ == 3) { unsigned c = 0; if (parseRgb(txt, c)) { pushUndo(); Particle2D* pe = dynamic_cast<Particle2D*>(editor_->selected()); if (pe) pe->color = c; } else lastMsg_ = "bad rgb, need r,g,b"; pendingRgb_ = 0; }
        else { std::string uid = editor_->selectedUi(); if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) { pushUndo(); b->text = txt; } } else { Node* s = editor_->selected(); if (s && std::string(s->typeName()) == "Label") { pushUndo(); static_cast<Label*>(s)->text = txt; } } }
    }
    if (ha) {
        std::string uid = editor_->selectedUi();
        if (!uid.empty()) { UiButton* b = editor_->findUi(uid); if (b) { pushUndo(); b->action = act; } }
        else { Node* s = editor_->selected(); Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr; if (n2) { pushUndo(); n2->action = act; } }
    }
    if (hnum) {
        if (pendingNumKind_ == "camzoom") {
            float v = (float)atof(num.c_str());
            if (v < 5.0f) v = 5.0f; if (v > 1000.0f) v = 1000.0f;
            Node* cn = editor_->scene() && editor_->scene()->root ? editor_->scene()->root->findByType("Camera2D") : nullptr;
            Camera2D* cm = cn ? static_cast<Camera2D*>(cn) : nullptr;
            if (cm) { pushUndo(); cm->zoom = v / 100.0f; }
            pendingNumKind_.clear();
        } else if (pendingNumKind_ == "camw" || pendingNumKind_ == "camh") {
            float v = (float)atof(num.c_str());
            if (v < 160.0f) v = 160.0f; if (v > 2160.0f) v = 2160.0f;
            if (pendingNumKind_ == "camw") projCamW_ = v; else projCamH_ = v;
            saveProjCamera();
            pendingNumKind_.clear();
        } else {
            float v = (float)atof(num.c_str());
            std::string uid = editor_->selectedUi();
            UiButton* b = uid.empty() ? nullptr : editor_->findUi(uid);
            Node* s = editor_->selected(); Node2D* n2 = s ? dynamic_cast<Node2D*>(s) : nullptr;
            bool lk2 = (n2 != nullptr) && n2->locked;
            if (!lk2 && (n2 || b)) pushUndo();
            if (!lk2 && n2) {
                if (pendingNumKind_ == "nx") { float dx = v - n2->position.x; n2->position.x = v; if (editor_->scene()) moveGroupButtons(editor_->scene(), n2->name, dx, 0); }
                else if (pendingNumKind_ == "ny") { float dy = v - n2->position.y; n2->position.y = v; if (editor_->scene()) moveGroupButtons(editor_->scene(), n2->name, 0, dy); }
                else if (pendingNumKind_ == "nrot") n2->rotation = v * 3.14159265f / 180.0f;
                else if (pendingNumKind_ == "nscl") { float f = v / 100.0f; if (f > 0.01f) { n2->scale.x = f; n2->scale.y = f; } }
                else if (pendingNumKind_ == "nw") n2->w = v;
                else if (pendingNumKind_ == "nh") n2->h = v;
                else if (pendingNumKind_ == "nalpha") { float a = v / 100.0f; if (a < 0) a = 0; if (a > 1) a = 1; n2->alpha = a; }
            }
            Particle2D* pe = dynamic_cast<Particle2D*>(s);
            if (!lk2 && pe) {
                if (pendingNumKind_ == "rate") pe->rate = v;
                else if (pendingNumKind_ == "life") { pe->life = v; if (pe->life < 0.05f) pe->life = 0.05f; }
                else if (pendingNumKind_ == "size") { pe->size = v; if (pe->size < 1.0f) pe->size = 1.0f; }
                else if (pendingNumKind_ == "spread") pe->spread = v;
                else if (pendingNumKind_ == "gravity") pe->gravity = v;
            }
            if (b) {
                if (pendingNumKind_ == "bx") b->touch.rect.x = v;
                else if (pendingNumKind_ == "by") b->touch.rect.y = v;
                else if (pendingNumKind_ == "bw") b->touch.rect.w = v;
                else if (pendingNumKind_ == "bh") b->touch.rect.h = v;
                else if (pendingNumKind_ == "bang") b->angle = v;
                else if (pendingNumKind_ == "balpha") { float a = v / 100.0f; if (a < 0) a = 0; if (a > 1) a = 1; b->alpha = a; }
            }
        }
    }
    if (hn || ht || ha || hnum) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }
}

inline std::string GameApp::stepEditor() {
    if (!editor_ || !editor_->scene()) {
        pendingName_ = false; pendingText_ = false; pendingAction_ = false; pendingNum_ = false; pendingRgb_ = 0;
        pendingSceneSave_ = false; pendingPrefabSave_ = false;
        pendingSound_ = false; pendingSoundTarget_.clear();
        clearDialogResults(); pendingNewProject_ = false; pendingHubRename_ = false; confirmDeleteDir_.clear();
        clearTransition();
        appMode_ = AppMode::Hub; rebuildHub(); return "";
    }
    consumeDialogResults();
    if (scriptMode_) imeApply();
    if (dragging_ || gizmoRot_ || gizmoSclX_ || gizmoSclY_ || gizmoRotUi_) { buildEditorPanels(); input_.setUi(&editorScene_.ui); }

    gameBackend_.begin(); Renderer gr(gameBackend_); gr.render(editorScene_, &ctx_);
    std::string out = gameBackend_.str();

    if (scriptMode_) {
        const int LINES = 24;
        for (int i = scriptScroll_; i < (int)scriptLines_.size() && i < scriptScroll_ + LINES; ++i) {
            float y = 70 + (float)(i - scriptScroll_) * 19;
            std::string txt = sanitizeLine(scriptLines_[i]);
            if (txt.size() > 68) txt = txt.substr(0, 68);
            out += "DRAW codeline|" + std::to_string(i) + "|" + txt + "|" + std::to_string((int)y) + "|14|" +
                   (i == curLine_ ? "#FFD700" : "#D8E0F0") + "\n";
        }
        if (curLine_ >= scriptScroll_ && curLine_ < scriptScroll_ + LINES && curLine_ < (int)scriptLines_.size()) {
            std::string shown = sanitizeLine(scriptLines_[curLine_]);
            if (shown.size() > 68) shown = shown.substr(0, 68);
            int cut = curCol_; if (cut < 0) cut = 0; if (cut > (int)shown.size()) cut = (int)shown.size();
            while (cut > 0 && cut < (int)shown.size() && ((unsigned char)shown[cut] & 0xC0) == 0x80) --cut;
            std::string pref = shown.substr(0, cut);
            float y = 70 + (float)(curLine_ - scriptScroll_) * 19;
            out += "DRAW caret|" + pref + "|340|" + std::to_string((int)y) + "|16|#4CC9F0\n";
        }
    }

    if (!scriptMode_ && !showSettings_ && !showPrefabs_) {
        out += "DRAW clipon\n";
        emitEditorViewport(*editor_->scene(), out, makeEditorRenderInput());
        Scene* es = editor_->scene();
        if (es && es->root) {
            std::string sel = editor_->selected() ? editor_->selected()->name : std::string();
            WorldXf ident;
            drawParticlePreviewTree(*es->root, ident, out, edZoom_, es->camX, es->camY, sel);
            drawTexturePreviewTree(*es->root, ident, out, edZoom_, es->camX, es->camY);

            std::vector<Node*> stack;
            stack.push_back(es->root.get());
            while (!stack.empty()) {
                Node* nd = stack.back(); stack.pop_back();
                for (auto& ch : nd->getChildren()) {
                    Node* cn = ch.get();
                    stack.push_back(cn);
                    Node2D* n2 = dynamic_cast<Node2D*>(cn);
                    if (!n2) continue;
                    auto sit = projSounds_.find(n2->name);
                    if (sit == projSounds_.end()) continue;
                    float sx = 0, sy = 0; proj(*es, n2->position.x, n2->position.y, sx, sy);
                    out += "DRAW rect|" + std::to_string((int)(sx - 14)) + "|" + std::to_string((int)(sy - 14)) + "|28|28|#2EC4B6|0\n";
                    out += "DRAW text|SND|" + std::to_string((int)(sx - 12)) + "|" + std::to_string((int)(sy - 6)) + "|12|#1A1A2E|0\n";
                    if (!sit->second.snd.empty()) {
                        std::string snm = sit->second.snd;
                        if (snm.size() > 12) snm = snm.substr(0, 12);
                        out += "DRAW text|" + snm + "|" + std::to_string((int)(sx - 20)) + "|" + std::to_string((int)(sy + 18)) + "|12|#2EC4B6|0\n";
                    }
                }
            }
        }
        emitEditorGizmos(*editor_->scene(), out, makeEditorRenderInput());

        {
            Node* snp = editor_ ? editor_->selected() : nullptr;
            if (snp && projSounds_.count(snp->name)) {
                SoundDef& sd = projSounds_[snp->name];
                out += "DRAW text|sound: " + (sd.snd.empty() ? std::string("-") : sd.snd) + "|310|498|14|#2EC4B6|0\n";
                out += "DRAW button|SET|310|516|56|26|#2EC4B6|0|\n";
                out += "DRAW button|LOOP|370|516|62|26|" + std::string(sd.loop ? "#FFD700" : "#3A4A6B") + "|0|\n";
                out += "DRAW button|AUTO|436|516|62|26|" + std::string(sd.autoplay ? "#FFD700" : "#3A4A6B") + "|0|\n";
                out += "DRAW button|PLAY|502|516|62|26|#40C040|0|\n";
                out += "DRAW button|STOP|568|516|62|26|#D62828|0|\n";
            }
        }

        // Отладочные хитбоксы в редакторе (внутри клипа вьюпорта).
        if (g_showBodies && es) {
            float Sd = 0.46875f * edZoom_;
            float OXd = 596.0f - Sd * (es->camX + 640.0f);
            float OYd = 310.0f - Sd * (es->camY + 360.0f);
            emitBodiesDebug(*es, out, Sd, OXd, OYd);
        }

        out += "DRAW clipoff\n";

        std::string wTxt = "W " + std::to_string((int)projCamW_);
        std::string hTxt = "H " + std::to_string((int)projCamH_);
        std::string oTxt = projVertical_ ? "ROT:PORT" : "ROT:LAND";
        out += "DRAW rect|" + std::to_string((int)CAM_PX0) + "|" + std::to_string((int)CAM_BTN_W_Y0) + "|" + std::to_string((int)(CAM_PX1 - CAM_PX0)) + "|" + std::to_string((int)(CAM_BTN_W_Y1 - CAM_BTN_W_Y0)) + "|#2A2F4AFF|0\n";
        out += "DRAW text|" + wTxt + "|" + std::to_string((int)CAM_PX0 + 6) + "|" + std::to_string((int)CAM_BTN_W_Y0 + 2) + "|14|#D8E0F0|0\n";
        out += "DRAW rect|" + std::to_string((int)CAM_PX0) + "|" + std::to_string((int)CAM_BTN_H_Y0) + "|" + std::to_string((int)(CAM_PX1 - CAM_PX0)) + "|" + std::to_string((int)(CAM_BTN_H_Y1 - CAM_BTN_H_Y0)) + "|#2A2F4AFF|0\n";
        out += "DRAW text|" + hTxt + "|" + std::to_string((int)CAM_PX0 + 6) + "|" + std::to_string((int)CAM_BTN_H_Y0 + 2) + "|14|#D8E0F0|0\n";
        out += "DRAW rect|" + std::to_string((int)CAM_PX0) + "|" + std::to_string((int)CAM_BTN_O_Y0) + "|" + std::to_string((int)(CAM_PX1 - CAM_PX0)) + "|" + std::to_string((int)(CAM_BTN_O_Y1 - CAM_BTN_O_Y0)) + "|#3A2F5AFF|0\n";
        out += "DRAW text|" + oTxt + "|" + std::to_string((int)CAM_PX0 + 6) + "|" + std::to_string((int)CAM_BTN_O_Y0 + 2) + "|14|#FFD700|0\n";
    }

    // Ряд функций: своя тёмная подложка, ниже рядов создания (y=656), не перекрывается.
    if (!scriptMode_ && showFuncs_) {
        int fy = (int)FNR_Y;
        out += "DRAW rect|300|" + std::to_string(fy - 4) + "|592|34|#14141C|0\n";
        out += "DRAW button|RIGIDBODY|300|" + std::to_string(fy) + "|110|26|#8E44AD|0|\n";
        out += "DRAW button|STATICBODY|414|" + std::to_string(fy) + "|110|26|#8E44AD|0|\n";
        out += "DRAW button|NOGRAV|528|" + std::to_string(fy) + "|80|26|#8E44AD|0|\n";
        out += "DRAW button|BOUNCY|612|" + std::to_string(fy) + "|80|26|#8E44AD|0|\n";
        out += "DRAW button|CLEAR|696|" + std::to_string(fy) + "|70|26|#555566|0|\n";
        out += "DRAW button|HBOX|770|" + std::to_string(fy) + "|80|26|" + std::string(g_showBodies ? "#33FF99" : "#555566") + "|0|\n";
        out += "DRAW button|X|860|" + std::to_string(fy) + "|32|26|#D62828|0|\n";
    }
    if (!scriptMode_ && funcMsgTimer_ > 0) {
        --funcMsgTimer_;
        out += "DRAW rect|300|64|592|26|#FFD700|0\n";
        out += "DRAW text|" + funcMsg_ + "|306|68|16|#1A1A2E|0\n";
    }
    if (!scriptMode_) {
        Node* fsn = editor_ ? editor_->selected() : nullptr;
        if (fsn) {
            std::string ff = funcsOf(fsn->name);
            out += "DRAW text|funcs: " + (ff.empty() ? std::string("-") : ff) + "|902|600|14|#7CFC00|0\n";
        }
    }

    processEditorActions();
    if (appMode_ != AppMode::Editor) return "";
    if (scriptMode_ && imeChanged_) { buildEditorPanels(); input_.setUi(&editorScene_.ui); imeChanged_ = false; }
    if (imeWantOn_) { out += "IME_ON\n"; imeWantOn_ = false; }
    if (imeWantOff_) { out += "IME_OFF\n"; imeWantOff_ = false; }
    if (pendingText_ && appMode_ == AppMode::Editor) { out += "REQ_TEXT|" + pendingTextCur_ + "\n"; pendingText_ = false; }
    if (pendingName_ && appMode_ == AppMode::Editor) out += "REQ_NAME|Object\n";
    if (pendingAction_ && appMode_ == AppMode::Editor) { out += "REQ_ACTION|" + pendingActionCur_ + "\n"; pendingAction_ = false; }
    if (pendingNum_ && appMode_ == AppMode::Editor) { out += "REQ_NUM|" + pendingNumCur_ + "\n"; pendingNum_ = false; }
    if (!pendingImportCategory_.empty() && appMode_ == AppMode::Editor) { out += "REQ_IMPORT|" + pendingImportCategory_ + "|" + project_.rootPath + "\n"; pendingImportCategory_.clear(); }
    drainSoundCmds(out);   // PLAY/STOP/MS звучат и гаснут прямо в редакторе
    out += "MODE|editor\n";
    out += "RES|1280|720\n";
    input_.endFrame(); return out;
}

} // namespace suka
