#pragma once

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace suka {

bool audioInit();
void audioShutdown();

int  audioLoadSound(const std::string& path, bool decode = false);
bool audioUnloadSound(int id);

int  audioPlaySound(int id, float volume = 1.0f, bool loop = false);
bool audioStopSound(int id);
bool audioPauseSound(int id);
bool audioResumeSound(int id);

bool audioSetSoundVolume(int id, float volume);
bool audioSetSoundPitch(int id, float pitch);
bool audioIsSoundPlaying(int id);

void  audioSetMasterVolume(float volume);
float audioGetMasterVolume();
void  audioStopAllSounds();

int  audioPlayMusic(const std::string& path, float volume = 1.0f);
void audioStopMusic();
bool audioSetMusicVolume(float volume);
bool audioIsMusicPlaying();

void registerSoundLuaApi(lua_State* L);

} // namespace suka
