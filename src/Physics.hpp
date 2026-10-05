#pragma once

#include <string>
#include <map>
#include <vector>
#include <set>
#include <cmath>
#include <mutex>
#include <algorithm>
#include <utility>

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
    float w = 0.0f;              // угловая скорость, рад/с
    bool gravity = true;
    float restitution = 0.0f;
    float friction = 0.4f;
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

inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

// ==== Коллайдеры: окружность или выпуклый полигон по форме ноды ====
struct Poly { std::vector<std::pair<float, float>> v; };

struct Ent {
    std::string nm;
    Node2D* n;
    Body* b;
    float cx, cy;
    float hw, hh;          // полуразмеры габарита
    float r;               // радиус для круга
    bool isCircle;
    Poly poly;             // мировые вершины (если не круг)
    float invM, invI;
};

inline void buildPoly(Ent& e, const std::string& shape) {
    e.poly.v.clear();
    float cs = std::cos(e.n->rotation), sn = std::sin(e.n->rotation);
    auto push = [&](float lx, float ly) {
        e.poly.v.push_back(std::make_pair(e.cx + lx * cs - ly * sn, e.cy + lx * sn + ly * cs));
    };
    if (shape == "diamond") {
        push(0, -e.hh); push(e.hw, 0); push(0, e.hh); push(-e.hw, 0);
    } else if (shape == "triangle") {
        push(0, -e.hh); push(e.hw, e.hh); push(-e.hw, e.hh);
    } else { // square и всё остальное
        push(-e.hw, -e.hh); push(e.hw, -e.hh); push(e.hw, e.hh); push(-e.hw, e.hh);
    }
}

inline void entInertia(Ent& e) {
    if (e.b->isStatic) { e.invM = 0.0f; e.invI = 0.0f; return; }
    e.invM = 1.0f / e.b->mass;
    float I;
    if (e.isCircle) I = 0.5f * e.b->mass * e.r * e.r;
    else I = e.b->mass * (4.0f * e.hw * e.hw + 4.0f * e.hh * e.hh) / 12.0f;
    e.invI = I > 1e-6f ? 1.0f / I : 0.0f;
}

struct Contact { float nx, ny, pen, px, py; };   // нормаль A->B

inline float polyCentroid(const Poly& P, float& gx, float& gy) {
    gx = 0; gy = 0;
    for (auto& v : P.v) { gx += v.first; gy += v.second; }
    gx /= (float)P.v.size(); gy /= (float)P.v.size();
    return 0;
}

// SAT для двух выпуклых полигонов
inline bool satPoly(const Ent& A, const Ent& B, Contact& c) {
    float cax, cay, cbx, cby;
    polyCentroid(A.poly, cax, cay);
    polyCentroid(B.poly, cbx, cby);
    float dx = cbx - cax, dy = cby - cay;

    float best = 1e18f, bnx = 0, bny = 0;
    const Poly* P[2] = { &A.poly, &B.poly };
    for (int p = 0; p < 2; ++p) {
        const Poly& Q = *P[p];
        const Poly& O = *P[1 - p];
        for (size_t i = 0; i < Q.v.size(); ++i) {
            size_t i2 = (i + 1) % Q.v.size();
            float ex = Q.v[i2].first - Q.v[i].first;
            float ey = Q.v[i2].second - Q.v[i].second;
            float ax = -ey, ay = ex;
            float L = std::sqrt(ax * ax + ay * ay);
            if (L < 1e-6f) continue;
            ax /= L; ay /= L;
            float minQ = 1e18f, maxQ = -1e18f, minO = 1e18f, maxO = -1e18f;
            for (auto& v : Q.v) { float d = v.first * ax + v.second * ay; if (d < minQ) minQ = d; if (d > maxQ) maxQ = d; }
            for (auto& v : O.v) { float d = v.first * ax + v.second * ay; if (d < minO) minO = d; if (d > maxO) maxO = d; }
            float ov = std::min(maxQ, maxO) - std::max(minQ, minO);
            if (ov <= 0.0f) return false;
            if (ov < best) {
                best = ov;
                float s = (dx * ax + dy * ay) >= 0.0f ? 1.0f : -1.0f;
                bnx = ax * s; bny = ay * s;
            }
        }
    }
    c.nx = bnx; c.ny = bny; c.pen = best;
    // точка контакта: самая глубокая вершина A вдоль нормали
    float bd = -1e18f;
    for (auto& v : A.poly.v) {
        float d = v.first * bnx + v.second * bny;
        if (d > bd) { bd = d; c.px = v.first; c.py = v.second; }
    }
    return true;
}

// круг <-> полигон
inline bool circlePoly(const Ent& C, const Ent& X, Contact& c, bool circleIsA) {
    // ближайшая точка границы полигона к центру круга + проверка "центр внутри"
    float bx = C.cx, by = C.cy;
    bool inside = true;
    {
        // внутри выпуклого полигона: все cross(edge, c-v) одного знака
        float sign0 = 0;
        for (size_t i = 0; i < X.poly.v.size(); ++i) {
            size_t i2 = (i + 1) % X.poly.v.size();
            float ex = X.poly.v[i2].first - X.poly.v[i].first;
            float ey = X.poly.v[i2].second - X.poly.v[i].second;
            float rx = C.cx - X.poly.v[i].first, ry = C.cy - X.poly.v[i].second;
            float cr = cross2(ex, ey, rx, ry);
            if (sign0 == 0 && std::fabs(cr) > 1e-6f) sign0 = cr > 0 ? 1 : -1;
            if (sign0 != 0 && cr * sign0 < 0) { inside = false; break; }
        }
    }
    float bestD = 1e18f, qx = 0, qy = 0;
    for (size_t i = 0; i < X.poly.v.size(); ++i) {
        size_t i2 = (i + 1) % X.poly.v.size();
        float ax = X.poly.v[i].first, ay = X.poly.v[i].second;
        float ex = X.poly.v[i2].first - ax, ey = X.poly.v[i2].second - ay;
        float L2 = ex * ex + ey * ey;
        float t = L2 > 1e-9f ? ((C.cx - ax) * ex + (C.cy - ay) * ey) / L2 : 0.0f;
        if (t < 0) t = 0; if (t > 1) t = 1;
        float px = ax + ex * t, py = ay + ey * t;
        float d = (px - C.cx) * (px - C.cx) + (py - C.cy) * (py - C.cy);
        if (d < bestD) { bestD = d; qx = px; qy = py; }
    }
    float d = std::sqrt(bestD);

    float nxPolyToCircle, nyPolyToCircle, pen;
    float cpx, cpy;
    if (inside) {
        // выталкиваем к ближайшей грани: нормаль полигона наружу
        float nx = C.cx - qx, ny = C.cy - qy;
        float L = std::sqrt(nx * nx + ny * ny);
        if (L < 1e-6f) { nx = 0; ny = -1; L = 1; }
        nxPolyToCircle = -nx / L; nyPolyToCircle = -ny / L;  // наружу от полигона через центр
        pen = C.r + d;
        cpx = qx; cpy = qy;
    } else {
        if (d >= C.r || d < 1e-6f) return false;
        nxPolyToCircle = (C.cx - qx) / d; nyPolyToCircle = (C.cy - qy) / d;
        pen = C.r - d;
        cpx = qx; cpy = qy;
    }
    // нормаль должна быть A->B
    if (circleIsA) { c.nx = -nxPolyToCircle; c.ny = -nyPolyToCircle; }   // A=круг: A->B = круг->полигон
    else { c.nx = nxPolyToCircle; c.ny = nyPolyToCircle; }               // A=полигон: A->B = полигон->круг
    c.pen = pen; c.px = cpx; c.py = cpy;
    return true;
}

inline bool makeContact(const Ent& A, const Ent& B, Contact& c) {
    if (A.isCircle && B.isCircle) {
        float dx = B.cx - A.cx, dy = B.cy - A.cy;
        float d = std::sqrt(dx * dx + dy * dy);
        float rr = A.r + B.r;
        if (d >= rr || d < 1e-6f) return false;
        c.nx = dx / d; c.ny = dy / d; c.pen = rr - d;
        c.px = A.cx + c.nx * A.r; c.py = A.cy + c.ny * A.r;
        return true;
    }
    if (A.isCircle != B.isCircle) {
        if (A.isCircle) return circlePoly(A, B, c, true);
        return circlePoly(B, A, c, false);
    }
    return satPoly(A, B, c);
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
            std::string shape = n->shape;
            e.isCircle = (shape == "circle");
            kv.second.isCircle = e.isCircle;
            float sx = std::fabs(n->scale.x), sy = std::fabs(n->scale.y);
            e.hw = std::max(1.0f, n->w * sx * 0.5f);
            e.hh = std::max(1.0f, n->h * sy * 0.5f);
            e.r = e.hw;
            if (!e.isCircle) buildPoly(e, shape);
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
        if (e.b->w  >  25.0f)  e.b->w  =  25.0f;
        if (e.b->w  < -25.0f)  e.b->w  = -25.0f;
        e.b->w *= (1.0f - 0.4f * dt);          // угловое затухание
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

            float vAx = A.b->vx - A.b->w * rAy, vAy = A.b->vy + A.b->w * rAx;
            float vBx = B.b->vx - B.b->w * rBy, vBy = B.b->vy + B.b->w * rBx;
            float rvx = vBx - vAx, rvy = vBy - vAy;
            float vn = rvx * c.nx + rvy * c.ny;

            float e_ = std::max(A.b->restitution, B.b->restitution);
            if (vn > -40.0f) e_ = 0.0f;

            float rnA = cross2(rAx, rAy, c.nx, c.ny);
            float rnB = cross2(rBx, rBy, c.nx, c.ny);
            float kn = A.invM + B.invM + rnA * rnA * A.invI + rnB * rnB * B.invI;
            if (kn <= 1e-9f) continue;
            float jn = -(1.0f + e_) * vn / kn;
            if (jn < 0.0f) jn = 0.0f;

            float jx = c.nx * jn, jy = c.ny * jn;
            A.b->vx -= jx * A.invM; A.b->vy -= jy * A.invM; A.b->w -= cross2(rAx, rAy, jx, jy) * A.invI;
            B.b->vx += jx * B.invM; B.b->vy += jy * B.invM; B.b->w += cross2(rBx, rBy, jx, jy) * B.invI;

            // трение
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

            // УСЛОВИЕ КАЧЕНИЯ для кругов: угловая скорость согласуется с касательной
            auto roll = [&](Ent& D, float ux, float uy) {
                if (!D.isCircle || D.b->isStatic) return;
                float t90x = -uy, t90y = ux;                 // u = от центра к контакту
                float vtan = D.b->vx * t90x + D.b->vy * t90y;
                float target = -vtan / (D.r > 1.0f ? D.r : 1.0f);
                D.b->w += (target - D.b->w) * 0.6f;
            };
            {
                float ux = c.px - A.cx, uy = c.py - A.cy;
                float L = std::sqrt(ux * ux + uy * uy);
                if (L > 1e-6f) roll(A, ux / L, uy / L);
                float wx2 = c.px - B.cx, wy2 = c.py - B.cy;
                float L2 = std::sqrt(wx2 * wx2 + wy2 * wy2);
                if (L2 > 1e-6f) roll(B, wx2 / L2, wy2 / L2);
            }

            // позиционная коррекция
            float slop = 0.5f, percent = 0.8f;
            float corr = std::max(c.pen - slop, 0.0f) * percent / (A.invM + B.invM + 1e-9f);
            A.cx -= c.nx * corr * A.invM; A.cy -= c.ny * corr * A.invM;
            B.cx += c.nx * corr * B.invM; B.cy += c.ny * corr * B.invM;
            if (!A.b->isStatic) { A.n->position.x = A.cx; A.n->position.y = A.cy; }
            if (!B.b->isStatic) { B.n->position.x = B.cx; B.n->position.y = B.cy; }

            if (c.ny < -0.5f) B.b->onGround = true;
            if (c.ny >  0.5f) A.b->onGround = true;
        }
    }

    // покой: гасим микровращение стоящих тел
    for (auto& e : ents) {
        if (e.b->isStatic) continue;
        if (e.b->onGround && std::fabs(e.b->w) < 0.5f) e.b->w = 0.0f;
    }
}

inline void physicsUpdate(Scene& sc, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;
    const int SUB = 3;
    for (int s = 0; s < SUB; ++s) physicsStepOnce(sc, dt / SUB);
}

// ==== Отладочные хитбоксы: рисуют РЕАЛЬНЫЙ коллайдер (полигон или круг) ====
inline void emitBodiesDebug(Scene& sc, std::string& out, float S, float OX, float OY) {
    if (!sc.root) return;
    std::lock_guard<std::mutex> lk(g_bodiesMtx);
    for (auto& kv : g_bodies) {
        Node* rn = sc.root->findNode(kv.first);
        Node2D* n = rn ? dynamic_cast<Node2D*>(rn) : nullptr;
        if (!n) continue;
        const char* col = kv.second.isStatic ? "#FF5555" : "#33FF99";
        float cx = n->position.x, cy = n->position.y;
        float hw = std::max(1.0f, n->w * std::fabs(n->scale.x) * 0.5f);
        float hh = std::max(1.0f, n->h * std::fabs(n->scale.y) * 0.5f);
        bool circ = (std::string(n->shape) == "circle");
        float th = 2.0f / (S > 0.01f ? S : 1.0f);
        if (th < 0.5f) th = 0.5f;

        std::vector<std::pair<float, float>> pts;
        if (circ) {
            const int SEG = 14;
            for (int k = 0; k <= SEG; ++k) {
                float a = k * 6.28318f / SEG;
                pts.push_back(std::make_pair(cx + std::cos(a) * hw, cy + std::sin(a) * hw));
            }
        } else {
            float cs = std::cos(n->rotation), sn = std::sin(n->rotation);
            std::string shape = n->shape;
            if (shape == "diamond") {
                pts.push_back(std::make_pair(cx + (0 * cs - -hh * sn), cy + (0 * sn + -hh * cs)));
                pts.push_back(std::make_pair(cx + (hw * cs - 0 * sn),  cy + (hw * sn + 0 * cs)));
                pts.push_back(std::make_pair(cx + (0 * cs - hh * sn),  cy + (0 * sn + hh * cs)));
                pts.push_back(std::make_pair(cx + (-hw * cs - 0 * sn), cy + (-hw * sn + 0 * cs)));
                pts.push_back(pts[0]);
            } else if (shape == "triangle") {
                pts.push_back(std::make_pair(cx + (0 * cs - -hh * sn), cy + (0 * sn + -hh * cs)));
                pts.push_back(std::make_pair(cx + (hw * cs - hh * sn), cy + (hw * sn + hh * cs)));
                pts.push_back(std::make_pair(cx + (-hw * cs - hh * sn), cy + (-hw * sn + hh * cs)));
                pts.push_back(pts[0]);
            } else {
                pts.push_back(std::make_pair(cx + (-hw * cs - -hh * sn), cy + (-hw * sn + -hh * cs)));
                pts.push_back(std::make_pair(cx + (hw * cs - -hh * sn),  cy + (hw * sn + -hh * cs)));
                pts.push_back(std::make_pair(cx + (hw * cs - hh * sn),   cy + (hw * sn + hh * cs)));
                pts.push_back(std::make_pair(cx + (-hw * cs - hh * sn),  cy + (-hw * sn + hh * cs)));
                pts.push_back(pts[0]);
            }
        }
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            float x0 = pts[k].first, y0 = pts[k].second;
            float x1 = pts[k + 1].first, y1 = pts[k + 1].second;
            float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
            float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + th;
            float ang = std::atan2(y1 - y0, x1 - x0) * 57.2957795f;
            float sx = OX + mx * S, sy = OY + my * S;
            out += "DRAW rect|" + std::to_string((int)(sx - len * S / 2)) + "|" + std::to_string((int)(sy - th * S / 2)) + "|" + std::to_string((int)(len * S)) + "|" + std::to_string((int)(th * S)) + "|" + col + "|" + std::to_string((int)ang) + "\n";
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
