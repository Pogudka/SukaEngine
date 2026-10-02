#pragma once

#include <string>
#include <cctype>
#include <cstdlib>

namespace suka {

inline unsigned dimColor(unsigned c, float k) {
    unsigned r = (unsigned)(((c >> 24) & 255) * k);
    unsigned g = (unsigned)(((c >> 16) & 255) * k);
    unsigned b = (unsigned)(((c >> 8) & 255) * k);
    unsigned a = (c & 255);

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

    return (r << 24) | (g << 16) | (b << 8) | a;
}

inline std::string sanitizeLine(const std::string& s) {
    std::string o;

    for (char c : s) {
        if (c == '|') o += '/';
        else if (c == '\t') o += "  ";
        else if ((unsigned char)c < 32) o += ' ';
        else o += c;
    }

    return o;
}

inline bool utf8Cont(char c) {
    return ((unsigned char)c & 0xC0) == 0x80;
}

inline int utf8Prev(const std::string& s, int pos) {
    if (pos <= 0) return 0;

    --pos;
    while (pos > 0 && utf8Cont(s[pos])) --pos;

    return pos;
}

inline int utf8Next(const std::string& s, int pos) {
    if (pos >= (int)s.size()) return (int)s.size();

    ++pos;
    while (pos < (int)s.size() && utf8Cont(s[pos])) ++pos;

    return pos;
}

inline int utf8CpToByte(const std::string& s, int cp) {
    int p = 0;

    for (int i = 0; i < cp && p < (int)s.size(); ++i) {
        p = utf8Next(s, p);
    }

    return p;
}

inline int utf8ByteToCp(const std::string& s, int bytePos) {
    int c = 0;
    int p = 0;

    while (p < bytePos && p < (int)s.size()) {
        p = utf8Next(s, p);
        ++c;
    }

    return c;
}

inline std::string rgbStr(unsigned c) {
    return std::to_string((c >> 24) & 255) + "," +
           std::to_string((c >> 16) & 255) + "," +
           std::to_string((c >> 8) & 255);
}

inline bool parseRgb(const std::string& s, unsigned& out) {
    int v[3];
    int idx = 0;
    std::string num;

    for (size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && isdigit((unsigned char)s[i])) {
            num += s[i];
            continue;
        }

        if (!num.empty()) {
            if (idx < 3) v[idx++] = atoi(num.c_str());
            num.clear();
        }
    }

    if (idx < 3) return false;

    for (int k = 0; k < 3; ++k) {
        if (v[k] < 0) v[k] = 0;
        if (v[k] > 255) v[k] = 255;
    }

    out = ((unsigned)v[0] << 24) |
          ((unsigned)v[1] << 16) |
          ((unsigned)v[2] << 8)  |
          0xFFu;

    return true;
}

inline std::string escapeJsonString(const std::string& s) {
    std::string o;

    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back((char)c);
        } else if (c < 32) {
            o.push_back(' ');
        } else {
            o.push_back((char)c);
        }
    }

    return o;
}

inline std::string sanitizeProjectDirName(const std::string& raw) {
    std::string o;

    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.') {
            o.push_back((char)c);
        } else if (c == ' ') {
            o.push_back('_');
        }
    }

    while (!o.empty() && o[0] == '.') o.erase(o.begin());

    if (o.size() > 32) o.resize(32);
    if (o.empty()) o = "Project";

    return o;
}

inline std::string safeProjectDisplayName(const std::string& raw) {
    std::string o;

    for (unsigned char c : raw) {
        if (c == '"' || c == '\\' || c == '|') continue;

        if (c < 32) o.push_back(' ');
        else o.push_back((char)c);
    }

    size_t a = 0;
    size_t b = o.size();

    while (a < b && std::isspace((unsigned char)o[a])) ++a;
    while (b > a && std::isspace((unsigned char)o[b - 1])) --b;

    o = o.substr(a, b - a);

    if (o.empty()) o = "Project";
    if (o.size() > 48) o.resize(48);

    return o;
}

} // namespace suka
