#pragma once

#include <string>
#include <map>
#include <vector>
#include <set>
#include <cmath>
#include <mutex>
#include <memory>
#include <algorithm>
#include <utility>
#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <box2d/box2d.h>

#include "Scene.hpp"

namespace suka {

// Масштаб: 100 пикселей = 1 метр (Box2D любит метры).
static const float PPM = 100.0f;
static const float INV_PPM = 1.0f / PPM;

struct Body {
    bool isStatic = false;
    bool isCircle = false;
    float mass = 1.0f;
    float vx = 0.0f, vy = 0.0f;   // желаемая/последняя скорость, px/s
    float w = 0.0f;               // угловая, рад/с
    bool gravity = true;
    bool hasGravityFlag = true;
    float restitution = 0.0f;
    float friction = 0.4f;
    bool onGround = false;
    bool pendingVel = false;      // применить vx/vy при создании тела
    b2Body* bb = nullptr;
};

inline std::map<std::string, Body> g_bodies;
inline std::mutex g_bodiesMtx;
inline float g_gravity = 900.0f;                       // px/s^2
inline bool g_showBodies = false;
inline std::vector<std::pair<std::string, std::string>> g_collideEvents;
inline std::mutex g_collideMtx;
inline std::unique_ptr<b2World> g_world;
inline Scene* g_physScene = nullptr;

// ---- слушатель контактов: on_collide ----
class SukaContactListener : public b2ContactListener {
public:
    void BeginContact(b2Contact* c) override {
        b2Body* a = c->GetFixtureA()->GetBody();
        b2Body* b = c->GetFixtureB()->GetBody();
        // Box2D v2.4.1: GetUserData() returns b2BodyUserData with uintptr_t pointer
        void* uda = reinterpret_cast<void*>(a->GetUserData().pointer);
        void* udb = reinterpret_cast<void*>(b->GetUserData().pointer);
        if (uda && udb) {
            const char* na = reinterpret_cast<const char*>(uda);
            const char* nb = reinterpret_cast<const char*>(udb);
            std::lock_guard<std::mutex> lk(g_collideMtx);
            g_collideEvents.push_back(std::make_pair(std::string(na), std::string(nb)));
        }
    }
};
inline SukaContactListener g_listener;

inline Body* bodyGet(const std::string& nm) {
    auto it = g_bodies.find(nm);
    return it == g_bodies.end() ? nullptr : &it->second;
}
inline void bodyAdd(const std::string& nm, bool isStatic, float mass) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    auto it = g_bodies.find(nm);
    if (it != g_bodies.end()) { it->second.isStatic = isStatic; return; }
    Body b;
    b.isStatic = isStatic;
    b.mass = mass > 0.01f ? mass : 1.0f;
    g_bodies[nm] = b;
}
inline void bodyRemove(const std::string& nm) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    auto it = g_bodies.find(nm);
    if (it == g_bodies.end()) return;
    if (it->second.bb && g_world) g_world->DestroyBody(it->second.bb);
    g_bodies.erase(it);
}
inline void pushCollideEvent(const std::string& a, const std::string& b) {
    std::lock_guard<std::mutex> lk(g_collideMtx);
    g_collideEvents.push_back(std::make_pair(a, b));
}

inline void ensureWorld() {
    if (!g_world) {
        g_world = std::make_unique<b2World>(b2Vec2(0.0f, g_gravity * INV_PPM));
        g_world->SetContactListener(&g_listener);
        g_world->SetAllowSleeping(true);
    }
}

// Полностью сбрасывает мир (вызывается автоматически при смене сцены).
inline void physicsReset() {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    for (auto& kv : g_bodies) kv.second.bb = nullptr;
    g_world.reset();
    g_physScene = nullptr;
    std::lock_guard<std::mutex> lk2(g_collideMtx);
    g_collideEvents.clear();
}

// Создание b2-тела по ноде
inline void createB2Body(const std::string& nm, Body& B, Node2D* n) {
    ensureWorld();
    b2BodyDef bd;
    bd.type = B.isStatic ? b2_staticBody : b2_dynamicBody;
    bd.position.Set(n->position.x * INV_PPM, n->position.y * INV_PPM);
    bd.angle = n->rotation;
    bd.allowSleep = true;
    bd.awake = true;
    b2Body* body = g_world->CreateBody(&bd);
    
    // Box2D v2.4.1: SetUserData takes b2BodyUserData structure
    b2BodyUserData userData;
    userData.pointer = reinterpret_cast<uintptr_t>(const_cast<char*>(nm.c_str()));
    body->SetUserData(userData);

    std::string shape = n->shape;
    B.isCircle = (shape == "circle");
    float hw = std::max(1.0f, n->w * std::fabs(n->scale.x)) * 0.5f * INV_PPM;
    float hh = std::max(1.0f, n->h * std::fabs(n->scale.y)) * 0.5f * INV_PPM;

    b2FixtureDef fd;
    fd.friction = B.friction;
    fd.restitution = B.restitution;
    fd.density = 1.0f;

    if (B.isCircle) {
        b2CircleShape cs;
        cs.m_radius = hw;
        fd.shape = &cs;
        body->CreateFixture(&fd);
    } else if (shape == "diamond" || shape == "triangle") {
        b2Vec2 vs[4];
        int cnt = 0;
        if (shape == "diamond") {
            vs[0].Set(0, -hh); vs[1].Set(hw, 0); vs[2].Set(0, hh); vs[3].Set(-hw, 0); cnt = 4;
        } else {
            vs[0].Set(0, -hh); vs[1].Set(hw, hh); vs[2].Set(-hw, hh); cnt = 3;
        }
        b2PolygonShape ps;
        ps.Set(vs, cnt);
        fd.shape = &ps;
        body->CreateFixture(&fd);
    } else {
        b2PolygonShape ps;
        ps.SetAsBox(hw, hh);
        fd.shape = &ps;
        body->CreateFixture(&fd);
    }

    if (!B.isStatic) {
        b2MassData md;
        body->GetMassData(&md);
        md.mass = B.mass;
        md.I = B.isCircle ? (0.5f * B.mass * hw * hw)
                          : (B.mass * (4.0f * hw * hw + 4.0f * hh * hh) / 12.0f);
        if (md.I < 1e-6f) md.I = 1e-6f;
        body->SetMassData(&md);
        if (B.pendingVel) {
            body->SetLinearVelocity(b2Vec2(B.vx * INV_PPM, B.vy * INV_PPM));
            body->SetAngularVelocity(B.w);
            B.pendingVel = false;
        }
    }
    body->SetGravityScale(B.gravity ? 1.0f : 0.0f);
    B.bb = body;
}

inline void physicsUpdate(Scene& sc, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;
    if (!sc.root) return;

    // Смена сцены -> полный сброс мира
    if (g_physScene != &sc) physicsReset();
    g_physScene = &sc;

    ensureWorld();
    g_world->SetGravity(b2Vec2(0.0f, g_gravity * INV_PPM));

    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lk(g_bodiesMtx);
        for (auto& kv : g_bodies) names.push_back(kv.first);
    }

    // Синхронизация записей с нодами и телами
    for (auto& nm : names) {
        Body* Bp = nullptr;
        std::string* stableName = nullptr;
        { 
            std::lock_guard<std::mutex> lk(g_bodiesMtx); 
            auto it = g_bodies.find(nm);
            if (it != g_bodies.end()) {
                Bp = &it->second;
                stableName = const_cast<std::string*>(&it->first); // stable pointer to map key
            }
        }
        if (!Bp || !stableName) continue;
        Body& B = *Bp;
        
        Node* rn = sc.root->findNode(nm);
        Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;
        if (!n) {
            if (B.bb && g_world) { g_world->DestroyBody(B.bb); }
            B.bb = nullptr;
            continue;
        }
        B.isCircle = (std::string(n->shape) == "circle");
        if (!B.bb) createB2Body(*stableName, B, n);
        if (!B.bb) continue;

        // живые правки параметров
        B.bb->SetGravityScale(B.gravity ? 1.0f : 0.0f);
        for (b2Fixture* f = B.bb->GetFixtureList(); f; f = f->GetNext()) {
            f->SetFriction(B.friction);
            f->SetRestitution(B.restitution);
        }
        if (B.isStatic) {
            // статика следует за позицией ноды (редактор/Lua)
            B.bb->SetTransform(b2Vec2(n->position.x * INV_PPM, n->position.y * INV_PPM), n->rotation);
        }
    }

    // Шаг мира
    const int SUB = 2;
    for (int s = 0; s < SUB; ++s) g_world->Step(dt / SUB, 8, 3);

    // Чтение результатов обратно в ноды + onGround + скорости для Lua
    for (auto& nm : names) {
        Body* Bp;
        { std::lock_guard<std::mutex> lk(g_bodiesMtx); Bp = bodyGet(nm); }
        if (!Bp || !Bp->bb) continue;
        Body& B = *Bp;
        b2Body* bb = Bp->bb;
        if (!B.isStatic) {
            b2Vec2 p = bb->GetPosition();
            Node* rn = sc.root->findNode(nm);
            Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;
            if (n) {
                n->position.x = p.x * PPM;
                n->position.y = p.y * PPM;
                n->rotation = bb->GetAngle();
            }
            b2Vec2 v = bb->GetLinearVelocity();
            B.vx = v.x * PPM; B.vy = v.y * PPM;
            B.w = bb->GetAngularVelocity();
        }
        // земля: любой касающийся контакт с точкой ниже центра
        B.onGround = false;
        for (b2ContactEdge* ce = bb->GetContactList(); ce; ce = ce->next) {
            b2Contact* ct = ce->contact;
            if (!ct->IsTouching()) continue;
            const b2Manifold* manifold = ct->GetManifold();
            if (!manifold) continue;
            b2WorldManifold wm;
            ct->GetWorldManifold(&wm);
            for (int k = 0; k < manifold->pointCount; ++k) {
                if (wm.points[k].y > bb->GetPosition().y + 0.01f) { B.onGround = true; break; }
            }
            if (B.onGround) break;
        }
    }
}

// ==== Отладочные хитбоксы: реальные формы Box2D ====
inline void emitBodiesDebug(Scene& sc, std::string& out, float S, float OX, float OY) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    for (auto& kv : g_bodies) {
        Body& B = kv.second;
        if (!B.bb) continue;
        const char* col = B.isStatic ? "#FF5555" : "#33FF99";
        b2Transform xf = B.bb->GetTransform();
        float th = 2.0f / (S > 0.01f ? S : 1.0f);
        if (th < 0.5f) th = 0.5f;

        auto seg = [&](float x0, float y0, float x1, float y1) {
            float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
            float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + th;
            float ang = std::atan2(y1 - y0, x1 - x0) * 57.2957795f;
            float sx = OX + mx * S, sy = OY + my * S;
            out += "DRAW rect|" + std::to_string((int)(sx - len * S / 2)) + "|" + std::to_string((int)(sy - th * S / 2)) + "|" + std::to_string((int)(len * S)) + "|" + std::to_string((int)(th * S)) + "|" + col + "|" + std::to_string((int)ang) + "\n";
        };

        for (b2Fixture* f = B.bb->GetFixtureList(); f; f = f->GetNext()) {
            b2Shape* sh = f->GetShape();
            if (sh->GetType() == b2Shape::e_circle) {
                b2CircleShape* cs = (b2CircleShape*)sh;
                float r = cs->m_radius * PPM;
                float cxp = xf.p.x * PPM, cyp = xf.p.y * PPM;
                const int SEG = 14;
                float px0 = cxp + r, py0 = cyp;
                for (int k = 1; k <= SEG; ++k) {
                    float a = k * 6.28318f / SEG;
                    float px1 = cxp + std::cos(a) * r, py1 = cyp + std::sin(a) * r;
                    seg(px0, py0, px1, py1);
                    px0 = px1; py0 = py1;
                }
            } else if (sh->GetType() == b2Shape::e_polygon) {
                b2PolygonShape* ps = (b2PolygonShape*)sh;
                int cnt = ps->m_count;
                for (int k = 0; k < cnt; ++k) {
                    b2Vec2 v0 = b2Mul(xf, ps->m_vertices[k]);
                    b2Vec2 v1 = b2Mul(xf, ps->m_vertices[(k + 1) % cnt]);
                    seg(v0.x * PPM, v0.y * PPM, v1.x * PPM, v1.y * PPM);
                }
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
static int l_add_staticbody(lua_State* L) { const char* nm = luaL_checkstring(L, 1); if (nm) bodyAdd(nm, true, 1.0f); return 0; }
static int l_remove_body(lua_State* L) { const char* nm = luaL_checkstring(L, 1); if (nm) bodyRemove(nm); return 0; }
static int l_set_velocity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;
    float vx = (float)luaL_checknumber(L, 2), vy = (float)luaL_checknumber(L, 3);
    b->vx = vx; b->vy = vy;
    if (b->bb) b->bb->SetLinearVelocity(b2Vec2(vx * INV_PPM, vy * INV_PPM));
    else b->pendingVel = true;
    return 0;
}
static int l_add_velocity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;
    b->vx += (float)luaL_checknumber(L, 2); b->vy += (float)luaL_checknumber(L, 3);
    if (b->bb) b->bb->SetLinearVelocity(b2Vec2(b->vx * INV_PPM, b->vy * INV_PPM));
    else b->pendingVel = true;
    return 0;
}
static int l_get_velocity_x(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->vx : 0.0); return 1; }
static int l_get_velocity_y(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->vy : 0.0); return 1; }
static int l_set_spin(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (b) { b->w = (float)luaL_checknumber(L, 2); if (b->bb) b->bb->SetAngularVelocity(b->w); }
    return 0;
}
static int l_get_spin(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); lua_pushnumber(L, b ? b->w : 0.0); return 1; }
static int l_set_gravity(lua_State* L) { g_gravity = (float)luaL_checknumber(L, 1); return 0; }
static int l_get_gravity(lua_State* L) { lua_pushnumber(L, g_gravity); return 1; }
static int l_set_bounce(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->restitution = (float)luaL_checknumber(L, 2); return 0; }
static int l_set_friction(lua_State* L) { Body* b = bodyGet(luaL_checkstring(L, 1)); if (b) b->friction = (float)luaL_checknumber(L, 2); return 0; }
static int l_set_mass(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;
    float m = (float)luaL_checknumber(L, 2);
    b->mass = m > 0.01f ? m : 1.0f;
    if (b->bb) {
        b2MassData md; b->bb->GetMassData(&md);
        md.mass = b->mass;
        b->bb->SetMassData(&md);
    }
    return 0;
}
static int l_no_gravity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (b) { b->gravity = lua_gettop(L) >= 2 ? lua_toboolean(L, 2) : false; if (b->bb) b->bb->SetGravityScale(b->gravity ? 1.0f : 0.0f); }
    return 0;
}
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
