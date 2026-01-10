# Roon-Knob Agent Guide

## Tools

| Tool | Binary | Purpose |
|------|--------|---------|
| **Beads** | `bd` | Task tracking |
| **Superego** | `sg` | Metacognitive review |
| **Working Memory** | `wm` | Cross-session knowledge |

First time after clone? Run `bd init --quiet`.

## When to Use Each

**Beads (`bd`)** — Track what you're working on.
- `bd ready` before starting
- `bd sync` at end of session (after closing tasks)

**Superego (`sg`)** — Second opinion at decision points.
- Before committing to a plan or architecture
- When choosing between approaches
- Before significant implementations
- Before merging PRs (`sg review pr`)

**Working Memory (`wm`)** — Capture context across sessions.
- `wm dive-prep` before complex work spanning multiple areas (e.g., OTA touching firmware + sidecar + WiFi)
- Hooks capture learnings automatically; invoke manually for focused dives

## Workflow

1. `bd ready` — see what's available
2. Claim a task, do the work
3. `sg review` before committing if the change is significant
4. `bd sync` at end of session

## Principles

- Beads is the source of truth for tasks (no Markdown TODOs)
- Superego catches strategic mistakes — invoke before committing to an approach
- If `bd sync` complains about JSONL being newer, run `bd import` first
