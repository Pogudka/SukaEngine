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
    float restitution = 0.0f;
    float friction = 0.0f;
    bool onGround = false;
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

inline void physicsStepOnce(Scene& sc, float dt) {
    if (!sc.root) return;

    struct Ent { std::string nm; Node2D* n; Body* b; float hw, hh; };
    std::vector<Ent> ents;

    {
        std::lock_guard<std::mutex> lk(g_bodiesMtx);
        for (auto& kv : g_bodies) {
            Node* rn = sc.root->findNode(kv.first);
            Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;
            if (!n) continue;
            Ent e;
            e.nm = kv.first; e.n = n; e.b = &kv.second;
            e.hw = std::fabs(n->w * n->scale.x) * 0.5f;
            e.hh = std::fabs(n->h * n->scale.y) * 0.5f;
            if (e.hw < 1.0f) e.hw = 1.0f;
            if (e.hh < 1.0f) e.hh = 1.0f;
            ents.push_back(e);
        }
    }

    // Интегрирование с ограничением скорости (меньше туннелирования)
    for (auto& e : ents) {
        if (e.b->isStatic) continue;
        if (e.b->gravity) e.b->vy += g_gravity * dt;
        if (e.b->vy >  1600.0f) e.b->vy =  1600.0f;
        if (e.b->vy < -1600.0f) e.b->vy = -1600.0f;
        if (e.b->vx >  2000.0f) e.b->vx =  2000.0f;
        if (e.b->vx < -2000.0f) e.b->vx = -2000.0f;
        e.n->position.x += e.b->vx * dt;
        e.n->position.y += e.b->vy * dt;
        e.b->onGround = false;
    }

    // Попарные AABB-коллизии с полным выталкиванием
    for (size_t i = 0; i < ents.size(); ++i) {
        for (size_t j = i + 1; j < ents.size(); ++j) {
            Ent& A = ents[i];
            Ent& B = ents[j];
            if (A.b->isStatic && B.b->isStatic) continue;

            float dx = B.n->position.x - A.n->position.x;
            float dy = B.n->position.y - A.n->position.y;
            float ox = (A.hw + B.hw) - std::fabs(dx);
            float oy = (A.hh + B.hh) - std::fabs(dy);
            if (ox <= 0.0f || oy <= 0.0f) continue;

            pushCollideEvent(A.nm, B.nm);

            float rest = std::max(A.b->restitution, B.b->restitution);

            if (ox < oy) {
                float s = dx > 0.0f ? 1.0f : -1.0f;
                if (A.b->isStatic) { B.n->position.x += ox * s; B.b->vx = -B.b->vx * rest; }
                else if (B.b->isStatic) { A.n->position.x -= ox * s; A.b->vx = -A.b->vx * rest; }
                else {
                    float wa = B.b->mass / (A.b->mass + B.b->mass);
                    float wb = 1.0f - wa;
                    A.n->position.x -= ox * s * wa;
                    B.n->position.x += ox * s * wb;
                    float va = A.b->vx, vb = B.b->vx;
                    A.b->vx = -va * rest; B.b->vx = -vb * rest;
                }
            } else {
                float s = dy > 0.0f ? 1.0f : -1.0f;
                if (A.b->isStatic) {
                    B.n->position.y += oy * s;
                    if (s < 0.0f) {
                        B.b->onGround = true;
                        if (B.b->friction > 0.0f) B.b->vx *= std::max(0.0f, 1.0f - B.b->friction * dt * 10.0f);
                    }
                    B.b->vy = -B.b->vy * rest;
                    if (std::fabs(B.b->vy) < 40.0f) B.b->vy = 0.0f;
                } else if (B.b->isStatic) {
                    A.n->position.y -= oy * s;
                    if (s > 0.0f) {
                        A.b->onGround = true;
                        if (A.b->friction > 0.0f) A.b->vx *= std::max(0.0f, 1.0f - A.b->friction * dt * 10.0f);
                    }
                    A.b->vy = -A.b->vy * rest;
                    if (std::fabs(A.b->vy) < 40.0f) A.b->vy = 0.0f;
                } else {
                    float wa = B.b->mass / (A.b->mass + B.b->mass);
                    float wb = 1.0f - wa;
                    A.n->position.y -= oy * s * wa;
                    B.n->position.y += oy * s * wb;
                    float va = A.b->vy, vb = B.b->vy;
                    A.b->vy = -va * rest; B.b->vy = -vb * rest;
                }
            }
        }
    }
}

inline void physicsUpdate(Scene& sc, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;
    // Два подшага: меньше проваливаний на быстрых падениях.
    physicsStepOnce(sc, dt * 0.5f);
    physicsStepOnce(sc, dt * 0.5f);
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
