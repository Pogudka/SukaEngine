#pragma once

#include <string>
#include <vector>
#include <set>
#include <memory>
#include <cmath>
#include <cctype>
#include <algorithm>

#include "Scene.hpp"
#include "Editor.hpp"
#include "Settings.hpp"
#include "Render.hpp"
#include "Resources.hpp"
#include "CommonTypes.hpp"
#include "UiUtils.hpp"

namespace suka {

struct EditorUiInput {
    Editor* editor;

    bool scriptMode;
    float edZoom;
    Manip manip;

    bool pickParent;
    bool showCreate;
    bool showAssets;
    bool showSettings;
    bool showPrefabs;
    bool showFiles;

    int& hierScroll;
    int& fsScroll;
    int& assetScroll;
    int& scriptScroll;
    int& prefabScroll;

    const std::set<std::string>& collapsed;
    const std::string& fsPath;
    const std::string& projectRoot;

    const std::string& scriptPath;
    const std::vector<std::string>& scriptLines;

    int curLine;
    int curCol;

    bool imeShown;

    const std::vector<std::string>& luaLog;
};

inline std::string editorDash(int depth) {
    return depth > 0 ? std::string(depth, '-') + " " : std::string();
}

inline bool editorHasUiGroup(Scene* esc, const std::string& name) {
    if (!esc) return false;
    for (auto& ub : esc->ui) if (ub.group == name) return true;
    return false;
}

inline bool editorIsImageName(const std::string& n) {
    size_t dot = n.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= n.size()) return false;
    std::string ext = n.substr(dot + 1);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp";
}

struct EdRow {
    std::string text;
    std::string action;
    bool sel;
    bool hasKids;
    bool open;
    std::string name;
};

inline void editorBuildTreeRows(
    Scene* esc, Node* n, int depth,
    const std::string& sel, const std::string& selUi,
    std::vector<EdRow>& rows, const std::set<std::string>& collapsed
) {
    for (auto& ch : n->getChildren()) {
        std::string nm = ch->name;
        bool kids = ch->childCount() > 0 || editorHasUiGroup(esc, nm);
        bool open = collapsed.count(nm) == 0;
        Node2D* ch2d = dynamic_cast<Node2D*>(ch.get());
        std::string lockMark = (ch2d && ch2d->locked) ? " [L]" : "";

        EdRow r;
        r.text = editorDash(depth) + (kids ? (open ? "[-] " : "[+] ") : "    ") + nm + lockMark + "   " + std::string(ch->typeName());
        r.action = "ed_select:" + nm;
        r.sel = (sel == nm);
        r.hasKids = kids;
        r.open = open;
        r.name = nm;
        rows.push_back(r);

        if (kids && open) {
            editorBuildTreeRows(esc, ch.get(), depth + 1, sel, selUi, rows, collapsed);
            for (auto& ub : esc->ui) {
                if (ub.group == nm) {
                    EdRow br;
                    br.text = editorDash(depth + 1) + "    " + ub.touch.id + "   Button";
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

inline void editorBuildScriptScene(const EditorUiInput& in, Scene& s) {
    Theme& th = currentTheme();
    const unsigned GO = 0xFF8800FFu;

    auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
        auto l = std::make_unique<Label>();
        l->name = nm; l->text = txt; l->fontSize = fs; l->color = col; l->position = Vec2{x, y};
        s.root->addChild(std::move(l));
    };

    const float VX0 = 300, VY0 = 64, VW = 592, VH = 492;

    addLbl("ScHdr", "SCRIPTS", 10, 40, 18, th.ink);

    std::vector<FileEntry> ls = FileBrowser::list(in.projectRoot + "/scripts");
    int li = 0;
    for (auto& it : ls) {
        if (it.isDir) continue;
        if (it.name.size() < 4 || it.name.compare(it.name.size() - 4, 4, ".lua") != 0) continue;
        if (li >= 11) break;
        UiButton b;
        b.touch.id = "scf" + std::to_string(li);
        b.touch.rect = Rect{8, 64 + (float)li * 28, 284, 26};
        b.text = "  " + it.name;
        b.action = "scrfile:" + it.name;
        b.color = (in.scriptPath == "scripts/" + it.name) ? GO : th.button;
        s.ui.push_back(b);
        ++li;
    }

    {
        UiButton b;
        b.touch.id = "scnew";
        b.touch.rect = Rect{8, 64 + (float)li * 28, 284, 26};
        b.text = "+ NEW SCRIPT";
        b.action = "snew";
        b.color = th.accent;
        s.ui.push_back(b);
    }

    auto edbg = std::make_unique<Node2D>();
    edbg->name = "__scbg"; edbg->shape = "square"; edbg->color = 0x101018FF;
    edbg->w = VW; edbg->h = VH; edbg->position = Vec2{VX0 + VW / 2, VY0 + VH / 2};
    s.root->addChild(std::move(edbg));

    { UiButton b; b.touch.id = "ssave"; b.touch.rect = Rect{300, 34, 70, 26}; b.text = "SAVE"; b.action = "ssave"; b.color = parseColor("#2E7D32"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "sclose"; b.touch.rect = Rect{374, 34, 70, 26}; b.text = "SCENE"; b.action = "tab_scene"; b.color = GO; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "kbtog"; b.touch.rect = Rect{448, 34, 80, 26}; b.text = in.imeShown ? "KB OFF" : "KB ON"; b.action = "kb_toggle"; b.color = in.imeShown ? th.button : th.accent; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "scu"; b.touch.rect = Rect{860, 70, 26, 26}; b.text = "^"; b.action = "scup"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "scd"; b.touch.rect = Rect{860, 100, 26, 26}; b.text = "v"; b.action = "scdn"; b.color = th.button; s.ui.push_back(b); }

    const int LINES = 24;
    const float LH = 19;
    int smax = (int)in.scriptLines.size() > LINES ? (int)in.scriptLines.size() - LINES : 0;
    if (in.scriptScroll < 0) in.scriptScroll = 0;
    if (in.scriptScroll > smax) in.scriptScroll = smax;

    for (int i = in.scriptScroll; i < (int)in.scriptLines.size() && i < in.scriptScroll + LINES; ++i) {
        float y = 70 + (float)(i - in.scriptScroll) * LH;
        std::string num = std::to_string(i + 1);
        while (num.size() < 3) num = " " + num;
        addLbl(("ScN" + std::to_string(i)).c_str(), num, 306, y, 14, parseColor("#667089"));
        std::string txt = sanitizeLine(in.scriptLines[i]);
        if (txt.size() > 68) txt = txt.substr(0, 68);
        addLbl(("ScC" + std::to_string(i)).c_str(), txt, 340, y, 14, i == in.curLine ? parseColor("#FFD700") : parseColor("#D8E0F0"));
    }

    if (in.curLine >= in.scriptScroll && in.curLine < in.scriptScroll + LINES) {
        int cps = utf8ByteToCp(in.scriptLines[in.curLine], in.curCol);
        auto cur = std::make_unique<Node2D>();
        cur->name = "__sccur"; cur->shape = "square"; cur->color = 0x4CC9F0FF; cur->w = 3; cur->h = 16;
        float cxp = 340 + cps * 8.0f;
        float cyp = 70 + (float)(in.curLine - in.scriptScroll) * LH + 8;
        cur->position = Vec2{cxp + 1, cyp};
        s.root->addChild(std::move(cur));
    }

    addLbl("ScInfo", in.scriptPath.empty() ? "(no script)" : in.scriptPath, 900, 64, 16, GO);
    addLbl("ScInfo2", "lines " + std::to_string((int)in.scriptLines.size()) + "   cur " + std::to_string(in.curLine + 1) + ":" + std::to_string(utf8ByteToCp(in.scriptLines.empty() ? std::string("") : in.scriptLines[in.curLine], in.curCol)), 900, 88, 14, th.ink);
    addLbl("ScInfo3", "tap line = cursor + keyboard", 900, 110, 14, th.ink);
    addLbl("ScInfo4", "SAVE writes file + reloads scripts", 900, 132, 14, th.ink);
}

inline Scene buildEditorScene(const EditorUiInput& in) {
    Theme& th = currentTheme();
    const unsigned GO = 0xFF8800FFu;

    Scene s;
    s.name = "Editor";
    s.root = std::make_unique<Node>();
    s.root->name = "EdRoot";

    auto addLbl = [&](const char* nm, const std::string& txt, float x, float y, float fs, unsigned col) {
        auto l = std::make_unique<Label>();
        l->name = nm; l->text = txt; l->fontSize = fs; l->color = col; l->position = Vec2{x, y};
        s.root->addChild(std::move(l));
    };

    { UiButton b; b.touch.id = "settings_btn"; b.touch.rect = Rect{10, 4, 32, 26}; b.text = "*"; b.action = "settings_open"; b.color = GO; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "tab_scene"; b.touch.rect = Rect{50, 4, 80, 26}; b.text = "Scene"; b.action = "tab_scene"; b.color = in.scriptMode ? th.button : GO; s.ui.push_back(b); }
    addLbl("Tab2D", "2D", 140, 8, 20, th.ink);
    addLbl("Tab3D", "3D", 190, 8, 20, th.ink);
    { UiButton b; b.touch.id = "tab_scripts"; b.touch.rect = Rect{230, 4, 90, 26}; b.text = "Scripts"; b.action = "tab_scripts"; b.color = in.scriptMode ? GO : th.button; s.ui.push_back(b); }
    addLbl("TabAss", "AssetLib", 330, 8, 20, th.ink);

    if (in.scriptMode) { editorBuildScriptScene(in, s); return s; }
    if (!in.editor || !in.editor->scene()) return s;

    auto fsBg = std::make_unique<Node2D>();
    fsBg->name = "FsBg"; fsBg->shape = "square"; fsBg->color = dimColor(th.bg, 0.6f);
    fsBg->w = 284; fsBg->h = 320; fsBg->position = Vec2{150, 536};
    s.root->addChild(std::move(fsBg));

    addLbl("DHdr", "Scene", 10, 40, 18, th.ink);
    addLbl("IHdr", "Inspector", 900, 40, 18, th.ink);
    addLbl("ZoomLbl", "zoom " + std::to_string((int)(in.edZoom * 100)) + "%", 380, 8, 16, th.ink);

    const char* mlab[3] = { "POS", "ROT", "SCL" };
    Manip mval[3] = { Manip::Move, Manip::Rotate, Manip::Scale };
    for (int k = 0; k < 3; ++k) {
        UiButton b;
        b.touch.id = std::string("manipbtn") + std::to_string(k);
        b.touch.rect = Rect{1090 + (float)k * 60, 34, 56, 28};
        b.text = mlab[k];
        b.action = std::string("manip:") + (k == 0 ? "move" : k == 1 ? "rotate" : "scale");
        b.color = (in.manip == mval[k]) ? GO : th.button;
        s.ui.push_back(b);
    }

    Scene* esc = in.editor->scene();
    Node* selNode = in.editor->selected();
    std::string sel = selNode ? selNode->name : std::string{};
    std::string selUi = in.editor->selectedUi();

    std::vector<EdRow> rows;
    if (esc && esc->root) editorBuildTreeRows(esc, esc->root.get(), 0, sel, selUi, rows, in.collapsed);
    if (esc) {
        for (auto& ub : esc->ui) {
            if (!ub.group.empty()) continue;
            EdRow r;
            r.text = "    " + ub.touch.id + "   Button";
            r.action = "ed_selectui:" + ub.touch.id;
            r.sel = (selUi == ub.touch.id);
            r.hasKids = false; r.open = false; r.name = "";
            rows.push_back(r);
        }
    }

    const int VIS = 10;
    int maxScroll = (int)rows.size() > VIS ? (int)rows.size() - VIS : 0;
    if (in.hierScroll < 0) in.hierScroll = 0;
    if (in.hierScroll > maxScroll) in.hierScroll = maxScroll;

    { UiButton b; b.touch.id = "hup"; b.touch.rect = Rect{248, 38, 20, 22}; b.text = "^"; b.action = "hier_up"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "hdn"; b.touch.rect = Rect{270, 38, 20, 22}; b.text = "v"; b.action = "hier_dn"; b.color = th.button; s.ui.push_back(b); }

    for (int i = in.hierScroll; i < (int)rows.size() && i < in.hierScroll + VIS; ++i) {
        float y = 64 + (float)(i - in.hierScroll) * 30;
        if (rows[i].hasKids) {
            UiButton f;
            f.touch.id = "fold" + std::to_string(i);
            f.touch.rect = Rect{8, y, 26, 28};
            f.text = rows[i].open ? "-" : "+";
            f.action = "fold:" + rows[i].name;
            f.color = th.accent;
            s.ui.push_back(f);
        }
        UiButton b;
        b.touch.id = "h" + std::to_string(i);
        float rx = rows[i].hasKids ? 36.0f : 8.0f;
        float rw = rows[i].hasKids ? 256.0f : 284.0f;
        b.touch.rect = Rect{rx, y, rw, 28};
        b.text = rows[i].text;
        b.action = rows[i].action;
        b.color = rows[i].sel ? GO : th.button;
        s.ui.push_back(b);
    }

    if (!selUi.empty()) {
        UiButton* ub = in.editor->findUi(selUi);
        if (ub) {
            addLbl("InName", ub->touch.id, 900, 64, 22, GO);
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
                s.ui.push_back(b);
            }
        }
    } else {
        Node2D* nd = (!sel.empty()) ? in.editor->find2d(sel) : nullptr;
        if (nd) {
            addLbl("InName", nd->name, 900, 64, 22, GO);
            addLbl("InType", std::string(nd->typeName()), 900, 92, 16, th.ink);
            addLbl("InPos", "Position  (" + std::to_string((int)nd->position.x) + ", " + std::to_string((int)nd->position.y) + ")", 900, 124, 16, th.ink);
            addLbl("InRot", "Rotation  " + std::to_string((int)(nd->rotation * 57.2957795f)), 900, 148, 16, th.ink);
            addLbl("InScl", "Scale  (" + std::to_string((int)(nd->scale.x * 100)) + "%, " + std::to_string((int)(nd->scale.y * 100)) + "%)", 900, 172, 16, th.ink);
            addLbl("InSiz", "Size  (" + std::to_string((int)nd->w) + ", " + std::to_string((int)nd->h) + ")", 900, 196, 16, th.ink);
            addLbl("InLck", "Locked  " + std::string(nd->locked ? "YES" : "no"), 900, 220, 16, nd->locked ? parseColor("#D62828") : th.ink);
            addLbl("InAlp", "Alpha  " + std::to_string((int)(nd->alpha * 100)) + "%", 900, 244, 16, th.ink);
            addLbl("InShp", "Shape  " + nd->shape, 900, 268, 16, th.ink);
            addLbl("InCol", "Color  " + colorToHex(nd->color) + "  (" + rgbStr(nd->color) + ")", 900, 292, 16, th.ink);
            addLbl("InTex", "Texture: " + (nd->texture.empty() ? std::string("(none)") : nd->texture), 900, 316, 16, th.ink);

            {
                std::string texNow = nd->texture;
                if (texNow.empty() && std::string(nd->typeName()) == "Sprite2D") texNow = static_cast<Sprite2D*>(nd)->texturePath;
                if (!texNow.empty()) {
                    UiButton rb;
                    rb.touch.id = "rmtex";
                    rb.touch.rect = Rect{1092, 312, 88, 26};
                    rb.text = "RM TEX";
                    rb.action = "clear_tex";
                    rb.color = parseColor("#D62828");
                    s.ui.push_back(rb);
                }
            }

            addLbl("InAct", "Touch: " + (nd->action.empty() ? std::string("(none)") : nd->action), 900, 340, 16, th.ink);

            if (std::string(nd->typeName()) == "Label") {
                addLbl("InText", "Text: " + static_cast<Label*>(nd)->text, 900, 364, 16, th.ink);
            }

            // Prefab inspector: source + children count + RELOAD. Nothing else.
            if (std::string(nd->typeName()) == "Prefab2D") {
                Prefab2D* pf = static_cast<Prefab2D*>(nd);
                addLbl("InSrc", "Source: " + (pf->sourcePath.empty() ? std::string("(none)") : pf->sourcePath), 900, 364, 16, parseColor("#8E44AD"));
                addLbl("InInst", "instance: " + std::to_string((int)pf->childCount()) + " children", 900, 388, 14, th.ink);
                { UiButton b; b.touch.id = "reloadpf"; b.touch.rect = Rect{900, 410, 100, 26}; b.text = "RELOAD"; b.action = "prefab_reload"; b.color = th.accent; s.ui.push_back(b); }
            }

            if (std::string(nd->typeName()) == "Particle2D") {
                Particle2D* pe = static_cast<Particle2D*>(nd);
                std::string cur = in.editor->currentEmitterPreset(sel);
                addLbl("InView", "View: " + (cur.empty() ? std::string("(custom)") : cur), 900, 364, 16, th.ink);

                int vk = 0;
                for (const auto& p : emitterPresets()) {
                    UiButton vb;
                    vb.touch.id = std::string("viewbtn") + std::to_string(vk);
                    vb.touch.rect = Rect{900 + (float)vk * 46, 386, 44, 26};
                    vb.text = p.name;
                    vb.action = std::string("view:") + p.name;
                    vb.color = (cur == p.name) ? GO : th.button;
                    s.ui.push_back(vb);
                    ++vk;
                }

                const char* pl[7] = { "RT", "LF", "SZ", "SP", "GR", "CL", "ON" };
                const char* pa[7] = { "pnum:rate", "pnum:life", "pnum:size", "pnum:spread", "pnum:gravity", "pcolor", "ponoff" };
                for (int k = 0; k < 7; ++k) {
                    UiButton pb;
                    pb.touch.id = std::string("pctl") + std::to_string(k);
                    pb.touch.rect = Rect{900 + (float)k * 40, 418, 38, 26};
                    pb.text = pl[k];
                    pb.action = pa[k];
                    pb.color = (k == 6) ? (pe->emitting ? GO : th.button) : th.button;
                    s.ui.push_back(pb);
                }
            } else if (std::string(nd->typeName()) != "Prefab2D") {
                const char* nl[7] = { "X", "Y", "ROT", "SCL", "W", "H", "A" };
                const char* na[7] = { "nx", "ny", "nrot", "nscl", "nw", "nh", "nalpha" };
                for (int k = 0; k < 7; ++k) {
                    UiButton b;
                    b.touch.id = std::string("numbtn") + std::to_string(k);
                    b.touch.rect = Rect{900 + (float)k * 46, 392, 44, 28};
                    b.text = nl[k];
                    b.action = std::string("num:") + na[k];
                    b.color = th.button;
                    s.ui.push_back(b);
                }
            }
        } else {
            addLbl("InNone", "(no selection)", 900, 92, 18, th.ink);
        }
    }

    {
        size_t ln = in.luaLog.size();
        int show = ln > 5 ? 5 : (int)ln;
        if (show == 0) addLbl("LogNone", "(lua log empty)", 900, 520, 13, th.ink);
        for (int i = 0; i < show; ++i) {
            std::string nm = "LogLn" + std::to_string(i);
            addLbl(nm.c_str(), in.luaLog[ln - show + i], 900, 520 + i * 16, 13, parseColor("#87CEEB"));
        }
    }

    std::string sb = (esc && esc->bgSet()) ? esc->bg : std::string("(theme)");
    addLbl("InBg", "scene bg: " + sb, 900, 426, 16, th.ink);

    { UiButton b; b.touch.id = "lckbtn"; b.touch.rect = Rect{900, 448, 44, 30}; b.text = "LCK"; b.action = "ed_lock"; b.color = parseColor("#D62828"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "bgbtn"; b.touch.rect = Rect{948, 448, 44, 30}; b.text = "BG"; b.action = "bg_rgb"; b.color = th.accent; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "actbtn"; b.touch.rect = Rect{996, 448, 44, 30}; b.text = "ACT"; b.action = "edit_action"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "texbtn"; b.touch.rect = Rect{1044, 448, 44, 30}; b.text = "T-"; b.action = "clear_tex"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "clnbtn"; b.touch.rect = Rect{1092, 448, 44, 30}; b.text = "DUP"; b.action = "ed_clone"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "parbtn"; b.touch.rect = Rect{1140, 448, 44, 30}; b.text = in.pickParent ? "PICK" : "PAR"; b.action = "ed_parent"; b.color = in.pickParent ? GO : th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "unpbtn"; b.touch.rect = Rect{900, 482, 44, 30}; b.text = "UNP"; b.action = "ed_unparent"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "cpybtn"; b.touch.rect = Rect{948, 482, 44, 30}; b.text = "CPY"; b.action = "ed_copy"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "pstbtn"; b.touch.rect = Rect{996, 482, 44, 30}; b.text = "PST"; b.action = "ed_paste"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "undbtn"; b.touch.rect = Rect{1044, 482, 44, 30}; b.text = "UND"; b.action = "ed_undo"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "redbtn"; b.touch.rect = Rect{1092, 482, 44, 30}; b.text = "RED"; b.action = "ed_redo"; b.color = th.button; s.ui.push_back(b); }

    const char* mv[4] = { "l", "u", "d", "r" };
    const char* mvTxt[4] = { "<", "^", "v", ">" };
    for (int k = 0; k < 4; ++k) {
        UiButton b;
        b.touch.id = std::string("mv") + std::to_string(k);
        b.touch.rect = Rect{900 + (float)k * 58, 616, 54, 48};
        b.text = mvTxt[k];
        b.action = std::string("ed_move:") + mv[k];
        b.color = th.button;
        s.ui.push_back(b);
    }

    // Toolbar: SAVE SCN | PF SAVE | PF ADD | RGB | DEL | SAVE | < | + | TXT | SCR | TEX | FILES
    float tx = 300;
    { UiButton b; b.touch.id = "savesc"; b.touch.rect = Rect{tx, 34, 92, 26}; tx += 94; b.text = "SAVE SCN"; b.action = "save_scene_as"; b.color = parseColor("#2E7D32"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "savepf"; b.touch.rect = Rect{tx, 34, 74, 26}; tx += 76; b.text = "PF SAVE"; b.action = "save_as_prefab"; b.color = parseColor("#8E44AD"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "addpf"; b.touch.rect = Rect{tx, 34, 74, 26}; tx += 76; b.text = "PF ADD"; b.action = "prefabs_open"; b.color = parseColor("#8E44AD"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "colbtn"; b.touch.rect = Rect{tx, 34, 54, 26}; tx += 56; b.text = "RGB"; b.action = "col_rgb"; b.color = th.accent; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "del"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "DEL"; b.action = "ed_del"; b.color = parseColor("#D62828"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "save"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "SAVE"; b.action = "ed_save"; b.color = parseColor("#2E7D32"); s.ui.push_back(b); }
    { UiButton b; b.touch.id = "eback"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "<"; b.action = "ed_back"; b.color = GO; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "plus"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "+"; b.action = "create_open"; b.color = GO; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "txt"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "TXT"; b.action = "edit_text"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "scr"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "SCR"; b.action = "ed_scr"; b.color = th.button; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "tex"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "TEX"; b.action = "assets_open"; b.color = in.showAssets ? GO : th.accent; s.ui.push_back(b); }
    { UiButton b; b.touch.id = "file"; b.touch.rect = Rect{tx, 34, 44, 26}; tx += 46; b.text = "FILES"; b.action = "files_open"; b.color = in.showFiles ? GO : th.button; s.ui.push_back(b); }

    if (in.showCreate) {
        const char* ct[6] = { "Node2D", "Node2D", "Node2D", "Node2D", "Label", "Sprite2D" };
        const char* cs[6] = { "square", "circle", "diamond", "triangle", "", "" };
        const char* cl[6] = { "CUBE", "CIRCLE", "DIAMOND", "TRIANGLE", "TEXT", "SPRITE" };
        for (int k = 0; k < 6; ++k) {
            UiButton b;
            b.touch.id = std::string("ct") + std::to_string(k);
            b.touch.rect = Rect{300 + (float)k * 58, 560, 54, 40};
            b.text = cl[k];
            b.action = std::string("create:") + ct[k] + ":" + cs[k];
            b.color = th.button;
            s.ui.push_back(b);
        }
        { UiButton b; b.touch.id = "ctcam"; b.touch.rect = Rect{300 + 6 * 58, 560, 54, 40}; b.text = "CAM"; b.action = "create_cam"; b.color = th.accent; s.ui.push_back(b); }
        { UiButton b; b.touch.id = "ctlit"; b.touch.rect = Rect{300 + 7 * 58, 560, 54, 40}; b.text = "LIGHT"; b.action = "create_light"; b.color = parseColor("#FFD700"); s.ui.push_back(b); }
        { UiButton b; b.touch.id = "ctgrp"; b.touch.rect = Rect{300 + 8 * 58, 560, 54, 40}; b.text = "GRP"; b.action = "create_grp"; b.color = parseColor("#808080"); s.ui.push_back(b); }
        { UiButton b; b.touch.id = "ctbtn"; b.touch.rect = Rect{300 + 9 * 58, 560, 54, 40}; b.text = "BTN"; b.action = "create_btn"; b.color = parseColor("#2EC4B6"); s.ui.push_back(b); }
        { UiButton b; b.touch.id = "ctprt"; b.touch.rect = Rect{300, 604, 54, 40}; b.text = "PART"; b.action = "create:Particle2D:none"; b.color = parseColor("#FF69B4"); s.ui.push_back(b); }
    }

    if (in.showAssets) {
        addLbl("FsHdr", "ASSETS", 10, 384, 18, th.ink);
        { UiButton b; b.touch.id = "asclose"; b.touch.rect = Rect{252, 382, 40, 22}; b.text = "X"; b.action = "assets_open"; b.color = parseColor("#D62828"); s.ui.push_back(b); }
        { UiButton b; b.touch.id = "asu"; b.touch.rect = Rect{210, 382, 20, 22}; b.text = "^"; b.action = "assets_up"; b.color = th.button; s.ui.push_back(b); }
        { UiButton b; b.touch.id = "asd"; b.touch.rect = Rect{232, 382, 20, 22}; b.text = "v"; b.action = "assets_dn"; b.color = th.button; s.ui.push_back(b); }
        std::vector<std::string> imgs;
        for (const auto& it : FileBrowser::list(in.projectRoot + "/assets")) {
            if (it.isDir) continue;
            if (editorIsImageName(it.name)) imgs.push_back(it.name);
        }
        std::sort(imgs.begin(), imgs.end());
        const int AMAXROWS = 9;
        int amax = (int)imgs.size() > AMAXROWS ? (int)imgs.size() - AMAXROWS : 0;
        if (in.assetScroll < 0) in.assetScroll = 0;
        if (in.assetScroll > amax) in.assetScroll = amax;
        addLbl("FsPath", "assets/  (" + std::to_string((int)imgs.size()) + ")", 10, 406, 15, GO);
        if (imgs.empty()) addLbl("AsEmpty", "drop png/jpg into assets/", 12, 432, 14, th.ink);
        float ay = 428;
        const float ASTEP = 28;
        for (int i = in.assetScroll; i < (int)imgs.size() && i < in.assetScroll + AMAXROWS; ++i) {
            UiButton tb;
            tb.touch.id = "tb" + std::to_string(i);
            tb.touch.rect = Rect{8, ay, 284, ASTEP - 2};
            tb.text = "  " + imgs[i];
            tb.action = "tex_pick:" + imgs[i];
            tb.color = th.accent;
            s.ui.push_back(tb);
            ay += ASTEP;
        }
        addLbl("AsHint", "tap = assign / create sprite", 10, 668, 12, th.ink);
    } else if (in.showFiles) {
        addLbl("FsHdr", "FILES", 10, 384, 18, th.ink);
        { UiButton b; b.touch.id = "fsclose"; b.touch.rect = Rect{252, 382, 40, 22}; b.text = "X"; b.action = "files_open"; b.color = parseColor("#D62828"); s.ui.push_back(b); }
        { UiButton b; b.touch.id = "fsu"; b.touch.rect = Rect{210, 382, 20, 22}; b.text = "^"; b.action = "fscroll_up"; b.color = th.button; s.ui.push_back(b); }
        { UiButton b; b.touch.id = "fsd"; b.touch.rect = Rect{232, 382, 20, 22}; b.text = "v"; b.action = "fscroll_dn"; b.color = th.button; s.ui.push_back(b); }

        struct FsRow { std::string text; std::string action; std::string delAction; unsigned col; };
        std::vector<FsRow> frows;
        if (!in.fsPath.empty()) frows.push_back({ "..", "fs_up", "", th.button });
        std::string abs = in.projectRoot + "/" + in.fsPath;
        for (const auto& it : FileBrowser::list(abs)) {
            if (it.name.rfind("snap_", 0) == 0) continue;
            if (it.name == "save.vars") continue;
            FsRow r;
            r.text = (it.isDir ? "/ " : "  ") + it.name;
            r.action = it.isDir ? ("fs_enter:" + it.name) : ("fs_pick:" + in.fsPath + it.name);
            r.delAction = "fs_del:" + in.fsPath + it.name;
            r.col = it.isDir ? th.button : th.accent;
            frows.push_back(r);
        }
        const int FMAXROWS = 9;
        int fmax = (int)frows.size() > FMAXROWS ? (int)frows.size() - FMAXROWS : 0;
        if (in.fsScroll < 0) in.fsScroll = 0;
        if (in.fsScroll > fmax) in.fsScroll = fmax;

        std::string shown = in.fsPath.empty() ? std::string("res/") : ("res/" + in.fsPath);
        addLbl("FsPath", shown + "  (" + std::to_string((int)frows.size()) + ")", 10, 406, 15, GO);

        float fy = 428;
        const float STEP = 28;
        for (int i = in.fsScroll; i < (int)frows.size() && i < in.fsScroll + FMAXROWS; ++i) {
            UiButton fb;
            fb.touch.id = "fs" + std::to_string(i);
            fb.touch.rect = Rect{8, fy, 236, STEP - 2};
            fb.text = frows[i].text;
            fb.action = frows[i].action;
            fb.color = frows[i].col;
            s.ui.push_back(fb);

            if (!frows[i].delAction.empty()) {
                UiButton db;
                db.touch.id = "fd" + std::to_string(i);
                db.touch.rect = Rect{248, fy, 40, STEP - 2};
                db.text = "X";
                db.action = frows[i].delAction;
                db.color = parseColor("#D62828");
                s.ui.push_back(db);
            }
            fy += STEP;
        }
        addLbl("FsHint", "tap file = load/assign  |  X = delete", 10, 668, 12, th.ink);
    }

    // Prefab list: tap a row = insert prefab instance at scene center.
    if (in.showPrefabs) {
        const unsigned BG = 0x14141CFF;
        auto btn = [&](const std::string& id, float x, float y, float w, float h,
                       const std::string& txt, const std::string& action, unsigned col) {
            UiButton b; b.touch.id = id; b.touch.rect = Rect{x, y, w, h};
            b.text = txt; b.action = action; b.color = col; s.ui.push_back(b);
        };
        btn("__prbg",    360, 120, 560, 480, "", "", BG);
        btn("__prtitle", 380, 132, 520,  40, "ADD PREFAB (tap to insert)", "", BG);
        btn("pr_close",  830, 132,  70,  32, "X", "prefabs_close", parseColor("#D62828"));

        std::vector<std::string> files;
        std::string dir = in.projectRoot + "/prefabs";
        for (const auto& it : FileBrowser::list(dir)) {
            if (it.isDir) continue;
            if (it.name.size() >= 4 && it.name.compare(it.name.size() - 4, 4, ".prf") == 0)
                files.push_back(it.name);
        }
        std::sort(files.begin(), files.end());

        int maxScroll = (int)files.size() > 10 ? (int)files.size() - 10 : 0;
        if (in.prefabScroll < 0) in.prefabScroll = 0;
        if (in.prefabScroll > maxScroll) in.prefabScroll = maxScroll;

        { UiButton b; b.touch.id = "pru"; b.touch.rect = Rect{880, 180, 24, 22}; b.text = "^"; b.action = "prefabs_up"; b.color = th.button; s.ui.push_back(b); }
        { UiButton b; b.touch.id = "prd"; b.touch.rect = Rect{880, 208, 24, 22}; b.text = "v"; b.action = "prefabs_dn"; b.color = th.button; s.ui.push_back(b); }

        if (files.empty()) {
            auto l = std::make_unique<Label>();
            l->name = "PrEmpty"; l->text = "no .prf yet: select node -> PF SAVE";
            l->fontSize = 16; l->color = th.ink; l->position = Vec2{400, 220};
            s.root->addChild(std::move(l));
        }

        for (int i = in.prefabScroll; i < (int)files.size() && i < in.prefabScroll + 10; ++i) {
            float y = 180 + (float)(i - in.prefabScroll) * 36;
            btn("prf" + std::to_string(i), 400, y, 460, 32,
                "  " + files[i], "prefab_add:prefabs/" + files[i], th.accent);
        }
    }

    if (in.showSettings) {
        const unsigned BG = 0x14141CFF;
        auto btn = [&](const std::string& id, float x, float y, float w, float h,
                       const std::string& txt, const std::string& action, unsigned col) {
            UiButton b;
            b.touch.id = id;
            b.touch.rect = Rect{x, y, w, h};
            b.text = txt;
            b.action = action;
            b.color = col;
            s.ui.push_back(b);
        };
        btn("__stbg",    360,  80, 560, 560, "", "", BG);
        btn("__sttitle", 380,  92, 520,  56, "SETTINGS", "", BG);
        btn("st_close",  830,  96,  70,  32, "CLOSE", "settings_close", parseColor("#D62828"));
        const char* catName[5] = { "Fonts", "Sprites", "Videos", "Models", "Sounds" };
        const char* catKey[5]  = { "fonts", "sprites", "videos", "models", "sounds" };
        bool catReady[5]       = { true, true, false, false, false };
        for (int i = 0; i < 5; ++i) {
            float y = 170 + (float)i * 88;
            btn(std::string("__stcat") + std::to_string(i), 400, y, 200, 36, catName[i], "", BG);
            btn(std::string("st_imp") + std::to_string(i), 400, y + 40, 220, 40,
                std::string("Import ") + catName[i] + (catReady[i] ? "" : " (soon)"),
                std::string("import_category:") + catKey[i],
                catReady[i] ? th.accent : parseColor("#666666"));
        }
        btn("__sthint", 380, 600, 520, 28, "files are copied into assets/<category>/", "", BG);
    }

    return s;
}

} // namespace suka
