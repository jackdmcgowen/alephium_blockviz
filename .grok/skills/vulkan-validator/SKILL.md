---
name: vulkan-validator
description: Before committing or pushing Vulkan/graphics changes, grep debug/log output for Vulkan Validation IDs (VUID strings ending in 4 digits), surface each hit, and plan+fix them before any git push or commit of related code. After fixes, delete the debug log and re-run the app to verify VUIDs are gone. Use when the user runs /vulkan-validator, asks to check validation, pre-push, VUID, validation layers, or "is it safe to push".
---

# Vulkan Validator (pre-commit / pre-push gate)

Hard gate for this repo: **do not commit or push graphics/Vulkan work while unresolved validation VUIDs remain.**

## When to run

- Before `git commit` or `git push` that touches engine/graphics/Vulkan paths
- After the app was run with validation layers enabled
- When the user reports validation spam, MSAA/sample mismatches, layout errors, etc.
- Explicitly via `/vulkan-validator`
- **After any validation fix** — always re-verify with a clean log (Step 5)

## Step 1 — Collect debug output

Search typical places (use what exists; do not invent paths):

| Source | Examples |
|--------|----------|
| Repo logs | `build/debug.log`, `debug.log`, `*.log` under `build/` |
| Console capture | Recent terminal / session output if the user pasted it |
| App cwd | Working directory used when launching the visualizer (repo root) |

**Primary log for this project:** `build/debug.log` (often cumulative across runs — treat as **stale** after code fixes until deleted and regenerated).

If no log is available and the user did not paste output:

1. Ask them to run the app once with validation enabled, **or** launch the Debug binary yourself from the repo root (Step 5).
2. **Do not push** while validation status is unknown after graphics changes.

## Step 2 — Grep for VUIDs

Match Validation Unique IDs. Primary pattern: **`VUID` text ending in 4 digits**.

Preferred ripgrep patterns (run from repo root):

```text
VUID-[A-Za-z0-9_-]*[0-9]{4}
```

Also catch looser forms:

```text
VUID-.*[0-9]{4}
```

```text
Validation Error|validation layer|vkCmd|VUID-
```

Scan:

- Pasted user messages
- Log files found in Step 1
- Any `*.log` under `build/` if present

**Record every distinct VUID** (e.g. `VUID-VkRenderingInfo-multisampledRenderToSingleSampled-06857`). Deduplicate by full VUID string.

**Note:** Hits in an old `build/debug.log` may predate a fix. After implementing fixes, **never** declare PASS from a pre-fix log — run Step 5 (delete + re-run + re-grep).

## Step 3 — Gate

| Finding | Action |
|---------|--------|
| **Any VUID hit** in a **fresh** log (or user paste of current run) | **BLOCK** commit/push. Proceed to Step 4. |
| **No VUID hits** in a **fresh** post-fix log | Gate **passes** for this check. |
| **Stale log only** (not deleted/regenerated after fixes) | **Incomplete** — run Step 5 before PASS. |
| **No logs available** after graphics changes | **Incomplete** — do not push until a validation-clean run is confirmed. |

Never dismiss VUIDs as “noise” without a documented false-positive reason and user agreement.

## Step 4 — Plan and implement the fix

For each distinct VUID:

1. **Quote** the VUID id and the one-line validation summary (attachment/sample/layout/stage).
2. **Locate** the call site / resource (e.g. `vkCmdBeginRendering`, pipeline `rasterizationSamples`, image create samples).
3. **Root cause** in one sentence (mismatched sample counts, wrong layout, missing barrier, etc.).
4. **Fix plan** — files + intended change (enter plan mode or write a short plan when multi-file / ambiguous).
5. **Implement** the fix; rebuild Debug|x64.
6. **Mandatory re-verify (Step 5)** — delete debug log, re-run app, re-grep. VUID must be gone.

Do **not** `git commit` or `git push` until every previously found VUID is cleared on a **fresh** log (or the user explicitly defers a listed VUID with a tracked follow-up — default is fix-first).

## Step 5 — Delete log and re-run to verify (required after fixes)

After any code change intended to clear VUIDs, **always** do a clean verification. Do not trust a cumulative log.

### 5.1 Stop the app if running

```powershell
Stop-Process -Name alephium_visualizer -Force -ErrorAction SilentlyContinue
```

### 5.2 Delete (or truncate) the debug log

From repo root (`alephium_blockviz`):

```powershell
Remove-Item -Force -ErrorAction SilentlyContinue "build\debug.log"
Remove-Item -Force -ErrorAction SilentlyContinue "debug.log"
```

Also clear any other validation captures you used if they would mix old and new output.

### 5.3 Ensure a current binary

Rebuild if sources changed since the last EXE write:

```text
MSBuild sln\alephium_visualizer.sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```

Confirm EXE is newer than the fixed sources when possible.

### 5.4 Run the app to regenerate the log

- Working directory: **repo root** (so `build/debug.log` and shaders resolve).
- Binary: `build\alephium_visualizer\Debug\alephium_visualizer.exe`
- Exercise the code path that triggered the VUID (e.g. hover/pick for picker samples; **select a block** for async Sobel; resize/minimize for swapchain).
- Let it run long enough to hit the path (a few seconds + the user action).
- Exit the app cleanly if practical so the log flushes.

If the agent cannot drive UI interactions, instruct the user which actions to perform, wait for them to exit, then continue 5.5.

### 5.5 Re-grep the new log

```text
VUID-[A-Za-z0-9_-]*[0-9]{4}
```

against the **new** `build/debug.log` only.

| Result | Action |
|--------|--------|
| **No VUIDs** (or only unrelated deferred IDs the user accepted) | Step 5 **PASS** for fixed clusters |
| **Same VUID returns** | Fix incomplete — return to Step 4 |
| **New VUIDs appear** | Record them; plan/fix before push |
| **Log missing after run** | Incomplete — check cwd, validation enablement, logging path |

### 5.6 Compare before/after

Report:

- VUIDs **before** fix (from stale/original scan)
- VUIDs **after** clean re-run
- Which IDs were **cleared** vs **remaining**

## Step 6 — Commit / push policy

Only after the gate passes on a **fresh** log:

1. Summarize: VUIDs found → fixed → verified by delete+re-run.
2. Then commit/push if the user requested it.

If the user asks to push while VUIDs remain on a fresh log:

- Refuse the push for validation-related work.
- List remaining VUIDs and the next fix step.

## Common VUID families in this project

| Symptom | Likely area |
|---------|-------------|
| Sample count mismatch (color vs depth vs pipeline) | MSAA targets, picker 1× depth, pipeline `rasterizationSamples` |
| Layout / access | Frame barriers, resolve, present transition |
| FRAGMENT stage on compute queue | Async Sobel CMP barriers (`sobel_compute`) |
| CB reset/begin while pending | Per-frame compute/overlay CBs + timeline wait |
| Dynamic rendering attachment formats | Pipeline rendering info vs `VkRenderingInfo` |

## Output format (report to user)

```markdown
## Vulkan validation gate
- Logs scanned: <paths or "user paste">
- Fresh log?: yes (deleted + re-ran) | no (stale / first scan)
- VUIDs found: <none | list>
- Status: PASS | BLOCK | INCOMPLETE
- Fixes: <none | plan/status per VUID>
- Re-verify: <not run | deleted log + ran EXE + re-grep result>
- Commit/push: allowed | blocked
```

## Non-goals

- Disabling validation layers to silence errors
- Declaring PASS from a log that still contains pre-fix history
- Pushing with “fix later” unless the user overrides in writing
- Full Vulkan spec tutoring — link the VUID and fix the code
