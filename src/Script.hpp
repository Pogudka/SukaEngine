#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>
#include <mutex>
#include <dirent.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "Script.hpp"
#include "LuaGameApi.hpp"
#include "Scene.hpp"

namespace suka {

std::vector<std::string> g_luaLog;

using VarMap = decltype(std::declval<Context&>().vars);

static Context* g_ctx = nullptr;
static SceneManager* g_sm = nullptr;
static Scene* g_scene = nullptr;
static VarMap* g_vars = nullptr;

static void logLua(const std::string& s) {
    g_luaLog.push_back(s);
    if (g_luaLog.size() > 80) g_luaLog.erase(g_luaLog.begin());
}

static std::string luaValueToString(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    if (t == LUA_TNIL) return "nil";
    if (t == LUA_TBOOLEAN) return lua_toboolean(L, idx) ? "true" : "false";
    if (t == LUA_TNUMBER) {
        double n = lua_tonumber(L, idx);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", n);
        return std::string(buf);
    }
    if (t == LUA_TSTRING) {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        if (!s) return "";
        return std::string(s, len);
    }
    return std::string("<") + lua_typename(L, t) + ">";
}

static int l_print(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += " ";
        out += luaValueToString(L, i);
    }
    logLua(out);
    return 0;
}

static int l_log(lua_State* L) {
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += " ";
        out += luaValueToString(L, i);
    }
    logLua(out);
    return 0;
}

static int l_set_var(lua_State* L) {
    const char* k = luaL_checkstring(L, 1);
    double v = luaL_checknumber(L, 2);
    if (g_vars && k) (*g_vars)[k] = v;
    return 0;
}

static int l_get_var(lua_State* L) {
    const char* k = luaL_checkstring(L, 1);
    if (g_vars && k) {
        auto it = g_vars->find(k);
        if (it != g_vars->end()) {
            lua_pushnumber(L, it->second);
            return 1;
        }
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

static int l_add_var(lua_State* L) {
    const char* k = luaL_checkstring(L, 1);
    double v = luaL_checknumber(L, 2);
    if (g_vars && k) {
        auto it = g_vars->find(k);
        if (it != g_vars->end()) it->second += v;
        else (*g_vars)[k] = v;
    }
    return 0;
}

static int l_get_score(lua_State* L) {
    lua_pushinteger(L, g_ctx ? (lua_Integer)g_ctx->score : 0);
    return 1;
}

static int l_set_score(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    if (g_ctx) g_ctx->score = (int)v;
    return 0;
}

static int l_add_score(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    if (g_ctx) g_ctx->score += (int)v;
    return 0;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string readFileToString(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::vector<std::string> listLuaFiles(const std::string& dir) {
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        if (endsWith(name, ".lua")) files.push_back(name);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

static void registerApi(lua_State* L) {
    if (!L) return;

    lua_register(L, "print", l_print);
    lua_register(L, "log", l_log);

    lua_register(L, "set_var", l_set_var);
    lua_register(L, "get_var", l_get_var);
    lua_register(L, "add_var", l_add_var);

    lua_register(L, "get_score", l_get_score);
    lua_register(L, "set_score", l_set_score);
    lua_register(L, "add_score", l_add_score);

    registerGameLuaApi(L);
}

void ScriptSystem::load(const std::string& root) {
    if (L) {
        g_ctx = nullptr;
        g_sm = nullptr;
        g_scene = nullptr;
        g_vars = nullptr;
        lua_close(L);
        L = nullptr;
    }

    {
        std::lock_guard<std::mutex> lk(g_luaCmdMtx);
        g_luaCmd = LuaGameCmd{};
    }

    L = luaL_newstate();
    if (!L) {
        logLua("lua: failed to create state");
        return;
    }

    luaL_openlibs(L);
    registerApi(L);

    std::string dir = root + "/scripts";
    std::vector<std::string> files = listLuaFiles(dir);

    for (const std::string& f : files) {
        std::string path = dir + "/" + f;
        std::string src = readFileToString(path);
        if (src.empty()) continue;

        if (luaL_loadbuffer(L, src.c_str(), src.size(), path.c_str()) != 0) {
            const char* err = lua_tostring(L, -1);
            logLua(std::string("lua load error: ") + (err ? err : "?"));
            lua_pop(L, 1);
            continue;
        }

        if (lua_pcall(L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            logLua(std::string("lua run error: ") + (err ? err : "?"));
            lua_pop(L, 1);
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_luaCmdMtx);
        g_luaCmd = LuaGameCmd{};
    }
}

void ScriptSystem::update(Scene& scene, Context& ctx, float dt, SceneManager& sm, VarMap& vars) {
    if (!L) return;

    g_ctx = &ctx;
    g_sm = &sm;
    g_scene = &scene;
    g_vars = &vars;

    lua_getglobal(L, "on_update");
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, dt);
        if (lua_pcall(L, 1, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            logLua(std::string("lua on_update error: ") + (err ? err : "?"));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }

    g_ctx = nullptr;
    g_sm = nullptr;
    g_scene = nullptr;
    g_vars = nullptr;
}

void ScriptSystem::callGlobal(const std::string& name, Context& ctx, SceneManager& sm, VarMap& vars, Scene* scene) {
    if (!L || name.empty()) return;

    g_ctx = &ctx;
    g_sm = &sm;
    g_scene = scene;
    g_vars = &vars;

    lua_getglobal(L, name.c_str());
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            logLua(std::string("lua call error ") + name + ": " + (err ? err : "?"));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
        logLua("lua: missing function " + name);
    }

    g_ctx = nullptr;
    g_sm = nullptr;
    g_scene = nullptr;
    g_vars = nullptr;
}

// Если в твоём Script.hpp объявлен деструктор ~ScriptSystem(), раскомментируй это:
//
// ScriptSystem::~ScriptSystem() {
//     if (L) {
//         lua_close(L);
//         L = nullptr;
//     }
// }

} // namespace suka
