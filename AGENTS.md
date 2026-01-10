# Roon-Knob Agent Guide

## Tools

| Tool | Binary/Skill | Purpose |
|------|--------------|---------|
| **Beads** | `bd` | Task tracking |
| **Superego** | `sg` | Metacognitive review |
| **Working Memory** | `wm` | Cross-session knowledge |
| **Open Horizons** | MCP tools | Strategic endeavor tracking |
| **Miranda** | `/miranda:*` | Workflow automation |

First time after clone? Run `bd init --quiet`.

## When to Use Each

**Beads (`bd`)** — Track what you're working on.
- `bd ready` before starting
- `bd sync` at end of session (after closing tasks)
- Task types: `feature`, `bug`, `task`, `chore`, `epic` (priority 0-4, lower = higher)
- Use `--deps parent-child:bd-XX` to nest subtasks under epics

**Superego (`sg`)** — Second opinion at decision points.
- Before committing to a plan or architecture
- When choosing between approaches
- Before significant implementations
- Before merging PRs (`sg review pr`)

**Working Memory (`wm`)** — Capture context across sessions.
- Hooks auto-extract learnings from sessions into `.wm/` on sync
- `wm dive-prep` before complex work — aggregates relevant context from past sessions
- Use for work spanning multiple areas (e.g., OTA touching firmware + sidecar + WiFi)

**Open Horizons** — Track strategic goals and learnings via MCP.
- `oh_get_endeavors` to see missions/aims/initiatives/tasks
- `oh_log_decision` to capture strategic decisions
- `oh_create_metis_candidate` when you discover a reusable pattern
- `oh_create_guardrail_candidate` for constraints that should be enforced

**Miranda** — Automate common workflows.
- `/miranda:mouse` — Work a beads task end-to-end, create PR for review
- `/miranda:drummer` — Batch review and merge open PRs
- `/miranda:notes` — Address PR review comments

## Workflow

1. `bd ready` — see what's available
2. Claim a task, do the work
3. `sg review` before committing if the change is significant
4. `bd sync` at end of session

## Principles

- Beads is the source of truth for tasks (no Markdown TODOs)
- Superego catches strategic mistakes — invoke before committing to an approach
- If `bd sync` complains about JSONL being newer, run `bd import` first
