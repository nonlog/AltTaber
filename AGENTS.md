# AltTaber Agent Development Rules

These rules apply to all automated development work in this repository.

## Commit and push workflow

- After a meaningful code/documentation modification is implemented and validated, create a local Git commit promptly. Do not leave completed changes uncommitted across development stages.
- Do **not** push a new fix immediately after committing it. Wait for the user to test the build and explicitly confirm that the change is usable/approved.
- After user approval, push the approved commit(s) using the **Codex identity**, not the user's personal Git identity.
- Repository-local commit identity should remain `Codex <codex@openai.com>` unless the user explicitly requests otherwise.
- Before pushing, verify the GitHub authentication/credential actually used for the push is the Codex identity. Commit author identity alone is not sufficient proof of push identity.
- Never rewrite or squash previously approved commits unless the user asks for it.

## Test-build discipline

- Development validation must use `D:\Workspace\general\AltTaber\test-build\AltTaber.exe`.
- Before visual/behavioral testing, verify that no installed/startup AltTaber copy is being mistaken for the test build.
- For the intermittent window-adhesion regression, use repeated independent sessions (`Alt+Tab`, release Alt, wait, repeat), and vary the foreground/source application. Do not rely only on holding Alt continuously.

## Documentation

- Maintain `CHANGELOG_DEV.md` with important fixes, regressions, validation results, and local artifact state.
- Follow the handoff-maintenance requirements in `AGENTD.md`.
