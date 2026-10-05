#include "Core.hpp"
#include "Sound.hpp"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"

#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define SUKA_SOUND_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SukaSound", __VA_ARGS__)
#define SUKA_SOUND_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SukaSound", __VA_ARGS__)
#else
#define SUKA_SOUND_LOGI(...) ((void)0)
#define SUKA_SOUND_LOGE(...) ((void)0)
#endif

namespace suka {
namespace {

ma_engine g_engine{};
bool g_inited = false;

int g_nextId = 1;
int g_musicId = 0;

struct Entry {
    std::unique_ptr<ma_sound> snd;
    std::string path;
    bool decode = false;

    // Эмуляция паузы для miniaudio 0.11.21:
    // ma_sound_set_paused() там нет, поэтому запоминаем позицию и останавливаем звук.
    bool paused = false;
    ma_uint64 pausedFrame = 0;

    float volume = 1.0f;
    bool loop = false;
};

std::unordered_map<int, Entry> g_sounds;
std::unordered_map<std::string, int> g_byPath;
std::mutex g_mtx;

float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

std::string resolvePath(const std::string& p) {
    if (p.empty()) return p;

    // Уже абсолютный путь.
    if (p[0] == '/' || (p.size() > 1 && p[1] == ':')) {
        return p;
    }

    std::string root = projectRootRef();
    if (!root.empty() && root.back() != '/') {
        root += '/';
    }

    return root + p;
}

bool ensureInitLocked() {
    if (g_inited) return true;

    ma_result r = ma_engine_init(nullptr, &g_engine);
    if (r != MA_SUCCESS) {
        SUKA_SOUND_LOGE("ma_engine_init failed: %d", (int)r);
        return false;
    }

    ma_engine_set_volume(&g_engine, 1.0f);
    g_inited = true;
    return true;
}

int loadLocked(const std::string& path, bool decode) {
    if (path.empty()) return 0;
    if (!ensureInitLocked()) return 0;

    auto cached = g_byPath.find(path);
    if (cached != g_byPath.end()) {
        auto ex = g_sounds.find(cached->second);
        if (ex != g_sounds.end() && ex->second.snd) {
            return cached->second;
        }

        // Кэш протух — чистим.
        g_byPath.erase(cached);
    }

    std::string full = resolvePath(path);

    auto snd = std::make_unique<ma_sound>();

    ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (decode) {
        flags |= MA_SOUND_FLAG_DECODE;
    }

    ma_result r = ma_sound_init_from_file(
        &g_engine,
        full.c_str(),
        flags,
        nullptr,
        nullptr,
        snd.get()
    );

    if (r != MA_SUCCESS) {
        SUKA_SOUND_LOGE("failed to load sound '%s': %d", full.c_str(), (int)r);
        return 0;
    }

    int id = g_nextId++;

    Entry e;
    e.snd = std::move(snd);
    e.path = path;
    e.decode = decode;
    e.paused = false;
    e.pausedFrame = 0;
    e.volume = 1.0f;
    e.loop = false;

    g_sounds.emplace(id, std::move(e));
    g_byPath.emplace(path, id);

    return id;
}

} // namespace

bool audioInit() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return ensureInitLocked();
}

void audioShutdown() {
    std::lock_guard<std::mutex> lk(g_mtx);

    for (auto& kv : g_sounds) {
        if (kv.second.snd) {
            ma_sound_stop(kv.second.snd.get());
            ma_sound_uninit(kv.second.snd.get());
        }
    }

    g_sounds.clear();
    g_byPath.clear();
    g_musicId = 0;

    if (g_inited) {
        ma_engine_uninit(&g_engine);
        g_inited = false;
    }
}

int audioLoadSound(const std::string& path, bool decode) {
    std::lock_guard<std::mutex> lk(g_mtx);
    return loadLocked(path, decode);
}

bool audioUnloadSound(int id) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end()) return false;

    std::string path = it->second.path;

    if (g_musicId == id) {
        g_musicId = 0;
    }

    if (it->second.snd) {
        ma_sound_stop(it->second.snd.get());
        ma_sound_uninit(it->second.snd.get());
    }

    g_byPath.erase(path);
    g_sounds.erase(it);

    return true;
}

int audioPlaySound(int id, float volume, bool loop) {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (!ensureInitLocked()) return 0;

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return 0;

    ma_sound* s = it->second.snd.get();

    it->second.paused = false;
    it->second.pausedFrame = 0;
    it->second.volume = clampf(volume, 0.0f, 4.0f);
    it->second.loop = loop;

    ma_sound_stop(s);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_set_volume(s, it->second.volume);
    ma_sound_set_looping(s, it->second.loop ? MA_TRUE : MA_FALSE);

    ma_result r = ma_sound_start(s);
    if (r != MA_SUCCESS) {
        SUKA_SOUND_LOGE("ma_sound_start failed: %d", (int)r);
        return 0;
    }

    return id;
}

bool audioStopSound(int id) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return false;

    ma_sound* s = it->second.snd.get();

    ma_sound_stop(s);
    ma_sound_seek_to_pcm_frame(s, 0);

    it->second.paused = false;
    it->second.pausedFrame = 0;

    return true;
}

bool audioPauseSound(int id) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return false;

    ma_sound* s = it->second.snd.get();

    if (it->second.paused) {
        return true;
    }

    if (!ma_sound_is_playing(s)) {
        return false;
    }

    ma_uint64 cursor = 0;
    ma_result rc = ma_sound_get_cursor_in_pcm_frames(s, &cursor);
    if (rc != MA_SUCCESS) {
        cursor = 0;
    }

    ma_sound_stop(s);

    it->second.paused = true;
    it->second.pausedFrame = cursor;

    return true;
}

bool audioResumeSound(int id) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return false;

    if (!it->second.paused) {
        return false;
    }

    ma_sound* s = it->second.snd.get();

    ma_sound_seek_to_pcm_frame(s, it->second.pausedFrame);
    ma_sound_set_volume(s, it->second.volume);
    ma_sound_set_looping(s, it->second.loop ? MA_TRUE : MA_FALSE);

    ma_result r = ma_sound_start(s);
    if (r != MA_SUCCESS) {
        SUKA_SOUND_LOGE("ma_sound_start(resume) failed: %d", (int)r);
        it->second.paused = false;
        it->second.pausedFrame = 0;
        return false;
    }

    it->second.paused = false;
    return true;
}

bool audioSetSoundVolume(int id, float volume) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return false;

    it->second.volume = clampf(volume, 0.0f, 4.0f);
    ma_sound_set_volume(it->second.snd.get(), it->second.volume);

    return true;
}

bool audioSetSoundPitch(int id, float pitch) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return false;

    ma_sound_set_pitch(it->second.snd.get(), clampf(pitch, 0.1f, 4.0f));
    return true;
}

bool audioIsSoundPlaying(int id) {
    std::lock_guard<std::mutex> lk(g_mtx);

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return false;

    if (it->second.paused) return false;

    return ma_sound_is_playing(it->second.snd.get()) != 0;
}

void audioSetMasterVolume(float volume) {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (!ensureInitLocked()) return;

    ma_engine_set_volume(&g_engine, clampf(volume, 0.0f, 1.0f));
}

float audioGetMasterVolume() {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (!ensureInitLocked()) return 0.0f;

    return ma_engine_get_volume(&g_engine);
}

void audioStopAllSounds() {
    std::lock_guard<std::mutex> lk(g_mtx);

    for (auto& kv : g_sounds) {
        if (kv.second.snd) {
            ma_sound_stop(kv.second.snd.get());
            ma_sound_seek_to_pcm_frame(kv.second.snd.get(), 0);
            kv.second.paused = false;
            kv.second.pausedFrame = 0;
        }
    }

    g_musicId = 0;
}

int audioPlayMusic(const std::string& path, float volume) {
    std::lock_guard<std::mutex> lk(g_mtx);

    int id = loadLocked(path, false);
    if (id == 0) return 0;

    auto it = g_sounds.find(id);
    if (it == g_sounds.end() || !it->second.snd) return 0;

    g_musicId = id;

    ma_sound* s = it->second.snd.get();

    it->second.paused = false;
    it->second.pausedFrame = 0;
    it->second.volume = clampf(volume, 0.0f, 1.0f);
    it->second.loop = true;

    ma_sound_stop(s);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_set_volume(s, it->second.volume);
    ma_sound_set_looping(s, MA_TRUE);

    ma_result r = ma_sound_start(s);
    if (r != MA_SUCCESS) {
        SUKA_SOUND_LOGE("music start failed: %d", (int)r);
        g_musicId = 0;
        return 0;
    }

    return id;
}

void audioStopMusic() {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (g_musicId == 0) return;

    auto it = g_sounds.find(g_musicId);
    if (it != g_sounds.end() && it->second.snd) {
        ma_sound_stop(it->second.snd.get());
        ma_sound_seek_to_pcm_frame(it->second.snd.get(), 0);
        it->second.paused = false;
        it->second.pausedFrame = 0;
    }

    g_musicId = 0;
}

bool audioSetMusicVolume(float volume) {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (g_musicId == 0) return false;

    auto it = g_sounds.find(g_musicId);
    if (it == g_sounds.end() || !it->second.snd) return false;

    it->second.volume = clampf(volume, 0.0f, 1.0f);
    ma_sound_set_volume(it->second.snd.get(), it->second.volume);

    return true;
}

bool audioIsMusicPlaying() {
    std::lock_guard<std::mutex> lk(g_mtx);

    if (g_musicId == 0) return false;

    auto it = g_sounds.find(g_musicId);
    if (it == g_sounds.end() || !it->second.snd) return false;

    if (it->second.paused) return false;

    return ma_sound_is_playing(it->second.snd.get()) != 0;
}

extern "C" {

static int l_audio_init(lua_State* L) {
    lua_pushboolean(L, suka::audioInit() ? 1 : 0);
    return 1;
}

static int l_audio_shutdown(lua_State* L) {
    suka::audioShutdown();
    return 0;
}

static int l_load_sound(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    bool decode = (lua_gettop(L) >= 2) && lua_toboolean(L, 2);

    int id = suka::audioLoadSound(path, decode);

    if (id == 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, id);
    }

    return 1;
}

static int l_unload_sound(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, suka::audioUnloadSound(id) ? 1 : 0);
    return 1;
}

static int l_play_sound(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);

    float vol = 1.0f;
    if (lua_gettop(L) >= 2) {
        vol = (float)lua_tonumber(L, 2);
    }

    bool loop = false;
    if (lua_gettop(L) >= 3) {
        loop = lua_toboolean(L, 3) != 0;
    }

    int res = suka::audioPlaySound(id, vol, loop);

    if (res == 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, res);
    }

    return 1;
}

static int l_stop_sound(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, suka::audioStopSound(id) ? 1 : 0);
    return 1;
}

static int l_pause_sound(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, suka::audioPauseSound(id) ? 1 : 0);
    return 1;
}

static int l_resume_sound(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, suka::audioResumeSound(id) ? 1 : 0);
    return 1;
}

static int l_set_sound_volume(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    float vol = (float)luaL_checknumber(L, 2);

    lua_pushboolean(L, suka::audioSetSoundVolume(id, vol) ? 1 : 0);
    return 1;
}

static int l_set_sound_pitch(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    float pitch = (float)luaL_checknumber(L, 2);

    lua_pushboolean(L, suka::audioSetSoundPitch(id, pitch) ? 1 : 0);
    return 1;
}

static int l_is_sound_playing(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, suka::audioIsSoundPlaying(id) ? 1 : 0);
    return 1;
}

static int l_set_master_volume(lua_State* L) {
    float vol = (float)luaL_checknumber(L, 1);
    suka::audioSetMasterVolume(vol);
    return 0;
}

static int l_get_master_volume(lua_State* L) {
    lua_pushnumber(L, suka::audioGetMasterVolume());
    return 1;
}

static int l_stop_all_sounds(lua_State* L) {
    suka::audioStopAllSounds();
    return 0;
}

static int l_play_music(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    float vol = 1.0f;
    if (lua_gettop(L) >= 2) {
        vol = (float)lua_tonumber(L, 2);
    }

    int id = suka::audioPlayMusic(path, vol);

    if (id == 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, id);
    }

    return 1;
}

static int l_stop_music(lua_State* L) {
    suka::audioStopMusic();
    return 0;
}

static int l_set_music_volume(lua_State* L) {
    float vol = (float)luaL_checknumber(L, 1);
    lua_pushboolean(L, suka::audioSetMusicVolume(vol) ? 1 : 0);
    return 1;
}

static int l_is_music_playing(lua_State* L) {
    lua_pushboolean(L, suka::audioIsMusicPlaying() ? 1 : 0);
    return 1;
}

} // extern "C"

void registerSoundLuaApi(lua_State* L) {
    if (!L) return;

    lua_register(L, "audio_init", l_audio_init);
    lua_register(L, "audio_shutdown", l_audio_shutdown);

    lua_register(L, "load_sound", l_load_sound);
    lua_register(L, "unload_sound", l_unload_sound);

    lua_register(L, "play_sound", l_play_sound);
    lua_register(L, "stop_sound", l_stop_sound);
    lua_register(L, "pause_sound", l_pause_sound);
    lua_register(L, "resume_sound", l_resume_sound);

    lua_register(L, "set_sound_volume", l_set_sound_volume);
    lua_register(L, "set_sound_pitch", l_set_sound_pitch);
    lua_register(L, "is_sound_playing", l_is_sound_playing);

    lua_register(L, "set_master_volume", l_set_master_volume);
    lua_register(L, "get_master_volume", l_get_master_volume);
    lua_register(L, "stop_all_sounds", l_stop_all_sounds);

    lua_register(L, "play_music", l_play_music);
    lua_register(L, "stop_music", l_stop_music);
    lua_register(L, "set_music_volume", l_set_music_volume);
    lua_register(L, "is_music_playing", l_is_music_playing);
}

} // namespace suka
