#pragma once

// Repo-relative path helpers (CWD must be repo root for VnV).
// expected/ = committed reference; out/ = gitignored working artifacts.

#include <string>

inline std::string vnv_mod_test_dir(const char* area, const char* test_id)
{
    return std::string("vnv/mod/tests/") + area + "/" + test_id;
}

inline std::string vnv_mod_expected_dir(const char* area, const char* test_id)
{
    return vnv_mod_test_dir(area, test_id) + "/expected";
}

inline std::string vnv_mod_out_dir(const char* area, const char* test_id)
{
    return vnv_mod_test_dir(area, test_id) + "/out";
}

inline std::string vnv_mod_expected_file(const char* area, const char* test_id, const char* file)
{
    return vnv_mod_expected_dir(area, test_id) + "/" + file;
}

inline std::string vnv_mod_out_file(const char* area, const char* test_id, const char* file)
{
    return vnv_mod_out_dir(area, test_id) + "/" + file;
}
