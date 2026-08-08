---
name: update-dev-user-logs
description: Writes a matching pair of changelog entries for this project — one technical entry in Dev_Log.md and one plain-language entry in User_Log.md — then commits and pushes both to GitHub. Use this whenever the user asks to update the logs, write a changelog entry, document a recent change, or says "yes" after being asked "want to update the logs?" per this project's CLAUDE.md workflow. Do not use this to write firmware or webpage code — it only records changes that were already made.
---

# Update the Dev Log and User Log

This project keeps two separate changelogs for two different audiences, and this skill's job is to keep them both accurate and in-sync without making the user write two versions of the same update themselves.

Per this project's `CLAUDE.md` workflow rule, this skill should only run after the user has already agreed to update the logs — that agreement is also the permission to commit and push in step 4, so there's no need to ask again partway through.

## The two files and why they're different

- **`Dev_Log.md`** — technical, for you and the user only. Bug details, what broke and why, tooling notes, file/function references, compile errors hit along the way. This is the record that makes future debugging and decisions faster.
- **`User_Log.md`** — plain-language, and it's not just a doc — it's *shown inside the app itself* (the "Last updated" stamp at the bottom of the page opens it in a modal). Anything technical here would be visible to whoever's just trying to use the headset, so it only covers what someone would actually notice or care about: new features, fixes, visible behavior changes. If a change has zero user-visible effect (an internal refactor, a tooling change, a memory/log update like this one), it's fine to skip the User_Log.md entry entirely rather than force one.

## Steps

**1. Read the current top of each file** to match its exact established format — don't guess, the formats differ between the two files:

- `Dev_Log.md` entries look like:
  ```
  ### YYYY-MM-DD — Short title
  **Files:** `path/to/file.ext`
  - Bullet describing what changed and why.
  - Another bullet, technical detail included (function names, root cause, etc).
  ```
  New entries are added in date order near the bottom of the dated-entries section (before the `---` and `## Current state` summary at the end of the file) — read the last 1-2 entries to see exactly where that boundary is before inserting.

- `User_Log.md` entries look like:
  ```
  ## YYYY-MM-DD
  - Plain-language bullet, no jargon.
  ```
  New entries go **newest-first** — right after the `# Nami Headset — Update Log` title line at the top, not appended at the bottom. If there's already an entry for today's date, add bullets to it rather than creating a duplicate date header.

**2. Write the Dev_Log.md entry.** Pull the technical specifics from the actual change: what files were touched, what the bug/goal was, what the fix was, and — when it's non-obvious — why. Match the terse, specific tone of existing entries rather than writing prose paragraphs.

**3. Write the User_Log.md entry** (skip this file only if the change is genuinely invisible to a user). Translate the same change into what someone actually experiences: not "replaced String concatenation with fixed buffers" but "the app runs more reliably during long sessions." If in doubt about whether something counts as user-visible, default to including a simple version of it — a small, honest entry beats silently dropping something the user might notice.

**4. Also update the `## Current state` summary at the bottom of `Dev_Log.md`** if the change affects something that summary describes (new capability, changed architecture, newly-resolved "not yet done" item). Don't touch it for changes that don't shift the overall picture.

**5. Stage, commit, and push.** Include the actual code/content files that changed alongside both logs in one commit — don't split the real change and its log entry into separate commits.
```
git add <changed files> Dev_Log.md User_Log.md
git commit -m "<description of the actual change, not just 'update logs'>"
git push origin main
```
The commit message should describe what changed in the project, the same way commits have throughout this repo's history — the fact that logs were also updated is a normal part of that, not the headline.

## A note on tone

This user is building toward more complex projects and treats this repo partly as a learning record — so it's worth the Dev_Log.md entry actually explaining *why* something was a problem, not just that it was fixed, wherever that context isn't obvious from the diff alone.
