#pragma once

#include <string>
#include <map>
#include <vector>
#include <set>
#include <cmath>
#include <mutex>
#include <algorithm>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "Scene.hpp"

namespace suka {

struct Body {
    bool isStatic = false;
    bool isCircle = false;
    float mass = 1.0f;
    float vx = 0.0f, vy = 0.0f;
    float w = 0.0f;             // угловая скорость, рад/с
    bool gravity = true;
    float restitution = 0.0f;
    float friction = 0.4f;      // по умолчанию небольшое трение => качение/торможение
    bool onGround = false;
};

inline std::map<std::string, Body> g_bodies;
inline std::mutex g_bodiesMtx;
inline float g_gravity = 900.0f;
inline bool g_showBodies = false;
inline std::vector<std::pair<std::string, std::string>> g_collideEvents;
inline std::mutex g_collideMtx;

inline Body* bodyGet(const std::string& nm) {
    auto it = g_bodies.find(nm);
    return it == g_bodies.end() ? nullptr : &it->second;
}
inline void bodyAdd(const std::string& nm, bool isStatic, float mass) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    auto it = g_bodies.find(nm);
    if (it != g_bodies.end()) { it->second.isStatic = isStatic; if (isStatic) it->second.gravity = false; return; }
    Body b;
    b.isStatic = isStatic;
    b.mass = mass > 0.01f ? mass : 1.0f;
    if (isStatic) b.gravity = false;
    g_bodies[nm] = b;
}
inline void bodyRemove(const std::string& nm) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    g_bodies.erase(nm);
}
inline void pushCollideEvent(const std::string& a, const std::string& b) {
    std::lock_guard<std::mutex> lk(g_collideMtx);
    for (const auto& p : g_collideEvents) if (p.first == a && p.second == b) return;
    g_collideEvents.push_back(std::make_pair(a, b));
}

struct Ent {
    std::string nm;
    Node2D* n;
    Body* b;
    float cx, cy;          // центр
    float hw, hh;          // полуразмеры (для круга hw = hh = r)
    float cs, sn;          // cos/sin поворота
    float invM, invI;      // обратные масса и момент инерции
};

inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

inline void entInertia(Ent& e) {
    if (e.b->isStatic) { e.invM = 0.0f; e.invI = 0.0f; return; }
    e.invM = 1.0f / e.b->mass;
    float I;
    if (e.b->isCircle) I = 0.5f * e.b->mass * e.hw * e.hw;
    else I = e.b->mass * (4.0f * e.hw * e.hw + 4.0f * e.hh * e.hh) / 12.0f;
    e.invI = I > 1e-6f ? 1.0f / I : 0.0f;
}

// ==== SAT для двух повёрнутых боксов ====
inline float projRadius(const Ent& e, float ax, float ay) {
    float du = ax * e.cs + ay * e.sn;
    float dv = -ax * e.sn + ay * e.cs;
    return e.hw * std::fabs(du) + e.hh * std::fabs(dv);
}
inline float obbOverlap(const Ent& A, const Ent& B, float& nx, float& ny) {
    float best = 1e18f, bnx = 0, bny = 0;
    float dx = B.cx - A.cx, dy = B.cy - A.cy;
    const float axes[4][2] = { {A.cs, A.sn}, {-A.sn, A.cs}, {B.cs, B.sn}, {-B.sn, B.cs} };
    for (int k = 0; k < 4; ++k) {
        float ax = axes[k][0], ay = axes[k][1];
        float ov = projRadius(A, ax, ay) + projRadius(B, ax, ay) - std::fabs(dx * ax + dy * ay);
        if (ov <= 0.0f) return 0.0f;
        if (ov < best) {
            best = ov;
            float s = (dx * ax + dy * ay) >= 0.0f ? 1.0f : -1.0f;
            bnx = ax * s; bny = ay * s;
        }
    }
    nx = bnx; ny = bny;
    return best;
}

// ближайшая точка бокса e к мировой точке (px,py)
inline void boxClosest(const Ent& e, float px, float py, float& qx, float& qy, bool& inside) {
    float lx = (px - e.cx) * e.cs + (py - e.cy) * e.sn;
    float ly = -(px - e.cx) * e.sn + (py - e.cy) * e.cs;
    float clx = std::max(-e.hw, std::min(e.hw, lx));
    float cly = std::max(-e.hh, std::min(e.hh, ly));
    inside = (clx == lx && cly == ly);
    qx = e.cx + clx * e.cs - cly * e.sn;
    qy = e.cy + clx * e.sn + cly * e.cs;
}

struct Contact { float nx, ny, pen, px, py; };

inline bool makeContact(const Ent& A, const Ent& B, Contact& c) {
    bool circA = A.b->isCircle, circB = B.b->isCircle;
    if (circA && circB) {
        float dx = B.cx - A.cx, dy = B.cy - A.cy;
        float d = std::sqrt(dx * dx + dy * dy);
        float r = A.hw + B.hw;
        if (d >= r || d < 1e-6f) return false;
        c.nx = dx / d; c.ny = dy / d; c.pen = r - d;
        c.px = A.cx + c.nx * A.hw; c.py = A.cy + c.ny * A.hw;
        return true;
    }
    if (circA != circB) {
        const Ent& C = circA ? A : B;
        const Ent& X = circA ? B : A;
        float qx, qy; bool inside;
        boxClosest(X, C.cx, C.cy, qx, qy, inside);
        float dx = C.cx - qx, dy = C.cy - qy;
        float d = std::sqrt(dx * dx + dy * dy);
        if (inside) {
            // центр круга внутри бокса: выталкиваем по ближайшей грани
            float lx = (C.cx - X.cx) * X.cs + (C.cy - X.cy) * X.sn;
            float ly = -(C.cx - X.cx) * X.sn + (C.cy - X.cy) * X.cs;
            float px_ = X.hw - std::fabs(lx), py_ = X.hh - std::fabs(ly);
            float nxL, nyL;
            if (px_ < py_) { nxL = lx > 0 ? 1 : -1; nyL = 0; c.pen = px_ + C.hw; }
            else { nxL = 0; nyL = ly > 0 ? 1 : -1; c.pen = py_ + C.hw; }
            float wx = nxL * X.cs - nyL * X.sn, wy = nxL * X.sn + nyL * X.cs;
            c.nx = wx; c.ny = wy;
            c.px = C.cx - wx * C.hw; c.py = C.cy - wy * C.hw;
            if (!circA) { c.nx = -c.nx; c.ny = -c.ny; }   // нормаль всегда A->B
            return true;
        }
        if (d >= C.hw || d < 1e-6f) return false;
        float wx = dx / d, wy = dy / d;      // от бокса к кругу
        c.pen = C.hw - d;
        c.px = qx; c.py = qy;
        if (circA) { c.nx = -wx; c.ny = -wy; }  // A=круг, нормаль A->B = от круга к боксу
        else { c.nx = wx; c.ny = wy; }          // A=бокс, B=круг: нормаль A->B = от бокса к кругу
        return true;
    }
    // бокс-бокс
    float nx, ny;
    float pen = obbOverlap(A, B, nx, ny);
    if (pen <= 0.0f) return false;
    c.nx = nx; c.ny = ny; c.pen = pen;
    // точка контакта: самый «глубокий» угол A вдоль нормали
    float best = -1e18f, bpx = A.cx, bpy = A.cy;
    for (int k = 0; k < 4; ++k) {
        float sx = (k & 1) ? 1.0f : -1.0f;
        float sy = (k & 2) ? 1.0f : -1.0f;
        float px_ = A.cx + (sx * A.hw) * A.cs - (sy * A.hh) * A.sn;
        float py_ = A.cy + (sx * A.hw) * A.sn + (sy * A.hh) * A.cs;
        float d = px_ * nx + py_ * ny;
        if (d > best) { best = d; bpx = px_; bpy = py_; }
    }
    c.px = bpx; c.py = bpy;
    return true;
}

inline void physicsStepOnce(Scene& sc, float dt) {
    if (!sc.root) return;

    std::vector<Ent> ents;
    {
        std::lock_guard<std::mutex> lk(g_bodiesMtx);
        for (auto& kv : g_bodies) {
            Node* rn = sc.root->findNode(kv.first);
            Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;
            if (!n) continue;
            Ent e;
            e.nm = kv.first; e.n = n; e.b = &kv.second;
            e.cx = n->position.x; e.cy = n->position.y;
            e.b->isCircle = (std::string(n->shape) == "circle");
            float sx = std::fabs(n->scale.x), sy = std::fabs(n->scale.y);
            if (e.b->isCircle) { e.hw = e.hh = std::max(1.0f, n->w * sx * 0.5f); }
            else { e.hw = std::max(1.0f, n->w * sx * 0.5f); e.hh = std::max(1.0f, n->h * sy * 0.5f); }
            e.cs = std::cos(n->rotation); e.sn = std::sin(n->rotation);
            entInertia(e);
            ents.push_back(e);
        }
    }

    // Интегрирование
    for (auto& e : ents) {
        if (e.b->isStatic) continue;
        if (e.b->gravity) e.b->vy += g_gravity * dt;
        if (e.b->vy >  2000.0f) e.b->vy =  2000.0f;
        if (e.b->vy < -2000.0f) e.b->vy = -2000.0f;
        if (e.b->vx >  2000.0f) e.b->vx =  2000.0f;
        if (e.b->vx < -2000.0f) e.b->vx = -2000.0f;
        if (e.b->w  >  30.0f)  e.b->w  =  30.0f;
        if (e.b->w  < -30.0f)  e.b->w  = -30.0f;
        e.n->position.x += e.b->vx * dt;
        e.n->position.y += e.b->vy * dt;
        e.n->rotation   += e.b->w * dt;
        e.b->onGround = false;
    }

    // Контакты и импульсы
    for (size_t i = 0; i < ents.size(); ++i) {
        for (size_t j = i + 1; j < ents.size(); ++j) {
            Ent& A = ents[i];
            Ent& B = ents[j];
            if (A.b->isStatic && B.b->isStatic) continue;
            Contact c;
            if (!makeContact(A, B, c)) continue;
            pushCollideEvent(A.nm, B.nm);

            float rAx = c.px - A.cx, rAy = c.py - A.cy;
            float rBx = c.px - B.cx, rBy = c.py - B.cy;

            // скорости точек контакта
            float vAx = A.b->vx - A.b->w * rAy, vAy = A.b->vy + A.b->w * rAx;
            float vBx = B.b->vx - B.b->w * rBy, vBy = B.b->vy + B.b->w * rBx;
            float rvx = vBx - vAx, rvy = vBy - vAy;
            float vn = rvx * c.nx + rvy * c.ny;

            float e_ = std::max(A.b->restitution, B.b->restitution);
            if (vn > -40.0f) e_ = 0.0f;   // покой без микро-отскоков

            float rnA = cross2(rAx, rAy, c.nx, c.ny);
            float rnB = cross2(rBx, rBy, c.nx, c.ny);
            float kn = A.invM + B.invM + rnA * rnA * A.invI + rnB * rnB * B.invI;
            if (kn <= 1e-9f) continue;
            float jn = -(1.0f + e_) * vn / kn;
            if (jn < 0.0f) jn = 0.0f;

            float jx = c.nx * jn, jy = c.ny * jn;
            A.b->vx -= jx * A.invM; A.b->vy -= jy * A.invM; A.b->w -= cross2(rAx, rAy, jx, jy) * A.invI;
            B.b->vx += jx * B.invM; B.b->vy += jy * B.invM; B.b->w += cross2(rBx, rBy, jx, jy) * B.invI;

            // трение (даёт качение кругам и опрокидывание кубам)
            float tx = -c.ny, ty = c.nx;
            vAx = A.b->vx - A.b->w * rAy; vAy = A.b->vy + A.b->w * rAx;
            vBx = B.b->vx - B.b->w * rBy; vBy = B.b->vy + B.b->w * rBx;
            float vt = (vBx - vAx) * tx + (vBy - vAy) * ty;
            float rtA = cross2(rAx, rAy, tx, ty);
            float rtB = cross2(rBx, rBy, tx, ty);
            float kt = A.invM + B.invM + rtA * rtA * A.invI + rtB * rtB * B.invI;
            if (kt > 1e-9f) {
                float jt = -vt / kt;
                float mu = std::max(A.b->friction, B.b->friction);
                float maxF = mu * jn;
                if (jt >  maxF) jt =  maxF;
                if (jt < -maxF) jt = -maxF;
                float fx = tx * jt, fy = ty * jt;
                A.b->vx -= fx * A.invM; A.b->vy -= fy * A.invM; A.b->w -= cross2(rAx, rAy, fx, fy) * A.invI;
                B.b->vx += fx * B.invM; B.b->vy += fy * B.invM; B.b->w += cross2(rBx, rBy, fx, fy) * B.invI;
            }

            // позиционная коррекция (без неё тела тонут друг в друге)
            float slop = 0.5f, percent = 0.8f;
            float corr = std::max(c.pen - slop, 0.0f) * percent / (A.invM + B.invM + 1e-9f);
            A.cx -= c.nx * corr * A.invM; A.cy -= c.ny * corr * A.invM;
            B.cx += c.nx * corr * B.invM; B.cy += c.ny * corr * B.invM;
            if (!A.b->isStatic) { A.n->position.x = A.cx; A.n->position.y = A.cy; }
            if (!B.b->isStatic) { B.n->position.x = B.cx; B.n->position.y = B.cy; }

            if (B.b->onGround == false && c.ny < -0.5f) B.b->onGround = true;
            if (A.b->onGround == false && c.ny >  0.5f) A.b->onGround = true;
        }
    }
}

inline void physicsUpdate(Scene& sc, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;
    const int SUB = 3;
    for (int s = 0; s < SUB; ++s) physicsStepOnce(sc, dt / SUB);
}

// ==== Отладочные хитбоксы: sx = OX + wx*S ====
inline void emitBodiesDebug(Scene& sc, std::string& out, float S, float OX, float OY) {
    if (!sc.root) return;
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    for (auto& kv : g_bodies) {
        Node* rn = sc.root->findNode(kv.first);
        Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;
        if (!n) continue;
        bool circ = kv.second.isCircle;
        const char* col = kv.second.isStatic ? "#FF5555" : "#33FF99";
        float cx = n->position.x, cy = n->position.y;
        float hw = std::max(1.0f, n->w * std::fabs(n->scale.x) * 0.5f);
        float hh = std::max(1.0f, n->h * std::fabs(n->scale.y) * 0.5f);
        if (circ) hh = hw;
        float cs = std::cos(n->rotation), sn = std::sin(n->rotation);
        float th = 2.0f / (S > 0.01f ? S : 1.0f);   // толщина линии ~2 px экрана
        if (th < 0.5f) th = 0.5f;
        if (circ) {
            const int SEG = 14;
            for (int k = 0; k < SEG; ++k) {
                float a0 = k * 6.28318f / SEG, a1 = (k + 1) * 6.28318f / SEG;
                float x0 = cx + std::cos(a0) * hw, y0 = cy + std::sin(a0) * hw;
                float x1 = cx + std::cos(a1) * hw, y1 = cy + std::sin(a1) * hw;
                float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
                float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + th;
                float ang = std::atan2(y1 - y0, x1 - x0) * 57.2957795f;
                float sx = OX + mx * S, sy = OY + my * S;
                out += "DRAW rect|" + std::to_string((int)(sx - len * S / 2)) + "|" + std::to_string((int)(sy - th * S / 2)) + "|" + std::to_string((int)(len * S)) + "|" + std::to_string((int)(th * S)) + "|" + col + "|" + std::to_string((int)ang) + "\n";
            }
        } else {
            float px[4], py[4];
            for (int k = 0; k < 4; ++k) {
                float sx_ = (k & 1) ? hw : -hw;
                float sy_ = (k & 2) ? hh : -hh;
                px[k] = cx + sx_ * cs - sy_ * sn;
                py[k] = cy + sx_ * sn + sy_ * cs;
            }
            const int order[4][2] = { {0,1}, {1,3}, {3,2}, {2,0} };
            for (int k = 0; k < 4; ++k) {
                float x0 = px[order[k][0]], y0 = py[order[k][0]];
                float x1 = px[order[k][1]], y1 = py[order[k][1]];
                float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
                float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + th;
                float ang = std::atan2(y1 - y0, x1 - x0) * 57.2957795f;
                float sx = OX + mx * S, sy = OY + my * S;
                out += "DRAW rect|" + std::to_string((int)(sx - len * S / 2)) + "|" + std::to_string((int)(sy - th * S / 2)) + "|" + std::to_string((int)(len * S)) + "|" + std::to_string((int)(th * S)) + "|" + col + "|" + std::to_string((int)ang) + "\n";
            }
        }
    }
}

// ================= Lua-биндинги =================
static int l_add_rigidbody(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    float mass = lua_gettop(L) >= 2 && lua_isnumber(L, 2) ? (float)lua_tonumber(L, 2) : 1.0f;
    if (nm) bodyAdd(nm, false, mass);
    return 0;
}
static int l_add_staticbody(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    if (nm) bodyAdd(nm, true, 1.0f);
    return 0;
}
static int l_remove_body(lua_State* L) { const char* nm = luaL_checkstring(L, 1); if (nm) bodyRemove(nm); return 0; }
static int l_set_velocity(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) { b->vx = (float)luaL_checknumber(L, 2); b->vy = (float)luaL_checknumber(L, 3); } return 0; }
static int l_add_velocity(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) { b->vx += (float)luaL_checknumber(L, 2); b->vy += (float)luaL_checknumber(L, 3); } return 0; }
static int l_get_velocity_x(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->vx : 0.0); return 1; }
static int l_get_velocity_y(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->vy : 0.0); return 1; }
static int l_set_spin(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->w = (float)luaL_checknumber(L, 2); return 0; }
static int l_get_spin(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->w : 0.0); return 1; }
static int l_set_gravity(lua_State* L) { g_gravity = (float)luaL_checknumber(L, 1); return 0; }
static int l_get_gravity(lua_State* L) { lua_pushnumber(L, g_gravity); return 1; }
static int l_set_bounce(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->restitution = (float)luaL_checknumber(L, 2); return 0; }
static int l_set_friction(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->friction = (float)luaL_checknumber(L, 2); return 0; }
static int l_set_mass(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) { float m = (float)luaL_checknumber(L, 2); b->mass = m > 0.01f ? m : 1.0f; } return 0; }
static int l_no_gravity(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->gravity = lua_gettop(L) >= 2 ? lua_toboolean(L, 2) : false; return 0; }
static int l_is_on_ground(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushboolean(L, b ? b->onGround : false); return 1; }
static int l_is_body(lua_State* L) { lua_pushboolean(L, bodyGet(luaL_checkstring(L, 1)) != nullptr); return 1; }
static int l_show_hitboxes(lua_State* L) { g_showBodies = lua_gettop(L) >= 1 ? lua_toboolean(L, 1) : true; return 0; }

inline void registerPhysicsLuaApi(lua_State* L) {
    if (!L) return;
    lua_register(L, "add_rigidbody", l_add_rigidbody);
    lua_register(L, "add_staticbody", l_add_staticbody);
    lua_register(L, "remove_body", l_remove_body);
    lua_register(L, "set_velocity", l_set_velocity);
    lua_register(L, "add_velocity", l_add_velocity);
    lua_register(L, "get_velocity_x", l_get_velocity_x);
    lua_register(L, "get_velocity_y", l_get_velocity_y);
    lua_register(L, "set_spin", l_set_spin);
    lua_register(L, "get_spin", l_get_spin);
    lua_register(L, "set_gravity", l_set_gravity);
    lua_register(L, "get_gravity", l_get_gravity);
    lua_register(L, "set_bounce", l_set_bounce);
    lua_register(L, "set_friction", l_set_friction);
    lua_register(L, "set_mass", l_set_mass);
    lua_register(L, "no_gravity", l_no_gravity);
    lua_register(L, "is_on_ground", l_is_on_ground);
    lua_register(L, "is_body", l_is_body);
    lua_register(L, "show_hitboxes", l_show_hitboxes);
}

} // namespace suka
