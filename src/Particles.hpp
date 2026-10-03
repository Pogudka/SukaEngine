#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <cstring>

namespace suka {

struct SpawnOpts {
    float vx = 0.0f;
    float vy = 0.0f;
    float spread = 0.0f;
    float gravity = 0.0f;
    float life = 1.0f;
    float lifeSpread = 0.0f;
    float size = 18.0f;
    float sizeEnd = 0.0f;          // by default particles shrink away
    float drag = 0.0f;             // per-second velocity damping
    unsigned color = 0xFFFFFFFFu;  // all-FF = white in any channel order
    char glyph[8] = {'\xe2','\x80','\xa2',0}; // "•" in UTF-8
};

struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    float life = 0, maxLife = 1;
    float size = 0, sizeEnd = 0;
    float drag = 0, gravity = 0;
    unsigned color = 0;
    char glyph[8] = {0};
};

class ParticleSystem {
public:
    void spawn(float x, float y, int count, const SpawnOpts& o) {
        if (count < 0) count = 0;
        if (count > 2000) count = 2000;

        std::uniform_real_distribution<float> uni(-1.0f, 1.0f);

        for (int i = 0; i < count; ++i) {
            Particle p;
            p.x = x;
            p.y = y;
            p.vx = o.vx + uni(rng_) * o.spread;
            p.vy = o.vy + uni(rng_) * o.spread;
            p.gravity = o.gravity;
            p.drag = o.drag;

            float lf = o.life + uni(rng_) * o.lifeSpread;
            if (lf < 0.05f) lf = 0.05f;
            p.life = lf;
            p.maxLife = lf;

            p.size = o.size;
            p.sizeEnd = o.sizeEnd;
            p.color = o.color;
            std::memcpy(p.glyph, o.glyph, sizeof(p.glyph));
            p.glyph[7] = 0;

            list_.push_back(p);
        }

        if (list_.size() > 20000) {
            list_.erase(list_.begin(), list_.begin() + (list_.size() - 20000));
        }
    }

    void update(float dt) {
        if (dt <= 0.0f) dt = 0.0f;

        for (size_t i = list_.size(); i-- > 0;) {
            Particle& p = list_[i];
            p.life -= dt;

            if (p.life <= 0.0f) {
                list_.erase(list_.begin() + i);
                continue;
            }

            if (p.drag != 0.0f) {
                float f = 1.0f - p.drag * dt;
                if (f < 0.0f) f = 0.0f;
                p.vx *= f;
                p.vy *= f;
            }

            p.vy += p.gravity * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
        }
    }

    void clear() { list_.clear(); }
    size_t count() const { return list_.size(); }
    const std::vector<Particle>& list() const { return list_; }

private:
    std::vector<Particle> list_;
    std::mt19937 rng_{std::random_device{}()};
};

inline ParticleSystem g_particles;

} // namespace suka
