#pragma once
#include "GameApp.hpp"

namespace suka {

inline void GameApp::feedMultiTouch(int phase, float x0, float y0, float x1, float y1) {
    if (appMode_ != AppMode::Editor || scriptMode_ || showCreate_) return;
    Scene* es = editor_ ? editor_->scene() : nullptr;
    if (!es) return;
    float mx = (x0 + x1) / 2.0f, my = (y0 + y1) / 2.0f;
    float dist = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    if (phase == 1) {
        pinching_ = true; pinchDist0_ = dist; pinchZoom0_ = edZoom_;
        float S = 0.46875f * edZoom_;
        pinchAX_ = 640 + es->camX + (mx - 596) / S;
        pinchAY_ = 360 + es->camY + (my - 310) / S;
        return;
    }
    if (phase == 3) { pinching_ = false; return; }
    if (!pinching_) return;
    float z = pinchZoom0_;
    if (pinchDist0_ > 4 && dist > 4) {
        z = pinchZoom0_ * (dist / pinchDist0_);
        if (z < 0.01f) z = 0.01f; if (z > 256.0f) z = 256.0f;
    }
    float S = 0.46875f * z;
    es->camX = pinchAX_ - 640 - (mx - 596) / S;
    es->camY = pinchAY_ - 360 - (my - 310) / S;
    edZoom_ = z;
}

inline void GameApp::feedTouch(int action, float x, float y) {
    if (action == 9) {
        if (appMode_ == AppMode::Editor && scriptMode_ && editor_) {
            int line = (int)x;
            int col  = (int)y;
            if (line < 0) line = 0;
            if (line >= (int)scriptLines_.size()) line = (int)scriptLines_.size() - 1;
            const std::string& L = scriptLines_[line];
            if (col < 0) col = 0;
            if (col > (int)L.size()) col = (int)L.size();
            while (col > 0 && col < (int)L.size() && ((unsigned char)L[col] & 0xC0) == 0x80) --col;
            curLine_ = line; curCol_ = col; compAnchor_ = -1;
            imeWantOn_ = true; imeShown_ = true; imeChanged_ = true;
        }
        return;
    }

    if (appMode_ == AppMode::Game && transActive_) return;

    RawTouch t;
    if (action == 0) t.action = RawTouch::Action::Down;
    else if (action == 2) t.action = RawTouch::Action::Move;
    else t.action = RawTouch::Action::Up;
    t.x = x; t.y = y;
    Scene* cur = uiScene();
    if (!cur) return;

    if (appMode_ == AppMode::Editor && scriptMode_) {
        if (t.action == RawTouch::Action::Down && x >= 300 && x <= 850 && y >= 64 && y <= 556) {
            const float LH = 19;
            int line = scriptScroll_ + (int)((y - 70) / LH);
            if (line < 0) line = 0;
            if (line >= (int)scriptLines_.size()) line = (int)scriptLines_.size() - 1;
            int colCp = (int)((x - 340) / 8.0f);
            const std::string& L = scriptLines_[line];
            int total = utf8ByteToCp(L, (int)L.size());
            if (colCp < 0) colCp = 0; if (colCp > total) colCp = total;
            curLine_ = line; curCol_ = utf8CpToByte(L, colCp);
            compAnchor_ = -1;
            imeWantOn_ = true; imeShown_ = true; imeChanged_ = true;
        }
        touch_.onTouch(t, *cur, input_);
        return;
    }

    if (appMode_ == AppMode::Game && t.action == RawTouch::Action::Down && sceneMgr_ && sceneMgr_->current()) {
        Scene* gs = sceneMgr_->current();
        float wx, wy; unprojGame(*gs, x, y, wx, wy);
        std::string hit = hitTest(gs->root.get(), wx, wy);
        if (!hit.empty()) {
            Node* fn = gs->root->findNode(hit);
            Node2D* n = fn ? dynamic_cast<Node2D*>(fn) : nullptr;
            if (n && !n->action.empty()) { pendingNodeAction_ = n->action; return; }
            if (n && projSounds_.count(n->name)) { queueNodeSound(n->name); return; }
        }
    }

    // Ряд кнопок функций (кнопка FN теперь в тулбаре как ui-кнопка).
    if (appMode_ == AppMode::Editor && !scriptMode_) {
        if (showFuncs_ && t.action == RawTouch::Action::Down && y >= FNR_Y && y <= FNR_Y + FNR_H) {
            if (x >= FNR_RIG_X0 && x <= FNR_RIG_X1) { addFuncToSelected("rigidbody"); return; }
            if (x >= FNR_STA_X0 && x <= FNR_STA_X1) { addFuncToSelected("staticbody"); return; }
            if (x >= FNR_NOG_X0 && x <= FNR_NOG_X1) { addFuncToSelected("nogravity"); return; }
            if (x >= FNR_BOU_X0 && x <= FNR_BOU_X1) { addFuncToSelected("bouncy"); return; }
            if (x >= FNR_CLR_X0 && x <= FNR_CLR_X1) { clearFuncsSelected(); return; }
            if (x >= FNR_X_X0  && x <= FNR_X_X1)  { showFuncs_ = false; return; }
        }
    }

    // Панель звука выделенного Sound-объекта (внизу вьюпорта).
    if (appMode_ == AppMode::Editor && !scriptMode_ && t.action == RawTouch::Action::Down
        && y >= SNP_Y0 && y <= SNP_Y1) {
        Node* sn0 = editor_ ? editor_->selected() : nullptr;
        if (sn0 && projSounds_.count(sn0->name)) {
            SoundDef& sd = projSounds_[sn0->name];
            if (x >= SNP_SET_X0 && x <= SNP_SET_X1) { pendingSound_ = true; pendingSoundTarget_ = sn0->name; pendingText_ = true; pendingTextCur_ = sd.snd; return; }
            if (x >= SNP_LOOP_X0 && x <= SNP_LOOP_X1) { sd.loop = !sd.loop; saveProjFuncs(); return; }
            if (x >= SNP_AUTO_X0 && x <= SNP_AUTO_X1) { sd.autoplay = !sd.autoplay; saveProjFuncs(); return; }
            if (x >= SNP_PLAY_X0 && x <= SNP_PLAY_X1) { queueNodeSound(sn0->name); return; }
            if (x >= SNP_STOP_X0 && x <= SNP_STOP_X1) { lua_stop_music(); return; }
        }
    }

    if (appMode_ == AppMode::Editor && !scriptMode_ && !showCreate_) {
        if (t.action == RawTouch::Action::Down && !showSettings_ && !showPrefabs_ && !showAssets_
            && x >= CAM_PX0 && x <= CAM_PX1) {
            if (y >= CAM_BTN_W_Y0 && y <= CAM_BTN_W_Y1) { pendingNum_ = true; pendingNumKind_ = "camw"; pendingNumCur_ = std::to_string((int)projCamW_); return; }
            if (y >= CAM_BTN_H_Y0 && y <= CAM_BTN_H_Y1) { pendingNum_ = true; pendingNumKind_ = "camh"; pendingNumCur_ = std::to_string((int)projCamH_); return; }
            if (y >= CAM_BTN_O_Y0 && y <= CAM_BTN_O_Y1) { projVertical_ = !projVertical_; saveProjCamera(); return; }
        }

        Scene* es = editor_ ? editor_->scene() : nullptr;
        float Z = edZoom_;

        if (t.action == RawTouch::Action::Down && pickParent_ && !pickChild_.empty() && es && es->root) {
            float wx, wy; unproj(*es, x, y, wx, wy);
            std::string hit = hitTest(es->root.get(), wx, wy);
            if (!hit.empty() && hit != pickChild_) {
                pushUndo(); attachChildTo(pickChild_, hit);
                lastMsg_ = "attached " + pickChild_ + " -> " + hit;
                if (pickChild_.rfind("UI:", 0) == 0) editor_->selectUi(pickChild_.substr(3));
                else editor_->select(pickChild_);
            } else {
                lastMsg_ = hit.empty() ? "no target under tap" : "cannot attach to self";
            }
            pickParent_ = false; pickChild_.clear();
            buildEditorPanels(); input_.setUi(&editorScene_.ui);
            return;
        }

        UiButton* gb = (editor_ && !editor_->selectedUi().empty()) ? editor_->findUi(editor_->selectedUi()) : nullptr;
        if (gb && es) {
            float bcx, bcy;
            proj(*es, gb->touch.rect.x + gb->touch.rect.w / 2, gb->touch.rect.y + gb->touch.rect.h / 2, bcx, bcy);
            if (t.action == RawTouch::Action::Down) {
                float dx = x - bcx, dy = y - bcy;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (manip_ == Manip::Rotate && std::fabs(dist - 70.0f) < 26.0f) {
                    gizmoRotUi_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = gb->angle; return;
                }
            } else if (t.action == RawTouch::Action::Move && gizmoRotUi_) {
                float dx = x - bcx, dy = y - bcy;
                gb->angle = gizmoStartRot_ + (std::atan2(dy, dx) - gizmoStartAngle_) * 57.2957795f; return;
            } else if (t.action == RawTouch::Action::Up) { gizmoRotUi_ = false; }
        }

        Node2D* g = (editor_ && editor_->selectedUi().empty()) ? (editor_->selected() ? dynamic_cast<Node2D*>(editor_->selected()) : nullptr) : nullptr;
        if (g && es) {
            float gwx, gwy, gwr, gsx, gsy;
            if (!nodeWorld(es, g->name, gwx, gwy, gwr, gsx, gsy)) { gwx = g->position.x; gwy = g->position.y; gsx = gsy = 1; }
            float scx, scy; proj(*es, gwx, gwy, scx, scy);
            if (t.action == RawTouch::Action::Down) {
                if (!g->locked) {
                    float dx = x - scx, dy = y - scy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (manip_ == Manip::Rotate) {
                        if (std::fabs(dist - 70.0f) < 26.0f) { gizmoRot_ = true; gizmoStartAngle_ = std::atan2(dy, dx); gizmoStartRot_ = g->rotation; return; }
                    } else if (manip_ == Manip::Scale) {
                        float hw = (g->w * gsx) * 0.46875f * Z / 2;
                        float hh = (g->h * gsy) * 0.46875f * Z / 2;
                        if (std::fabs(x - (scx + hw + 24)) < 28 && std::fabs(dy) < 28) { gizmoSclX_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSX_ = g->scale.x; return; }
                        if (std::fabs(y - (scy + hh + 24)) < 28 && std::fabs(dx) < 28) { gizmoSclY_ = true; gizmoStartDist_ = dist > 1 ? dist : 1; gizmoStartSY_ = g->scale.y; return; }
                    } else {
                        if (std::fabs(dy) < 16 && dx > 8 && dx < 64) { lockAxis_ = 1; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                        if (std::fabs(dx) < 16 && dy > 8 && dy < 64) { lockAxis_ = 2; dragging_ = true; dragNode_ = g; editor_->select(g->name); startDragParent(es, g->name); return; }
                    }
                }
            } else if (t.action == RawTouch::Action::Move) {
                if (gizmoRot_) { g->rotation = gizmoStartRot_ + (std::atan2(y - scy, x - scx) - gizmoStartAngle_); return; }
                if (gizmoSclX_ || gizmoSclY_) {
                    float dist = std::sqrt((x - scx) * (x - scx) + (y - scy) * (y - scy));
                    float f = dist / gizmoStartDist_; if (f < 0.05f) f = 0.05f;
                    if (gizmoSclX_) g->scale.x = gizmoStartSX_ * f;
                    if (gizmoSclY_) g->scale.y = gizmoStartSY_ * f;
                    return;
                }
            } else if (t.action == RawTouch::Action::Up) { gizmoRot_ = false; gizmoSclX_ = false; gizmoSclY_ = false; lockAxis_ = 0; }
        }

        const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;
        bool inVP = (x >= VX0 && x <= VX0 + VW && y >= VY0 && y <= VY0 + VH);
        if (t.action == RawTouch::Action::Down && inVP && es) {
            float wx, wy; unproj(*es, x, y, wx, wy);
            std::string uiHit = hitUi(es, wx, wy);
            if (!uiHit.empty()) { editor_->selectUi(uiHit); dragUi_ = editor_->findUi(uiHit); dragging_ = (dragUi_ != nullptr); lockAxis_ = 0; }
            else if (es->root) {
                std::string hit = hitTest(es->root.get(), wx, wy);
                if (!hit.empty()) {
                    editor_->select(hit);
                    Node2D* hitN = editor_->find2d(hit);
                    if (hitN && !hitN->locked) { dragNode_ = hitN; dragging_ = true; lockAxis_ = 0; startDragParent(es, hit); }
                    else { dragNode_ = nullptr; dragging_ = false; }
                }
            }
        } else if (t.action == RawTouch::Action::Move && dragging_ && es) {
            float wx, wy; unproj(*es, x, y, wx, wy);
            if (dragUi_) { dragUi_->touch.rect.x = wx - dragUi_->touch.rect.w / 2; dragUi_->touch.rect.y = wy - dragUi_->touch.rect.h / 2; }
            else if (dragNode_) {
                float oldX = dragOX_ + dragNode_->position.x * dragPSX_;
                float oldY = dragOY_ + dragNode_->position.y * dragPSY_;
                float nx, ny;
                if (lockAxis_ == 1) { nx = wx; ny = oldY; }
                else if (lockAxis_ == 2) { nx = oldX; ny = wy; }
                else { nx = wx; ny = wy; }
                float dlx = (nx - oldX) / dragPSX_;
                float dly = (ny - oldY) / dragPSY_;
                dragNode_->position.x += dlx; dragNode_->position.y += dly;
                moveGroupButtons(es, dragNode_->name, nx - oldX, ny - oldY);
            }
        } else if (t.action == RawTouch::Action::Up) { dragging_ = false; dragNode_ = nullptr; dragUi_ = nullptr; lockAxis_ = 0; }
    }

    touch_.onTouch(t, *cur, input_);
}

} // namespace suka
