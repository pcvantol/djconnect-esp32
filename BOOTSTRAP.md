# DJConnect ESP32 Repository Bootstrap

This repository owns ESP32 firmware and constrained device runtime. It adopts
AI-Native Engineering Operating System 2.2 from
`pcvantol/djconnect/docs/governance/PLATFORM_ARCHITECT_SYSTEM_INSTRUCTIONS.md`
by reference and never copies central governance.

Start with `git switch main`, `git pull --ff-only`, current-main/clean-tree and
predecessor verification. Read `AGENTS.md`, this file, rolling records,
roadmap and prompt index. Reconcile `MERGED_UNRECONCILED` before work and check
whether requested behavior already exists. Lifecycle: `LOCAL_IN_PROGRESS`,
`REVIEWABLE_FROZEN`, `MERGED_UNRECONCILED`, `MERGED_RECONCILED`. Cleanup is
fail-closed until merge, history, remote deletion and clean tree are verified.
