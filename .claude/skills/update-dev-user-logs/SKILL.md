---
name: update-dev-user-logs
description: Writes a matching pair of changelog entries for this project — one technical entry in Dev_Log.md and one plain-language entry in User_Log.md — then commits and pushes both to GitHub. Use this whenever the user asks to update the logs, write a changelog entry, document a recent change, or says "yes" after being asked "want to update the logs?" per this project's CLAUDE.md workflow. Do not use this to write firmware or webpage code — it only records changes that were already made.
---

# Update the Dev Log and User Log

This project keeps two separate changelogs for two different audiences, and this skill's job is to keep them both accurate and in-sync without making the user write two versions of the same update themselves.

Per this project's `CLAUDE.md` workflow rule, this skill should only run after the user has already agreed to update the logs — that agreement is also the permission to commit and push in step 4, so there's no need to ask again partway through.

## The two files and why they're different

- **`Dev_Log.md`** — technical, for you and the user only, and (as of 2026-08-18) a **living document, not a chronicle**. It was condensed from 179 lines of append-only dated entries down to three standing sections — `## Lessons & gotchas`, `## Current state`, `## Not yet done` — specifically to stop it growing back into session-by-session narrative bloat. Edit those sections in place; don't append new `### YYYY-MM-DD` entries (the full session history is recoverable from `git log` if ever needed, so nothing is lost by not re-narrating it here).
- **`User_Log.md`** — plain-language, and it's not just a doc — it's *shown inside the app itself* (the "Last updated" stamp at the bottom of the page reads live off this file's newest entry, see `updateStamp()` in `index.html`). Anything technical here would be visible to whoever's just trying to use the headset, so it only covers what someone would actually notice or care about: new features, fixes, visible behavior changes. If a change has zero user-visible effect, it's fine to skip the User_Log.md entry entirely rather than force one. Unlike Dev_Log.md, this one **does** stay chronological, newest-first — it's a changelog by design.

## Steps

**1. Read `Dev_Log.md` in full** (it's short) and the top of `User_Log.md` to see the current state of both before editing.

**2. Update `Dev_Log.md` in place** — no new dated section, just fold the change into whichever of the three existing sections it affects:

- **`## Lessons & gotchas`** — add a bullet here only for something durable and non-obvious: a bug with a root cause worth remembering, a tooling/hardware gotcha, a technique that generalizes. Three things to get right:
  - **Check `CLAUDE.md` first.** It has its own rule (in "Key files") for which of the two files a given fact belongs in — this file only ever holds what CLAUDE.md doesn't already say in full. Don't restate a CLAUDE.md fact here even in different words; if it's already covered there, skip it.
  - **Capture the diagnostic method, not just the fact, when the method itself is reusable.** If a bug was found by a specific technique (e.g. "checked via serial logging whether the firmware even attempted the send, before assuming a wireless issue"), write that down alongside the fact — the technique is often more valuable for next time than the specific bug was, since it transfers to a whole class of similar problems.
  - **Check whether the reasoning is already a code comment** before restating it in full. If the fix left behind an explanatory comment at the relevant lines, write a short bullet that points to it (file + function/line context) instead of duplicating the explanation. If no such comment exists, the log entry is the only place the reasoning lives — write the full explanation, and say so.
  - Keep entries evergreen: phrase them as standalone principles, not "on this date we found...". Time-bound specifics (current pin numbers, COM ports, board models) belong in CLAUDE.md's operational facts or "Current state" below, not here, since a lesson phrased around a specific current value goes stale the next time that value changes.
- **`## Current state`** — update whatever line(s) the change affects (new capability, changed default, changed active board/config). Don't touch lines the change doesn't affect.
- **`## Not yet done`** — add, remove, or update an item if the change starts, finishes, or reshapes something on this list.

A change can touch one, two, or all three sections — or none, if it's genuinely too minor to be worth a Dev_Log line (rare, but possible for something trivial).

**3. Write the User_Log.md entry** (skip this file only if the change is genuinely invisible to a user), in the existing dated format:
  ```
  ## YYYY-MM-DD
  - Plain-language bullet, no jargon.
  ```
  New entries go **newest-first**, right after the `# Nami Headset — Update Log` title line. If there's already an entry for today's date, add bullets to it rather than creating a duplicate date header. Translate the technical change into what someone actually experiences: not "replaced String concatenation with fixed buffers" but "the app runs more reliably during long sessions." If in doubt about whether something counts as user-visible, default to including a simple version of it — a small, honest entry beats silently dropping something the user might notice.

**4. Stage, commit, and push.** Include the actual code/content files that changed alongside both logs in one commit — don't split the real change and its log entry into separate commits.
```
git add <changed files> Dev_Log.md User_Log.md
git commit -m "<description of the actual change, not just 'update logs'>"
git push origin main
```
The commit message should describe what changed in the project, the same way commits have throughout this repo's history — the fact that logs were also updated is a normal part of that, not the headline.

## A note on tone

This user is building toward more complex projects and treats this repo partly as a learning record — so it's worth the Dev_Log.md entry actually explaining *why* something was a problem, not just that it was fixed, wherever that context isn't obvious from the diff alone.
