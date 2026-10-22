## Development

Enable the Ponytail plugin for all development. If it is not installed, read and follow https://raw.githubusercontent.com/DietrichGebert/ponytail/refs/heads/main/AGENTS.md before starting development.

Keep code comments and README files free of issue references. Commit messages may link to issues, but always use the full issue URL, never shorthand such as `#1`.

## Project orientation

ModelDigi is a future Minimoog Model D clone. Its shared C oscillator bank runs in a host renderer and on the Digitakt mk1, where all three oscillators and their modulation sound. Digitakt firmware targets OS1.54 only. Digitone is a scaffold whose placeholder tasks succeed without producing a working port. Do not add other synthesis sections, controls or extra workflows unless requested.

Read [Architecture](docs/architecture.md) for integration boundaries and [Development and verification](docs/development.md) for setup, commands and checks. Before implementing Model D behavior, establish manufacturer sources and document numeric constraints and confidence separately from measurements.

- Manage language runtimes with mise. `mise run test` runs host DSP checks without stock firmware or the cross-toolchain; it does not verify device integration, persistence, CPU headroom or analog fidelity.
- The Digitakt machine uses permanent machine ID 42 and the approved two-page control meanings. Changes to the ID or saved parameter meanings need explicit authorization.
- The same engine and runtime Analog toggle are used by the firmware, emulator and host tools. Analog off bypasses the character layer and freezes drift for clean reference checks. Cost and equivalence checks cover both runtime settings; do not introduce separate clean/analog builds.
- Digitakt load budgets assume DigiHealth is installed with FAST AUDIO on. Preserve oscillator headroom and level matching checks.
- The engine renders at 48 kHz without oversampling; its edge-correction kernel is part of the response the checks model. The static alias limit is −60 dBc for fundamentals up to 12 kHz and −50 dBc above. Cost reductions may change output if every frozen instrument tolerance and the alias limit still pass; keep the 10% firmware render reserve in the worst case. Keep printed specifications, circuit assumptions, model choices and hardware measurements distinct.
- Stock firmware and raw research remain local and ignored. Keep device-specific firmware assumptions out of the shared sound engine and leave dependency submodules unchanged unless the task requires them.

## GitHub issues

Use the `gh` CLI for all issue operations, including opening, updating, and closing issues. When working on a fork, ensure issues are enabled on the fork before creating issues.

Before implementing a feature or fix, record a plan in a GitHub issue covering the intended outcome, approach, and verification. Create sub-issues from that plan as needed. Initial scaffolding in an empty repository without a GitHub remote may remain local and uncommitted; configure the remote before subsequent feature work.

Exception: If the user explicitly says `YOLO` for the current task, skip planning and issue creation for a small fix or narrowly scoped change. Make the change and commit directly on `main`; do not create a worktree or branch.

Every issue body must contain these two sections:

- `## Human Readable`: Keep it short and easy to understand, with minimal jargon and technical detail.
- `## Machine Readable`: Include as much or as little technical detail and jargon as needed.

## Plan and research documents

Keep short-lived feature plans and research notes uncommitted and out of PRs and merges.

Exception: If another issue needs these documents as a reference, commit and push them on a separate reference branch and link to them from the issue. Keep those documents out of the feature PR and merge.

Research documents may be committed and merged when they provide lasting, low-level technical reference, such as how a resistor value affects oscillator behavior. Keep feature-specific debugging narratives, such as a page event causing a feature to fail, in issues or temporary notes instead.

## Git and scope

- Write commit messages that conform to Tim Pope's standards, without emojis.
- Never use `git -C`; run Git commands from the intended working directory.
- Use `git worktree add` for feature branches, never `git checkout -b`.
- Keep future `main` history linear: squash-merge PRs and rebase feature branches when updating them from `main`. Do not rewrite published history.
- Delete merged branch references after merging. Preserve active branches and local research, firmware and measurement evidence when cleaning up worktrees.
- Inspect existing patterns first and mirror their boundaries. Keep changes scoped to the explicit request.
- If instructions are ambiguous or conflict, stop and ask before implementing.
- Treat established public and stable boundaries as off-limits unless explicitly requested.
- Before changing generated files, schemas, APIs, persisted formats or other broad-impact artifacts, explain why and wait for confirmation.
- When corrected, pause, reassess and reduce the diff.
- Prefer `rg` for text searches and `fd` for file searches. Use mise for all language runtimes and project tasks.

## Research sources and storage

- Start with manufacturer service and repair manuals, schematics, owner's manuals and component datasheets. Look on manufacturer support sites and established synthesizer documentation archives; use documented repair notes as supporting evidence. Distinguish original specifications from observations about a particular unit.
- Store downloaded sources, inspection crops and working notes under `research/<topic>/`, keeping this material local and gitignored. Record source URLs, document titles and revisions, and relevant pages or sheets alongside the material.
- Store lasting technical references in `docs/<topic>.md`, following the policy above. Cite the source documents and relevant pages or sheets; distinguish source readings, calculations and measurements, and state uncertainty rather than filling gaps with assumptions.
- Write documentation around concepts, architecture, signal flow, constraints and design rationale. Keep it independent of implementation filenames, function names and the current code layout; avoid descriptions such as "we do this in file X" that become stale as the implementation changes.
