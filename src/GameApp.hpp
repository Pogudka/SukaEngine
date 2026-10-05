#pragma once

#include <string>
#include <memory>
#include <vector>
#include <set>
#include <map>
#include <cmath>
#include <mutex>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <algorithm>

#include "Core.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "Particles.hpp"
#include "Render.hpp"
#include "Touch.hpp"
#include "Input.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Hub.hpp"
#include "Editor.hpp"
#include "Script.hpp"
#include "Tween.hpp"

#include "CommonTypes.hpp"
#include "UiUtils.hpp"
#include "ProjectOps.hpp"
#include "SceneUtils.hpp"
#include "HubUI.hpp"
#include "EditorUI.hpp"
#include "EditorRender.hpp"
#include "LuaGameApi.hpp"
#include "Physics.hpp"

namespace suka {

static void walkEmitters(Node& n, const WorldXf& parent, float dt) {
    Node2D* n2 = dynamic_cast<Node2D*>(&n);
    if (!n2) { for (const auto& c : n.getChildren()) walkEmitters(*c, parent, dt); return; }
    WorldXf w = parent.child(n2->position, n2->rotation, n2->scale.x, n2->scale.y);
    if (std::string(n2->typeName()) == "Particle2D") {
        Particle2D* pe = static_cast<Particle2D*>(n2);
        if (pe->emitting) {
            SpawnOpts o;
            float avgScale = (w.sx + w.sy) * 0.5f;
            if (avgScale < 0.01f) avgScale = 0.01f;
            o.vx = pe->vx * avgScale; o.vy = pe->vy * avgScale; o.spread = pe->spread * avgScale;
            o.gravity = pe->gravity;
            o.life = pe->life; o.lifeSpread = pe->lifeSpread;
            o.size = pe->size * avgScale; o.sizeEnd = pe->sizeEnd * avgScale;
            o.drag = pe->drag; o.color = pe->color; o.rot = w.rot;
            for (int i = 0; i < 8; ++i) o.glyph[i] = 0;
            size_t gn = pe->glyph.size(); if (gn > 7) gn = 7;
            for (size_t i = 0; i < gn; ++i) o.glyph[i] = pe->glyph[i]; o.glyph[gn] = 0;
            int toSpawn = 0;
            if (pe->burstPending) { toSpawn += pe->burst; pe->burstPending = false; }
            if (pe->rate > 0.0f) { pe->acc += pe->rate * dt; int wh = (int)pe->acc; pe->acc -= wh; toSpawn += wh; }
            if (toSpawn > 0) g_particles.spawn(w.x, w.y, toSpawn, o);
        } else { pe->acc = 0.0f; }
    }
    for (const auto& c : n2->getChildren()) walkEmitters(*c, w, dt);
}

// Центр вьюпорта редактора: (596, 310) — вьюпорт 300,64 .. 892,556.
static void projEditor(float wx, float wy, float& sx, float& sy, float zoom, float camX, float camY) {
    float S = 0.46875f * zoom;
    sx = 596 + (wx - camX - 640) * S;
    sy = 310 + (wy - camY - 360) * S;
}

static void drawParticlePreviewTree(Node& n, const WorldXf& parent, std::string& out,
                                     float zoom, float camX, float camY, const std::string& sel) {
    Node2D* n2 = dynamic_cast<Node2D*>(&n);
    if (!n2) { for (const auto& c : n.getChildren()) drawParticlePreviewTree(*c, parent, out, zoom, camX, camY, sel); return; }
    WorldXf w = parent.child(n2->position, n2->rotation, n2->scale.x, n2->scale.y);
    if (std::string(n2->typeName()) == "Particle2D") {
        Particle2D* pe = static_cast<Particle2D*>(n2);
        bool isSel = (sel == n2->name);
        float avgScale = (w.sx + w.sy) * 0.5f; if (avgScale < 0.01f) avgScale = 0.01f;
        float base = pe->size * avgScale; if (base < 8.0f) base = 8.0f;
        unsigned col = pe->color;
        float alpha = isSel ? 1.0f : 0.55f;
        float sx = 0, sy = 0; projEditor(w.x, w.y, sx, sy, zoom, camX, camY);
        std::string markerGlyph = isSel ? std::string("\xe2\x97\x89") : pe->glyph;
        float markerSize = isSel ? base * 0.75f : base * 0.45f;
        out += std::string("DRAW text|") + markerGlyph + "|" + std::to_string((int)sx) + "|" + std::to_string((int)sy) + "|" + std::to_string((int)markerSize) + "|" + colorToHexA(withAlpha(col, alpha)) + "|" + std::to_string(w.rot * 57.2957795f) + "\n";
        if (isSel) {
            for (int i = 0; i < 12; ++i) {
                float ang = w.rot + (float)i * 0.5235987756f;
                float rad = base * (0.75f + (float)(i % 3) * 0.28f);
                float px = w.x + std::cos(ang) * rad;
                float py = w.y + std::sin(ang) * rad;
                float psx = 0, psy = 0; projEditor(px, py, psx, psy, zoom, camX, camY);
                float psz = base * 0.32f; if (psz < 4.0f) psz = 4.0f;
                out += std::string("DRAW text|") + pe->glyph + "|" + std::to_string((int)psx) + "|" + std::to_string((int)psy) + "|" + std::to_string((int)psz) + "|" + colorToHexA(withAlpha(col, 0.75f)) + "|" + std::to_string(w.rot * 57.2957795f) + "\n";
            }
        }
    }
    for (const auto& c : n2->getChildren()) drawParticlePreviewTree(*c, w, out, zoom, camX, camY, sel);
}

static void drawTexturePreviewTree(Node& n, const WorldXf& parent, std::string& out,
                                    float zoom, float camX, float camY) {
    Node2D* n2 = dynamic_cast<Node2D*>(&n);
    if (!n2) { for (const auto& c : n.getChildren()) drawTexturePreviewTree(*c, parent, out, zoom, camX, camY); return; }
    WorldXf w = parent.child(n2->position, n2->rotation, n2->scale.x, n2->scale.y);
    if (std::string(n2->typeName()) == "Prefab2D") return;
    Sprite2D* sp = dynamic_cast<Sprite2D*>(n2);
    std::string tex = !n2->texture.empty() ? n2->texture : (sp ? sp->texturePath : std::string());
    if (!tex.empty()) {
        float bw = sp ? sp->size.x : n2->w;
        float bh = sp ? sp->size.y : n2->h;
        float ww = bw * w.sx * zoom;
        float hh = bh * w.sy * zoom;
        float sx = 0, sy = 0;
        projEditor(w.x, w.y, sx, sy, zoom, camX, camY);
        out += "DRAW tex|" + resolveAssetPath(tex) + "|"
             + std::to_string((int)(sx - ww / 2)) + "|" + std::to_string((int)(sy - hh / 2)) + "|"
             + std::to_string((int)ww) + "|" + std::to_string((int)hh) + "|"
             + std::to_string(w.rot * 57.2957795f) + "\n";
    }
    for (const auto& c : n2->getChildren()) drawTexturePreviewTree(*c, w, out, zoom, camX, camY);
}

// Hit-зоны панелей редактора (логические 1280x720).
static const float CAM_PX0 = 792.0f, CAM_PX1 = 888.0f;
static const float CAM_BTN_W_Y0 = 66.0f,  CAM_BTN_W_Y1 = 90.0f;
static const float CAM_BTN_H_Y0 = 92.0f,  CAM_BTN_H_Y1 = 116.0f;
static const float CAM_BTN_O_Y0 = 118.0f, CAM_BTN_O_Y1 = 142.0f;

// Ряд кнопок функций (ниже рядов создания 560..644, со своей подложкой).
static const float FNR_Y = 656.0f, FNR_H = 26.0f;
static const float FNR_RIG_X0 = 300.0f, FNR_RIG_X1 = 410.0f;
static const float FNR_STA_X0 = 414.0f, FNR_STA_X1 = 524.0f;
static const float FNR_NOG_X0 = 528.0f, FNR_NOG_X1 = 608.0f;
static const float FNR_BOU_X0 = 612.0f, FNR_BOU_X1 = 692.0f;
static const float FNR_CLR_X0 = 696.0f, FNR_CLR_X1 = 766.0f;
static const float FNR_X_X0  = 860.0f, FNR_X_X1  = 892.0f;
static const float FNR_HB_X0 = 770.0f, FNR_HB_X1 = 850.0f;   // кнопка HBOX в ряду функций

// Панель звука — ВНУТРИ вьюпорта (низ), чтобы не налезала на инспектор.
static const float SNP_Y0 = 516.0f, SNP_Y1 = 542.0f;
static const float SNP_SET_X0 = 310.0f,  SNP_SET_X1 = 366.0f;
static const float SNP_LOOP_X0 = 370.0f, SNP_LOOP_X1 = 432.0f;
static const float SNP_AUTO_X0 = 436.0f, SNP_AUTO_X1 = 498.0f;
static const float SNP_PLAY_X0 = 502.0f, SNP_PLAY_X1 = 564.0f;
static const float SNP_STOP_X0 = 568.0f, SNP_STOP_X1 = 630.0f;

class GameApp {
public:
    using Manip = suka::Manip;
    enum class Mode { Console, String };
    enum class AppMode { Hub, Game, Editor };

    struct SoundDef {
        std::string snd;
        bool loop = false;
        bool autoplay = false;
    };

    bool init(Mode mode, const std::string& gameDir) {
        (void)mode;
        loadSettings();
        hubState_.games = ProjectList::scan();
        hubState_.selectedDir = gameDir;
        if (hubState_.selectedDir.empty() && !hubState_.games.empty()) hubState_.selectedDir = hubState_.games.front().dir;
        logicW_ = 1280.0f; logicH_ = 720.0f;
        input_.screenWidth = logicW_; input_.screenHeight = logicH_;
        orientVertical_ = false; emitOrient_ = false; orientName_ = "landscape";
        projCamW_ = 1280.0f; projCamH_ = 720.0f; projVertical_ = false;
        screenRatio_ = 0.0f;
        clearTransition();
        rebuildHub();
        appMode_ = AppMode::Hub;
        return true;
    }

    void submitText(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); textRes_ = t; hasText_ = true; }
    void submitName(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); nameRes_ = t; hasName_ = true; }
    void submitAction(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); actionRes_ = t; hasAction_ = true; }
    void submitNumber(const std::string& t) { std::lock_guard<std::mutex> lk(dlgMtx_); numRes_ = t; hasNum_ = true; }
    void submitScriptText(const std::string& t) { std::lock_guard<std::mutex> lk(imeMtx_); imeTextQ_.push_back(t); }
    void submitScriptCompose(const std::string& t) { std::lock_guard<std::mutex> lk(imeMtx_); imeCompQ_.push_back(t); }
    void submitScriptFinish() { std::lock_guard<std::mutex> lk(imeMtx_); imeFinish_ = true; }
    void submitScriptKey(int k) { std::lock_guard<std::mutex> lk(imeMtx_); imeKeyQ_.push_back(k); }
    void submitImportFile(const std::string& category, const std::string& relativePath);

    void feedMultiTouch(int phase, float x0, float y0, float x1, float y1);
    void feedTouch(int action, float x, float y);

    std::string stepFrame() {
        consumeSysRatio();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (lastMs_ > 0) { float dt = (ms - lastMs_) / 1000.0f; if (dt > 0.0001f) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f; }
        lastMs_ = ms;
        if (appMode_ == AppMode::Hub) return stepHub();
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
    void consumeSysRatio() {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        if (hasAction_ && !actionRes_.empty() && actionRes_.rfind("sys:ratio|", 0) == 0) {
            float r = (float)atof(actionRes_.c_str() + 10);
            screenRatio_ = (r > 0.2f && r < 5.0f) ? r : 0.0f;
            hasAction_ = false; actionRes_.clear();
        }
    }
    void queueNodeSound(const std::string& nm) {
        auto it = projSounds_.find(nm);
        if (it == projSounds_.end() || it->second.snd.empty()) return;
        if (it->second.loop) lua_play_music(it->second.snd, true);
        else lua_play_sound(it->second.snd);
    }
    static double jsonNum(const std::string& s, const std::string& key, double def) {
        size_t p = s.find(key);
        if (p == std::string::npos) return def;
        p += key.size();
        while (p < s.size() && (s[p] == ':' || s[p] == ' ' || s[p] == '\t')) ++p;
        const char* c = s.c_str() + p;
        char* end = nullptr;
        double v = std::strtod(c, &end);
        if (end == c) return def;
        return v;
    }
    static bool jsonBool(const std::string& s, const std::string& key, bool def) {
        size_t p = s.find(key);
        if (p == std::string::npos) return def;
        p += key.size();
        while (p < s.size() && (s[p] == ':' || s[p] == ' ' || s[p] == '\t')) ++p;
        if (s.compare(p, 4, "true") == 0) return true;
        if (s.compare(p, 5, "false") == 0) return false;
        return def;
    }
    void loadProjCamera(const std::string& root) {
        projCamW_ = 1280.0f; projCamH_ = 720.0f; projVertical_ = false;
        if (root.empty()) return;
        std::string path = root + "/editor_camera.json";
        if (!fileExists(path)) return;
        std::string s = readFile(path);
        double w = jsonNum(s, "\"w\"", 1280.0);
        double h = jsonNum(s, "\"h\"", 720.0);
        if (w < 160.0) w = 160.0; if (w > 2160.0) w = 2160.0;
        if (h < 160.0) h = 160.0; if (h > 2160.0) h = 2160.0;
        projCamW_ = (float)w; projCamH_ = (float)h;
        projVertical_ = jsonBool(s, "\"vertical\"", false);
    }
    void saveProjCamera() {
        if (project_.rootPath.empty()) return;
        std::ofstream f(project_.rootPath + "/editor_camera.json");
        if (!f.good()) return;
        f << "{\"w\":" << (int)projCamW_ << ",\"h\":" << (int)projCamH_
          << ",\"vertical\":" << (projVertical_ ? "true" : "false") << "}\n";
        f.close();
    }
    void applyScreenRatio() {
        if (screenRatio_ <= 0.0f) return;
        float h = projCamH_;
        float w = h * screenRatio_;
        if (w < 160.0f) w = 160.0f;
        if (w > 2160.0f) w = 2160.0f;
        logicW_ = w; logicH_ = h;
        input_.screenWidth = logicW_; input_.screenHeight = logicH_;
    }
    void clearTransition() {
        transActive_ = false; transType_ = 0; transPhase_ = 0;
        transProgress_ = 0.0f; transDuration_ = 0.4f;
        transOffset_ = 0.0f; transAlpha_ = 0.0f; transTarget_.clear();
    }
    void proj(const Scene& sc, float wx, float wy, float& sx, float& sy) { float S = 0.46875f * edZoom_; sx = 596 + (wx - sc.camX - 640) * S; sy = 310 + (wy - sc.camY - 360) * S; }
    void unproj(const Scene& sc, float sx, float sy, float& wx, float& wy) { float S = 0.46875f * edZoom_; wx = 640 + sc.camX + (sx - 596) / S; wy = 360 + sc.camY + (sy - 310) / S; }
    void unprojGame(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        float hx = logicW_ * 0.5f, hy = logicH_ * 0.5f;
        Node* cn = sc.root ? sc.root->findByType("Camera2D") : nullptr;
        if (cn) { Camera2D* cam = static_cast<Camera2D*>(cn); float z = cam->zoom > 0.01f ? cam->zoom : 1.0f; wx = (sx - hx) / z + cam->position.x; wy = (sy - hy) / z + cam->position.y; }
        else { wx = sx; wy = sy; }
    }
    std::string hitUi(Scene* es, float wx, float wy) {
        if (!es) return "";
        for (auto& b : es->ui) if (wx >= b.touch.rect.x && wx <= b.touch.rect.x + b.touch.rect.w && wy >= b.touch.rect.y && wy <= b.touch.rect.y + b.touch.rect.h) return b.touch.id;
        return "";
    }
    void startDragParent(Scene* es, const std::string& name) {
        dragOX_ = 0; dragOY_ = 0; dragPSX_ = 1; dragPSY_ = 1;
        if (es && es->root) { bool found = false; parentTransform(es->root.get(), name, 0, 0, 1, 1, dragOX_, dragOY_, dragPSX_, dragPSY_, found); }
        if (dragPSX_ < 0.01f) dragPSX_ = 1; if (dragPSY_ < 0.01f) dragPSY_ = 1;
    }
    void setNodeTexture(const std::string& name, const std::string& rel) {
        // Аудиофайлы текстурой не назначаются: объект бы «исчез» (shape=none, битмапа нет).
        size_t dguard = rel.find_last_of('.');
        if (dguard != std::string::npos) {
            std::string exg = rel.substr(dguard + 1);
            for (auto& cg : exg) cg = (char)std::tolower((unsigned char)cg);
            if (exg == "mp3" || exg == "ogg" || exg == "wav") return;
        }
        editor_->setTexture(name, rel);
        Node2D* n = editor_->find2d(name);
        if (!n) return;
        Sprite2D* sp = dynamic_cast<Sprite2D*>(n);
        if (sp) sp->texturePath = rel;
        if (!rel.empty()) n->shape = "none";
    }
    void clearDialogResults() {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        hasText_ = false; hasName_ = false; hasAction_ = false; hasNum_ = false;
    }
    bool takeNameResult(std::string& out) {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        if (!hasName_) return false; out = nameRes_; hasName_ = false; return true;
    }
    EditorUiInput makeEditorUiInput() {
        return EditorUiInput{
            editor_.get(), scriptMode_, edZoom_, manip_, pickParent_, showCreate_, showAssets_, showSettings_, showPrefabs_, showFiles_,
            hierScroll_, fsScroll_, assetScroll_, scriptScroll_, prefabScroll_,
            collapsed_, fsPath_, project_.rootPath, pendingDeleteFile_, scriptPath_, scriptLines_,
            curLine_, curCol_, imeShown_, g_luaLog, showFuncs_
        };
    }
    EditorRenderInput makeEditorRenderInput() { return EditorRenderInput{ editor_.get(), edZoom_, manip_, pickParent_, pickChild_, lastMsg_, fps_, projCamW_, projCamH_, projVertical_, screenRatio_ }; }
    void buildEditorPanels() { editorScene_ = buildEditorScene(makeEditorUiInput()); input_.setUi(&editorScene_.ui); }
    std::string funcsOf(const std::string& nm) { auto it = projFuncs_.find(nm); return it == projFuncs_.end() ? std::string() : it->second; }

    // ---- объявления крупных методов (тела в файлах-частях) ----
    void rebuildHub();
    std::string stepHub();
    bool enterGame(const std::string& dir);
    std::string stepGame();
    void runAction(const std::string& act);
    void processUi();
    bool enterEditor(const std::string& dir);
    int applyEditorAction(const std::string& act);
    void processEditorActions();
    void consumeDialogResults();
    std::string stepEditor();
    void startTransition(int type, const std::string& target, float duration);
    void performSceneChange(const std::string& target);
    void updateTransition(float dt);
    void consumeLuaCmd();
    void ensureGameButtons();
    void drainSoundCmds(std::string& out);
    void drainCollideEvents();
    void consumeOverlayAction();
    void attachChildTo(const std::string& child, const std::string& parent);
    void detachChild(const std::string& child);
    void pushUndo();
    bool loadSnap(const std::string& rel);
    bool doUndo();
    bool doRedo();
    void loadScript(const std::string& rel);
    void saveScript();
    void attachScript(const std::string& name);
    void saveVars();
    void loadVars();
    void scTypeChar(char c);
    void scCompose(const std::string& text);
    void scCommit(const std::string& text);
    void scFinish() { compAnchor_ = -1; }
    void scBackspace();
    void scMove(int d);
    void scClampView();
    void imeApply();
    void addFuncToSelected(const std::string& fn);
    void clearFuncsSelected();
    void loadProjFuncs();
    void saveProjFuncs();
    void applyProjFuncs();

public:
    // ---- поля ----
    AppMode appMode_ = AppMode::Hub;
    HubState hubState_;
    Scene hubScene_;
    Scene editorScene_;
    std::unique_ptr<Editor> editor_;
    ScriptSystem scripts_;
    std::set<std::string> scripted_;
    Manip manip_ = Manip::Move;
    bool showCreate_ = false;
    bool showAssets_ = false;
    bool showSettings_ = false;
    bool showPrefabs_ = false;
    bool showFiles_ = true;
    bool playFromEditor_ = false;
    std::string lastEditorDir_;
    std::string pendingDeleteFile_;
    bool dragging_ = false;
    bool pendingText_ = false;
    bool pinching_ = false;
    bool pendingName_ = false;
    bool pendingAction_ = false;
    bool pendingNum_ = false;
    bool pendingSceneSave_ = false;
    bool pendingPrefabSave_ = false;
    int pendingKind_ = 0;
    int pendingRgb_ = 0;
    std::string pendingNumKind_;
    std::string pendingNumCur_;
    bool gizmoRot_ = false;
    bool gizmoSclX_ = false;
    bool gizmoSclY_ = false;
    bool gizmoRotUi_ = false;
    int lockAxis_ = 0;
    float gizmoStartAngle_ = 0;
    float gizmoStartRot_ = 0;
    float gizmoStartDist_ = 1;
    float gizmoStartSX_ = 1;
    float gizmoStartSY_ = 1;
    bool pickParent_ = false;
    std::string pickChild_;
    std::string pendingType_;
    std::string pendingShape_;
    std::string pendingActionCur_;
    std::string pendingNodeAction_;
    std::string lastMsg_;
    float edZoom_ = 1.0f;
    float pinchDist0_ = 0;
    float pinchZoom0_ = 1.0f;
    float pinchAX_ = 0;
    float pinchAY_ = 0;
    float dragOX_ = 0;
    float dragOY_ = 0;
    float dragPSX_ = 1;
    float dragPSY_ = 1;
    int hierScroll_ = 0;
    int fsScroll_ = 0;
    int assetScroll_ = 0;
    int prefabScroll_ = 0;
    std::set<std::string> collapsed_;
    std::vector<std::string> undoStack_;
    std::vector<std::string> redoStack_;
    int snapCounter_ = 0;
    std::unique_ptr<Node> clipboard_;
    int clipCounter_ = 0;
    bool dbg_ = false;
    float fps_ = 0;
    long long lastMs_ = 0;
    int nodeCount_ = 0;
    int lastDraws_ = 0;
    bool scriptMode_ = false;
    std::string scriptPath_;
    std::vector<std::string> scriptLines_;
    int curLine_ = 0;
    int curCol_ = 0;
    int scriptScroll_ = 0;
    int compAnchor_ = -1;
    bool imeWantOn_ = false;
    bool imeWantOff_ = false;
    bool imeChanged_ = false;
    bool imeShown_ = false;
    std::mutex imeMtx_;
    std::vector<std::string> imeTextQ_;
    std::vector<std::string> imeCompQ_;
    std::vector<int> imeKeyQ_;
    bool imeFinish_ = false;
    Node2D* dragNode_ = nullptr;
    UiButton* dragUi_ = nullptr;
    int createCounter_ = 0;
    std::string pendingTextCur_;
    std::string fsPath_;
    std::string pendingImportCategory_;
    std::mutex dlgMtx_;
    bool hasText_ = false;
    bool hasName_ = false;
    bool hasAction_ = false;
    bool hasNum_ = false;
    std::string textRes_;
    std::string nameRes_;
    std::string actionRes_;
    std::string numRes_;
    bool pendingLoadVars_ = false;
    bool saveVarsEnabled_ = false;
    bool pendingNewProject_ = false;
    bool pendingHubRename_ = false;
    std::string pendingHubDir_;
    std::string pendingHubCurrentName_;
    std::string confirmDeleteDir_;
    float saveTimer_ = 0.0f;
    ProjectInfo project_;
    std::string fontPath_;
    ResourceManager resources_;
    std::unique_ptr<SceneManager> sceneMgr_;
    InputManager input_;
    TouchProcessor touch_;
    StringRenderBackend gameBackend_;
    Context ctx_;

    float logicW_ = 1280.0f;
    float logicH_ = 720.0f;
    bool orientVertical_ = false;
    bool emitOrient_ = false;
    std::string orientName_ = "landscape";

    float projCamW_ = 1280.0f;
    float projCamH_ = 720.0f;
    bool projVertical_ = false;
    float screenRatio_ = 0.0f;

    bool transActive_ = false;
    int transType_ = 0;
    int transPhase_ = 0;
    float transProgress_ = 0.0f;
    float transDuration_ = 0.4f;
    float transOffset_ = 0.0f;
    float transAlpha_ = 0.0f;
    std::string transTarget_;

    std::map<std::string, std::string> projFuncs_;
    bool showFuncs_ = false;
    std::string funcMsg_;
    int funcMsgTimer_ = 0;
    std::map<std::string, SoundDef> projSounds_;
    bool pendingSound_ = false;
    std::string pendingSoundTarget_;
};

} // namespace suka

#include "GameAppHub.hpp"
#include "GameAppTouch.hpp"
#include "GameAppGame.hpp"
#include "GameAppEditor.hpp"
