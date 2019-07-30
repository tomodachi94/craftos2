#include <lua.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <pthread.h>
#include <string>
#include <vector>
#include <sstream>

const char * rom_path = "/usr/share/craftos";
const char * bios_path = "/usr/share/craftos/bios.lua";
#ifdef FS_ROOT
const char * base_path = "";
#else
const char * base_path = "$HOME/.craftos/computer/0";
#endif
char * base_path_expanded;

std::vector<std::string> split(std::string strToSplit, char delimeter) 
{
    std::stringstream ss(strToSplit);
    std::string item;
    std::vector<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter))
    {
       splittedStrings.push_back(item);
    }
    return splittedStrings;
}

extern "C" {

char * fixpath(const char * path) {
    std::vector<std::string> elems = split(path, '/');
    std::vector<std::string> pathc;
    for (std::string s : elems) {
        if (s == "..") {if (pathc.size() < 1) return NULL; else pathc.pop_back();}
        else if (s != "." && s != "") pathc.push_back(s);
    }
    const char * bp = (pathc.size() > 0 && pathc[0] == "rom") ? rom_path : expandEnvironment(base_path);
    std::stringstream ss;
    ss << bp;
    for (std::string s : pathc) ss << "/" << s;
    //if (bp != base_path_expanded) free(bp);
    std::string retstr = ss.str();
    char * retval = (char*)malloc(retstr.size() + 1);
    strcpy(retval, retstr.c_str());
    //printf("%s\n", retval);
    return retval;
}

char * expandEnvironment(const char * src) {
    if (base_path_expanded != NULL && std::string(src) == std::string(base_path)) return base_path_expanded;
    wordexp_t p;
    wordexp(src, &p, 0);
    int size = 0;
    for (int i = 0; i < p.we_wordc; i++) size += strlen(p.we_wordv[i]);
    char * retval = (char*)malloc(size + 1);
    strcpy(retval, p.we_wordv[0]);
    for (int i = 1; i < p.we_wordc; i++) strcat(retval, p.we_wordv[i]);
    if (std::string(src) == std::string(base_path)) base_path_expanded = retval;
    return retval;
}

void * createThread(void*(*func)(void*), void* arg) {
    pthread_t * tid = new pthread_t;
    pthread_create(tid, NULL, func, arg);
    return (void*)tid;
}

void joinThread(void* thread) {
    pthread_join(*(pthread_t*)thread, NULL);
}

int createDirectory(const char * path) {
    if (mkdir(path, 0777) != 0) {
        if (errno == ENOENT && strcmp(path, "/") != 0) {
            char * dir = (char*)malloc(strlen(path) + 1);
            strcpy(dir, path);
            if (createDirectory(dirname(dir))) return 1;
            free(dir);
            mkdir(path, 0777);
        } else return 1;
    }
    return 0;
}

int getUptime() {
    struct sysinfo info;
    sysinfo(&info);
    return info.uptime;
}

void msleep(unsigned long time) {
    usleep(time * 1000);
}

unsigned long long getFreeSpace(char* path) {
	struct statvfs st;
	if (statvfs(path, &st) != 0) err(L, path, "No such file or directory");
	return st.f_bavail * st.f_bsize;
}

void platform_fs_find(lua_State* L, char* wildcard) {
	glob_t g;
	int rval = 0;
	rval = glob(wildcard, 0, NULL, &g);
	if (rval == 0) {
		for (int i = 0; i < g.gl_pathc; i++) {
			lua_pushnumber(L, i + 1);
			lua_pushstring(L, g.gl_pathv[i]);
			lua_settable(L, -3);
		}
		globfree(&g);
	}
}
}