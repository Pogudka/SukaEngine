#pragma once

#include <string>
#include <iostream>

namespace suka {

class ISoundBackend {
public:
    virtual ~ISoundBackend() = default;
    virtual void load(const std::string& name, const std::string& path) = 0;
    virtual void play(const std::string& name) = 0;
};

class LogSoundBackend : public ISoundBackend {
public:
    void load(const std::string& name, const std::string& path) override {
        std::cout << "[Sound] load: " << name << " -> " << path << "\n";
    }
    void play(const std::string& name) override {
        std::cout << "[Sound] play: " << name << "\n";
    }
};

class SoundManager {
public:
    explicit SoundManager(ISoundBackend& backend) : backend_(backend) {}

    void load(const std::string& name, const std::string& path) {
        backend_.load(name, path);
    }
    void play(const std::string& name) {
        backend_.play(name);
    }

private:
    ISoundBackend& backend_;
};

} // namespace suka