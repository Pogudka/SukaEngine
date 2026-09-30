#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "Json.hpp"

namespace suka {

class ResourceManager {
public:
    std::vector<std::string> loaded;
    std::vector<std::string> failed;

    bool loadTexture(const std::string& path) {
        for (const auto& p : loaded) {
            if (p == path) return true; // уже загружено
        }

        bool ok = fileExists(path);

        std::cout << (ok ? "  [OK] texture: " : "  [NO] texture: ") << path << "\n";

        if (ok) loaded.push_back(path);
        else failed.push_back(path);

        return ok;
    }
};

} // namespace suka