#pragma once

#include <cstdio>

#ifdef SMART_GRIND_SIM
#define BUILD_NUMBER 0
#define GIT_COMMIT_ID "desktop-simulator"
#define GIT_BRANCH "desktop-simulator"
#else
#include "git_info.h"
#endif

// Build information - BUILD_FIRMWARE_VERSION is automatically updated by release scripts
#define BUILD_FIRMWARE_VERSION "1.6.2"                                          // Firmware version string (updated by release automation)
// BUILD_NUMBER is now defined in git_info.h as an auto-incrementing integer

// GitHub repository whose releases this firmware offers as updates, and the
// GitHub Pages site that mirrors their binaries (deployed by the web flasher
// workflow). Change both together when publishing from another fork.
#define BUILD_RELEASE_REPOSITORY "marvin-rse/smart-grind-by-weight"
#define BUILD_RELEASE_PAGES_URL "https://marvin-rse.github.io/smart-grind-by-weight/"
#define BUILD_DATE __DATE__                                                // Compiler-provided build date
#define BUILD_TIME __TIME__                                                // Compiler-provided build time

inline const char* get_git_commit_id() {
    return GIT_COMMIT_ID;
}

inline const char* get_git_branch() {
    return GIT_BRANCH;
}

inline const char* get_build_datetime() {
    static char datetime[32];
    snprintf(datetime, sizeof(datetime), "%s %s", __DATE__, __TIME__);
    return datetime;
}
