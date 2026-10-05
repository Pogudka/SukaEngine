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
    float mass = 1.0f;
    float vx = 0.0f, vy = 0.0f;
    bool gravity = true;
    float restitution = 0.0f;   // 0..1 упругость
    float friction = 0.0f;      // гашение касательной скорости в контакте
    bool onGround = false;
    float spin = 0.0f;          // угловая скорость для качения кругов
};

inline std::map<std::string, Body> g_bodies;
inline std::mutex g_bodiesMtx;
inline float g_gravity = 900.0f;
inline std::vector<std::pair<std::string, std::string>> g_collideEvents;
inline std::mutex g_collideMtx;

inline Body* bodyGet(const std::string& nm) {
    auto it = g_bodies.find(nm);
    return it == g_bodies.end() ? nullptr : &it->second;
}
inline void bodyAdd(const std::string& nm, bool isStatic, float mass) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
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

// ==== Ориентированный бокс ====
struct Ent {
    std::string nm;
    Node2D* n;
    Body* b;
    float cx, cy, hw, hh, rot, cs, sn;
};

// Радиус проекции бокса на ось (ax,ay)
inline float projRadius(const Ent& e, float ax, float ay) {
    // оси бокса: u=(cs,sn), v=(-sn,cs)
    float du = ax * e.cs + ay * e.sn;
    float dv = -ax * e.sn + ay * e.cs;
    return e.hw * std::fabs(du) + e.hh * std::fabs(dv);
}

// SAT для двух повёрнутых прямоугольников. Возвращает глубину проникновения
// и нормаль n (направлена от A к B); 0 если нет пересечения.
inline float obbOverlap(const Ent& A, const Ent& B, float& nx, float& ny) {
    float best = 1e18f;
    float bnx = 0, bny = 0;
    float dx = B.cx - A.cx, dy = B.cy - A.cy;

    const float axes[4][2] = {
        { A.cs,  A.sn },
        { -A.sn, A.cs },
        { B.cs,  B.sn },
        { -B.sn, B.cs }
    };
    for (int k = 0; k < 4; ++k) {
        float ax = axes[k][0], ay = axes[k][1];
        float ra = projRadius(A, ax, ay);
        float rb = projRadius(B, ax, ay);
        float d = std::fabs(dx * ax + dy * ay);
        float ov = ra + rb - d;
        if (ov <= 0.0f) return 0.0f;              // разделяющая ось найдена
        if (ov < best) {
            best = ov;
            float s = (dx * ax + dy * ay) >= 0.0f ? 1.0f : -1.0f;
            bnx = ax * s; bny = ay * s;            // нормаль от A к B
        }
    }
    nx = bnx; ny = bny;
    return best;
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
            e.hw = std::fabs(n->w * n->scale.x) * 0.5f;
            e.hh = std::fabs(n->h * n->scale.y) * 0.5f;
            if (e.hw < 1.0f) e.hw = 1.0f;
            if (e.hh < 1.0f) e.hh = 1.0f;
            e.rot = n->rotation;
            e.cs = std::cos(e.rot); e.sn = std::sin(e.rot);
            ents.push_back(e);
        }
    }

    // Интегрирование + качение кругов
    for (auto& e : ents) {
        if (e.b->isStatic) continue;
        if (e.b->gravity) e.b->vy += g_gravity * dt;
        if (e.b->vy >  1600.0f) e.b->vy =  1600.0f;
        if (e.b->vy < -1600.0f) e.b->vy = -1600.0f;
        if (e.b->vx >  2000.0f) e.b->vx =  2000.0f;
        if (e.b->vx < -2000.0f) e.b->vx = -2000.0f;
        e.n->position.x += e.b->vx * dt;
        e.n->position.y += e.b->vy * dt;
        if (e.b->onGround && e.b->spin != 0.0f) {
            e.n->rotation += e.b->spin * dt;
        } else {
            e.b->spin *= 0.9f;
        }
        e.b->onGround = false;
    }

    // Попарные OBB-коллизии
    for (size_t i = 0; i < ents.size(); ++i) {
        for (size_t j = i + 1; j < ents.size(); ++j) {
            Ent& A = ents[i];
            Ent& B = ents[j];
            if (A.b->isStatic && B.b->isStatic) continue;

            float nx = 0, ny = 0;
            float pen = obbOverlap(A, B, nx, ny);
            if (pen <= 0.0f) continue;

            pushCollideEvent(A.nm, B.nm);

            float rest = std::max(A.b->restitution, B.b->restitution);
            float fric = std::max(A.b->friction, B.b->friction);

            auto resolve = [&](Ent& D, float dnx, float dny, float share) {
                // выталкивание по нормали контакта
                D.n->position.x += dnx * pen * share;
                D.n->position.y += dny * pen * share;
                D.cx += dnx * pen * share;
                D.cy += dny * pen * share;
                // отражение нормальной составляющей скорости
                float vn = D.b->vx * dnx + D.b->vy * dny;
                if (vn < 0.0f) {
                    D.b->vx -= (1.0f + rest) * vn * dnx;
                    D.b->vy -= (1.0f + rest) * vn * dny;
                    float vn2 = D.b->vx * dnx + D.b->vy * dny;
                    if (std::fabs(vn2) < 30.0f) { D.b->vx -= vn2 * dnx; D.b->vy -= vn2 * dny; }
                }
                // касательная: скольжение вдоль поверхности + трение
                float tx = -dny, ty = dnx;
                float vt = D.b->vx * tx + D.b->vy * ty;
                if (fric > 0.0f) {
                    float f = std::min(1.0f, fric * dt * 10.0f);
                    D.b->vx -= tx * vt * f;
                    D.b->vy -= ty * vt * f;
                    vt *= (1.0f - f);
                }
                if (dny < -0.5f) D.b->onGround = true;   // нормаль вверх -> стоим
                if (std::string(D.n->shape) == "circle") {
                    float r = D.hw > 1.0f ? D.hw : 1.0f;
                    D.b->spin = vt / r;                  // качение
                }
            };

            if (A.b->isStatic) {
                resolve(B, nx, ny, 1.0f);
            } else if (B.b->isStatic) {
                resolve(A, -nx, -ny, 1.0f);
            } else {
                float wa = B.b->mass / (A.b->mass + B.b->mass);
                float wb = 1.0f - wa;
                resolve(A, -nx, -ny, wa);
                resolve(B,  nx,  ny, wb);
            }
        }
    }
}

inline void physicsUpdate(Scene& sc, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;
    // Три подшага: стабильнее на наклонных и быстрых падениях.
    const int SUB = 3;
    for (int s = 0; s < SUB; ++s) physicsStepOnce(sc, dt / SUB);
}

// ================= Lua-биндинги физики =================
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
static int l_remove_body(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    if (nm) bodyRemove(nm);
    return 0;
}
static int l_set_velocity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (b) { b->vx = (float)luaL_checknumber(L, 2); b->vy = (float)luaL_checknumber(L, 3); }
    return 0;
}
static int l_add_velocity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (b) { b->vx += (float)luaL_checknumber(L, 2); b->vy += (float)luaL_checknumber(L, 3); }
    return 0;
}
static int l_get_velocity_x(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->vx : 0.0); return 1; }
static int l_get_velocity_y(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->vy : 0.0); return 1; }
static int l_set_gravity(lua_State* L) { g_gravity = (float)luaL_checknumber(L, 1); return 0; }
static int l_get_gravity(lua_State* L) { lua_pushnumber(L, g_gravity); return 1; }
static int l_set_bounce(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->restitution = (float)luaL_checknumber(L, 2); return 0; }
static int l_set_friction(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->friction = (float)luaL_checknumber(L, 2); return 0; }
static int l_set_mass(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) { float m = (float)luaL_checknumber(L, 2); b->mass = m > 0.01f ? m : 1.0f; } return 0; }
static int l_no_gravity(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->gravity = lua_gettop(L) >= 2 ? lua_toboolean(L, 2) : false; return 0; }
static int l_is_on_ground(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushboolean(L, b ? b->onGround : false); return 1; }
static int l_is_body(lua_State* L) { lua_pushboolean(L, bodyGet(luaL_checkstring(L, 1)) != nullptr); return 1; }

inline void registerPhysicsLuaApi(lua_State* L) {
    if (!L) return;
    lua_register(L, "add_rigidbody", l_add_rigidbody);
    lua_register(L, "add_staticbody", l_add_staticbody);
    lua_register(L, "remove_body", l_remove_body);
    lua_register(L, "set_velocity", l_set_velocity);
    lua_register(L, "add_velocity", l_add_velocity);
    lua_register(L, "get_velocity_x", l_get_velocity_x);
    lua_register(L, "get_velocity_y", l_get_velocity_y);
    lua_register(L, "set_gravity", l_set_gravity);
    lua_register(L, "get_gravity", l_get_gravity);
    lua_register(L, "set_bounce", l_set_bounce);
    lua_register(L, "set_friction", l_set_friction);
    lua_register(L, "set_mass", l_set_mass);
    lua_register(L, "no_gravity", l_no_gravity);
    lua_register(L, "is_on_ground", l_is_on_ground);
    lua_register(L, "is_body", l_is_body);
}

} // namespace suka
