#pragma once

#include <string>
#include <memory>
#include <vector>
#include <set>
#include <cmath>
#include <mutex>
#include <chrono>
#include <fstream>
#include <cstdio>

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
#include "Script.hpp"

#include "CommonTypes.hpp"
#include "UiUtils.hpp"
#include "ProjectOps.hpp"
#include "SceneUtils.hpp"
#include "HubUI.hpp"
#include "EditorUI.hpp"
#include "EditorRender.hpp"

namespace suka {

class GameApp {
public:
    using Manip = suka::Manip;

    enum class Mode {
        Console,
        String
    };

    enum class AppMode {
        Hub,
        Game,
        Editor
    };

    bool init(Mode mode, const std::string& gameDir) {
        (void)mode;

        loadSettings();

        hubState_.games = ProjectList::scan();
        hubState_.selectedDir = gameDir;

        if (hubState_.selectedDir.empty() && !hubState_.games.empty()) {
            hubState_.selectedDir = hubState_.games.front().dir;
        }

        input_.screenWidth = 1280.0f;
        input_.screenHeight = 720.0f;

        rebuildHub();

        appMode_ = AppMode::Hub;

        return true;
    }

    void submitText(const std::string& t) {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        textRes_ = t;
        hasText_ = true;
    }

    void submitName(const std::string& t) {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        nameRes_ = t;
        hasName_ = true;
    }

    void submitAction(const std::string& t) {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        actionRes_ = t;
        hasAction_ = true;
    }

    void submitNumber(const std::string& t) {
        std::lock_guard<std::mutex> lk(dlgMtx_);
        numRes_ = t;
        hasNum_ = true;
    }

    void submitScriptText(const std::string& t) {
        std::lock_guard<std::mutex> lk(imeMtx_);
        imeTextQ_.push_back(t);
    }

    void submitScriptCompose(const std::string& t) {
        std::lock_guard<std::mutex> lk(imeMtx_);
        imeCompQ_.push_back(t);
    }

    void submitScriptFinish() {
        std::lock_guard<std::mutex> lk(imeMtx_);
        imeFinish_ = true;
    }

    void submitScriptKey(int k) {
        std::lock_guard<std::mutex> lk(imeMtx_);
        imeKeyQ_.push_back(k);
    }

    void feedMultiTouch(int phase, float x0, float y0, float x1, float y1) {
        if (appMode_ != AppMode::Editor || scriptMode_ || showCreate_) return;

        Scene* es = editor_ ? editor_->scene() : nullptr;
        if (!es) return;

        float mx = (x0 + x1) / 2.0f;
        float my = (y0 + y1) / 2.0f;
        float dist = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));

        if (phase == 1) {
            pinching_ = true;
            pinchDist0_ = dist;
            pinchZoom0_ = edZoom_;

            float S = 0.46875f * edZoom_;

            pinchAX_ = 640 + es->camX + (mx - 596) / S;
            pinchAY_ = 360 + es->camY + (my - 310) / S;

            return;
        }

        if (phase == 3) {
            pinching_ = false;
            return;
        }

        if (!pinching_) return;

        float z = pinchZoom0_;

        if (pinchDist0_ > 4 && dist > 4) {
            z = pinchZoom0_ * (dist / pinchDist0_);

            if (z < 0.4f) z = 0.4f;
            if (z > 3.0f) z = 3.0f;
        }

        float S = 0.46875f * z;

        es->camX = pinchAX_ - 640 - (mx - 596) / S;
        es->camY = pinchAY_ - 360 - (my - 310) / S;

        edZoom_ = z;
    }

    void feedTouch(int action, float x, float y) {
        RawTouch t;

        if (action == 0) t.action = RawTouch::Action::Down;
        else if (action == 2) t.action = RawTouch::Action::Move;
        else t.action = RawTouch::Action::Up;

        t.x = x;
        t.y = y;

        Scene* cur = uiScene();
        if (!cur) return;

        if (appMode_ == AppMode::Editor && scriptMode_) {
            if (t.action == RawTouch::Action::Down && x >= 300 && x <= 850 && y >= 64 && y <= 556) {
                const float LH = 19;

                int line = scriptScroll_ + (int)((y - 70) / LH);

                if (line < 0) line = 0;
                if (line >= (int)scriptLines_.size()) line = (int)scriptLines_.size() - 1;

                int colCp = (int)((x - 340) / 7.2f);

                const std::string& L = scriptLines_[line];
                int total = utf8ByteToCp(L, (int)L.size());

                if (colCp < 0) colCp = 0;
                if (colCp > total) colCp = total;

                curLine_ = line;
                curCol_ = utf8CpToByte(L, colCp);

                compAnchor_ = -1;

                imeWantOn_ = true;
                imeShown_ = true;
                imeChanged_ = true;
            }

            touch_.onTouch(t, *cur, input_);
            return;
        }

        if (appMode_ == AppMode::Game && t.action == RawTouch::Action::Down && sceneMgr_ && sceneMgr_->current()) {
            Scene* gs = sceneMgr_->current();

            float wx, wy;
            unprojGame(*gs, x, y, wx, wy);

            std::string hit = hitTest(gs->root.get(), wx, wy);

            if (!hit.empty()) {
                Node* fn = gs->root->findNode(hit);
                Node2D* n = fn ? dynamic_cast<Node2D*>(fn) : nullptr;

                if (n && !n->action.empty()) {
                    pendingNodeAction_ = n->action;
                    return;
                }
            }
        }

        if (appMode_ == AppMode::Editor && !scriptMode_ && !showCreate_) {
            Scene* es = editor_ ? editor_->scene() : nullptr;
            float Z = edZoom_;

            if (t.action == RawTouch::Action::Down && pickParent_ && !pickChild_.empty() && es && es->root) {
                float wx, wy;
                unproj(*es, x, y, wx, wy);

                std::string hit = hitTest(es->root.get(), wx, wy);

                if (!hit.empty() && hit != pickChild_) {
                    pushUndo();
                    attachChildTo(pickChild_, hit);

                    lastMsg_ = "attached " + pickChild_ + " -> " + hit;

                    if (pickChild_.rfind("UI:", 0) == 0) {
                        editor_->selectUi(pickChild_.substr(3));
                    } else {
                        editor_->select(pickChild_);
                    }
                } else {
                    lastMsg_ = hit.empty() ? "no target under tap" : "cannot attach to self";
                }

                pickParent_ = false;
                pickChild_.clear();

                buildEditorPanels();
                input_.setUi(&editorScene_.ui);

                return;
            }

            UiButton* gb = (editor_ && !editor_->selectedUi().empty())
                ? editor_->findUi(editor_->selectedUi())
                : nullptr;

            if (gb && es) {
                float bcx, bcy;
                proj(*es, gb->touch.rect.x + gb->touch.rect.w / 2, gb->touch.rect.y + gb->touch.rect.h / 2, bcx, bcy);

                if (t.action == RawTouch::Action::Down) {
                    float dx = x - bcx;
                    float dy = y - bcy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    float R = 70.0f * Z;

                    if (manip_ == Manip::Rotate && std::fabs(dist - R) < 26.0f * Z) {
                        gizmoRotUi_ = true;
                        gizmoStartAngle_ = std::atan2(dy, dx);
                        gizmoStartRot_ = gb->angle;

                        return;
                    }
                } else if (t.action == RawTouch::Action::Move && gizmoRotUi_) {
                    float dx = x - bcx;
                    float dy = y - bcy;

                    gb->angle = gizmoStartRot_ + (std::atan2(dy, dx) - gizmoStartAngle_) * 57.2957795f;

                    return;
                } else if (t.action == RawTouch::Action::Up) {
                    gizmoRotUi_ = false;
                }
            }

            Node2D* g = (editor_ && editor_->selectedUi().empty())
                ? (editor_->selected() ? dynamic_cast<Node2D*>(editor_->selected()) : nullptr)
                : nullptr;

            if (g && es) {
                float gwx, gwy, gwr, gsx, gsy;

                if (!nodeWorld(es, g->name, gwx, gwy, gwr, gsx, gsy)) {
                    gwx = g->position.x;
                    gwy = g->position.y;
                    gsx = gsy = 1;
                }

                float scx, scy;
                proj(*es, gwx, gwy, scx, scy);

                if (t.action == RawTouch::Action::Down) {
                    if (!g->locked) {
                        float dx = x - scx;
                        float dy = y - scy;
                        float dist = std::sqrt(dx * dx + dy * dy);

                        if (manip_ == Manip::Rotate) {
                            float R = 70.0f * Z;

                            if (std::fabs(dist - R) < 26.0f * Z) {
                                gizmoRot_ = true;
                                gizmoStartAngle_ = std::atan2(dy, dx);
                                gizmoStartRot_ = g->rotation;

                                return;
                            }
                        } else if (manip_ == Manip::Scale) {
                            float hw = (g->w * gsx) * 0.46875f * Z / 2;
                            float hh = (g->h * gsy) * 0.46875f * Z / 2;

                            if (std::fabs(x - (scx + hw + 24 * Z)) < 28 * Z && std::fabs(dy) < 28 * Z) {
                                gizmoSclX_ = true;
                                gizmoStartDist_ = dist > 1 ? dist : 1;
                                gizmoStartSX_ = g->scale.x;

                                return;
                            }

                            if (std::fabs(y - (scy + hh + 24 * Z)) < 28 * Z && std::fabs(dx) < 28 * Z) {
                                gizmoSclY_ = true;
                                gizmoStartDist_ = dist > 1 ? dist : 1;
                                gizmoStartSY_ = g->scale.y;

                                return;
                            }
                        } else {
                            if (std::fabs(dy) < 16 * Z && dx > 8 * Z && dx < 64 * Z) {
                                lockAxis_ = 1;
                                dragging_ = true;
                                dragNode_ = g;

                                editor_->select(g->name);
                                startDragParent(es, g->name);

                                return;
                            }

                            if (std::fabs(dx) < 16 * Z && dy > 8 * Z && dy < 64 * Z) {
                                lockAxis_ = 2;
                                dragging_ = true;
                                dragNode_ = g;

                                editor_->select(g->name);
                                startDragParent(es, g->name);

                                return;
                            }
                        }
                    }
                } else if (t.action == RawTouch::Action::Move) {
                    if (gizmoRot_) {
                        float dx = x - scx;
                        float dy = y - scy;

                        g->rotation = gizmoStartRot_ + (std::atan2(dy, dx) - gizmoStartAngle_);

                        return;
                    }

                    if (gizmoSclX_ || gizmoSclY_) {
                        float dist = std::sqrt((x - scx) * (x - scx) + (y - scy) * (y - scy));
                        float f = dist / gizmoStartDist_;

                        if (f < 0.05f) f = 0.05f;

                        if (gizmoSclX_) g->scale.x = gizmoStartSX_ * f;
                        if (gizmoSclY_) g->scale.y = gizmoStartSY_ * f;

                        return;
                    }
                } else if (t.action == RawTouch::Action::Up) {
                    gizmoRot_ = false;
                    gizmoSclX_ = false;
                    gizmoSclY_ = false;
                    lockAxis_ = 0;
                }
            }

            const float VX0 = 300;
            const float VY0 = 64;
            const float VW = 592;
            const float VH = 492;

            bool inVP = (x >= VX0 && x <= VX0 + VW && y >= VY0 && y <= VY0 + VH);

            if (t.action == RawTouch::Action::Down && inVP && es) {
                float wx, wy;
                unproj(*es, x, y, wx, wy);

                std::string uiHit = hitUi(es, wx, wy);

                if (!uiHit.empty()) {
                    editor_->selectUi(uiHit);

                    dragUi_ = editor_->findUi(uiHit);
                    dragging_ = (dragUi_ != nullptr);
                    lockAxis_ = 0;
                } else if (es->root) {
                    std::string hit = hitTest(es->root.get(), wx, wy);

                    if (!hit.empty()) {
                        editor_->select(hit);

                        Node2D* hitN = editor_->find2d(hit);

                        if (hitN && !hitN->locked) {
                            dragNode_ = hitN;
                            dragging_ = true;
                            lockAxis_ = 0;

                            startDragParent(es, hit);
                        } else {
                            dragNode_ = nullptr;
                            dragging_ = false;
                        }
                    }
                }
            } else if (t.action == RawTouch::Action::Move && dragging_ && es) {
                float wx, wy;
                unproj(*es, x, y, wx, wy);

                if (dragUi_) {
                    dragUi_->touch.rect.x = wx - dragUi_->touch.rect.w / 2;
                    dragUi_->touch.rect.y = wy - dragUi_->touch.rect.h / 2;
                } else if (dragNode_) {
                    float oldX = dragOX_ + dragNode_->position.x * dragPSX_;
                    float oldY = dragOY_ + dragNode_->position.y * dragPSY_;

                    float nx, ny;

                    if (lockAxis_ == 1) {
                        nx = wx;
                        ny = oldY;
                    } else if (lockAxis_ == 2) {
                        nx = oldX;
                        ny = wy;
                    } else {
                        nx = wx;
                        ny = wy;
                    }

                    float dlx = (nx - oldX) / dragPSX_;
                    float dly = (ny - oldY) / dragPSY_;

                    dragNode_->position.x += dlx;
                    dragNode_->position.y += dly;

                    moveGroupButtons(es, dragNode_->name, nx - oldX, ny - oldY);
                }
            } else if (t.action == RawTouch::Action::Up) {
                dragging_ = false;
                dragNode_ = nullptr;
                dragUi_ = nullptr;
                lockAxis_ = 0;
            }
        }

        touch_.onTouch(t, *cur, input_);
    }

    std::string stepFrame() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();

        if (lastMs_ > 0) {
            float dt = (ms - lastMs_) / 1000.0f;

            if (dt > 0.0001f) {
                fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f;
            }
        }

        lastMs_ = ms;

        if (appMode_ == AppMode::Hub) return stepHub();
        if (appMode_ == AppMode::Editor) return stepEditor();

        return stepGame();
    }

    SceneManager& sceneMgr() {
        return *sceneMgr_;
    }

    ProjectInfo& project() {
        return project_;
    }

private:
    Scene* uiScene() {
        if (appMode_ == AppMode::Hub) return &hubScene_;
        if (appMode_ == AppMode::Editor) return &editorScene_;

        return (sceneMgr_ && sceneMgr_->current()) ? sceneMgr_->current() : nullptr;
    }

    void proj(const Scene& sc, float wx, float wy, float& sx, float& sy) {
        float S = 0.46875f * edZoom_;

        sx = 596 + (wx - sc.camX - 640) * S;
        sy = 310 + (wy - sc.camY - 360) * S;
    }

    void unproj(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        float S = 0.46875f * edZoom_;

        wx = 640 + sc.camX + (sx - 596) / S;
        wy = 360 + sc.camY + (sy - 310) / S;
    }

    void unprojGame(const Scene& sc, float sx, float sy, float& wx, float& wy) {
        Node* cn = sc.root ? sc.root->findByType("Camera2D") : nullptr;

        if (cn) {
            Camera2D* cam = static_cast<Camera2D*>(cn);
            float z = cam->zoom > 0.01f ? cam->zoom : 1.0f;

            wx = (sx - 640) / z + cam->position.x;
            wy = (sy - 360) / z + cam->position.y;
        } else {
            wx = sx;
            wy = sy;
        }
    }

    std::string hitUi(Scene* es, float wx, float wy) {
        if (!es) return "";

        for (auto& b : es->ui) {
            if (wx >= b.touch.rect.x && wx <= b.touch.rect.x + b.touch.rect.w &&
                wy >= b.touch.rect.y && wy <= b.touch.rect.y + b.touch.rect.h) {
                return b.touch.id;
            }
        }

        return "";
    }

    void startDragParent(Scene* es, const std::string& name) {
        dragOX_ = 0;
        dragOY_ = 0;
        dragPSX_ = 1;
        dragPSY_ = 1;

        if (es && es->root) {
            bool found = false;

            parentTransform(
                es->root.get(),
                name,
                0, 0, 1, 1,
                dragOX_, dragOY_, dragPSX_, dragPSY_,
                found
            );
        }

        if (dragPSX_ < 0.01f) dragPSX_ = 1;
        if (dragPSY_ < 0.01f) dragPSY_ = 1;
    }

    void attachChildTo(const std::string& child, const std::string& parent) {
        if (!editor_ || !editor_->scene()) return;

        Scene* sc = editor_->scene();

        if (child.rfind("UI:", 0) == 0) {
            UiButton* b = editor_->findUi(child.substr(3));
            if (b) b->group = parent;

            return;
        }

        if (!sc || !sc->root) return;

        Node* root = sc->root.get();

        if (child == parent) return;

        Node* cn = root->findNode(child);
        Node* pn = root->findNode(parent);

        if (!cn || !pn) return;
        if (cn->containsName(parent)) return;

        Node* owner = root->findParentOf(child);

        if (!owner || owner == pn) return;

        std::unique_ptr<Node> up = owner->takeChild(child);

        if (up) pn->addChild(std::move(up));
    }

    void detachChild(const std::string& child) {
        if (!editor_ || !editor_->scene()) return;

        Scene* sc = editor_->scene();

        if (child.rfind("UI:", 0) == 0) {
            UiButton* b = editor_->findUi(child.substr(3));
            if (b) b->group.clear();

            return;
        }

        if (!sc || !sc->root) return;

        Node* root = sc->root.get();
        Node* owner = root->findParentOf(child);

        if (!owner || owner == root) return;

        std::unique_ptr<Node> up = owner->takeChild(child);

        if (up) root->addChild(std::move(up));
    }

    void pushUndo() {
        if (!editor_ || !editor_->scene()) return;

        std::string rel = "snap_" + std::to_string(snapCounter_++) + ".json";

        editor_->save(project_.rootPath + "/" + rel);

        undoStack_.push_back(rel);

        if (undoStack_.size() > 12) {
            undoStack_.erase(undoStack_.begin());
        }

        redoStack_.clear();
    }

    void loadSnap(const std::string& rel) {
        if (!sceneMgr_) return;

        if (sceneMgr_->restartScene(rel, resources_)) {
            editor_->attach(sceneMgr_->current());

            scripted_.clear();

            dragging_ = false;
            dragNode_ = nullptr;
            dragUi_ = nullptr;
            pickParent_ = false;

            buildEditorPanels();
            input_.setUi(&editorScene_.ui);
        }
    }

    void doUndo() {
        if (!editor_ || undoStack_.empty()) {
            lastMsg_ = "nothing to undo";
            return;
        }

        std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";

        editor_->save(project_.rootPath + "/" + cur);
        redoStack_.push_back(cur);

        std::string rel = undoStack_.back();
        undoStack_.pop_back();

        loadSnap(rel);

        lastMsg_ = "undo";
    }

    void doRedo() {
        if (!editor_ || redoStack_.empty()) {
            lastMsg_ = "nothing to redo";
            return;
        }

        std::string cur = "snap_" + std::to_string(snapCounter_++) + ".json";

        editor_->save(project_.rootPath + "/" + cur);
        undoStack_.push_back(cur);

        std::string rel = redoStack_.back();
        redoStack_.pop_back();

        loadSnap(rel);

        lastMsg_ = "redo";
    }

    void loadScript(const std::string& rel) {
        scriptPath_ = rel;

        std::string s = readFile(project_.rootPath + "/" + rel);

        scriptLines_.clear();

        std::string cur;

        for (char c : s) {
            if (c == '\n') {
                scriptLines_.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }

        scriptLines_.push_back(cur);

        if (scriptLines_.empty()) scriptLines_.push_back("");

        curLine_ = 0;
        curCol_ = 0;
        scriptScroll_ = 0;
        compAnchor_ = -1;
    }

    void saveScript() {
        if (scriptPath_.empty()) return;

        std::ofstream f(project_.rootPath + "/" + scriptPath_);

        for (size_t i = 0; i < scriptLines_.size(); ++i) {
            f << scriptLines_[i];

            if (i + 1 < scriptLines_.size()) f << "\n";
        }

        f.close();

        scripts_.load(project_.rootPath);

        lastMsg_ = "script saved: " + scriptPath_;
    }

    void attachScript(const std::string& name) {
        std::string rel = "scripts/" + name + ".lua";

        ProjectCreator::createScript(project_.rootPath, rel, name);

        scripts_.load(project_.rootPath);

        scripted_.insert(name);
    }

    void clearDialogResults() {
        std::lock_guard<std::mutex> lk(dlgMtx_);

        hasText_ = false;
        hasName_ = false;
        hasAction_ = false;
        hasNum_ = false;
    }

    bool takeNameResult(std::string& out) {
        std::lock_guard<std::mutex> lk(dlgMtx_);

        if (!hasName_) return false;

        out = nameRes_;
        hasName_ = false;

        return true;
    }

    void scTypeChar(char c) {
        if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");

        std::string& L = scriptLines_[curLine_];

        if (c == '\n') {
            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();

            std::string tail = L.substr(curCol_);
            L = L.substr(0, curCol_);

            scriptLines_.insert(scriptLines_.begin() + curLine_ + 1, tail);

            curLine_++;
            curCol_ = 0;
        } else {
            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();

            L.insert(L.begin() + curCol_, c);
            curCol_++;
        }

        scClampView();
    }

    void scCompose(const std::string& text) {
        if (curLine_ >= (int)scriptLines_.size()) scriptLines_.push_back("");

        std::string& L = scriptLines_[curLine_];

        if (compAnchor_ < 0 || compAnchor_ > (int)L.size()) compAnchor_ = curCol_;

        if (curCol_ > compAnchor_) {
            L.erase(L.begin() + compAnchor_, L.begin() + curCol_);
            curCol_ = compAnchor_;
        }

        for (char c : text) {
            if (c == '\n') c = ' ';

            if (curCol_ > (int)L.size()) curCol_ = (int)L.size();

            L.insert(L.begin() + curCol_, c);
            curCol_++;
        }

        scClampView();
    }

    void scCommit(const std::string& text) {
        if (compAnchor_ >= 0) {
            std::string& L = scriptLines_[curLine_];

            if (compAnchor_ <= (int)L.size() && curCol_ > compAnchor_) {
                L.erase(L.begin() + compAnchor_, L.begin() + curCol_);
                curCol_ = compAnchor_;
            }

            compAnchor_ = -1;
        }

        for (char c : text) scTypeChar(c);
    }

    void scFinish() {
        compAnchor_ = -1;
    }

    void scBackspace() {
        compAnchor_ = -1;

        if (curLine_ >= (int)scriptLines_.size()) return;

        std::string& L = scriptLines_[curLine_];

        if (curCol_ > 0) {
            int p = utf8Prev(L, curCol_);

            L.erase(L.begin() + p, L.begin() + curCol_);
            curCol_ = p;
        } else if (curLine_ > 0) {
            size_t prevLen = scriptLines_[curLine_ - 1].size();

            scriptLines_[curLine_ - 1] += L;
            scriptLines_.erase(scriptLines_.begin() + curLine_);

            curLine_--;
            curCol_ = (int)prevLen;
        }

        scClampView();
    }

    void scMove(int d) {
        compAnchor_ = -1;

        if (curLine_ >= (int)scriptLines_.size()) curLine_ = (int)scriptLines_.size() - 1;

        std::string& L = scriptLines_[curLine_];

        if (curCol_ > (int)L.size()) curCol_ = (int)L.size();

        if (d < 0) {
            if (curCol_ > 0) {
                curCol_ = utf8Prev(L, curCol_);
            } else if (curLine_ > 0) {
                curLine_--;
                curCol_ = (int)scriptLines_[curLine_].size();
            }
        } else {
            if (curCol_ < (int)L.size()) {
                curCol_ = utf8Next(L, curCol_);
            } else if (curLine_ + 1 < (int)scriptLines_.size()) {
                curLine_++;
                curCol_ = 0;
            }
        }

        scClampView();
    }

    void scClampView() {
        const int LINES = 24;

        if (curLine_ < scriptScroll_) scriptScroll_ = curLine_;
        if (curLine_ >= scriptScroll_ + LINES) scriptScroll_ = curLine_ - LINES + 1;
        if (scriptScroll_ < 0) scriptScroll_ = 0;
    }

    void imeApply() {
        std::vector<std::string> tq;
        std::vector<std::string> cq;
        std::vector<int> kq;
        bool fin = false;

        {
            std::lock_guard<std::mutex> lk(imeMtx_);

            tq.swap(imeTextQ_);
            cq.swap(imeCompQ_);
            kq.swap(imeKeyQ_);

            fin = imeFinish_;
            imeFinish_ = false;
        }

        if (tq.empty() && cq.empty() && kq.empty() && !fin) return;

        for (auto& s : cq) scCompose(s);
        for (auto& s : tq) scCommit(s);

        if (fin) scFinish();

        for (int k : kq) {
            if (k == 67) {
                scBackspace();
            } else if (k == 66) {
                compAnchor_ = -1;
                scTypeChar('\n');
            } else if (k == 21) {
                scMove(-1);
            } else if (k == 22) {
                scMove(1);
            }
        }

        imeChanged_ = true;
    }

    void rebuildHub() {
        hubState_.games = ProjectList::scan();

        HubUiInput hin{
            hubState_.games,
            hubState_.selectedDir,
            confirmDeleteDir_
        };

        hubScene_ = buildHubScene(hin);

        input_.setUi(&hubScene_.ui);
    }

    EditorUiInput makeEditorUiInput() {
        return EditorUiInput{
            editor_.get(),
            scriptMode_,
            edZoom_,
            manip_,
            pickParent_,
            showCreate_,
            hierScroll_,
            fsScroll_,
            scriptScroll_,
            collapsed_,
            fsPath_,
            project_.rootPath,
            scriptPath_,
            scriptLines_,
            curLine_,
            curCol_,
            imeShown_,
            g_luaLog
        };
    }

    EditorRenderInput makeEditorRenderInput() {
        return EditorRenderInput{
            editor_.get(),
            edZoom_,
            manip_,
            pickParent_,
            pickChild_,
            lastMsg_,
            fps_
        };
    }

    void buildEditorPanels() {
        editorScene_ = buildEditorScene(makeEditorUiInput());
        input_.setUi(&editorScene_.ui);
    }

    std::string stepHub() {
        if (!pendingNewProject_ && !pendingHubRename_) {
            clearDialogResults();
        }

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

                if (!nm.empty() && !pendingHubDir_.empty()) {
                    std::string root = projectsDir() + pendingHubDir_;
                    setProjectDisplayName(root, nm);
                }

                pendingHubDir_.clear();
                pendingHubCurrentName_.clear();

                rebuildHub();
            }
        }

        gameBackend_.begin();

        Renderer r(gameBackend_);
        r.render(hubScene_, nullptr);

        std::string out = gameBackend_.str();

        HubAct a = processHubScene(hubScene_);

        if (a.kind == 3) {
            if (!pendingNewProject_) {
                pendingNewProject_ = true;
                pendingHubRename_ = false;
                confirmDeleteDir_.clear();
            }
        } else if (a.kind == 4) {
            hubState_.selectedDir = a.dir;
            confirmDeleteDir_.clear();

            rebuildHub();
        } else if (a.kind == 5) {
            cycleTheme();
            saveSettings();

            confirmDeleteDir_.clear();

            rebuildHub();
        } else if (a.kind == 6) {
            std::string root = projectsDir() + hubState_.selectedDir;

            bool cur = projectWantsSave(root);
            setProjectSaveFlag(root, !cur);

            confirmDeleteDir_.clear();

            rebuildHub();
        } else if (a.kind == 7) {
            if (!hubState_.selectedDir.empty()) {
                pendingHubDir_ = hubState_.selectedDir;
                pendingHubCurrentName_ = sanitizeLine(projectDisplayName(pendingHubDir_));

                pendingHubRename_ = true;
                pendingNewProject_ = false;
                confirmDeleteDir_.clear();
            }
        } else if (a.kind == 8) {
            confirmDeleteDir_ = hubState_.selectedDir;

            rebuildHub();
        } else if (a.kind == 9) {
            if (!hubState_.selectedDir.empty() && confirmDeleteDir_ == hubState_.selectedDir) {
                std::string oldDir = hubState_.selectedDir;
                std::string root = projectsDir() + oldDir;

                removePathRecursive(root);

                sceneMgr_.reset();
                editor_.reset();

                hubState_.games = ProjectList::scan();

                bool stillExists = false;

                for (const auto& g : hubState_.games) {
                    if (g.dir == oldDir) {
                        stillExists = true;
                        break;
                    }
                }

                if (!stillExists) {
                    if (!hubState_.games.empty()) {
                        hubState_.selectedDir = hubState_.games.front().dir;
                    } else {
                        hubState_.selectedDir.clear();
                    }
                }

                confirmDeleteDir_.clear();

                rebuildHub();
            }
        } else if (a.kind == 10) {
            if (!hubState_.selectedDir.empty()) {
                std::string sv = projectsDir() + hubState_.selectedDir + "/save.vars";
                std::remove(sv.c_str());
            }

            confirmDeleteDir_.clear();

            rebuildHub();
        } else if (a.kind == 1) {
            confirmDeleteDir_.clear();
            pendingHubRename_ = false;
            pendingNewProject_ = false;

            enterGame(a.dir);
        } else if (a.kind == 2) {
            confirmDeleteDir_.clear();
            pendingHubRename_ = false;
            pendingNewProject_ = false;

            enterEditor(a.dir);
        }

        if (pendingNewProject_) {
            out += "REQ_NAME|Project\n";
        } else if (pendingHubRename_) {
            out += "REQ_NAME|" + pendingHubCurrentName_ + "\n";
        }

        input_.endFrame();

        return out;
    }

    bool enterGame(const std::string& dir) {
        ProjectInfo pi;

        if (!ProjectLoader::load(projectsDir() + dir + "/project.json", pi)) {
            return false;
        }

        project_ = pi;
        g_projectRoot = project_.rootPath;

        fontPath_ = pi.rootPath + "/" + pi.defaultFont;

        if (!fileExists(fontPath_)) {
            fontPath_ = std::string(PROJECT_ROOT) + "/assets/fonts/Ubuntu-Regular.ttf";
        }

        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);

        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) {
            return false;
        }

        scripts_.load(pi.rootPath);

        ctx_ = Context();
        g_luaLog.clear();

        dbg_ = false;

        saveVarsEnabled_ = projectWantsSave(pi.rootPath);
        pendingLoadVars_ = saveVarsEnabled_;
        saveTimer_ = 0.0f;

        pendingNewProject_ = false;
        pendingHubRename_ = false;
        confirmDeleteDir_.clear();

        clearDialogResults();

        Scene* sc = sceneMgr_->current();

        UiButton close;
        close.touch.id = "close";
        close.touch.rect = Rect{1180, 10, 90, 70};
        close.text = "X";
        close.action = "hub:";
        close.color = parseColor("#D62828");

        sc->ui.push_back(close);

        UiButton dbg;
        dbg.touch.id = "dbg";
        dbg.touch.rect = Rect{1080, 10, 90, 70};
        dbg.text = "DBG";
        dbg.action = "dbg:";
        dbg.color = parseColor("#808080");

        sc->ui.push_back(dbg);

        input_.setUi(&sc->ui);
        touch_.resetJoystick();

        appMode_ = AppMode::Game;

        return true;
    }

    std::string stepGame() {
        if (!sceneMgr_ || !sceneMgr_->current()) return "";

        clearDialogResults();

        ctx_.coinCollectedThisFrame = false;
        ctx_.jumpPressedThisFrame = false;
        ctx_.input = input_.state();

        if (!pendingNodeAction_.empty()) {
            runAction(pendingNodeAction_);
            pendingNodeAction_.clear();
        }

        processUi();

        if (appMode_ != AppMode::Game) return "";

        sceneMgr_->update(ctx_, 1.0 / 60.0, input_, resources_);
        scripts_.update(*sceneMgr_->current(), ctx_, 1.0 / 60.0, *sceneMgr_, ctx_.vars);

        if (pendingLoadVars_) {
            if (saveVarsEnabled_) {
                loadVarsFromFile(project_.rootPath, ctx_);
            }

            pendingLoadVars_ = false;
        }

        if (saveVarsEnabled_) {
            saveTimer_ += 1.0f / 60.0f;

            if (saveTimer_ >= 1.0f) {
                saveVarsToFile(project_.rootPath, ctx_);
                saveTimer_ = 0.0f;
            }
        }

        std::string out;

        if (ctx_.coinCollectedThisFrame) out += "SOUND coin\n";
        if (ctx_.jumpPressedThisFrame) out += "SOUND jump\n";

        gameBackend_.begin();

        Renderer renderer(gameBackend_);
        renderer.render(*sceneMgr_->current(), &ctx_);

        out += gameBackend_.str();

        if (dbg_) {
            nodeCount_ = countNodes(sceneMgr_->current()->root.get());
            lastDraws_ = 0;

            for (size_t i = 0; i + 4 < out.size(); ++i) {
                if (out[i] == 'D' && out[i + 1] == 'R' && out[i + 2] == 'A' && out[i + 3] == 'W') {
                    ++lastDraws_;
                }
            }

            out += "DRAW text|fps " + std::to_string((int)fps_) +
                   "  nodes " + std::to_string(nodeCount_) +
                   "  draws " + std::to_string(lastDraws_) +
                   "|20|100|18|#FFD700|0\n";

            out += "DRAW text|vars " + std::to_string((int)ctx_.vars.size()) +
                   "  score " + std::to_string(ctx_.score) +
                   "|20|124|18|#FFD700|0\n";

            size_t ln = g_luaLog.size();
            int show = ln > 4 ? 4 : (int)ln;

            for (int i = 0; i < show; ++i) {
                out += "DRAW text|" + g_luaLog[ln - show + i] +
                       "|20|" + std::to_string(148 + i * 20) +
                       "|16|#87CEEB|0\n";
            }
        }

        input_.endFrame();

        return out;
    }

    void runAction(const std::string& act) {
        Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr;
        if (!sc) return;

        if (act == "dbg:") {
            dbg_ = !dbg_;
            return;
        }

        const std::string pRestart = "restart_scene:";
        const std::string pChange = "change_scene:";
        const std::string pAdd = "add_var:";
        const std::string pSet = "set_var:";
        const std::string pHub = "hub:";
        const std::string pCall = "call:";

        if (act.rfind(pHub, 0) == 0) {
            if (saveVarsEnabled_) {
                saveVarsToFile(project_.rootPath, ctx_);
            }

            appMode_ = AppMode::Hub;

            pendingNewProject_ = false;
            pendingHubRename_ = false;
            confirmDeleteDir_.clear();

            clearDialogResults();

            rebuildHub();
        } else if (act.rfind(pRestart, 0) == 0) {
            sceneMgr_->requestChange(act.substr(pRestart.size()), true);
        } else if (act.rfind(pChange, 0) == 0) {
            sceneMgr_->requestChange(act.substr(pChange.size()), false);
        } else if (act.rfind(pCall, 0) == 0) {
            scripts_.callGlobal(act.substr(pCall.size()), ctx_, *sceneMgr_, ctx_.vars);
        } else if (act.rfind(pAdd, 0) == 0 || act.rfind(pSet, 0) == 0) {
            bool isAdd = act.rfind(pAdd, 0) == 0;

            std::string rest = act.substr(isAdd ? pAdd.size() : pSet.size());
            size_t c = rest.find(':');

            if (c != std::string::npos) {
                std::string name = rest.substr(0, c);
                double v = atof(rest.substr(c + 1).c_str());

                if (isAdd) ctx_.vars[name] += v;
                else ctx_.vars[name] = v;
            }
        }
    }

    void processUi() {
        Scene* sc = sceneMgr_ ? sceneMgr_->current() : nullptr;
        if (!sc) return;

        for (auto& b : sc->ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            runAction(b.action);

            if (appMode_ != AppMode::Game) return;
        }
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;

        if (!ProjectLoader::load(projectsDir() + dir + "/project.json", pi)) {
            appMode_ = AppMode::Hub;
            rebuildHub();

            return false;
        }

        project_ = pi;
        g_projectRoot = project_.rootPath;

        fontPath_ = pi.rootPath + "/" + pi.defaultFont;

        if (!fileExists(fontPath_)) {
            fontPath_ = std::string(PROJECT_ROOT) + "/assets/fonts/Ubuntu-Regular.ttf";
        }

        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);

        if (!sceneMgr_->restartScene("scenes/main.json", resources_)) {
            if (!sceneMgr_->restartScene(pi.mainScene, resources_)) {
                appMode_ = AppMode::Hub;
                rebuildHub();

                return false;
            }
        }

        editor_ = std::make_unique<Editor>();
        editor_->attach(sceneMgr_->current());

        showCreate_ = false;

        pendingText_ = false;
        pendingName_ = false;
        pendingAction_ = false;
        pendingNum_ = false;
        pendingRgb_ = 0;

        fsPath_ = "";
        manip_ = Manip::Move;

        pinching_ = false;

        hierScroll_ = 0;
        fsScroll_ = 0;

        pickParent_ = false;
        lastMsg_.clear();

        edZoom_ = 1.0f;

        undoStack_.clear();
        redoStack_.clear();

        clipboard_.reset();

        scriptMode_ = false;
        scriptPath_.clear();
        scriptLines_.clear();

        compAnchor_ = -1;
        imeShown_ = false;

        pendingLoadVars_ = false;
        saveVarsEnabled_ = false;
        saveTimer_ = 0.0f;

        pendingNewProject_ = false;
        pendingHubRename_ = false;
        confirmDeleteDir_.clear();

        clearDialogResults();

        buildEditorPanels();

        input_.setUi(&editorScene_.ui);
        touch_.resetJoystick();

        appMode_ = AppMode::Editor;

        return true;
    }

    void processEditorActions() {
        if (!editor_) return;

        Node* sn = editor_->selected();

        std::string sel = sn ? sn->name : std::string{};
        std::string selUi = editor_->selectedUi();

        UiButton* ub = selUi.empty() ? nullptr : editor_->findUi(selUi);
        Node2D* s2 = (!sel.empty()) ? editor_->find2d(sel) : nullptr;

        bool lk = (s2 != nullptr) && s2->locked;
        bool changed = false;

        for (auto& b : editorScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            if (b.action == "kb_toggle") {
                if (imeShown_) {
                    imeWantOff_ = true;
                    imeShown_ = false;
                } else {
                    imeWantOn_ = true;
                    imeShown_ = true;
                }

                changed = true;
            } else if (b.action == "tab_scripts") {
                if (!scriptMode_) {
                    scriptMode_ = true;

                    if (scriptPath_.empty()) loadScript("scripts/main.lua");

                    imeWantOn_ = true;
                    imeShown_ = true;

                    changed = true;
                }
            } else if (b.action == "tab_scene") {
                if (scriptMode_) {
                    scriptMode_ = false;
