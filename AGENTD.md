# AltTaber Handoff Maintenance Rules

This file defines the handoff-documentation discipline for ongoing agent-driven development.

- Keep `D:\Workspace\general\AltTaber\.local\CHATGPT_WEB_HANDOFF.md` current throughout development, not only at the end of a conversation.
- Update the handoff immediately after a meaningful milestone such as: root-cause discovery, important code change, build completion, key test result, regression discovery, change of plan, local commit, user approval, or push/release.
- Before starting a potentially long/risky experiment, ensure the handoff already contains enough current state that a fresh conversation can resume safely if the current session is interrupted.
- The handoff must record at minimum:
  - current objective and user-visible regression;
  - relevant source files/functions and implementation decisions;
  - exact test-build/build paths;
  - correct regression reproduction method;
  - latest validation results and known failures;
  - latest local commit hash/status and whether it has been pushed;
  - next recommended debugging/development step.
- Keep `CHANGELOG_DEV.md` synchronized with durable implementation/validation milestones. The handoff is for continuation state; the changelog is the chronological development record.
- Never claim a regression is fixed in the handoff solely because one automated screenshot looked correct; distinguish `not reproduced in current regression run` from `user-confirmed fixed`.
