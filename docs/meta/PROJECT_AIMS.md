# Project Aims Alignment

How roon-knob connects to broader goals and how to use that connection when prioritizing work.

This document applies the [Open Horizons](https://muness.com/posts/open-horizons/) framework—specifically its principle of **nested feedback loops** operating at different scales. Small-scale decisions (what to fix this week) should reinforce larger-scale aims (what this project is for), and vice versa.

## Why This Project Exists

Roon Knob turns a $50 commodity device into a purpose-built physical controller for Roon. It's a side project that demonstrates technology as a force-multiplier: taking off-the-shelf hardware and firmware expertise and compressing them into something non-developers can use.

It's also a **learning platform** for:
- AI coding agents (Claude Code workflows)
- Metacognitive agent review (superego)
- Embedded systems development (ESP-IDF, LVGL, FreeRTOS)
- UI/UX design on constrained devices
- Roon extension development

The learning value is as important as the product output. Experiments here inform how I work on other projects.

## Connection to Author's Aims

### Primary: Aim 1 — Create Leverage Through Technology (Force-Multiplier)

This is the core alignment. The project:

- Transforms commodity hardware into specialized tooling
- Demonstrates "compression engine" in action: Roon API + ESP32 embedded dev + LVGL UI + Docker networking → something a motivated user can deploy
- Creates real behavior change (physical knob on desk changes how people interact with their music)

**Implication for prioritization:** Work that lowers the barrier to unlocking this leverage multiplies reach. Every setup simplification expands who can benefit.

### Secondary: Aim 3 — Reach and Equip People (Spread)

The Roon community is self-selected: people who value quality and will invest effort. They're the right audience for "better ways of working with music." But even technically capable users hit friction.

**Implication for prioritization:** Simplification = spread. Documentation clarity matters. Error recovery matters.

### Constraint: Aim 4 — Build and Protect Margin

This is a side project. Scope creep threatens the sustainability that makes it possible to maintain. The key tension: polish vs. feature expansion vs. architectural rewrites.

**Implication for prioritization:** Bias toward small wins with compounding returns. Be skeptical of "while we're at it" scope expansion. Ruthlessly cut features that don't serve core use case.

## Decision Framework

When new feedback arrives, apply these filters in order:

1. **Do I want it?** → This is a personal controller. If it sounds fun, do it.
2. **Does it block someone from first success?** → High priority
3. **Is it a quick win (< 1 hour)?** → Do opportunistically
4. **Do multiple users request it?** → Validate demand, then prioritize
5. **Does it require architecture change?** → Park unless compelling case
6. **Does it expand scope beyond core use case?** → Default to no

This maps to Open Horizons' principle of **Planning Over Plans**: use this framework to guide decisions, but remain adaptive as new information arrives.

## Prioritization Lenses

Different ways to organize work, each revealing different priorities. Choose the lens that fits the decision at hand.

### Lens 1: Friction → Polish → Expansion → Architecture

| Category | Description | When to do |
|----------|-------------|------------|
| **Friction Reducers** | Remove barriers to first success | High priority—multiplies reach |
| **Quick Polish** | Small UX wins, minimal effort | Do opportunistically |
| **Feature Expansion** | New capabilities | Only if strong user signal |
| **Architecture Changes** | Fundamental redesign | Rarely—high investment, protect margin |

### Lens 2: By Learning Signal

Some work teaches you about users; other work just ships features. Per Open Horizons, feedback loops matter—choose work that generates learning, not just output.

| Category | Example | Learning Value |
|----------|---------|----------------|
| **Discovery** | Why did a user flash at wrong address? | High—reveals mental model gaps |
| **Validation** | Multiple users request same feature | High—confirms need |
| **Extrapolation** | Single user's architectural vision | Medium—one perspective |
| **Known need** | Fix a tagging bug | Low—just needs doing |

### Lens 3: By User Journey Stage

| Stage | Friction Points | Impact |
|-------|-----------------|--------|
| **Pre-purchase** | — | N/A (README does this job) |
| **First flash** | Address confusion, bootloader issues | Blocks adoption entirely |
| **First connection** | mDNS unreliability, bridge discovery | Blocks adoption entirely |
| **Daily use** | Tap targets, wake behavior, display | Quality of experience |
| **Advanced use** | Zone filtering, rotation, Bluetooth | Power user satisfaction |

---

See [ROADMAP_IDEAS.md](ROADMAP_IDEAS.md) for current user feedback and prioritized work items.
