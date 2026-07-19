# VnV · mod (module tests)

Non-graphics, CPU-only verification.

## Layout rule (required)

```text
vnv/mod/tests/<area>/<test_id>/test.cpp
vnv/mod/tests/<area>/<test_id>/expected/   # optional, committed reference files
vnv/mod/tests/<area>/<test_id>/out/        # optional, gitignored runtime dumps
vnv/mod/tests/<area>/_shared/              # fixtures only (not a test)
vnv/mod/tests/<area>/main.cpp              # suite driver
```

- **One test = one subfolder** under the area (`domain/`, `network/`, …).  
- Primary file is always **`test.cpp`** (MSVC needs unique `ObjectFileName` in the vcxproj).  
- Create **`expected/`** only when the test needs reference files.  
- Do not drop multi-test `.cpp` files at the area root.

| Project | Sources | Binary |
|---------|---------|--------|
| `mod_domain` | `vnv/mod/tests/domain/` | `build/mod_domain/<Config>/mod_domain.exe` |
| `mod_network` | `vnv/mod/tests/network/` | `build/mod_network/<Config>/mod_network.exe` |

```powershell
.\scripts\run_vnv.ps1          # default runs all mod
.\scripts\run_vnv.ps1 -Mod
```
