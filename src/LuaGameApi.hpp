#pragma once

#include <string>
#include <mutex>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace suka {

struct LuaGameCmd {
    bool transition = false;
    int type = 0;                 // 0 fade, 1 slide_left, 2 slide_right, 3 instant
    std::string scene;
    float duration = 0.4f;
};

inline LuaGameCmd g_luaCmd;
inline std::mutex g_luaCmdMtx;

inline std::string asciiLower(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) out += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    return out;
}

inline int parseTransitionType(const std::string& type) {
    std::string t = asciiLower(type);
    if (t.find("instant") != std::string::npos || t.find("cut") != std::string::npos ||
        t.find("none") != std::string::npos || t.find("direct") != std::string::npos) return 3;
    if ((t.find("slide") != std::string::npos || t.find("swipe") != std::string::npos) &&
        (t.find("left") != std::string::npos || t == "l")) return 1;
    if ((t.find("slide") != std::string::npos || t.find("swipe") != std::string::npos) &&
        (t.find("right") != std::string::npos || t == "r")) return 2;
    return 0; // fade / black / dissolve / unknown -> fade
}

inline void lua_transition(const std::string& type, const std::string& scene, float duration) {
    if (scene.empty()) return;
    if (duration < 0.05f) duration = 0.4f;
    if (duration > 5.0f) duration = 5.0f;
    std::lock_guard<std::mutex> lk(g_luaCmdMtx);
    g_luaCmd.transition = true;
    g_luaCmd.type = parseTransitionType(type);
    g_luaCmd.scene = scene;
    g_luaCmd.duration = duration;
}

inline int l_transition(lua_State* L) {
    const char* type = luaL_checkstring(L, 1);
    const char* scene = luaL_checkstring(L, 2);
    double d = 0.4;
    if (lua_isnumber(L, 3)) d = lua_tonumber(L, 3);
    lua_transition(type ? type : "fade", scene ? scene : "", float(d));
    return 0;
}

inline void registerGameLuaApi(lua_State* L) {
    if (!L) return;
    lua_pushcfunction(L, l_transition);
    lua_setglobal(L, "transition");
}

} // namespace suka
