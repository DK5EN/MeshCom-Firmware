// test/support/repo_file.h -- the one way a native suite finds and reads a
// file of the repository (TESTGATE-01 step 3.3).
//
// Before this header five different root finders lived in the suites, and
// two readers returned an empty string for a file they could not open, so a
// text pin on a moved file passed or failed for the wrong reason.
//
// Include it by relative path ("../support/repo_file.h"): not every native
// env has -I test/support.
//
//   repo_root()             repository root (the directory with platformio.ini)
//   repo_path(rel)          absolute path of a repo-relative path
//   repo_fopen(rel, mode)   FILE * or NULL -- for optional files
//   repo_try_slurp(rel, s)  false when the file cannot be read -- for optional files
//   repo_slurp(rel)         whole file; FAILS THE TEST when it cannot be read
#pragma once

#include <cstdio>
#include <string>
#include <unistd.h>

#include <unity.h>

inline bool repo_file_readable_(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

inline std::string repo_root_from_(std::string dir)
{
    for (int up = 0; up < 12 && !dir.empty(); up++)
    {
        if (repo_file_readable_(dir + "/platformio.ini"))
            return dir;
        const size_t cut = dir.find_last_of('/');
        if (cut == std::string::npos)
            break;
        dir.erase(cut);
    }
    return std::string();
}

// The root is searched upwards from the working directory (pio runs a test
// from the project root, a debugger may not) and then from this header's own
// path. No root is a failed test, never an empty path.
inline const std::string &repo_root()
{
    static std::string root;
    if (!root.empty())
        return root;
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        root = repo_root_from_(cwd);
    if (root.empty())
    {
        std::string here = __FILE__;
        if (!here.empty() && here[0] != '/' && getcwd(cwd, sizeof(cwd)) != NULL)
            here = std::string(cwd) + "/" + here;
        const size_t cut = here.find_last_of('/');
        if (cut != std::string::npos)
            root = repo_root_from_(here.substr(0, cut));
    }
    if (root.empty())
        TEST_FAIL_MESSAGE("repo_file.h: no platformio.ini above the working directory or above this header");
    return root;
}

inline std::string repo_path(const char *rel)
{
    return repo_root() + "/" + rel;
}

inline FILE *repo_fopen(const char *rel, const char *mode = "r")
{
    return fopen(repo_path(rel).c_str(), mode);
}

inline bool repo_try_slurp(const char *rel, std::string &out)
{
    out.clear();
    FILE *f = repo_fopen(rel, "rb");
    if (f == NULL)
        return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    const bool ok = !ferror(f);
    fclose(f);
    return ok;
}

inline std::string repo_slurp(const char *rel)
{
    std::string out;
    if (!repo_try_slurp(rel, out))
    {
        static char msg[512];
        snprintf(msg, sizeof(msg), "repo_file.h: cannot read %s", rel);
        TEST_FAIL_MESSAGE(msg);
    }
    return out;
}
