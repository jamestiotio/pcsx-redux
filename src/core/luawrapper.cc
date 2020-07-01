/***************************************************************************
 *   Copyright (C) 2019 PCSX-Redux authors                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

#include "core/luawrapper.h"

#include <assert.h>

static int callwrap(lua_State* raw, lua_CFunction func) {
    PCSX::Lua L(raw);
    int r = 0;

    try {
        r = func(raw);
    } catch (std::exception& e) {
        L.error(std::string("LuaException: ") + e.what());
    }
    return r;
}

PCSX::Lua::Lua() : L(lua_open()) {
    assert(("Couldn't create Lua VM", L));
    setCallWrap(callwrap);
}

PCSX::Lua& PCSX::Lua::operator=(Lua&& oL) noexcept {
    if (this == &oL) return *this;

    assert(("Can't assign a Lua VM to another one.", !L));

    L = oL.L;
    oL.L = nullptr;

    return *this;
}

void PCSX::Lua::close() {
    assert(("Can't close an already closed VM", L));

    lua_close(L);
    L = nullptr;
}

void PCSX::Lua::open_base() {
    int n = gettop();
    luaopen_base(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_table() {
    int n = gettop();
    luaopen_table(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_string() {
    int n = gettop();
    luaopen_string(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_math() {
    int n = gettop();
    luaopen_math(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_debug() {
    int n = gettop();
    luaopen_debug(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_bit() {
    int n = gettop();
    luaopen_bit(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_jit() {
    int n = gettop();
    luaopen_jit(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::open_ffi() {
    int n = gettop();
    luaopen_ffi(L);
    while (n < gettop()) pop();
}

void PCSX::Lua::setCallWrap(lua_CallWrapper wrapper) {
    push((void*)wrapper);
    luaJIT_setmode(L, -1, LUAJIT_MODE_WRAPCFUNC | LUAJIT_MODE_ON);
    pop();
}

void PCSX::Lua::declareFunc(const char* name, lua_CFunction f, int i) {
    checkstack(2);
    lua_pushstring(L, name);
    lua_pushcfunction(L, f);
    if ((i < 0) && (i > LUA_REGISTRYINDEX)) i += 2;
    lua_settable(L, i);
}

void PCSX::Lua::call(const char* f, int i, int nargs) {
    checkstack(1);
    lua_pushstring(L, f);
    lua_gettable(L, i);
    lua_insert(L, -1 - nargs);
    int r = lua_resume(L, nargs);

    if ((r == LUA_YIELD) || (r == 0)) return;

    pushLuaContext();

    switch (r) {
        case LUA_ERRRUN:
            throw std::runtime_error("Runtime error while running LUA code.");
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error while running LUA code.");
        case LUA_ERRERR:
            throw std::runtime_error("Error in Error function.");
        case LUA_ERRSYNTAX:
            throw std::runtime_error("Syntax error in Lua code.");
        default:
            throw std::runtime_error(std::string("Unknow error while running LUA code (err code: ") +
                                     std::to_string(r) + ")");
    }
}

void PCSX::Lua::pcall(int nargs) {
    int r = lua_pcall(L, nargs, LUA_MULTRET, 0);

    if (r == 0) return;

    pushLuaContext();

    switch (r) {
        case LUA_ERRRUN:
            throw std::runtime_error("Runtime error while running LUA code.");
        case LUA_ERRMEM:
            throw std::runtime_error("Memory allocation error while running LUA code.");
        case LUA_ERRERR:
            throw std::runtime_error("Error in Error function.");
        case LUA_ERRSYNTAX:
            throw std::runtime_error("Syntax error in Lua code.");
        default:
            throw std::runtime_error(std::string("Unknow error while running LUA code (err code: ") +
                                     std::to_string(r) + ")");
    }
}

void PCSX::Lua::settable(int i, bool raw) {
    if (raw) {
        lua_rawset(L, i);
    } else {
        lua_settable(L, i);
    }
}

void PCSX::Lua::gettable(int i, bool raw) {
    if (raw) {
        lua_rawget(L, i);
    } else {
        lua_gettable(L, i);
    }
}

void PCSX::Lua::getglobal(const char* name) {
    push(name);
    gettable(LUA_GLOBALSINDEX);
}

void PCSX::Lua::pushLuaContext() {
    std::string whole_msg;
    struct lua_Debug ar;
    bool got_error = false;
    int level = 0;

    do {
        if (lua_getstack(L, level, &ar) == 1) {
            if (lua_getinfo(L, "nSl", &ar) != 0) {
                push(std::string("at ") + ar.source + ":" + std::to_string(ar.currentline) + " (" +
                     (ar.name ? ar.name : "[top]") + ")");
            } else {
                got_error = true;
            }
        } else {
            got_error = true;
        }
        level++;
    } while (!got_error);
}

void PCSX::Lua::error(const char* msg) {
    push(msg);

    if (yielded()) {
        pushLuaContext();

        throw std::runtime_error("Runtime error while running yielded C code.");
    } else {
        lua_error(L);
    }
}

bool PCSX::Lua::isobject(int i) {
    bool r = false;
    if (istable(i)) {
        push("__obj");
        gettable(i);
        r = isuserdata();
        pop();
    } else {
        r = isnil(i);
    }
    return r;
}

std::string PCSX::Lua::tostring(int i) {
    switch (type(i)) {
        case LUA_TNIL:
            return "(nil)";
        case LUA_TBOOLEAN:
            return toboolean(i) ? "true" : "false";
        case LUA_TNUMBER:
            return std::to_string(tonumber(i));
        default: {
            size_t l;
            const char* const r = lua_tolstring(L, i, &l);
            return std::string(r, l);
        }
    }
    return "<lua-NULL>";
}

std::string PCSX::Lua::escapeString(const std::string& s) {
    std::string r = "";
    int i;
    for (i = 0; i < s.size(); i++) {
        switch (s[i]) {
            case '"':
            case '\\':
                r += '\\';
                r += s[i];
                break;
            case '\n':
                r += "\\n";
                break;
            case '\r':
                r += "\\r";
                break;
            case '\0':
                r += "\\000";
                break;
            default:
                r += s[i];
        }
    }
    return r;
}
