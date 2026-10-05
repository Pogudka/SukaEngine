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
    bool isTrigger = false;

    float mass = 1.0f;
    float vx = 0.0f, vy = 0.0f;   // px/s
    float w = 0.0f;               // rad/s

    bool gravity = true;
    bool hasGravityFlag = true;

    float restitution = 0.0f;
    float friction = 0.4f;

    bool onGround = false;
    bool pendingVel = false;

    b2Body* bb = nullptr;
};

struct TriggerEvent {
    std::string a;
    std::string b;
    bool enter;
};

inline std::map<std::string, Body> g_bodies;
inline std::map<b2Body*, std::string> g_bodyNames;

inline std::mutex g_bodiesMtx;
inline std::mutex g_bodyNamesMtx;

inline float g_gravity = 900.0f;                       // px/s^2
inline bool g_showBodies = false;

inline std::vector<std::pair<std::string, std::string>> g_collideEvents;
inline std::mutex g_collideMtx;

inline std::vector<TriggerEvent> g_triggerEvents;
inline std::mutex g_triggerMtx;

inline std::unique_ptr<b2World> g_world;
inline Scene* g_physScene = nullptr;

// ---- слушатель контактов: on_collide / on_trigger ----
class SukaContactListener : public b2ContactListener {
public:
    void BeginContact(b2Contact* c) override {
        handle(c, true);
    }

    void EndContact(b2Contact* c) override {
        handle(c, false);
    }

private:
    void handle(b2Contact* c, bool begin) {
        b2Fixture* fa = c->GetFixtureA();
        b2Fixture* fb = c->GetFixtureB();
        if (!fa || !fb) return;

        b2Body* ba = fa->GetBody();
        b2Body* bb = fb->GetBody();
        if (!ba || !bb) return;

        const bool sensor = fa->IsSensor() || fb->IsSensor();

        std::string na, nb;

        {
            std::lock_guard<std::mutex> lk(g_bodyNamesMtx);

            auto ita = g_bodyNames.find(ba);
            auto itb = g_bodyNames.find(bb);

            if (ita == g_bodyNames.end() || itb == g_bodyNames.end()) return;

            na = ita->second;
            nb = itb->second;
        }

        if (sensor) {
            std::lock_guard<std::mutex> lk(g_triggerMtx);
            g_triggerEvents.push_back(TriggerEvent{na, nb, begin});
        } else if (begin) {
            std::lock_guard<std::mutex> lk(g_collideMtx);
            g_collideEvents.push_back(std::make_pair(na, nb));
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
    if (it != g_bodies.end()) {
        it->second.isStatic = isStatic;
        return;
    }

    Body b;
    b.isStatic = isStatic;
    b.mass = mass > 0.01f ? mass : 1.0f;
    g_bodies[nm] = b;
}

inline void bodyRemove(const std::string& nm) {
    std::lock_guard<std::mutex> lk(g_bodiesMtx);

    auto it = g_bodies.find(nm);
    if (it == g_bodies.end()) return;

    if (it->second.bb && g_world) {
        {
            std::lock_guard<std::mutex> ln(g_bodyNamesMtx);
            g_bodyNames.erase(it->second.bb);
        }
        g_world->DestroyBody(it->second.bb);
    }

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

    for (auto& kv : g_bodies) {
        kv.second.bb = nullptr;
    }

    {
        std::lock_guard<std::mutex> ln(g_bodyNamesMtx);
        g_bodyNames.clear();
    }

    g_world.reset();
    g_physScene = nullptr;

    {
        std::lock_guard<std::mutex> lk2(g_collideMtx);
        g_collideEvents.clear();
    }

    {
        std::lock_guard<std::mutex> lk3(g_triggerMtx);
        g_triggerEvents.clear();
    }
}

// Создание b2-тела по ноде
inline void createB2Body(const std::string& nm, Body& B, Node2D* n) {
    ensureWorld();

    b2BodyDef bd;

    // Триггеры всегда статические сенсоры.
    if (B.isTrigger) {
        bd.type = b2_staticBody;
    } else {
        bd.type = B.isStatic ? b2_staticBody : b2_dynamicBody;
    }

    bd.position.Set(n->position.x * INV_PPM, n->position.y * INV_PPM);
    bd.angle = n->rotation;
    bd.allowSleep = true;
    bd.awake = true;

    b2Body* body = g_world->CreateBody(&bd);

    {
        std::lock_guard<std::mutex> lk(g_bodyNamesMtx);
        g_bodyNames[body] = nm;
    }

    std::string shape = n->shape;
    B.isCircle = (shape == "circle");

    float hw = std::max(1.0f, n->w * std::fabs(n->scale.x)) * 0.5f * INV_PPM;
    float hh = std::max(1.0f, n->h * std::fabs(n->scale.y)) * 0.5f * INV_PPM;

    b2FixtureDef fd;
    fd.friction = B.friction;
    fd.restitution = B.restitution;
    fd.density = 1.0f;
    fd.isSensor = B.isTrigger;

    if (B.isCircle) {
        b2CircleShape cs;
        cs.m_radius = hw;
        fd.shape = &cs;
        body->CreateFixture(&fd);
    } else if (shape == "diamond" || shape == "triangle") {
        b2Vec2 vs[4];
        int cnt = 0;

        if (shape == "diamond") {
            vs[0].Set(0, -hh);
            vs[1].Set(hw, 0);
            vs[2].Set(0, hh);
            vs[3].Set(-hw, 0);
            cnt = 4;
        } else {
            vs[0].Set(0, -hh);
            vs[1].Set(hw, hh);
            vs[2].Set(-hw, hh);
            cnt = 3;
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

    if (!B.isStatic && !B.isTrigger) {
        b2MassData md;
        body->GetMassData(&md);

        md.mass = B.mass;
        md.I = B.isCircle
            ? (0.5f * B.mass * hw * hw)
            : (B.mass * (4.0f * hw * hw + 4.0f * hh * hh) / 12.0f);

        if (md.I < 1e-6f) md.I = 1e-6f;

        body->SetMassData(&md);

        if (B.pendingVel) {
            body->SetLinearVelocity(b2Vec2(B.vx * INV_PPM, B.vy * INV_PPM));
            body->SetAngularVelocity(B.w);
            B.pendingVel = false;
        }
    }

    body->SetGravityScale(B.isTrigger ? 0.0f : (B.gravity ? 1.0f : 0.0f));
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
            if (B.bb && g_world) {
                {
                    std::lock_guard<std::mutex> ln(g_bodyNamesMtx);
                    g_bodyNames.erase(B.bb);
                }
                g_world->DestroyBody(B.bb);
            }
            B.bb = nullptr;
            continue;
        }

        B.isCircle = (std::string(n->shape) == "circle");

        if (!B.bb) createB2Body(*stableName, B, n);
        if (!B.bb) continue;

        // Живые правки параметров
        B.bb->SetGravityScale(B.isTrigger ? 0.0f : (B.gravity ? 1.0f : 0.0f));

        for (b2Fixture* f = B.bb->GetFixtureList(); f; f = f->GetNext()) {
            f->SetFriction(B.friction);
            f->SetRestitution(B.restitution);
        }

        if (B.isStatic || B.isTrigger) {
            // Статика и триггеры следуют за позицией ноды.
            B.bb->SetTransform(
                b2Vec2(n->position.x * INV_PPM, n->position.y * INV_PPM),
                n->rotation
            );
        }
    }

    // Шаг мира
    const int SUB = 2;
    for (int s = 0; s < SUB; ++s) {
        g_world->Step(dt / SUB, 8, 3);
    }

    // Чтение результатов обратно в ноды + onGround + скорости для Lua
    for (auto& nm : names) {
        Body* Bp;
        {
            std::lock_guard<std::mutex> lk(g_bodiesMtx);
            Bp = bodyGet(nm);
        }

        if (!Bp || !Bp->bb) continue;

        Body& B = *Bp;
        b2Body* bb = Bp->bb;

        if (!B.isStatic && !B.isTrigger) {
            b2Vec2 p = bb->GetPosition();

            Node* rn = sc.root->findNode(nm);
            Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;

            if (n) {
                n->position.x = p.x * PPM;
                n->position.y = p.y * PPM;
                n->rotation = bb->GetAngle();
            }

            b2Vec2 v = bb->GetLinearVelocity();
            B.vx = v.x * PPM;
            B.vy = v.y * PPM;
            B.w = bb->GetAngularVelocity();
        }

        // Земля: любой касающийся НЕ-сенсорный контакт с точкой ниже центра.
        B.onGround = false;

        for (b2ContactEdge* ce = bb->GetContactList(); ce; ce = ce->next) {
            b2Contact* ct = ce->contact;
            if (!ct || !ct->IsTouching()) continue;

            if (ct->GetFixtureA()->IsSensor() || ct->GetFixtureB()->IsSensor()) {
                continue;
            }

            const b2Manifold* manifold = ct->GetManifold();
            if (!manifold) continue;

            b2WorldManifold wm;
            ct->GetWorldManifold(&wm);

            for (int k = 0; k < manifold->pointCount; ++k) {
                if (wm.points[k].y > bb->GetPosition().y + 0.01f) {
                    B.onGround = true;
                    break;
                }
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

        const char* col = "#33FF99";
        if (B.isTrigger) col = "#FFFF33";
        else if (B.isStatic) col = "#FF5555";

        b2Transform xf = B.bb->GetTransform();

        float th = 2.0f / (S > 0.01f ? S : 1.0f);
        if (th < 0.5f) th = 0.5f;

        auto seg = [&](float x0, float y0, float x1, float y1) {
            float mx = (x0 + x1) * 0.5f;
            float my = (y0 + y1) * 0.5f;

            float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + th;
            float ang = std::atan2(y1 - y0, x1 - x0) * 57.2957795f;

            float sx = OX + mx * S;
            float sy = OY + my * S;

            out += "DRAW rect|"
                 + std::to_string((int)(sx - len * S / 2)) + "|"
                 + std::to_string((int)(sy - th * S / 2)) + "|"
                 + std::to_string((int)(len * S)) + "|"
                 + std::to_string((int)(th * S)) + "|"
                 + col + "|"
                 + std::to_string((int)ang) + "\n";
        };

        for (b2Fixture* f = B.bb->GetFixtureList(); f; f = f->GetNext()) {
            b2Shape* sh = f->GetShape();

            if (sh->GetType() == b2Shape::e_circle) {
                b2CircleShape* cs = (b2CircleShape*)sh;

                float r = cs->m_radius * PPM;
                float cxp = xf.p.x * PPM;
                float cyp = xf.p.y * PPM;

                const int SEG = 14;
                float px0 = cxp + r;
                float py0 = cyp;

                for (int k = 1; k <= SEG; ++k) {
                    float a = k * 6.28318f / SEG;
                    float px1 = cxp + std::cos(a) * r;
                    float py1 = cyp + std::sin(a) * r;

                    seg(px0, py0, px1, py1);

                    px0 = px1;
                    py0 = py1;
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

// ================= Raycast =================
struct SukaRayCallback : public b2RayCastCallback {
    bool hit = false;
    std::string name;
    std::string ignore;

    b2Vec2 point{0.0f, 0.0f};
    b2Vec2 normal{0.0f, 0.0f};
    float fraction = 0.0f;

    float ReportFixture(b2Fixture* fixture,
                        const b2Vec2& point_,
                        const b2Vec2& normal_,
                        float fraction_) override {
        if (!fixture) return 1.0f;

        // Сенсоры/триггеры raycastом не бьём, если не захотим позже отдельно.
        if (fixture->IsSensor()) return 1.0f;

        b2Body* body = fixture->GetBody();
        if (!body) return 1.0f;

        std::string nm;

        {
            std::lock_guard<std::mutex> lk(g_bodyNamesMtx);
            auto it = g_bodyNames.find(body);
            if (it == g_bodyNames.end()) return 1.0f;
            nm = it->second;
        }

        if (!ignore.empty() && nm == ignore) {
            return 1.0f;
        }

        hit = true;
        name = nm;
        point = point_;
        normal = normal_;
        fraction = fraction_;

        // Останавливаемся на первом попадании.
        return 0.0f;
    }
};

// ================= Lua-биндинги =================
static int l_add_rigidbody(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    float mass = lua_gettop(L) >= 2 && lua_isnumber(L, 2)
        ? (float)lua_tonumber(L, 2)
        : 1.0f;

    if (nm) bodyAdd(nm, false, mass);
    return 0;
}

static int l_add_staticbody(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    if (nm) bodyAdd(nm, true, 1.0f);
    return 0;
}

static int l_add_trigger(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    if (!nm) return 0;

    bodyAdd(nm, true, 1.0f);

    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    auto it = g_bodies.find(nm);
    if (it != g_bodies.end()) {
        it->second.isStatic = true;
        it->second.isTrigger = true;

        // Если тело уже было создано как обычное — пересоздадим в следующем кадре.
        if (it->second.bb && g_world) {
            {
                std::lock_guard<std::mutex> ln(g_bodyNamesMtx);
                g_bodyNames.erase(it->second.bb);
            }
            g_world->DestroyBody(it->second.bb);
            it->second.bb = nullptr;
        }
    }

    return 0;
}

static int l_set_trigger(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    bool val = lua_gettop(L) >= 2 && lua_toboolean(L, 2);

    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    auto it = g_bodies.find(nm);
    if (it == g_bodies.end()) return 0;

    Body& B = it->second;
    if (B.isTrigger == val) return 0;

    B.isTrigger = val;
    if (val) B.isStatic = true;

    // Пересоздаём тело, чтобы применить isSensor/type.
    if (B.bb && g_world) {
        {
            std::lock_guard<std::mutex> ln(g_bodyNamesMtx);
            g_bodyNames.erase(B.bb);
        }
        g_world->DestroyBody(B.bb);
        B.bb = nullptr;
    }

    return 0;
}

static int l_is_trigger(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    lua_pushboolean(L, b ? b->isTrigger : false);
    return 1;
}

static int l_remove_body(lua_State* L) {
    const char* nm = luaL_checkstring(L, 1);
    if (nm) bodyRemove(nm);
    return 0;
}

static int l_set_velocity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;

    float vx = (float)luaL_checknumber(L, 2);
    float vy = (float)luaL_checknumber(L, 3);

    b->vx = vx;
    b->vy = vy;

    if (b->bb) {
        b->bb->SetLinearVelocity(b2Vec2(vx * INV_PPM, vy * INV_PPM));
    } else {
        b->pendingVel = true;
    }

    return 0;
}

static int l_add_velocity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;

    b->vx += (float)luaL_checknumber(L, 2);
    b->vy += (float)luaL_checknumber(L, 3);

    if (b->bb) {
        b->bb->SetLinearVelocity(b2Vec2(b->vx * INV_PPM, b->vy * INV_PPM));
    } else {
        b->pendingVel = true;
    }

    return 0;
}

static int l_get_velocity_x(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    lua_pushnumber(L, b ? b->vx : 0.0);
    return 1;
}

static int l_get_velocity_y(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    lua_pushnumber(L, b ? b->vy : 0.0);
    return 1;
}

static int l_set_spin(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;

    b->w = (float)luaL_checknumber(L, 2);
    if (b->bb) b->bb->SetAngularVelocity(b->w);

    return 0;
}

static int l_get_spin(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    lua_pushnumber(L, b ? b->w : 0.0);
    return 1;
}

static int l_set_gravity(lua_State* L) {
    g_gravity = (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_get_gravity(lua_State* L) {
    lua_pushnumber(L, g_gravity);
    return 1;
}

static int l_set_bounce(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (b) b->restitution = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_set_friction(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (b) b->friction = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_set_mass(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;

    float m = (float)luaL_checknumber(L, 2);
    b->mass = m > 0.01f ? m : 1.0f;

    if (b->bb) {
        b2MassData md;
        b->bb->GetMassData(&md);
        md.mass = b->mass;
        b->bb->SetMassData(&md);
    }

    return 0;
}

static int l_no_gravity(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    if (!b) return 0;

    b->gravity = lua_gettop(L) >= 2 ? lua_toboolean(L, 2) : false;

    if (b->bb) {
        b->bb->SetGravityScale(b->isTrigger ? 0.0f : (b->gravity ? 1.0f : 0.0f));
    }

    return 0;
}

static int l_is_on_ground(lua_State* L) {
    Body* b = bodyGet(luaL_checkstring(L, 1));
    lua_pushboolean(L, b ? b->onGround : false);
    return 1;
}

static int l_is_body(lua_State* L) {
    lua_pushboolean(L, bodyGet(luaL_checkstring(L, 1)) != nullptr);
    return 1;
}

static int l_show_hitboxes(lua_State* L) {
    g_showBodies = lua_gettop(L) >= 1 ? lua_toboolean(L, 1) : true;
    return 0;
}

// raycast(x1, y1, x2, y2 [, ignore_body])
// returns: nil OR name, hx, hy, nx, ny, fraction
static int l_raycast(lua_State* L) {
    ensureWorld();
    if (!g_world) return 0;

    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);

    SukaRayCallback cb;

    if (lua_gettop(L) >= 5 && lua_isstring(L, 5)) {
        cb.ignore = lua_tostring(L, 5);
    }

    b2Vec2 p1(x1 * INV_PPM, y1 * INV_PPM);
    b2Vec2 p2(x2 * INV_PPM, y2 * INV_PPM);

    b2Filter filter;
    filter.categoryBits = 0xFFFF;
    filter.maskBits = 0xFFFF;

    g_world->RayCast(&cb, p1, p2, 1.0f, filter);

    if (!cb.hit) return 0;

    lua_pushstring(L, cb.name.c_str());
    lua_pushnumber(L, cb.point.x * PPM);
    lua_pushnumber(L, cb.point.y * PPM);
    lua_pushnumber(L, cb.normal.x);
    lua_pushnumber(L, cb.normal.y);
    lua_pushnumber(L, cb.fraction);

    return 6;
}

inline void registerPhysicsLuaApi(lua_State* L) {
    if (!L) return;

    lua_register(L, "add_rigidbody", l_add_rigidbody);
    lua_register(L, "add_staticbody", l_add_staticbody);
    lua_register(L, "add_trigger", l_add_trigger);
    lua_register(L, "set_trigger", l_set_trigger);
    lua_register(L, "is_trigger", l_is_trigger);
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

    lua_register(L, "raycast", l_raycast);
}

// Вызывать каждый кадр после physicsUpdate().
// Диспатчит события в Lua:
// on_collide(a, b)
// on_trigger(a, b)
// on_trigger_exit(a, b)
inline void physicsDispatchLua(lua_State* L) {
    if (!L) return;

    // Обычные коллизии
    {
        std::vector<std::pair<std::string, std::string>> events;

        {
            std::lock_guard<std::mutex> lk(g_collideMtx);
            events.swap(g_collideEvents);
        }

        for (auto& e : events) {
            lua_getglobal(L, "on_collide");

            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, e.first.c_str());
                lua_pushstring(L, e.second.c_str());

                if (lua_pcall(L, 2, 0, 0) != 0) {
                    // Логаем ошибку? Пока просто глотаем, чтобы не ронять игру.
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
        }
    }

    // Триггеры
    {
        std::vector<TriggerEvent> events;

        {
            std::lock_guard<std::mutex> lk(g_triggerMtx);
            events.swap(g_triggerEvents);
        }

        for (auto& e : events) {
            const char* fn = e.enter ? "on_trigger" : "on_trigger_exit";

            lua_getglobal(L, fn);

            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, e.a.c_str());
                lua_pushstring(L, e.b.c_str());

                if (lua_pcall(L, 2, 0, 0) != 0) {
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
        }
    }
}

} // namespace suka
