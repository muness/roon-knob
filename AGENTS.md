
Roon-Knob Agent Guide

Purpose

This project uses Beads (bd) for all issue and task management — nothing else.
It tracks design tasks, firmware milestones, simulator features, and sidecar bugs in one place.

Beads is lightweight, git-integrated, and agent-friendly. We only use it for task tracking, not code style, linting, or test enforcement.

⸻

Quick Start

Initialize

If you see database not found:

bd init --quiet

This sets up a local .beads/ database linked to git.
Do this once per clone of the repo.

Core Commands

bd ready            # list unblocked issues
bd create "Add mDNS discovery" -t feature -p 1 --json
bd update bd-12 --status in_progress
bd close bd-12 --reason "done"
bd sync             # force export/import/push immediately

Each commit automatically syncs .beads/issues.jsonl.

⸻

Task Types
	•	feature – new capability (e.g., OTA, zone picker)
	•	bug – broken behavior
	•	task – supporting work (refactor, docs)
	•	chore – maintenance or CI fixes
	•	epic – parent issue grouping subtasks

Priorities
	•	0 critical
	•	1 high
	•	2 normal
	•	3 low
	•	4 backlog / idea

⸻

Workflow for Contributors & Agents
	1.	Check for open work

bd ready --json


	2.	Claim a task

bd update bd-42 --status in_progress


	3.	Do the work.
	4.	If you discover a new need or idea:

bd create "Add volume debounce" -p 2 --deps discovered-from:bd-42


	5.	Finish and close

bd close bd-42 --reason "implemented"


	6.	Sync to push all updates

bd sync



⸻

Recommended Structure

Use Beads to plan and track:

Category	Examples
Firmware	LVGL layout, OTA updates, Wi-Fi provisioning
Simulator	SDL key map, round mask visuals, mock data
Sidecar	mDNS advertisement, /image proxy, volume rate limit
Infra	CI build scripts, Homebrew setup, Mac install docs


⸻

Multi-Agent Use

Multiple people or AIs can work in parallel — Beads handles merging automatically.
	•	Always run bd sync at the end of a session.
	•	To avoid collisions, check bd ready before starting new work.
	•	Optionally use Agent Mail for real-time coordination, but it’s not required for Roon-Knob.

⸻

Issue Relationships

Link work contextually:
	•	discovered-from – a task uncovered during another
	•	blocks – one task must finish before another
	•	parent-child – epic/subtask hierarchy

Example:

bd create "Add OTA download" -t feature -p 1
bd create "Validate OTA rollback" -t task -p 2 --deps parent-child:bd-17


⸻

Planning Docs

Store planning material (designs, mockups, notes) in history/ instead of the repo root.

history/
  2025-11-setup-notes.md
  ui-wireframe.png

They are optional and can be ignored by git.

⸻

Good Habits
	•	✅ One source of truth: Beads issues.
	•	✅ Always close finished work and sync.
	•	✅ Use --json for machine interactions.
	•	✅ Keep commit messages brief and reference the issue (bd-42).
	•	❌ No Markdown TODOs.
	•	❌ No external trackers.

⸻

End of Session

When you finish:

bd sync
git pull --rebase
git push

That ensures .beads/issues.jsonl is consistent with git and everyone else’s view of the project.


