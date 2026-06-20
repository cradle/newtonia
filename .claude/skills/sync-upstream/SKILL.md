---
name: sync-upstream
description: Sync the local master branch with the upstream Newtonia repo. Use when the user wants to fetch the latest upstream (https://github.com/cradle/newtonia.git), merge it into local master, resolve any conflicts, and push. Triggers like "sync upstream", "pull latest upstream", "merge upstream master", "update master from upstream".
---

# Sync Upstream

Fetch the latest `master` from the upstream Newtonia repository, merge it into
the local `master`, resolve any conflicts, and push the result to `origin`.

Upstream URL: `https://github.com/cradle/newtonia.git`

## Steps

1. **Ensure the `upstream` remote exists** and points at the canonical repo.
   Add it if missing (it usually is, since `origin` is the fork/work repo):

   ```sh
   git remote get-url upstream 2>/dev/null \
     || git remote add upstream https://github.com/cradle/newtonia.git
   ```

   If `upstream` exists but points elsewhere, fix it:

   ```sh
   git remote set-url upstream https://github.com/cradle/newtonia.git
   ```

2. **Fetch upstream `master`** (retry up to 4× with exponential backoff —
   2s, 4s, 8s, 16s — on network failure only):

   ```sh
   git fetch upstream master
   ```

3. **Check out local `master`** and make sure it is current with `origin`:

   ```sh
   git checkout master
   git pull origin master
   ```

   If a local `master` does not exist, create it tracking `origin/master`:

   ```sh
   git checkout -b master origin/master
   ```

4. **Merge upstream into local master:**

   ```sh
   git merge upstream/master
   ```

5. **Resolve conflicts if the merge stops.**
   - List them: `git status --short | grep '^UU\|^AA\|^DD'` (or `git diff --name-only --diff-filter=U`).
   - Open each conflicted file, reconcile both sides on its merits — preserve
     the intent of both the upstream change and the local Xbox-port work.
     Do **not** blindly take one side.
   - Respect this repo's conventions in CLAUDE.md (GL-prefix pattern,
     weapon/pickup pairs, C++11, platform abstraction via `gl_compat.h`, etc.).
   - After editing, stage and complete the merge:

     ```sh
     git add <resolved-files>
     git commit --no-edit   # keeps the default merge commit message
     ```

   - If conflicts are extensive or their resolution is genuinely ambiguous
     (the two sides make incompatible design choices), STOP and ask the user
     with `AskUserQuestion` rather than guessing.

6. **Sanity-check the merge** before pushing. At minimum confirm the tree is
   clean and, when C/C++ files changed, syntax-check the touched ones the way
   the pre-commit hook does:

   ```sh
   g++ -std=c++11 -fsyntax-only -I. -I/usr/include/SDL2 <changed-file.cpp>
   ```

7. **Push to origin** (retry up to 4× with exponential backoff on network
   failure only):

   ```sh
   git push -u origin master
   ```

## Notes

- Never force-push `master`. If the push is rejected as non-fast-forward,
  re-`pull origin master`, re-merge, and try again — do not `--force`.
- Do **not** open a pull request unless the user explicitly asks for one.
- If the merge is already up to date (`Already up to date.`), report that and
  skip the push.
- Report a clear summary at the end: what was fetched, whether conflicts
  occurred and how they were resolved, and the push result.
