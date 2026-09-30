#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <iostream>
#include <cstdlib>
#include <cctype>

namespace suka {

struct LuaValue {
    enum Type { Nil, Num, Str, Bool } type = Nil;
    double num = 0;
    std::string str;
    bool boolean = false;

    static LuaValue numV(double v) { LuaValue r; r.type = Num; r.num = v; return r; }
    static LuaValue strV(const std::string& s) { LuaValue r; r.type = Str; r.str = s; return r; }
    static LuaValue boolV(bool b) { LuaValue r; r.type = Bool; r.boolean = b; return r; }

    bool truthy() const {
        return !(type == Nil || (type == Bool && !boolean));
    }

    std::string toString() const {
        if (type == Num) {
            if (num == (long long)num) return std::to_string((long long)num);
            return std::to_string(num);
        }
        if (type == Str) return str;
        if (type == Bool) return boolean ? "true" : "false";
        return "nil";
    }
};

struct Tok {
    enum T { End, Name, Num, Str, Op } t = End;
    std::string s;
    double num = 0;
};

inline std::vector<Tok> luaTokenize(const std::string& src) {
    std::vector<Tok> out;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (std::isspace((unsigned char)c)) { i++; continue; }
        if (c == '-' && i + 1 < src.size() && src[i + 1] == '-') {
            while (i < src.size() && src[i] != '\n') i++;
            continue;
        }
        if (std::isdigit((unsigned char)c)) {
            size_t s = i;
            while (i < src.size() && (std::isdigit((unsigned char)src[i]) || src[i] == '.')) i++;
            Tok tk; tk.t = Tok::Num; tk.num = atof(src.substr(s, i - s).c_str());
            out.push_back(tk);
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t s = i;
            while (i < src.size() && (std::isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
            Tok tk; tk.t = Tok::Name; tk.s = src.substr(s, i - s);
            out.push_back(tk);
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c; i++;
            std::string v;
            while (i < src.size() && src[i] != q) {
                if (src[i] == '\\' && i + 1 < src.size()) i++;
                v += src[i++];
            }
            i++;
            Tok tk; tk.t = Tok::Str; tk.s = v;
            out.push_back(tk);
            continue;
        }
        if (i + 1 < src.size()) {
            std::string two = src.substr(i, 2);
            if (two == "==" || two == "~=" || two == "<=" || two == ">=" || two == "..") {
                Tok tk; tk.t = Tok::Op; tk.s = two;
                out.push_back(tk);
                i += 2;
                continue;
            }
        }
        Tok tk; tk.t = Tok::Op; tk.s = std::string(1, c);
        out.push_back(tk);
        i++;
    }
    Tok end; end.t = Tok::End;
    out.push_back(end);
    return out;
}

struct LExpr; using LE = std::shared_ptr<LExpr>;
struct LExpr {
    enum K { Num, Str, Bool, Nil, Name, Call, Bin, Un } k = Nil;
    double num = 0;
    std::string s;
    bool b = false;
    std::string op;
    LE l, r;
    std::vector<LE> args;
};

struct LStmt; using LS = std::shared_ptr<LStmt>;
struct LStmt {
    enum K { Assign, CallS, If, While, Func, Return } k = Assign;
    std::string name;
    LE value, cond;
    std::vector<LS> body, els;
    std::vector<std::pair<LE, std::vector<LS>>> eifs;
    std::vector<std::string> params;
    std::vector<LE> args;
};

class LuaVM;

struct LuaParser {
    std::vector<Tok> t;
    size_t p = 0;

    const Tok& cur() const { return t[p]; }
    bool isOp(const std::string& o) const { return t[p].t == Tok::Op && t[p].s == o; }
    bool isKw(const std::string& k) const { return t[p].t == Tok::Name && t[p].s == k; }
    bool eatOp(const std::string& o) { if (isOp(o)) { p++; return true; } return false; }
    bool eatKw(const std::string& k) { if (isKw(k)) { p++; return true; } return false; }

    bool atEndKw() const {
        return isKw("end") || isKw("else") || isKw("elseif") || t[p].t == Tok::End;
    }

    std::vector<LS> parseChunk() {
        std::vector<LS> out;
        while (!atEndKw()) {
            LS s = parseStmt();
            if (s) out.push_back(s);
        }
        return out;
    }

    LS parseStmt() {
        if (eatKw("local")) { /* local = просто переменная */ }
        if (isKw("if")) return parseIf();
        if (isKw("while")) return parseWhile();
        if (isKw("function")) return parseFunc();
        if (isKw("return")) {
            p++;
            LS s = std::make_shared<LStmt>();
            s->k = LStmt::Return;
            if (!atEndKw()) s->value = parseExpr();
            return s;
        }
        if (cur().t == Tok::Name) {
            std::string name = cur().s;
            if (p + 1 < t.size() && t[p + 1].t == Tok::Op && t[p + 1].s == "=") {
                p += 2;
                LS s = std::make_shared<LStmt>();
                s->k = LStmt::Assign;
                s->name = name;
                s->value = parseExpr();
                return s;
            }
        }
        LS s = std::make_shared<LStmt>();
        s->k = LStmt::CallS;
        s->value = parseExpr();
        return s;
    }

    LS parseIf() {
        p++; // if
        LS s = std::make_shared<LStmt>();
        s->k = LStmt::If;
        s->cond = parseExpr();
        eatKw("then");
        s->body = parseChunk();
        while (isKw("elseif")) {
            p++;
            LE c = parseExpr();
            eatKw("then");
            std::vector<LS> b = parseChunk();
            s->eifs.push_back({c, b});
        }
        if (eatKw("else")) s->els = parseChunk();
        eatKw("end");
        return s;
    }

    LS parseWhile() {
        p++; // while
        LS s = std::make_shared<LStmt>();
        s->k = LStmt::While;
        s->cond = parseExpr();
        eatKw("do");
        s->body = parseChunk();
        eatKw("end");
        return s;
    }

    LS parseFunc() {
        p++; // function
        LS s = std::make_shared<LStmt>();
        s->k = LStmt::Func;
        s->name = cur().s;
        p++;
        eatOp("(");
        while (!isOp(")")) {
            s->params.push_back(cur().s);
            p++;
            if (!eatOp(",")) break;
        }
        eatOp(")");
        s->body = parseChunk();
        eatKw("end");
        return s;
    }

    LE parseExpr() { return parseOr(); }

    LE parseOr() {
        LE l = parseAnd();
        while (isKw("or")) { p++; LE r = parseAnd(); LE n = std::make_shared<LExpr>(); n->k = LExpr::Bin; n->op = "or"; n->l = l; n->r = r; l = n; }
        return l;
    }
    LE parseAnd() {
        LE l = parseCmp();
        while (isKw("and")) { p++; LE r = parseCmp(); LE n = std::make_shared<LExpr>(); n->k = LExpr::Bin; n->op = "and"; n->l = l; n->r = r; l = n; }
        return l;
    }
    LE parseCmp() {
        LE l = parseAdd();
        while (isOp("==") || isOp("~=") || isOp("<") || isOp(">") || isOp("<=") || isOp(">=")) {
            std::string op = cur().s; p++;
            LE r = parseAdd();
            LE n = std::make_shared<LExpr>(); n->k = LExpr::Bin; n->op = op; n->l = l; n->r = r; l = n;
        }
        return l;
    }
    LE parseAdd() {
        LE l = parseMul();
        while (isOp("+") || isOp("-") || isOp("..")) {
            std::string op = cur().s; p++;
            LE r = parseMul();
            LE n = std::make_shared<LExpr>(); n->k = LExpr::Bin; n->op = op; n->l = l; n->r = r; l = n;
        }
        return l;
    }
    LE parseMul() {
        LE l = parseUn();
        while (isOp("*") || isOp("/") || isOp("%")) {
            std::string op = cur().s; p++;
            LE r = parseUn();
            LE n = std::make_shared<LExpr>(); n->k = LExpr::Bin; n->op = op; n->l = l; n->r = r; l = n;
        }
        return l;
    }
    LE parseUn() {
        if (isOp("-") || isKw("not")) {
            std::string op = cur().s; p++;
            LE r = parseUn();
            LE n = std::make_shared<LExpr>(); n->k = LExpr::Un; n->op = op; n->l = r;
            return n;
        }
        return parsePrim();
    }
    LE parsePrim() {
        if (cur().t == Tok::Num) { LE e = std::make_shared<LExpr>(); e->k = LExpr::Num; e->num = cur().num; p++; return e; }
        if (cur().t == Tok::Str) { LE e = std::make_shared<LExpr>(); e->k = LExpr::Str; e->s = cur().s; p++; return e; }
        if (isOp("(")) { p++; LE e = parseExpr(); eatOp(")"); return e; }
        if (cur().t == Tok::Name) {
            std::string name = cur().s;
            p++;
            if (isOp("(")) {
                p++;
                LE e = std::make_shared<LExpr>(); e->k = LExpr::Call; e->s = name;
                while (!isOp(")")) {
                    e->args.push_back(parseExpr());
                    if (!eatOp(",")) break;
                }
                eatOp(")");
                return e;
            }
            LE e = std::make_shared<LExpr>(); e->k = LExpr::Name; e->s = name;
            return e;
        }
        LE e = std::make_shared<LExpr>(); e->k = LExpr::Nil; p++;
        return e;
    }
};

class LuaVM {
public:
    using NativeFn = std::function<LuaValue(LuaVM&, std::vector<LuaValue>&)>;
    struct Func { std::vector<std::string> params; std::vector<LS> body; };

    std::map<std::string, LuaValue> globals;
    std::map<std::string, Func> funcs;
    std::map<std::string, NativeFn> natives;
    void* host = nullptr;

    void setNative(const std::string& n, NativeFn f) { natives[n] = f; }
    bool has(const std::string& n) const { return funcs.count(n) > 0; }
    
    bool load(const std::string& src) {
        LuaParser pr;
        pr.t = luaTokenize(src);
        std::vector<LS> chunk = pr.parseChunk();
        LuaValue ret;
        execBlock(chunk, ret);
        return true;
    }
    
    LuaValue call(const std::string& n, std::vector<LuaValue> args) {
        auto it = funcs.find(n);
        if (it == funcs.end()) return LuaValue();
        return callFunc(it->second, args);
    }

    LuaValue callFunc(const Func& f, std::vector<LuaValue> args) {
        std::vector<std::pair<std::string, LuaValue>> saved;
        for (size_t i = 0; i < f.params.size(); ++i) {
            saved.push_back({f.params[i], globals[f.params[i]]});
            globals[f.params[i]] = i < args.size() ? args[i] : LuaValue();
        }
        LuaValue ret;
        execBlock(f.body, ret);
        for (auto& sv : saved) globals[sv.first] = sv.second;
        return ret;
    }

    bool execBlock(const std::vector<LS>& block, LuaValue& ret) {
        for (const auto& s : block) {
            if (execStmt(s, ret)) return true;
            if (++steps_ > 2000000) {
                std::cout << "[Lua] execution limit reached\n";
                return true;
            }
        }
        return false;
    }

    bool execStmt(const LS& s, LuaValue& ret) {
        switch (s->k) {
            case LStmt::Assign:
                globals[s->name] = evalExpr(s->value);
                return false;
            case LStmt::CallS:
                evalExpr(s->value);
                return false;
            case LStmt::Return:
                ret = s->value ? evalExpr(s->value) : LuaValue();
                return true;
            case LStmt::Func: {
                Func f;
                f.params = s->params;
                f.body = s->body;
                funcs[s->name] = f;
                return false;
            }
            case LStmt::If: {
                if (evalExpr(s->cond).truthy()) {
                    LuaValue r;
                    return execBlock(s->body, r);
                }
                for (auto& e : s->eifs) {
                    if (evalExpr(e.first).truthy()) {
                        LuaValue r;
                        return execBlock(e.second, r);
                    }
                }
                if (!s->els.empty()) {
                    LuaValue r;
                    return execBlock(s->els, r);
                }
                return false;
            }
            case LStmt::While: {
                int guard = 0;
                while (evalExpr(s->cond).truthy()) {
                    LuaValue r;
                    if (execBlock(s->body, r)) return true;
                    if (++guard > 1000000) break;
                }
                return false;
            }
        }
        return false;
    }

    LuaValue evalExpr(const LE& e) {
        switch (e->k) {
            case LExpr::Num: return LuaValue::numV(e->num);
            case LExpr::Str: return LuaValue::strV(e->s);
            case LExpr::Bool: return LuaValue::boolV(e->b);
            case LExpr::Nil: return LuaValue();
            case LExpr::Name: {
                if (e->s == "true") return LuaValue::boolV(true);
                if (e->s == "false") return LuaValue::boolV(false);
                if (e->s == "nil") return LuaValue();
                auto it = globals.find(e->s);
                if (it != globals.end()) return it->second;
                return LuaValue();
            }
            case LExpr::Call: {
                std::vector<LuaValue> args;
                for (auto& a : e->args) args.push_back(evalExpr(a));
                auto fi = funcs.find(e->s);
                if (fi != funcs.end()) return callFunc(fi->second, args);
                auto ni = natives.find(e->s);
                if (ni != natives.end()) return ni->second(*this, args);
                std::cout << "[Lua] unknown function: " << e->s << "\n";
                return LuaValue();
            }
            case LExpr::Un: {
                LuaValue v = evalExpr(e->l);
                if (e->op == "-") return LuaValue::numV(-v.num);
                return LuaValue::boolV(!v.truthy());
            }
            case LExpr::Bin: {
                if (e->op == "and") {
                    LuaValue l = evalExpr(e->l);
                    return l.truthy() ? evalExpr(e->r) : l;
                }
                if (e->op == "or") {
                    LuaValue l = evalExpr(e->l);
                    return l.truthy() ? l : evalExpr(e->r);
                }
                LuaValue l = evalExpr(e->l);
                LuaValue r = evalExpr(e->r);
                const std::string& op = e->op;
                if (op == "..") return LuaValue::strV(l.toString() + r.toString());
                if (op == "+") return LuaValue::numV(l.num + r.num);
                if (op == "-") return LuaValue::numV(l.num - r.num);
                if (op == "*") return LuaValue::numV(l.num * r.num);
                if (op == "/") return LuaValue::numV(r.num != 0 ? l.num / r.num : 0);
                if (op == "%") return LuaValue::numV(r.num != 0 ? std::fmod(l.num, r.num) : 0);
                if (op == "==") return LuaValue::boolV(l.num == r.num || (l.type == LuaValue::Str && l.str == r.str));
                if (op == "~=") return LuaValue::boolV(!(l.num == r.num || (l.type == LuaValue::Str && l.str == r.str)));
                if (op == "<") return LuaValue::boolV(l.num < r.num);
                if (op == ">") return LuaValue::boolV(l.num > r.num);
                if (op == "<=") return LuaValue::boolV(l.num <= r.num);
                if (op == ">=") return LuaValue::boolV(l.num >= r.num);
                return LuaValue();
            }
        }
        return LuaValue();
    }

private:
    int steps_ = 0;
};

} // namespace suka