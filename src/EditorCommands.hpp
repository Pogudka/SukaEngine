#pragma once

#include <string>
#include <iostream>

#include "Json.hpp"
#include "Editor.hpp"
#include "Project.hpp"

namespace suka {

class EditorCommands {
public:
    static bool run(const std::string& path, Editor& editor, const std::string& projectRoot) {
        if (!fileExists(path)) return false;

        std::string json = readFile(path);
        size_t b = 0, e = 0;
        if (!jsonFindArray(json, "commands", b, e)) return false;

        std::string arr = json.substr(b, e - b + 1);
        int count = 0;

        for (const auto& obj : jsonSplitObjects(arr)) {
            std::string action, name, type, text, savePath, shape, color, texPath;
            jsonGetString(obj, "action", action);
            jsonGetString(obj, "name", name);
            jsonGetString(obj, "type", type);
            jsonGetString(obj, "text", text);
            jsonGetString(obj, "path", savePath);
            jsonGetString(obj, "shape", shape);
            jsonGetString(obj, "color", color);
            jsonGetString(obj, "texture", texPath);

            if (action == "select") {
                editor.select(name);
            }
            else if (action == "move") {
                float dx = 0, dy = 0;
                jsonGetNumber(obj, "dx", dx);
                jsonGetNumber(obj, "dy", dy);
                editor.moveSelected(dx, dy);
            }
            else if (action == "add") {
                float x = 0, y = 0;
                jsonGetNumber(obj, "x", x);
                jsonGetNumber(obj, "y", y);
                Node2D* created = editor.addNode(type, name, x, y);
                if (created) {
                    if (!shape.empty()) editor.setShape(name, shape);
                    if (!color.empty()) editor.setColor(name, color);
                    if (!texPath.empty()) editor.setTexture(name, texPath);
                    float w = 0, h = 0;
                    if (jsonGetNumber(obj, "w", w)) created->w = w;
                    if (jsonGetNumber(obj, "h", h)) created->h = h;
                }
            }
            else if (action == "delete") {
                editor.deleteNode(name);
            }
            else if (action == "set_text") {
                editor.setTextSelected(text);
            }
            else if (action == "set_shape") {
                editor.setShape(name, shape);
            }
            else if (action == "set_color") {
                editor.setColor(name, color);
            }
            else if (action == "set_texture") {
                editor.setTexture(name, texPath);
            }
            else if (action == "create_script") {
                ProjectCreator::createScript(projectRoot, savePath, name);
            }
            else if (action == "create_project") {
                ProjectCreator::createProject(name, text.empty() ? name : text);
            }
            else if (action == "save") {
                editor.save(projectRoot + "/" + savePath);
            }

            count++;
        }

        std::cout << "[EditorCommands] executed: " << count
                  << " commands from " << path << "\n";
        return true;
    }
};

} // namespace suka