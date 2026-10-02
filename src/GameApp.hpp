#pragma once

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <set>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <chrono>
#include <fstream>
#include <cctype>
#include <cstdio>

#include <dirent.h>
#include <sys/stat.h>

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

namespace suka {

inline unsigned dimColor(unsigned c, float k) {
    unsigned r = (unsigned)(((c >> 24) & 255) * k);
    unsigned g = (unsigned)(((c >> 16) & 255) * k);
    unsigned b = (unsigned)(((c >> 8)  & 255) * k);
    unsigned a = (c & 255);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

inline std::string sanitizeLine(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '|') o += '/';
        else if (c == '\t') o += "  ";
        else if ((unsigned char)c < 32) o += ' ';
        else o += c;
    }
    return o;
}

inline bool utf8Cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

inline int utf8Prev(const std::string& s, int pos) {
    if (pos <= 0) return 0;
    --pos;
    while (pos > 0 && utf8Cont(s[pos])) --pos;
    return pos;
}

inline int utf8Next(const std::string& s, int pos) {
    if (pos >= (int)s.size()) return (int)s.size();
    ++pos;
    while (pos < (int)s.size() && utf8Cont(s[pos])) ++pos;
    return pos;
}

inline int utf8CpToByte(const std::string& s, int cp) {
    int p = 0;
    for (int i = 0; i < cp && p < (int)s.size(); ++i) p = utf8Next(s, p);
    return p;
}

inline int utf8ByteToCp(const std::string& s, int bytePos) {
    int c = 0, p = 0;
    while (p < bytePos && p < (int)s.size()) {
        p = utf8Next(s, p);
        ++c;
    }
    return c;
}

inline std::string rgbStr(unsigned c) {
    return std::to_string((c >> 24) & 255) + "," +
           std::to_string((c >> 16) & 255) + "," +
           std::to_string((c >> 8) & 255);
}

inline bool parseRgb(const std::string& s, unsigned& out) {
    int v[3];
    int idx = 0;
    std::string num;

    for (size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && isdigit((unsigned char)s[i])) {
            num += s[i];
            continue;
        }
        if (!num.empty()) {
            if (idx < 3) v[idx++] = atoi(num.c_str());
            num.clear();
        }
    }

    if (idx < 3) return false;

    for (int k = 0; k < 3; ++k) {
        if (v[k] < 0) v[k] = 0;
        if (v[k] > 255) v[k] = 255;
    }

    out = ((unsigned)v[0] << 24) | ((unsigned)v[1] << 16) | ((unsigned)v[2] << 8) | 0xFFu;
    return true;
}

inline std::string escapeJsonString(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back((char)c);
        } else if (c < 32) {
            o.push_back(' ');
        } else {
            o.push_back((char)c);
        }
    }
    return o;
}

inline std::string sanitizeProjectDirName(const std::string& raw) {
    std::string o;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.') {
            o.push_back((char)c);
        } else if (c == ' ') {
            o.push_back('_');
        }
    }

    while (!o.empty() && o[0] == '.') o.erase(o.begin());
    if (o.size() > 32) o.resize(32);
    if (o.empty()) o = "Project";
    return o;
}

inline std::string safeProjectDisplayName(const std::string& raw) {
    std::string o;
    for (unsigned char c : raw) {
        if (c == '"' || c == '\\' || c == '|') continue;
        if (c < 32) o.push_back(' ');
        else o.push_back((char)c);
    }

    size_t a = 0, b = o.size();
    while (a < b && std::isspace((unsigned char)o[a])) ++a;
    while (b > a && std::isspace((unsigned char)o[b - 1])) --b;
    o = o.substr(a, b - a);

    if (o.empty()) o = "Project";
    if (o.size() > 48) o.resize(48);
    return o;
}

inline bool removePathRecursive(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;

    if (!S_ISDIR(st.st_mode)) {
        return std::remove(path.c_str()) == 0;
    }

    DIR* dir = opendir(path.c_str());
    if (!dir) return false;

    bool ok = true;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;

        std::string child = path + "/" + name;
        if (!removePathRecursive(child)) ok = false;
    }
    closedir(dir);

    if (rmdir(path.c_str()) != 0) ok = false;
    return ok;
}

inline bool setProjectDisplayName(const std::string& root, const std::string& newName) {
    std::string path = root + "/project.json";
    std::string s = readFile(path);
    if (s.empty()) return false;

    std::string clean = safeProjectDisplayName(newName);
    std::string esc = escapeJsonString(clean);

    size_t key = s.find("\"name\"");
    if (key != std::string::npos) {
        size_t colon = s.find(':', key + 6);
        if (colon == std::string::npos) return false;

        size_t q1 = s.find('"', colon + 1);
        if (q1 == std::string::npos) return false;

        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) return false;

        s.replace(q1 + 1, q2 - q1 - 1, esc);
    } else {
        size_t last = s.rfind('}');
        if (last == std::string::npos) return false;

        size_t ins = last;
        while (ins > 0 && std::isspace((unsigned char)s[ins - 1])) --ins;

        if (ins > 0 && s[ins - 1] != '{') s.insert(ins, ",\n  ");
        else s.insert(ins, "\n  ");

        s.insert(ins, "\"name\": \"" + esc + "\"");
    }

    std::ofstream f(path);
    if (!f.good()) return false;
    f << s;
    f.close();
    return true;
}

class GameApp {
public:
    enum class Mode { Console, String };
    enum class AppMode { Hub, Game, Editor };
    enum class Manip { Move, Rotate, Scale };

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

                    if (pickChild_.rfind("UI:", 0) == 0) editor_->selectUi(pickChild_.substr(3));
                    else editor_->select(pickChild_);
                } else {
                    lastMsg_ = hit.empty() ? "no target under tap" : "cannot attach to self";
                }

                pickParent_ = false;
                pickChild_.clear();
                buildEditorPanels();
                input_.setUi(&editorScene_.ui);
                return;
            }

            UiButton* gb = (!editor_->selectedUi().empty()) ? editor_->findUi(editor_->selectedUi()) : nullptr;
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

            const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
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
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (lastMs_ > 0) {
            float dt = (ms - lastMs_) / 1000.0f;
            if (dt > 0.0001f) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f;
        }
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
        for (auto& b : es->ui) {
            if (wx >= b.touch.rect.x && wx <= b.touch.rect.x + b.touch.rect.w &&
                wy >= b.touch.rect.y && wy <= b.touch.rect.y + b.touch.rect.h) {
                return b.touch.id;
            }
        }
        return "";
    }

    static int countNodes(const Node* n) {
        if (!n) return 0;
        int c = 1;
        for (auto& ch : n->getChildren()) c += countNodes(ch.get());
        return c;
    }

    static bool nodeWorldRec(Node* n, const std::string& name,
                             float ox, float oy, float orot, float osx, float osy,
                             float& wx, float& wy, float& wr, float& wsx, float& wsy) {
        Node2D* n2d = dynamic_cast<Node2D*>(n);
        if (!n2d) {
            for (auto& ch : n->getChildren()) {
                if (nodeWorldRec(ch.get(), name, ox, oy, orot, osx, osy, wx, wy, wr, wsx, wsy)) return true;
            }
            return false;
        }

        float cr = std::cos(orot);
        float sr = std::sin(orot);

        float cx = ox + (n2d->position.x * osx) * cr - (n2d->position.y * osy) * sr;
        float cy = oy + (n2d->position.x * osx) * sr + (n2d->position.y * osy) * cr;
        float crot = orot + n2d->rotation;
        float csx = osx * n2d->scale.x;
        float csy = osy * n2d->scale.y;

        if (n2d->name == name) {
            wx = cx;
            wy = cy;
            wr = crot;
            wsx = csx;
            wsy = csy;
            return true;
        }

        for (auto& ch : n2d->getChildren()) {
            if (nodeWorldRec(ch.get(), name, cx, cy, crot, csx, csy, wx, wy, wr, wsx, wsy)) return true;
        }

        return false;
    }

    static bool nodeWorld(Scene* sc, const std::string& name,
                          float& wx, float& wy, float& wr, float& wsx, float& wsy) {
        if (!sc || !sc->root) return false;
        return nodeWorldRec(sc->root.get(), name, 0, 0, 0, 1, 1, wx, wy, wr, wsx, wsy);
    }

    void parentXf(Node* n, const std::string& name,
                  float cx, float cy, float sx, float sy,
                  float& ox, float& oy, float& psx, float& psy, bool& found) {
        if (found) return;

        if (n->name == name) {
            ox = cx;
            oy = cy;
            psx = sx;
            psy = sy;
            found = true;
            return;
        }

        float ncx = cx, ncy = cy, nsx = sx, nsy = sy;
        Node2D* n2 = dynamic_cast<Node2D*>(n);

        if (n2) {
            float cr = std::cos(n2->rotation);
            float sr = std::sin(n2->rotation);

            ncx = cx + (n2->position.x * sx) * cr - (n2->position.y * sy) * sr;
            ncy = cy + (n2->position.x * sx) * sr + (n2->position.y * sy) * cr;
            nsx = sx * n2->scale.x;
            nsy = sy * n2->scale.y;
        }

        for (auto& ch : n->getChildren()) {
            parentXf(ch.get(), name, ncx, ncy, nsx, nsy, ox, oy, psx, psy, found);
        }
    }

    void startDragParent(Scene* es, const std::string& name) {
        dragOX_ = 0;
        dragOY_ = 0;
        dragPSX_ = 1;
        dragPSY_ = 1;

        if (es->root) {
            bool f = false;
            parentXf(es->root.get(), name, 0, 0, 1, 1, dragOX_, dragOY_, dragPSX_, dragPSY_, f);
        }

        if (dragPSX_ < 0.01f) dragPSX_ = 1;
        if (dragPSY_ < 0.01f) dragPSY_ = 1;
    }

    void moveGroupButtons(Scene* es, const std::string& group, float dx, float dy) {
        for (auto& b : es->ui) {
            if (b.group == group) {
                b.touch.rect.x += dx;
                b.touch.rect.y += dy;
            }
        }
    }

    void attachChildTo(const std::string& child, const std::string& parent) {
        if (!editor_ || !editor_->scene()) return;

        Scene* sc = editor_->scene();

        if (child.rfind("UI:", 0) == 0) {
            UiButton* b = editor_->findUi(child.substr(3));
            if (b) b->group = parent;
            return;
        }

        if (!sc->root) return;

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

        if (!sc->root) return;

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
        if (undoStack_.size() > 12) undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }

    void loadSnap(const std::string& rel) {
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

    static bool projectWantsSave(const std::string& root) {
        std::string s = readFile(root + "/project.json");
        size_t p = s.find("\"save_vars\"");
        if (p == std::string::npos) return false;

        p = s.find(':', p + 11);
        if (p == std::string::npos) return false;
        ++p;

        while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
        if (p >= s.size()) return false;

        char c = s[p];
        if (c == 't' || c == 'T' || c == '1') return true;
        if (c == 'f' || c == 'F' || c == '0') return false;

        if (c == '"') {
            size_t e = s.find('"', p + 1);
            if (e == std::string::npos) return false;
            std::string v = s.substr(p + 1, e - p - 1);
            return v == "1" || v == "true" || v == "on" || v == "yes";
        }

        return false;
    }

    static bool setProjectSaveFlag(const std::string& root, bool on) {
        std::string path = root + "/project.json";
        std::string s = readFile(path);
        if (s.empty()) return false;

        std::string val = on ? "true" : "false";
        size_t key = s.find("\"save_vars\"");

        if (key != std::string::npos) {
            size_t colon = s.find(':', key + 11);
            if (colon == std::string::npos) return false;

            size_t st = colon + 1;
            while (st < s.size() && std::isspace((unsigned char)s[st])) ++st;

            size_t en = st;
            if (en < s.size() && s[en] == '"') {
                en = s.find('"', en + 1);
                if (en == std::string::npos) return false;
                ++en;
            } else {
                while (en < s.size() &&
                       (std::isalnum((unsigned char)s[en]) ||
                        s[en] == '_' || s[en] == '.' || s[en] == '+' || s[en] == '-')) {
                    ++en;
                }
            }

            if (st == en) return false;
            s.replace(st, en - st, val);
        } else {
            size_t last = s.rfind('}');
            if (last == std::string::npos) return false;

            size_t ins = last;
            while (ins > 0 && std::isspace((unsigned char)s[ins - 1])) --ins;

            if (ins > 0 && s[ins - 1] != '{') s.insert(ins, ",\n  ");
            else s.insert(ins, "\n  ");

            s.insert(ins, "\"save_vars\": " + val);
        }

        std::ofstream f(path);
        if (!f.good()) return false;
        f << s;
        f.close();
        return true;
    }

    void saveVars() {
        if (project_.rootPath.empty()) return;

        std::ofstream f(project_.rootPath + "/save.vars");
        if (!f.good()) return;

        f << "__score__=" << ctx_.score << "\n";
        for (const auto& kv : ctx_.vars) {
            f << kv.first << "=" << kv.second << "\n";
        }
        f.close();
    }

    void loadVars() {
        if (project_.rootPath.empty()) return;

        std::string path = project_.rootPath + "/save.vars";
        if (!fileExists(path)) return;

        std::string s = readFile(path);
        size_t pos = 0;

        while (pos <= s.size()) {
            size_t nl = s.find('\n', pos);
            std::string line;

            if (nl == std::string::npos) {
                line = s.substr(pos);
                pos = s.size() + 1;
            } else {
                line = s.substr(pos, nl - pos);
                pos = nl + 1;
            }

            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string k = line.substr(0, eq);
            double v = atof(line.substr(eq + 1).c_str());

            if (k == "__score__") ctx_.score = (int)v;
            else if (!k.empty()) ctx_.vars[k] = v;
        }
    }

    std::string uniqueProjectDir(const std::string& base) {
        std::string b = base.empty() ? std::string("Project") : base;
        std::string dir = b;
        int suffix = 2;

        while (suffix < 1000) {
            bool exists = false;
            for (const auto& g : hubState_.games) {
                if (g.dir == dir) {
                    exists = true;
                    break;
                }
            }

            if (!exists && !fileExists(PROJECT_ROOT + "/projects/" + dir)) break;

            dir = b + "_" + std::to_string(suffix++);
        }

        return dir;
    }

    std::string projectDisplayName(const std::string& dir) const {
        std::string disp = dir;

        ProjectInfo info;
        std::string path = PROJECT_ROOT + "/projects/" + dir + "/project.json";
        if (ProjectLoader::load(path, info) && !info.name.empty()) disp = info.name;

        return disp;
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
            if (curCol_ > 0) curCol_ = utf8Prev(L, curCol_);
            else if (curLine_ > 0) {
                curLine_--;
                curCol_ = (int)scriptLines_[curLine_].size();
            }
        } else {
            if (curCol_ < (int)L.size()) curCol_ = utf8Next(L, curCol_);
            else if (curLine_ + 1 < (int)scriptLines_.size()) {
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
        std::vector<std::string> tq, cq;
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
            if (k == 67) scBackspace();
            else if (k == 66) {
                compAnchor_ = -1;
                scTypeChar('\n');
            } else if (k == 21) scMove(-1);
            else if (k == 22) scMove(1);
        }

        imeChanged_ = true;
    }

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
        hdr->name = "ProjHdr";
        hdr->text = "PROJECTS";
        hdr->fontSize = 22;
        hdr->color = th.ink;
        hdr->position = Vec2{14, 34};
        s.root->addChild(std::move(hdr));

        for (size_t i = 0; i < hubState_.games.size(); ++i) {
            const auto& g = hubState_.games[i];
            std::string disp = sanitizeLine(projectDisplayName(g.dir));

            UiButton row;
            row.touch.id = "sel_" + g.dir;
            row.touch.rect = Rect{10, 70 + (float)i * 56, 240, 48};
            row.text = (hubState_.selectedDir == g.dir ? "* " : "  ") + disp;
            row.action = "sel:" + g.dir;
            row.color = (hubState_.selectedDir == g.dir) ? th.accent : th.button;

            s.ui.push_back(row);
        }

        if (!hubState_.selectedDir.empty()) {
            ProjectInfo info;
            ProjectLoader::load(PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/project.json", info);

            std::string dispName = info.name.empty() ? hubState_.selectedDir : info.name;
            dispName = sanitizeLine(dispName);

            auto nm = std::make_unique<Label>();
            nm->name = "SelName";
            nm->text = dispName;
            nm->fontSize = 34;
            nm->color = th.ink;
            nm->position = Vec2{740, 120};
            s.root->addChild(std::move(nm));

            auto sc = std::make_unique<Label>();
            sc->name = "SelScene";
            sc->text = "scene: " + info.mainScene;
            sc->fontSize = 20;
            sc->color = th.ink;
            sc->position = Vec2{740, 170};
            s.root->addChild(std::move(sc));

            UiButton play;
            play.touch.id = "play";
            play.touch.rect = Rect{740, 280, 150, 60};
            play.text = "Play";
            play.action = "play:" + hubState_.selectedDir;
            play.color = th.accent;
            s.ui.push_back(play);

            UiButton edit;
            edit.touch.id = "edit";
            edit.touch.rect = Rect{910, 280, 150, 60};
            edit.text = "Edit";
            edit.action = "edit:" + hubState_.selectedDir;
            edit.color = th.button;
            s.ui.push_back(edit);

            std::string root = PROJECT_ROOT + "/projects/" + hubState_.selectedDir;
            bool svOn = projectWantsSave(root);

            UiButton sv;
            sv.touch.id = "toggle_save";
            sv.touch.rect = Rect{740, 360, 150, 60};
            sv.text = svOn ? "SAVE: ON" : "SAVE: OFF";
            sv.action = "save_toggle";
            sv.color = svOn ? parseColor("#2E7D32") : th.button;
            s.ui.push_back(sv);

            UiButton ren;
            ren.touch.id = "hub_rename";
            ren.touch.rect = Rect{910, 360, 150, 60};
            ren.text = "NAME";
            ren.action = "hub_rename";
            ren.color = th.button;
            s.ui.push_back(ren);

            bool confirming = (confirmDeleteDir_ == hubState_.selectedDir);

            UiButton del;
            del.touch.id = "hub_delete";
            del.touch.rect = Rect{740, 440, 150, 60};
            del.text = confirming ? "SURE?" : "DELETE";
            del.action = confirming ? "hub_del_yes" : "hub_del";
            del.color = parseColor("#D62828");
            s.ui.push_back(del);

            UiButton rst;
            rst.touch.id = "hub_reset";
            rst.touch.rect = Rect{910, 440, 150, 60};
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
            hint->position = Vec2{740, 300};
            s.root->addChild(std::move(hint));
        }

        UiButton nb;
        nb.touch.id = "new_project";
        nb.touch.rect = Rect{740, 560, 150, 60};
        nb.text = "+ NEW";
        nb.action = "new";
        nb.color = th.button;
        s.ui.push_back(nb);

        UiButton tb;
        tb.touch.id = "theme";
        tb.touch.rect = Rect{910, 560, 150, 60};
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

    HubAct processHubLandscape() {
        HubAct a;

        for (auto& b : hubScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            if (b.action == "new") a.kind = 3;
            else if (b.action == "theme") a.kind = 5;
            else if (b.action == "save_toggle") a.kind = 6;
            else if (b.action == "hub_rename") a.kind = 7;
            else if (b.action == "hub_del") a.kind = 8;
            else if (b.action == "hub_del_yes") a.kind = 9;
            else if (b.action == "hub_reset") a.kind = 10;
            else if (b.action.rfind("play:", 0) == 0) {
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

    std::string stepHub() {
        if (!pendingNewProject_ && !pendingHubRename_) {
            clearDialogResults();
        }

        std::string nm;

        if (pendingNewProject_) {
            if (takeNameResult(nm)) {
                pendingNewProject_ = false;

                if (!nm.empty()) {
                    std::string dir = uniqueProjectDir(sanitizeProjectDirName(nm));
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
                    std::string root = PROJECT_ROOT + "/projects/" + pendingHubDir_;
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

        HubAct a = processHubLandscape();

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
            std::string root = PROJECT_ROOT + "/projects/" + hubState_.selectedDir;
            bool cur = projectWantsSave(root);
            setProjectSaveFlag(root, !cur);
            confirmDeleteDir_.clear();
            rebuildHub();
        } else if (a.kind == 7) {
            if (!hubState_.selectedDir.empty()) {
                pendingHubDir_ = hubState_.selectedDir;
                pendingHubCurrentName_ = projectDisplayName(pendingHubDir_);
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
                std::string root = PROJECT_ROOT + "/projects/" + oldDir;

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
                    if (!hubState_.games.empty()) hubState_.selectedDir = hubState_.games.front().dir;
                    else hubState_.selectedDir.clear();
                }

                confirmDeleteDir_.clear();
                rebuildHub();
            }
        } else if (a.kind == 10) {
            if (!hubState_.selectedDir.empty()) {
                std::string sv = PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/save.vars";
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
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) return false;

        project_ = pi;
        g_projectRoot = project_.rootPath;
        fontPath_ = pi.rootPath + "/" + pi.defaultFont;

        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";

        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) return false;

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
            if (saveVarsEnabled_) loadVars();
            pendingLoadVars_ = false;
        }

        if (saveVarsEnabled_) {
            saveTimer_ += 1.0f / 60.0f;
            if (saveTimer_ >= 1.0f) {
                saveVars();
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
                if (out[i] == 'D' && out[i + 1] == 'R' && out[i + 2] == 'A' && out[i + 3] == 'W') ++lastDraws_;
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
        Scene* sc = sceneMgr_->current();
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
            if (saveVarsEnabled_) saveVars();

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
        Scene* sc = sceneMgr_->current();
        if (!sc) return;

        for (auto& b : sc->ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            runAction(b.action);
            if (appMode_ != AppMode::Game) return;
        }
    }

    std::string hitTest(const Node* n, float wx, float wy) {
        if (!n) return "";

        std::string bestBox, bestNear;
        float bestDist = 1e9f;

        collectHit(n, wx, wy, 0, 0, 1, 1, bestBox, bestNear, bestDist);
        return bestBox.empty() ? bestNear : bestBox;
    }

    void collectHit(const Node* n, float wx, float wy,
                    float ox, float oy, float psx, float psy,
                    std::string& bestBox, std::string& bestNear, float& bestDist) const {
        if (!n) return;

        std::string tn = std::string(n->typeName());

        if (tn != "Node" && tn != "Camera2D" && n->name.rfind("__", 0) != 0) {
            const Node2D* d = static_cast<const Node2D*>(n);

            float cxw = ox + d->position.x * psx;
            float cyw = oy + d->position.y * psy;

            float hw = (d->w * d->scale.x * psx) / 2;
            if (hw < 28) hw = 28;

            float hh = (d->h * d->scale.y * psy) / 2;
            if (hh < 28) hh = 28;

            if (wx >= cxw - hw && wx <= cxw + hw && wy >= cyw - hh && wy <= cyw + hh) {
                if (bestBox.empty()) bestBox = d->name;
            }

            float dx = wx - cxw;
            float dy = wy - cyw;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < 45.0f && dist < bestDist) {
                bestDist = dist;
                bestNear = d->name;
            }

            for (const auto& ch : n->getChildren()) {
                collectHit(ch.get(), wx, wy, cxw, cyw, psx * d->scale.x, psy * d->scale.y,
                           bestBox, bestNear, bestDist);
            }

            return;
        }

        for (const auto& ch : n->getChildren()) {
            collectHit(ch.get(), wx, wy, ox, oy, psx, psy, bestBox, bestNear, bestDist);
        }
    }

    void attachScript(const std::string& name) {
        std::string rel = "scripts/" + name + ".lua";
        ProjectCreator::createScript(project_.rootPath, rel, name);
        scripts_.load(project_.rootPath);
        scripted_.insert(name);
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) {
            appMode_ = AppMode::Hub;
            rebuildHub();
            return false;
        }

        project_ = pi;
        g_projectRoot = project_.rootPath;
        fontPath_ = pi.rootPath + "/" + pi.defaultFont;

        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";

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

    static std::string dash(int depth) {
        return depth > 0 ? std::string(depth, '-') + " " : "";
    }

    bool hasUiGroup(Scene* esc, const std::string& name) {
        if (!esc) return false;
        for (auto& ub : esc->ui) {
            if (ub.group == name) return true;
        }
        return false;
    }

    struct EdRow {
        std::string text, action;
        bool sel;
        bool hasKids;
        bool open;
        std::string name;
    };

    void buildTreeRows(Scene* esc, Node* n, int depth,
                       const std::string& sel, const std::string& selUi,
                       std::vector<EdRow>& rows) {
        for (auto& ch : n->getChildren()) {
            std::string nm = ch->name;

            bool kids = ch->childCount() > 0 || hasUiGroup(esc, nm);
            bool open = collapsed_.count(nm) == 0;

            Node2D* ch2d = dynamic_cast<Node2D*>(ch.get());
            std::string lockMark = (ch2d && ch2d->locked) ? " [L]" : "";

            EdRow r;
            r.text = dash(depth) + (kids ? (open ? "[-] " : "[+] ") : "    ") + nm + lockMark + "   " + std::string(ch->typeName());
            r.action = "ed_select:" + nm;
            r.sel = (sel == nm);
            r.hasKids = kids;
            r.open = open;
            r.name = nm;

            rows.push_back(r);

            if (kids && open) {
                buildTreeRows(esc, ch.get(), depth + 1, sel, selUi, rows);

                for (auto& ub : esc->ui) {
                    if (ub.group == nm) {
                        EdRow br;
                        br.text = dash(depth + 1) + "    " + ub.touch.id + "   Button";
                        br.action = "ed_selectui:" + ub.touch.id;
                        br.sel = (selUi == ub.touch.id);
                        br.hasKids = false;
                        br.open = false;
                        br.name = "";

                        rows.push_back(br);
                    }
                }
            }
        }
    }

    void buildEditorPanels() {
        Theme& th = currentTheme();
        const unsigned GODOT_ORANGE = 0xFF8800FFu;

        editorScene_ = Scene();
        editorScene_.name = "Editor";
        editorScene_.root = std::make_unique<Node>();
        editorScene_.root->name = "EdRoot";

        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
            auto l = std::make_unique<Label>();
            l->name = nm;
            l->text = txt;
            l->fontSize = fs;
            l->color = col;
            l->position = Vec2{x, y};
            editorScene_.root->addChild(std::move(l));
        };

        {
            UiButton b;
            b.touch.id = "tab_scene";
            b.touch.rect = Rect{10, 4, 80, 26};
            b.text = "Scene";
            b.action = "tab_scene";
            b.color = scriptMode_ ? th.button : GODOT_ORANGE;
            editorScene_.ui.push_back(b);
        }

        addLbl("Tab2D", "2D", 110, 8, 20, th.ink);
        addLbl("Tab3D", "3D", 160, 8, 20, th.ink);

        {
            UiButton b;
            b.touch.id = "tab_scripts";
            b.touch.rect = Rect{200, 4, 90, 26};
            b.text = "Scripts";
            b.action = "tab_scripts";
            b.color = scriptMode_ ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        addLbl("TabAss", "AssetLib", 300, 8, 20, th.ink);

        if (scriptMode_) {
            buildScriptPanels(th);
            return;
        }

        auto fsBg = std::make_unique<Node2D>();
        fsBg->name = "FsBg";
        fsBg->shape = "square";
        fsBg->color = dimColor(th.bg, 0.6f);
        fsBg->w = 284;
        fsBg->h = 320;
        fsBg->position = Vec2{150, 536};
        editorScene_.root->addChild(std::move(fsBg));

        addLbl("DHdr", "Scene", 10, 40, 18, th.ink);
        addLbl("IHdr", "Inspector", 900, 40, 18, th.ink);
        addLbl("ZoomLbl", "zoom " + std::to_string((int)(edZoom_ * 100)) + "%", 380, 8, 16, th.ink);

        const char* mlab[3] = { "POS", "ROT", "SCL" };
        Manip mval[3] = { Manip::Move, Manip::Rotate, Manip::Scale };

        for (int k = 0; k < 3; ++k) {
            UiButton b;
            b.touch.id = std::string("manipbtn") + std::to_string(k);
            b.touch.rect = Rect{1090 + (float)k * 60, 34, 56, 28};
            b.text = mlab[k];
            b.action = std::string("manip:") + (k == 0 ? "move" : k == 1 ? "rotate" : "scale");
            b.color = (manip_ == mval[k]) ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        Scene* esc = editor_ ? editor_->scene() : nullptr;
        Node* selNode = editor_ ? editor_->selected() : nullptr;
        std::string sel = selNode ? selNode->name : std::string{};
        std::string selUi = editor_ ? editor_->selectedUi() : std::string{};

        std::vector<EdRow> rows;

        if (esc && esc->root) buildTreeRows(esc, esc->root.get(), 0, sel, selUi, rows);

        if (esc) {
            for (auto& ub : esc->ui) {
                if (!ub.group.empty()) continue;

                EdRow r;
                r.text = "    " + ub.touch.id + "   Button";
                r.action = "ed_selectui:" + ub.touch.id;
                r.sel = (selUi == ub.touch.id);
                r.hasKids = false;
                r.open = false;
                r.name = "";

                rows.push_back(r);
            }
        }

        const int VIS = 10;
        int maxScroll = (int)rows.size() > VIS ? (int)rows.size() - VIS : 0;

        if (hierScroll_ < 0) hierScroll_ = 0;
        if (hierScroll_ > maxScroll) hierScroll_ = maxScroll;

        {
            UiButton b;
            b.touch.id = "hup";
            b.touch.rect = Rect{248, 38, 20, 22};
            b.text = "^";
            b.action = "hier_up";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "hdn";
            b.touch.rect = Rect{270, 38, 20, 22};
            b.text = "v";
            b.action = "hier_dn";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        for (int i = hierScroll_; i < (int)rows.size() && i < hierScroll_ + VIS; ++i) {
            float y = 64 + (float)(i - hierScroll_) * 30;

            if (rows[i].hasKids) {
                UiButton f;
                f.touch.id = "fold" + std::to_string(i);
                f.touch.rect = Rect{8, y, 26, 28};
                f.text = rows[i].open ? "-" : "+";
                f.action = "fold:" + rows[i].name;
                f.color = th.accent;
                editorScene_.ui.push_back(f);
            }

            UiButton b;
            b.touch.id = "h" + std::to_string(i);

            float rx = rows[i].hasKids ? 36.0f : 8.0f;
            float rw = rows[i].hasKids ? 256.0f : 284.0f;

            b.touch.rect = Rect{rx, y, rw, 28};
            b.text = rows[i].text;
            b.action = rows[i].action;
            b.color = rows[i].sel ? GODOT_ORANGE : th.button;

            editorScene_.ui.push_back(b);
        }

        if (!selUi.empty()) {
            UiButton* ub = editor_->findUi(selUi);

            if (ub) {
                addLbl("InName", ub->touch.id, 900, 64, 22, GODOT_ORANGE);
                addLbl("InType", "Button", 900, 92, 16, th.ink);
                addLbl("InPos", "Pos  (" + std::to_string((int)ub->touch.rect.x) + ", " + std::to_string((int)ub->touch.rect.y) + ")", 900, 124, 16, th.ink);
                addLbl("InSiz", "Size (" + std::to_string((int)ub->touch.rect.w) + ", " + std::to_string((int)ub->touch.rect.h) + ")", 900, 148, 16, th.ink);
                addLbl("InCol", "Color " + colorToHex(ub->color) + " (" + rgbStr(ub->color) + ") A" + std::to_string((int)(ub->alpha * 100)) + "%", 900, 172, 16, th.ink);
                addLbl("InText", "Text: " + ub->text, 900, 196, 16, th.ink);
                addLbl("InAct", "Action: " + (ub->action.empty() ? std::string("(none)") : ub->action), 900, 220, 16, th.ink);
                addLbl("InAng", "Angle " + std::to_string((int)ub->angle), 900, 244, 16, th.ink);
                addLbl("InTex", "Texture: " + (ub->texture.empty() ? std::string("(none)") : ub->texture), 900, 268, 16, th.ink);
                addLbl("InGrp", "Group: " + (ub->group.empty() ? std::string("(none)") : ub->group), 900, 292, 16, th.ink);

                const char* nl[6] = { "X", "Y", "W", "H", "R", "A" };
                const char* nk[6] = { "bx", "by", "bw", "bh", "bang", "balpha" };

                for (int k = 0; k < 6; ++k) {
                    UiButton b;
                    b.touch.id = std::string("numbtn") + std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 46, 320, 44, 28};
                    b.text = nl[k];
                    b.action = std::string("num:") + nk[k];
                    b.color = th.button;
                    editorScene_.ui.push_back(b);
                }
            }
        } else {
            Node2D* s = (editor_ && !sel.empty()) ? editor_->find2d(sel) : nullptr;

            if (s) {
                addLbl("InName", s->name, 900, 64, 22, GODOT_ORANGE
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
            if (curCol_ > 0) curCol_ = utf8Prev(L, curCol_);
            else if (curLine_ > 0) {
                curLine_--;
                curCol_ = (int)scriptLines_[curLine_].size();
            }
        } else {
            if (curCol_ < (int)L.size()) curCol_ = utf8Next(L, curCol_);
            else if (curLine_ + 1 < (int)scriptLines_.size()) {
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
        std::vector<std::string> tq, cq;
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
            if (k == 67) scBackspace();
            else if (k == 66) {
                compAnchor_ = -1;
                scTypeChar('\n');
            } else if (k == 21) scMove(-1);
            else if (k == 22) scMove(1);
        }

        imeChanged_ = true;
    }

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
        hdr->name = "ProjHdr";
        hdr->text = "PROJECTS";
        hdr->fontSize = 22;
        hdr->color = th.ink;
        hdr->position = Vec2{14, 34};
        s.root->addChild(std::move(hdr));

        for (size_t i = 0; i < hubState_.games.size(); ++i) {
            const auto& g = hubState_.games[i];
            std::string disp = sanitizeLine(projectDisplayName(g.dir));

            UiButton row;
            row.touch.id = "sel_" + g.dir;
            row.touch.rect = Rect{10, 70 + (float)i * 56, 240, 48};
            row.text = (hubState_.selectedDir == g.dir ? "* " : "  ") + disp;
            row.action = "sel:" + g.dir;
            row.color = (hubState_.selectedDir == g.dir) ? th.accent : th.button;

            s.ui.push_back(row);
        }

        if (!hubState_.selectedDir.empty()) {
            ProjectInfo info;
            ProjectLoader::load(PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/project.json", info);

            std::string dispName = info.name.empty() ? hubState_.selectedDir : info.name;
            dispName = sanitizeLine(dispName);

            auto nm = std::make_unique<Label>();
            nm->name = "SelName";
            nm->text = dispName;
            nm->fontSize = 34;
            nm->color = th.ink;
            nm->position = Vec2{740, 120};
            s.root->addChild(std::move(nm));

            auto sc = std::make_unique<Label>();
            sc->name = "SelScene";
            sc->text = "scene: " + info.mainScene;
            sc->fontSize = 20;
            sc->color = th.ink;
            sc->position = Vec2{740, 170};
            s.root->addChild(std::move(sc));

            UiButton play;
            play.touch.id = "play";
            play.touch.rect = Rect{740, 280, 150, 60};
            play.text = "Play";
            play.action = "play:" + hubState_.selectedDir;
            play.color = th.accent;
            s.ui.push_back(play);

            UiButton edit;
            edit.touch.id = "edit";
            edit.touch.rect = Rect{910, 280, 150, 60};
            edit.text = "Edit";
            edit.action = "edit:" + hubState_.selectedDir;
            edit.color = th.button;
            s.ui.push_back(edit);

            std::string root = PROJECT_ROOT + "/projects/" + hubState_.selectedDir;
            bool svOn = projectWantsSave(root);

            UiButton sv;
            sv.touch.id = "toggle_save";
            sv.touch.rect = Rect{740, 360, 150, 60};
            sv.text = svOn ? "SAVE: ON" : "SAVE: OFF";
            sv.action = "save_toggle";
            sv.color = svOn ? parseColor("#2E7D32") : th.button;
            s.ui.push_back(sv);

            UiButton ren;
            ren.touch.id = "hub_rename";
            ren.touch.rect = Rect{910, 360, 150, 60};
            ren.text = "NAME";
            ren.action = "hub_rename";
            ren.color = th.button;
            s.ui.push_back(ren);

            bool confirming = (confirmDeleteDir_ == hubState_.selectedDir);

            UiButton del;
            del.touch.id = "hub_delete";
            del.touch.rect = Rect{740, 440, 150, 60};
            del.text = confirming ? "SURE?" : "DELETE";
            del.action = confirming ? "hub_del_yes" : "hub_del";
            del.color = parseColor("#D62828");
            s.ui.push_back(del);

            UiButton rst;
            rst.touch.id = "hub_reset";
            rst.touch.rect = Rect{910, 440, 150, 60};
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
            hint->position = Vec2{740, 300};
            s.root->addChild(std::move(hint));
        }

        UiButton nb;
        nb.touch.id = "new_project";
        nb.touch.rect = Rect{740, 560, 150, 60};
        nb.text = "+ NEW";
        nb.action = "new";
        nb.color = th.button;
        s.ui.push_back(nb);

        UiButton tb;
        tb.touch.id = "theme";
        tb.touch.rect = Rect{910, 560, 150, 60};
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

    HubAct processHubLandscape() {
        HubAct a;

        for (auto& b : hubScene_.ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            if (b.action == "new") a.kind = 3;
            else if (b.action == "theme") a.kind = 5;
            else if (b.action == "save_toggle") a.kind = 6;
            else if (b.action == "hub_rename") a.kind = 7;
            else if (b.action == "hub_del") a.kind = 8;
            else if (b.action == "hub_del_yes") a.kind = 9;
            else if (b.action == "hub_reset") a.kind = 10;
            else if (b.action.rfind("play:", 0) == 0) {
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

    std::string stepHub() {
        if (!pendingNewProject_ && !pendingHubRename_) {
            clearDialogResults();
        }

        std::string nm;

        if (pendingNewProject_) {
            if (takeNameResult(nm)) {
                pendingNewProject_ = false;

                if (!nm.empty()) {
                    std::string dir = uniqueProjectDir(sanitizeProjectDirName(nm));
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
                    std::string root = PROJECT_ROOT + "/projects/" + pendingHubDir_;
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

        HubAct a = processHubLandscape();

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
            std::string root = PROJECT_ROOT + "/projects/" + hubState_.selectedDir;
            bool cur = projectWantsSave(root);
            setProjectSaveFlag(root, !cur);
            confirmDeleteDir_.clear();
            rebuildHub();
        } else if (a.kind == 7) {
            if (!hubState_.selectedDir.empty()) {
                pendingHubDir_ = hubState_.selectedDir;
                pendingHubCurrentName_ = projectDisplayName(pendingHubDir_);
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
                std::string root = PROJECT_ROOT + "/projects/" + oldDir;

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
                    if (!hubState_.games.empty()) hubState_.selectedDir = hubState_.games.front().dir;
                    else hubState_.selectedDir.clear();
                }

                confirmDeleteDir_.clear();
                rebuildHub();
            }
        } else if (a.kind == 10) {
            if (!hubState_.selectedDir.empty()) {
                std::string sv = PROJECT_ROOT + "/projects/" + hubState_.selectedDir + "/save.vars";
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
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) return false;

        project_ = pi;
        g_projectRoot = project_.rootPath;
        fontPath_ = pi.rootPath + "/" + pi.defaultFont;

        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";

        sceneMgr_ = std::make_unique<SceneManager>(pi.rootPath, fontPath_);
        if (!sceneMgr_->restartScene(pi.mainScene, resources_)) return false;

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
            if (saveVarsEnabled_) loadVars();
            pendingLoadVars_ = false;
        }

        if (saveVarsEnabled_) {
            saveTimer_ += 1.0f / 60.0f;
            if (saveTimer_ >= 1.0f) {
                saveVars();
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
                if (out[i] == 'D' && out[i + 1] == 'R' && out[i + 2] == 'A' && out[i + 3] == 'W') ++lastDraws_;
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
        Scene* sc = sceneMgr_->current();
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
            if (saveVarsEnabled_) saveVars();

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
        Scene* sc = sceneMgr_->current();
        if (!sc) return;

        for (auto& b : sc->ui) {
            if (!b.touch.pressEdge || b.action.empty()) continue;

            runAction(b.action);
            if (appMode_ != AppMode::Game) return;
        }
    }

    std::string hitTest(const Node* n, float wx, float wy) {
        if (!n) return "";

        std::string bestBox, bestNear;
        float bestDist = 1e9f;

        collectHit(n, wx, wy, 0, 0, 1, 1, bestBox, bestNear, bestDist);
        return bestBox.empty() ? bestNear : bestBox;
    }

    void collectHit(const Node* n, float wx, float wy,
                    float ox, float oy, float psx, float psy,
                    std::string& bestBox, std::string& bestNear, float& bestDist) const {
        if (!n) return;

        std::string tn = std::string(n->typeName());

        if (tn != "Node" && tn != "Camera2D" && n->name.rfind("__", 0) != 0) {
            const Node2D* d = static_cast<const Node2D*>(n);

            float cxw = ox + d->position.x * psx;
            float cyw = oy + d->position.y * psy;

            float hw = (d->w * d->scale.x * psx) / 2;
            if (hw < 28) hw = 28;

            float hh = (d->h * d->scale.y * psy) / 2;
            if (hh < 28) hh = 28;

            if (wx >= cxw - hw && wx <= cxw + hw && wy >= cyw - hh && wy <= cyw + hh) {
                if (bestBox.empty()) bestBox = d->name;
            }

            float dx = wx - cxw;
            float dy = wy - cyw;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist < 45.0f && dist < bestDist) {
                bestDist = dist;
                bestNear = d->name;
            }

            for (const auto& ch : n->getChildren()) {
                collectHit(ch.get(), wx, wy, cxw, cyw, psx * d->scale.x, psy * d->scale.y,
                           bestBox, bestNear, bestDist);
            }

            return;
        }

        for (const auto& ch : n->getChildren()) {
            collectHit(ch.get(), wx, wy, ox, oy, psx, psy, bestBox, bestNear, bestDist);
        }
    }

    void attachScript(const std::string& name) {
        std::string rel = "scripts/" + name + ".lua";
        ProjectCreator::createScript(project_.rootPath, rel, name);
        scripts_.load(project_.rootPath);
        scripted_.insert(name);
    }

    bool enterEditor(const std::string& dir) {
        ProjectInfo pi;
        if (!ProjectLoader::load(PROJECT_ROOT + "/projects/" + dir + "/project.json", pi)) {
            appMode_ = AppMode::Hub;
            rebuildHub();
            return false;
        }

        project_ = pi;
        g_projectRoot = project_.rootPath;
        fontPath_ = pi.rootPath + "/" + pi.defaultFont;

        if (!fileExists(fontPath_)) fontPath_ = PROJECT_ROOT + "/assets/fonts/Ubuntu-Regular.ttf";

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

    static std::string dash(int depth) {
        return depth > 0 ? std::string(depth, '-') + " " : "";
    }

    bool hasUiGroup(Scene* esc, const std::string& name) {
        if (!esc) return false;
        for (auto& ub : esc->ui) {
            if (ub.group == name) return true;
        }
        return false;
    }

    struct EdRow {
        std::string text, action;
        bool sel;
        bool hasKids;
        bool open;
        std::string name;
    };

    void buildTreeRows(Scene* esc, Node* n, int depth,
                       const std::string& sel, const std::string& selUi,
                       std::vector<EdRow>& rows) {
        for (auto& ch : n->getChildren()) {
            std::string nm = ch->name;

            bool kids = ch->childCount() > 0 || hasUiGroup(esc, nm);
            bool open = collapsed_.count(nm) == 0;

            Node2D* ch2d = dynamic_cast<Node2D*>(ch.get());
            std::string lockMark = (ch2d && ch2d->locked) ? " [L]" : "";

            EdRow r;
            r.text = dash(depth) + (kids ? (open ? "[-] " : "[+] ") : "    ") + nm + lockMark + "   " + std::string(ch->typeName());
            r.action = "ed_select:" + nm;
            r.sel = (sel == nm);
            r.hasKids = kids;
            r.open = open;
            r.name = nm;

            rows.push_back(r);

            if (kids && open) {
                buildTreeRows(esc, ch.get(), depth + 1, sel, selUi, rows);

                for (auto& ub : esc->ui) {
                    if (ub.group == nm) {
                        EdRow br;
                        br.text = dash(depth + 1) + "    " + ub.touch.id + "   Button";
                        br.action = "ed_selectui:" + ub.touch.id;
                        br.sel = (selUi == ub.touch.id);
                        br.hasKids = false;
                        br.open = false;
                        br.name = "";

                        rows.push_back(br);
                    }
                }
            }
        }
    }

    void buildEditorPanels() {
        Theme& th = currentTheme();
        const unsigned GODOT_ORANGE = 0xFF8800FFu;

        editorScene_ = Scene();
        editorScene_.name = "Editor";
        editorScene_.root = std::make_unique<Node>();
        editorScene_.root->name = "EdRoot";

        auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
            auto l = std::make_unique<Label>();
            l->name = nm;
            l->text = txt;
            l->fontSize = fs;
            l->color = col;
            l->position = Vec2{x, y};
            editorScene_.root->addChild(std::move(l));
        };

        {
            UiButton b;
            b.touch.id = "tab_scene";
            b.touch.rect = Rect{10, 4, 80, 26};
            b.text = "Scene";
            b.action = "tab_scene";
            b.color = scriptMode_ ? th.button : GODOT_ORANGE;
            editorScene_.ui.push_back(b);
        }

        addLbl("Tab2D", "2D", 110, 8, 20, th.ink);
        addLbl("Tab3D", "3D", 160, 8, 20, th.ink);

        {
            UiButton b;
            b.touch.id = "tab_scripts";
            b.touch.rect = Rect{200, 4, 90, 26};
            b.text = "Scripts";
            b.action = "tab_scripts";
            b.color = scriptMode_ ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        addLbl("TabAss", "AssetLib", 300, 8, 20, th.ink);

        if (scriptMode_) {
            buildScriptPanels(th);
            return;
        }

        auto fsBg = std::make_unique<Node2D>();
        fsBg->name = "FsBg";
        fsBg->shape = "square";
        fsBg->color = dimColor(th.bg, 0.6f);
        fsBg->w = 284;
        fsBg->h = 320;
        fsBg->position = Vec2{150, 536};
        editorScene_.root->addChild(std::move(fsBg));

        addLbl("DHdr", "Scene", 10, 40, 18, th.ink);
        addLbl("IHdr", "Inspector", 900, 40, 18, th.ink);
        addLbl("ZoomLbl", "zoom " + std::to_string((int)(edZoom_ * 100)) + "%", 380, 8, 16, th.ink);

        const char* mlab[3] = { "POS", "ROT", "SCL" };
        Manip mval[3] = { Manip::Move, Manip::Rotate, Manip::Scale };

        for (int k = 0; k < 3; ++k) {
            UiButton b;
            b.touch.id = std::string("manipbtn") + std::to_string(k);
            b.touch.rect = Rect{1090 + (float)k * 60, 34, 56, 28};
            b.text = mlab[k];
            b.action = std::string("manip:") + (k == 0 ? "move" : k == 1 ? "rotate" : "scale");
            b.color = (manip_ == mval[k]) ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        Scene* esc = editor_ ? editor_->scene() : nullptr;
        Node* selNode = editor_ ? editor_->selected() : nullptr;
        std::string sel = selNode ? selNode->name : std::string{};
        std::string selUi = editor_ ? editor_->selectedUi() : std::string{};

        std::vector<EdRow> rows;

        if (esc && esc->root) buildTreeRows(esc, esc->root.get(), 0, sel, selUi, rows);

        if (esc) {
            for (auto& ub : esc->ui) {
                if (!ub.group.empty()) continue;

                EdRow r;
                r.text = "    " + ub.touch.id + "   Button";
                r.action = "ed_selectui:" + ub.touch.id;
                r.sel = (selUi == ub.touch.id);
                r.hasKids = false;
                r.open = false;
                r.name = "";

                rows.push_back(r);
            }
        }

        const int VIS = 10;
        int maxScroll = (int)rows.size() > VIS ? (int)rows.size() - VIS : 0;

        if (hierScroll_ < 0) hierScroll_ = 0;
        if (hierScroll_ > maxScroll) hierScroll_ = maxScroll;

        {
            UiButton b;
            b.touch.id = "hup";
            b.touch.rect = Rect{248, 38, 20, 22};
            b.text = "^";
            b.action = "hier_up";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "hdn";
            b.touch.rect = Rect{270, 38, 20, 22};
            b.text = "v";
            b.action = "hier_dn";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        for (int i = hierScroll_; i < (int)rows.size() && i < hierScroll_ + VIS; ++i) {
            float y = 64 + (float)(i - hierScroll_) * 30;

            if (rows[i].hasKids) {
                UiButton f;
                f.touch.id = "fold" + std::to_string(i);
                f.touch.rect = Rect{8, y, 26, 28};
                f.text = rows[i].open ? "-" : "+";
                f.action = "fold:" + rows[i].name;
                f.color = th.accent;
                editorScene_.ui.push_back(f);
            }

            UiButton b;
            b.touch.id = "h" + std::to_string(i);

            float rx = rows[i].hasKids ? 36.0f : 8.0f;
            float rw = rows[i].hasKids ? 256.0f : 284.0f;

            b.touch.rect = Rect{rx, y, rw, 28};
            b.text = rows[i].text;
            b.action = rows[i].action;
            b.color = rows[i].sel ? GODOT_ORANGE : th.button;

            editorScene_.ui.push_back(b);
        }

        if (!selUi.empty()) {
            UiButton* ub = editor_->findUi(selUi);

            if (ub) {
                addLbl("InName", ub->touch.id, 900, 64, 22, GODOT_ORANGE);
                addLbl("InType", "Button", 900, 92, 16, th.ink);
                addLbl("InPos", "Pos  (" + std::to_string((int)ub->touch.rect.x) + ", " + std::to_string((int)ub->touch.rect.y) + ")", 900, 124, 16, th.ink);
                addLbl("InSiz", "Size (" + std::to_string((int)ub->touch.rect.w) + ", " + std::to_string((int)ub->touch.rect.h) + ")", 900, 148, 16, th.ink);
                addLbl("InCol", "Color " + colorToHex(ub->color) + " (" + rgbStr(ub->color) + ") A" + std::to_string((int)(ub->alpha * 100)) + "%", 900, 172, 16, th.ink);
                addLbl("InText", "Text: " + ub->text, 900, 196, 16, th.ink);
                addLbl("InAct", "Action: " + (ub->action.empty() ? std::string("(none)") : ub->action), 900, 220, 16, th.ink);
                addLbl("InAng", "Angle " + std::to_string((int)ub->angle), 900, 244, 16, th.ink);
                addLbl("InTex", "Texture: " + (ub->texture.empty() ? std::string("(none)") : ub->texture), 900, 268, 16, th.ink);
                addLbl("InGrp", "Group: " + (ub->group.empty() ? std::string("(none)") : ub->group), 900, 292, 16, th.ink);

                const char* nl[6] = { "X", "Y", "W", "H", "R", "A" };
                const char* nk[6] = { "bx", "by", "bw", "bh", "bang", "balpha" };

                for (int k = 0; k < 6; ++k) {
                    UiButton b;
                    b.touch.id = std::string("numbtn") + std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 46, 320, 44, 28};
                    b.text = nl[k];
                    b.action = std::string("num:") + nk[k];
                    b.color = th.button;
                    editorScene_.ui.push_back(b);
                }
            }
        } else {
            Node2D* s = (editor_ && !sel.empty()) ? editor_->find2d(sel) : nullptr;

            if (s) {
                addLbl("InName", s->name, 900, 64, 22, GODOT_ORANGE);
                addLbl("InType", std::string(s->typeName()), 900, 92, 16, th.ink);
                addLbl("InPos", "Position  (" + std::to_string((int)s->position.x) + ", " + std::to_string((int)s->position.y) + ")", 900, 124, 16, th.ink);
                addLbl("InRot", "Rotation  " + std::to_string((int)(s->rotation * 57.2957795f)), 900, 148, 16, th.ink);
                addLbl("InScl", "Scale  (" + std::to_string((int)(s->scale.x * 100)) + "%, " + std::to_string((int)(s->scale.y * 100)) + "%)", 900, 172, 16, th.ink);
                addLbl("InSiz", "Size  (" + std::to_string((int)s->w) + ", " + std::to_string((int)s->h) + ")", 900, 196, 16, th.ink);
                addLbl("InLck", "Locked  " + std::string(s->locked ? "YES" : "no"), 900, 220, 16, s->locked ? parseColor("#D62828") : th.ink);
                addLbl("InAlp", "Alpha  " + std::to_string((int)(s->alpha * 100)) + "%", 900, 244, 16, th.ink);
                addLbl("InShp", "Shape  " + s->shape, 900, 268, 16, th.ink);
                addLbl("InCol", "Color  " + colorToHex(s->color) + "  (" + rgbStr(s->color) + ")", 900, 292, 16, th.ink);
                addLbl("InTex", "Texture: " + (s->texture.empty() ? std::string("(none)") : s->texture), 900, 316, 16, th.ink);
                addLbl("InAct", "Touch: " + (s->action.empty() ? std::string("(none)") : s->action), 900, 340, 16, th.ink);

                if (std::string(s->typeName()) == "Label") {
                    addLbl("InText", "Text: " + static_cast<Label*>(s)->text, 900, 364, 16, th.ink);
                }

                const char* nl[7] = { "X", "Y", "ROT", "SCL", "W", "H", "A" };
                const char* na[7] = { "nx", "ny", "nrot", "nscl", "nw", "nh", "nalpha" };

                for (int k = 0; k < 7; ++k) {
                    UiButton b;
                    b.touch.id = std::string("numbtn") + std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 46, 392, 44, 28};
                    b.text = nl[k];
                    b.action = std::string("num:") + na[k];
                    b.color = th.button;
                    editorScene_.ui.push_back(b);
                }
            } else {
                addLbl("InNone", "(no selection)", 900, 92, 18, th.ink);
            }
        }

        {
            size_t ln = g_luaLog.size();
            int show = ln > 5 ? 5 : (int)ln;

            if (show == 0) addLbl("LogNone", "(lua log empty)", 900, 520, 13, th.ink);

            for (int i = 0; i < show; ++i) {
                std::string nm = "LogLn" + std::to_string(i);
                addLbl(nm.c_str(), g_luaLog[ln - show + i], 900, 520 + i * 16, 13, parseColor("#87CEEB"));
            }
        }

        std::string sb = (editor_ && editor_->scene() && editor_->scene()->bgSet())
            ? editor_->scene()->bg
            : std::string("(theme)");

        addLbl("InBg", "scene bg: " + sb, 900, 426, 16, th.ink);

        {
            UiButton b;
            b.touch.id = "lckbtn";
            b.touch.rect = Rect{900, 448, 44, 30};
            b.text = "LCK";
            b.action = "ed_lock";
            b.color = parseColor("#D62828");
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "bgbtn";
            b.touch.rect = Rect{948, 448, 44, 30};
            b.text = "BG";
            b.action = "bg_rgb";
            b.color = th.accent;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "actbtn";
            b.touch.rect = Rect{996, 448, 44, 30};
            b.text = "ACT";
            b.action = "edit_action";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "texbtn";
            b.touch.rect = Rect{1044, 448, 44, 30};
            b.text = "T-";
            b.action = "clear_tex";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "clnbtn";
            b.touch.rect = Rect{1092, 448, 44, 30};
            b.text = "DUP";
            b.action = "ed_clone";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "parbtn";
            b.touch.rect = Rect{1140, 448, 44, 30};
            b.text = pickParent_ ? "PICK" : "PAR";
            b.action = "ed_parent";
            b.color = pickParent_ ? GODOT_ORANGE : th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "unpbtn";
            b.touch.rect = Rect{900, 482, 44, 30};
            b.text = "UNP";
            b.action = "ed_unparent";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "cpybtn";
            b.touch.rect = Rect{948, 482, 44, 30};
            b.text = "CPY";
            b.action = "ed_copy";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "pstbtn";
            b.touch.rect = Rect{996, 482, 44, 30};
            b.text = "PST";
            b.action = "ed_paste";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "undbtn";
            b.touch.rect = Rect{1044, 482, 44, 30};
            b.text = "UND";
            b.action = "ed_undo";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "redbtn";
            b.touch.rect = Rect{1092, 482, 44, 30};
            b.text = "RED";
            b.action = "ed_redo";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        const char* mv[4] = { "l", "u", "d", "r" };
        const char* mvTxt[4] = { "<", "^", "v", ">" };

        for (int k = 0; k < 4; ++k) {
            UiButton b;
            b.touch.id = std::string("mv") + std::to_string(k);
            b.touch.rect = Rect{900 + (float)k * 58, 616, 54, 48};
            b.text = mvTxt[k];
            b.action = std::string("ed_move:") + mv[k];
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        float tx = 300;
        const char* shapes[4] = { "square", "circle", "diamond", "triangle" };
        const char* shTxt[4] = { "SQ", "CI", "DI", "TR" };

        for (int k = 0; k < 4; ++k) {
            UiButton b;
            b.touch.id = std::string("sh") + std::to_string(k);
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = shTxt[k];
            b.action = std::string("ed_shape:") + shapes[k];
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "colbtn";
            b.touch.rect = Rect{tx, 34, 54, 26};
            tx += 56;
            b.text = "RGB";
            b.action = "col_rgb";
            b.color = th.accent;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "del";
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = "DEL";
            b.action = "ed_del";
            b.color = parseColor("#D62828");
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "save";
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = "SAVE";
            b.action = "ed_save";
            b.color = parseColor("#2E7D32");
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "eback";
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = "<";
            b.action = "ed_back";
            b.color = GODOT_ORANGE;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "plus";
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = "+";
            b.action = "create_open";
            b.color = GODOT_ORANGE;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "txt";
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = "TXT";
            b.action = "edit_text";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "scr";
            b.touch.rect = Rect{tx, 34, 44, 26};
            tx += 46;
            b.text = "SCR";
            b.action = "ed_scr";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        if (showCreate_) {
            const char* ct[10] = { "Node2D", "Node2D", "Node2D", "Node2D", "Label", "Sprite2D", "", "", "", "" };
            const char* cs[10] = { "square", "circle", "diamond", "triangle", "", "", "", "", "", "" };
            const char* cl[10] = { "CUBE", "CIRCLE", "DIAMOND", "TRIANGLE", "TEXT", "SPRITE", "CAM", "LIGHT", "GRP", "BTN" };

            for (int k = 0; k < 6; ++k) {
                UiButton b;
                b.touch.id = std::string("ct") + std::to_string(k);
                b.touch.rect = Rect{300 + (float)k * 58, 560, 54, 40};
                b.text = cl[k];
                b.action = std::string("create:") + ct[k] + ":" + cs[k];
                b.color = th.button;
                editorScene_.ui.push_back(b);
            }

            {
                UiButton b;
                b.touch.id = "ctcam";
                b.touch.rect = Rect{300 + 6 * 58, 560, 54, 40};
                b.text = "CAM";
                b.action = "create_cam";
                b.color = th.accent;
                editorScene_.ui.push_back(b);
            }

            {
                UiButton b;
                b.touch.id = "ctlit";
                b.touch.rect = Rect{300 + 7 * 58, 560, 54, 40};
                b.text = "LIGHT";
                b.action = "create_light";
                b.color = parseColor("#FFD700");
                editorScene_.ui.push_back(b);
            }

            {
                UiButton b;
                b.touch.id = "ctgrp";
                b.touch.rect = Rect{300 + 8 * 58, 560, 54, 40};
                b.text = "GRP";
                b.action = "create_grp";
                b.color = parseColor("#808080");
                editorScene_.ui.push_back(b);
            }

            {
                UiButton b;
                b.touch.id = "ctbtn";
                b.touch.rect = Rect{300 + 9 * 58, 560, 54, 40};
                b.text = "BTN";
                b.action = "create_btn";
                b.color = parseColor("#2EC4B6");
                editorScene_.ui.push_back(b);
            }
        }

        addLbl("FsHdr", "FILES", 10, 384, 18, th.ink);

        {
            UiButton b;
            b.touch.id = "fsu";
            b.touch.rect = Rect{248, 382, 20, 22};
            b.text = "^";
            b.action = "fscroll_up";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        {
            UiButton b;
            b.touch.id = "fsd";
            b.touch.rect = Rect{270, 382, 20, 22};
            b.text = "v";
            b.action = "fscroll_dn";
            b.color = th.button;
            editorScene_.ui.push_back(b);
        }

        struct FsRow {
            std::string text, action;
            unsigned col;
        };

        std::vector<FsRow> frows;

        if (!fsPath_.empty()) frows.push_back({ "..", "fs_up", th.button });

        std::string abs = project_.rootPath + "/" + fsPath_;

        for (const auto& it : FileBrowser::list(abs)) {
            if (it.name.rfind("snap_", 0) == 0) continue;
            if (it.name == "save.vars") continue;

            FsRow r;
            r.text = (it.isDir ? "/ " : "  ") + it.name;
            r.action = it.isDir ? ("fs_enter:" + it.name) : ("fs_pick:" + fsPath_ + it.name);
            r.col = it.isDir ? th.button : th.accent;

            frows.push_back(r);
        }

        const int FMAXROWS = 8;
        int fmax = (int)frows.size() > FMAXROWS ? (int)frows.size() - FMAXROWS : 0;

        if (fsScroll_ < 0) fsScroll_ = 0;
        if (fsScroll_ > fmax) fsScroll_ = fmax;

        std::string shown = fsPath_.empty() ? std::string("res/") : ("res/" + fsPath_);
        addLbl("FsPath", shown + "  (" + std::to_string((int)frows.size()) + ")", 10, 406, 15, GODOT_ORANGE);

        float fy = 428;
        const float STEP = 28;

        for (int i = fsScroll_; i < (int)frows.size() && i < fsScroll_ + FMAXROWS; ++i) {
            UiButton fb;
            fb.touch.id = "fs" + std::to_string(i);
            fb.touch.rect = Rect{8, fy, 284, STEP - 2};
            fb.text = frows[i].text;
            fb.action = frows[i].action;
            fb.color = frows[i].col;

            editorScene_.ui.push_back(fb);
            fy += STEP;
        }
}
