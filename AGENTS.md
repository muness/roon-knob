# Roon-Knob Agent Guide

## Tools

| Tool | Binary/Skill | Purpose |
|------|--------------|---------|
| **GitHub Issues** | `gh` | Task tracking |
| **Superego** | `sg` | Metacognitive review |
| **Working Memory** | `wm` | Cross-session knowledge |
| **Open Horizons** | MCP tools | Strategic endeavor tracking |
| **Miranda** | `/miranda:*` | Workflow automation |

## When to Use Each

**GitHub Issues** — Track what you're working on.
- `gh issue list` before starting
- Create issues for new work, close them when done
- Use task lists in parent issues to break down large features

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
- `/miranda:oh-task` — Work a GitHub issue end-to-end, create PR for review
- `/miranda:oh-merge` — Batch review and merge open PRs for GitHub issues
- `/miranda:oh-notes` — Address PR review comments

## Workflow

1. `gh issue list` — see what's available
2. Claim an issue, do the work
3. `sg review` before committing if the change is significant
4. Close the issue when done

## Principles

- GitHub Issues is the source of truth for tasks (no Markdown TODOs)
- Superego catches strategic mistakes — invoke before committing to an approach
