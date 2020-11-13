/*
 * lib.cpp
 * CraftOS-PC 2
 * 
 * This file implements convenience functions for libraries.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2020 JackMacWindows.
 */

#define CRAFTOSPC_INTERNAL
#include "util.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <sstream>
#include <Poco/Base64Decoder.h>
#include <Poco/Base64Encoder.h>
#include <Computer.hpp>
#include "platform.hpp"
#include "terminal/SDLTerminal.hpp"
#include "runtime.hpp"
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#ifdef WIN32
#define PATH_SEP L"\\"
#define PATH_SEPC '\\'
#else
#include <libgen.h>
#define PATH_SEP "/"
#define PATH_SEPC '/'
#endif

char computer_key = 'C';
void* getCompCache_glob = NULL;
Computer * getCompCache_comp = NULL;

Computer * _get_comp(lua_State *L) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, 1);
    void * retval = lua_touserdata(L, -1);
    lua_pop(L, 1);
    getCompCache_glob = *(void**)(((ptrdiff_t)L) + sizeof(void*)*3 + 3 + alignof(void*) - ((sizeof(void*)*3 + 3) % alignof(void*)));
    getCompCache_comp = (Computer*)retval;
    return (Computer*)retval;
}

void load_library(Computer *comp, lua_State *L, library_t lib) {
    luaL_register(L, lib.name, lib.functions);
    if (lib.init != NULL) lib.init(comp);
}

std::string b64encode(std::string orig) {
    std::stringstream ss;
    Poco::Base64Encoder enc(ss);
    enc.write(orig.c_str(), orig.size());
    enc.close();
    return ss.str();
}

std::string b64decode(std::string orig) {
    std::stringstream ss;
    std::stringstream out(orig);
    Poco::Base64Decoder dec(out);
    std::copy(std::istreambuf_iterator<char>(dec), std::istreambuf_iterator<char>(), std::ostreambuf_iterator<char>(ss));
    return ss.str();
}

std::vector<std::string> split(std::string strToSplit, char delimeter) {
    std::stringstream ss(strToSplit);
    std::string item;
    std::vector<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter)) {
        splittedStrings.push_back(item);
    }
    return splittedStrings;
}

std::vector<std::wstring> split(std::wstring strToSplit, wchar_t delimeter) {
    std::wstringstream ss(strToSplit);
    std::wstring item;
    std::vector<std::wstring> splittedStrings;
    while (std::getline(ss, item, delimeter)) {
        splittedStrings.push_back(item);
    }
    return splittedStrings;
}

static std::string concat(std::list<std::string> &c, char sep) {
    std::stringstream ss;
    bool started = false;
    for (std::string s : c) {
        if (started) ss << sep;
        ss << s;
        started = true;
    }
    return ss.str();
}

static std::list<std::string> split_list(std::string strToSplit, char delimeter) {
    std::stringstream ss(strToSplit);
    std::string item;
    std::list<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter)) {
        splittedStrings.push_back(item);
    }
    return splittedStrings;
}

path_t fixpath_mkdir(Computer * comp, std::string path, bool md, std::string * mountPath) {
    if (md && fixpath_ro(comp, path.c_str())) return path_t();
    std::list<std::string> components = path.find("/") != path_t::npos ? split_list(path, '/') : split_list(path, '\\');
    while (components.size() > 0 && components.front().empty()) components.pop_front();
    if (components.empty()) return fixpath(comp, "", true);
    components.pop_back();
    std::list<std::string> append;
    path_t maxPath = fixpath(comp, concat(components, '/').c_str(), false, true, mountPath);
    while (maxPath.empty()) {
        append.push_front(components.back());
        components.pop_back();
        if (components.empty()) return path_t();
        maxPath = fixpath(comp, concat(components, '/').c_str(), false, true, mountPath);
    }
    if (!md) return maxPath;
    if (createDirectory(maxPath + PATH_SEP + wstr(concat(append, PATH_SEPC))) != 0) return path_t();
    return fixpath(comp, path.c_str(), false, true, mountPath);
}

static bool nothrow(std::function<void()> f) { try { f(); return true; } catch (std::exception &e) { return false; } }

path_t fixpath(Computer *comp, const char * path, bool exists, bool addExt, std::string * mountPath, bool getAllResults, bool * isRoot) {
    std::vector<std::string> elems = split(path, '/');
    std::list<std::string> pathc;
    for (std::string s : elems) {
        if (s == "..") { if (pathc.size() < 1) return path_t(); else pathc.pop_back(); } else if (s != "." && s != "") pathc.push_back(s);
    }
    while (pathc.size() > 0 && pathc.front().empty()) pathc.pop_front();
    if (comp->isDebugger && addExt && pathc.size() == 1 && pathc.front() == "bios.lua")
#ifdef STANDALONE_ROM
        return WS(":bios.lua");
#else
        return getROMPath() + PATH_SEP + WS("bios.lua");
#endif
    pathstream_t ss;
    if (addExt) {
        std::pair<size_t, std::vector<path_t> > max_path = std::make_pair(0, std::vector<path_t>(1, comp->dataDir));
        std::list<std::string> * mount_list = NULL;
        for (auto it = comp->mounts.begin(); it != comp->mounts.end(); it++) {
            if (pathc.size() >= std::get<0>(*it).size() && std::equal(std::get<0>(*it).begin(), std::get<0>(*it).end(), pathc.begin())) {
                if (std::get<0>(*it).size() > max_path.first) {
                    max_path = std::make_pair(std::get<0>(*it).size(), std::vector<path_t>(1, std::get<1>(*it)));
                    mount_list = &std::get<0>(*it);
                } else if (std::get<0>(*it).size() == max_path.first) {
                    max_path.second.push_back(std::get<1>(*it));
                }
            }
        }
        for (size_t i = 0; i < max_path.first; i++) pathc.pop_front();
        if (isRoot != NULL) *isRoot = pathc.empty();
        if (exists) {
            bool found = false;
            for (path_t p : max_path.second) {
                pathstream_t sstmp;
                struct_stat st;
                sstmp << p;
                for (std::string s : pathc) sstmp << PATH_SEP << wstr(s);
                if (
#ifdef STANDALONE_ROM
                (p == WS("rom:") && nothrow([&sstmp]() {standaloneROM.path(sstmp.str()); })) || (p == WS("debug:") && nothrow([&sstmp]() {standaloneDebug.path(sstmp.str()); })) ||
#endif
                    (platform_stat((sstmp.str()).c_str(), &st) == 0)) {
                    if (getAllResults && found) ss << "\n";
                    ss << sstmp.str();
                    found = true;
                    if (!getAllResults) break;
                }
            }
            if (!found) return path_t();
        } else if (pathc.size() > 1) {
            bool found = false;
            std::string back = pathc.back();
            pathc.pop_back();
            for (path_t p : max_path.second) {
                pathstream_t sstmp;
                struct_stat st;
                sstmp << p;
                for (std::string s : pathc) sstmp << PATH_SEP << wstr(s);
                if (
#ifdef STANDALONE_ROM
                (p == WS("rom:") && (nothrow([&sstmp, back]() {standaloneROM.path(sstmp.str() + WS("/") + wstr(back)); }) || (nothrow([&sstmp]() {standaloneROM.path(sstmp.str()); }) && standaloneROM.path(sstmp.str()).isDir))) ||
                    (p == WS("debug:") && (nothrow([&sstmp, back]() {standaloneDebug.path(sstmp.str() + WS("/") + wstr(back)); }) || (nothrow([&sstmp]() {standaloneDebug.path(sstmp.str()); }) && standaloneDebug.path(sstmp.str()).isDir))) ||
#endif
                    (platform_stat((sstmp.str() + PATH_SEP + wstr(back)).c_str(), &st) == 0) || (platform_stat(sstmp.str().c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    ) {
                    if (getAllResults && found) ss << "\n";
                    ss << sstmp.str() << PATH_SEP << wstr(back);
                    found = true;
                    if (!getAllResults) break;
                }
            }
            if (!found) return path_t();
        } else {
            ss << max_path.second.front();
            for (std::string s : pathc) ss << PATH_SEP << wstr(s);
        }
        if (mountPath != NULL) {
            if (mount_list == NULL) *mountPath = "hdd";
            else {
                std::stringstream ss2;
                for (auto it = mount_list->begin(); it != mount_list->end(); it++) {
                    if (it != mount_list->begin()) ss2 << "/";
                    ss2 << *it;
                }
                *mountPath = ss2.str();
            }
        }
    } else for (std::string s : pathc) ss << (ss.tellp() == 0 ? WS("") : WS("/")) << wstr(s);
    return ss.str();
}

bool fixpath_ro(Computer *comp, const char * path) {
    std::vector<std::string> elems = split(path, '/');
    std::list<std::string> pathc;
    for (std::string s : elems) {
        if (s == "..") { if (pathc.size() < 1) return false; else pathc.pop_back(); } else if (s != "." && s != "") pathc.push_back(s);
    }
    std::pair<size_t, bool> max_path = std::make_pair(0, false);
    for (auto it = comp->mounts.begin(); it != comp->mounts.end(); it++)
        if (pathc.size() >= std::get<0>(*it).size() && std::get<0>(*it).size() > max_path.first && std::equal(std::get<0>(*it).begin(), std::get<0>(*it).end(), pathc.begin()))
            max_path = std::make_pair(std::get<0>(*it).size(), std::get<2>(*it));
    return max_path.second;
}

std::set<std::string> getMounts(Computer * computer, const char * comp_path) {
    std::vector<std::string> elems = split(comp_path, '/');
    std::list<std::string> pathc;
    std::set<std::string> retval;
    for (std::string s : elems) {
        if (s == "..") { if (pathc.size() < 1) return retval; else pathc.pop_back(); } else if (s != "." && s != "") pathc.push_back(s);
    }
    for (auto it = computer->mounts.begin(); it != computer->mounts.end(); it++)
        if (pathc.size() + 1 == std::get<0>(*it).size() && std::equal(pathc.begin(), pathc.end(), std::get<0>(*it).begin()))
            retval.insert(std::get<0>(*it).back());
    return retval;
}