# Claude Agent Instructions

See [AGENTS.md](AGENTS.md) for complete instructions on working with this project.

## Key Points

- This project uses **Beads (bd)** for all task and issue management
- Initialize once per clone: `bd init --quiet`
- Core workflow: check `bd ready`, claim tasks, sync when done
- Always run `bd sync` at the end of a session
- No external trackers, no Markdown TODOs

## Roadmap-Driven Development

The [README.md](README.md#roadmap) contains the project roadmap with release blockers and future improvements. When starting work:

1. **Check the roadmap** in README.md to understand priorities
2. **Check beads** for existing tasks: `bd ready`
3. **Create beads tasks** for roadmap items if they don't exist yet
4. **Break down large features** into fine-grained subtasks using beads parent-child relationships
5. **Update roadmap status** in README.md when features are completed

### Example Workflow

```bash
# Check what's ready to work on
bd ready

# If implementing a roadmap feature, create parent task first
bd create "WiFi Provisioning (SoftAP)" -t epic -p 0 --json

# Break into subtasks
bd create "Add SoftAP mode to wifi_manager" -t task -p 1 --deps parent-child:bd-XX
bd create "Create captive portal HTML" -t task -p 1 --deps parent-child:bd-XX
bd create "Add provisioning timeout logic" -t task -p 1 --deps parent-child:bd-XX

# Claim and work on a subtask
bd update bd-YY --status in_progress

# Complete and sync
bd close bd-YY --reason "implemented"
bd sync
```

### Keeping Roadmap in Sync

- When a roadmap feature is **fully complete**, update its status in README.md from "Not started" to "Complete"
- When work is **in progress**, update to "In progress"
- Beads is the source of truth for task details; README roadmap is the high-level view
