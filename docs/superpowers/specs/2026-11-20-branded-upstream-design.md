# Branded upstream branch: take each Creality release with a normal merge

## Intent
- SanityPrint follows every CrealityPrint release.
- Releases come a month to a year apart.
- Each release must be routine work: one merge, a few real conflicts.
- Today each release is a full refresh; the 7.1.1 → 7.3.2 refresh lost features (filament sync button, feedback-button removal).
- Cause: `main` and Creality share no history after v7.1.1, and the CrealityPrint → SanityPrint rename touches thousands of lines.

## Goals
- A `creality-branded` branch holds each Creality release with SanityPrint branding applied by scripts.
- `main` takes a release with `git merge creality-branded`.
- The rename and the recolor never cause a merge conflict.
- A merge conflicts only where Creality and `main` both edited the same lines on purpose.
- `git describe --tags` on `main` finds the Creality tag (`version.inc` needs it).

## Non-goals
- No automatic detection of new Creality releases.
- No hosted CI for this flow.
- No change to how features are built on `main`.
- No rework of the filament-profile edits; they merge as normal edits.

## Anti-goals
- A hand edit on `creality-branded`: that branch holds only tag + script output.
- A rewrite of `main`'s history.
- A branding script whose output differs between two runs on the same input.

## Constraints
- Windows host; Git for Windows, Python 3, PowerShell 5.1.
- Branding scripts run on a checkout that is not the script's own repo root.
- `version.inc` reads `creality_release.txt`; `scripts/update_creality_build.ps1` writes it.
- Merges to `main` follow `reviewBeforeMain: agent` and the pre-push merge tier.

## Approaches
- **Picked — branded upstream branch, normal merge.**
  - Branding scripts turn each Creality tag into a branded tree on `creality-branded`.
  - `main` merges that branch; the merge base is the last branded release.
  - Trade-off: all permanent branding must be script-driven.
- **Rejected — patch stack rebased onto each tag.**
  - Small, named conflicts.
  - Rewrites `main` every release; bad for worktrees and pushed work.
- **Rejected — full refresh each release.**
  - No setup.
  - Slow, and it loses features.
- The owner picked the first approach (2026-11-20).

## Design

### What is branding and what is a feature
- Branding = a whole-tree mechanical change. It goes in a script.
  - Text rename (`scripts/rename_app.py`), including its `FILE_RENAMES`.
  - Color swap (`scripts/recolor_blue_steel.py`).
  - Binary asset swap: logos, icons, splash (from commits 29e4acbc, 20993ac1).
- A feature = a semantic code edit. It stays a normal commit on `main`.
  - Filament sync, templates, material fields.
  - No cloud, no Creality links, no feedback button (commit 7d5c91a9).
  - These merge like any code; a conflict means Creality changed the same lines.
- Builder's call: move a 7d5c91a9 edit into a script only when a script can express it as a pure replacement.

### Branding driver
- New `scripts/brand_tree.py --root <dir>`.
  - Runs, in order: rename, recolor, asset swap.
  - Applies to `<dir>`, never to the script's own repo.
  - Exits non-zero, naming the step, when a step fails.
  - Prints one count line per step, then `brand_tree: <n> files changed`.
- `rename_app.py` and `recolor_blue_steel.py` take `--root`; the default stays the repo root.
- Asset swap copies `branding/assets/<path>` over `<root>/<path>` for every file in `branding/assets/`.
  - `branding/assets/` holds the SanityPrint logo, icon and splash files.
  - Paths are post-rename paths (my call), because the swap runs after the rename.
  - The set = every binary or SVG file that 29e4acbc and 20993ac1 changed, at its name on `upstream-refresh`.
- Idempotent: a second run on its own output changes nothing.
- `--check` mode: exit non-zero when a second run would change something.
- Leak audit (owner, 2026-11-20: "List them, don't stop").
  - After the steps, list each remaining Creality app-name string and known Creality green, with file and line.
  - It never fails the run; it prints `brand_tree: <n> leaks`.
  - An allow-list file names hits that are correct, e.g. the Creality printer vendor and `resources/profiles/Creality`.
  - The runbook: fix the script for each real leak before the merge to `main`.

### Branch guard (owner, 2026-11-20: "Pre-push guard")
- A pre-push hook check runs for each pushed commit on `creality-branded`.
- For each new commit, it re-runs `brand_tree.py` on that commit's Creality-tag parent.
- It refuses the push when the result tree differs from the commit's tree.
- Its refusal names the commit and the first differing path, and the fix: `brand_release.ps1 <tag>`.
- It adds to the existing `.githooks` pre-push hook; pushes of other branches skip it.
- It uses the branding scripts of the pushing checkout; it checks only commits new to the remote, so a later script change never fails old commits.

### Branded commit
- New `scripts/brand_release.ps1 <tag>`.
  1. Make a temporary worktree at `<tag>`, detached.
  2. Run `brand_tree.py --root <worktree>`.
  3. `git add -A` in that temporary worktree only.
  4. Build the commit with `git commit-tree`: tree = branded tree, parents = current `creality-branded` tip and `<tag>`.
  5. Move `creality-branded` to the new commit.
  6. Delete the temporary worktree.
- The first branded commit has one parent: `<tag>`.
- The tree never comes from a merge resolution, so this step has no conflicts.
- Message: `brand: Creality <tag> with SanityPrint branding (brand_tree <n> files)`.
- Refuses when `<tag>` is missing.
- Refuses when the new tree equals the current tip's tree (nothing to commit).
- Rebrand (my call): when a branding script changes, run `brand_release.ps1 <current tag>` again.
  - It makes a new branded commit of the same tag; parents = tip and tag.
  - Do this before branding the next tag, so `main` merges the script change on its own.
- Line endings (my call): the temporary worktree uses `core.autocrlf=false`; `text_io.py` keeps each file's endings.

### Taking a release on `main`
1. `git fetch` Creality's tags (remote `upstream`).
2. `powershell -File scripts/brand_release.ps1 <tag>`.
3. In a worktree of `main`: `git merge creality-branded`.
4. Resolve real conflicts; use `scripts/json_merge3.py` for profile JSON.
5. `powershell -File scripts/update_creality_build.ps1`; commit `creality_release.txt`.
6. Build, run, check the title bar, the AI button, filament sync.
7. Review agent, then merge to `main` and push.
- Write this as a runbook in `docs/` and as a project skill.

### Bootstrap (one time)
1. `brand_release.ps1 v7.3.2` makes the first branded commit.
2. Merge `creality-branded` into `upstream-refresh`.
   - Base = v7.3.2; both sides already hold the rename and recolor.
   - Expect few conflicts; each one shows where the scripts and the refresh disagree.
   - Fix the script, not the merge, where the script is wrong; rebuild the branded commit.
   - Stop rule (my call): a conflict with a cause other than branding goes to the owner, listed by file, before it is resolved.
3. Finish the pending promotion: `git merge -s ours main` on `upstream-refresh`, then fast-forward `main`.
4. Push `creality-branded`.
- After this, `main` contains `creality-branded`, so the next release merges from v7.3.2's branded commit.

### Verification
- `brand_tree.py --check` on the branded tree exits 0.
- Fixture test: a small tree with known Creality strings, colors and an asset; expected output after one run and after two runs.
- After bootstrap: `git diff creality-branded upstream-refresh --stat` lists only feature files, not branding.
- `git describe --tags --abbrev=0` on `main` prints `v7.3.2`.
- The app starts; version 7.3.2.6412; the AI button works.

## Left to the builder
- Exact asset list for `branding/assets/`; read it from 29e4acbc and 20993ac1.
- Fixture contents.
- Runbook wording and skill name.
- Whether `brand_release.ps1` is PowerShell or Python.

## Glossary
- **merge base**: the commit git compares both sides against; here it is the last branded release, which is why the rename never conflicts.
- **`git commit-tree`**: builds a commit from a tree and chosen parents; this is why the branded commit needs no merge.
- **idempotent**: running twice gives the same tree as running once; `--check` tests this.
- **`-s ours`**: a merge that keeps our tree and only records the other side as merged; it joins the old `main` history.
- **merge tier**: the pre-push check level for a push to `main` (`build_check.ps1 -Merge`).
