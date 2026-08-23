# AltTaber Development Changelog

This file records local development work that has not necessarily been committed or published upstream.

## 2026-08-23

### v0.6.3 release preparation

- User confirmed the current test build is usable and approved publishing a release.
- Release version bumped from `0.6.2` to `0.6.3` in `CMakeLists.txt` and the GitHub Actions packaging/release workflow.
- Release notes were refreshed to describe the Windows 11 parity work, minimized-window aspect-ratio fix, DWM thumbnail adhesion fix, and removal of the one-frame fallback-icon flash.
- Release plan: locally validate the 0.6.3 build, commit the release metadata as Codex, fast-forward `origin/master`, create/push tag `v0.6.3`, wait for the GitHub Actions release workflow, then verify the published ZIP and SHA-256.

Release result:

- `origin/master` was fast-forwarded through release-preparation commit `9c6dc5d3dd2858f7a04d0f4e0c6230a938970154`.
- Tag `v0.6.3` was pushed and GitHub Actions run `32638381629` completed successfully.
- Public release: `https://github.com/nonlog/AltTaber/releases/tag/v0.6.3`.
- Published asset: `AltTaber-0.6.3-win64.zip`, 48,380,552 bytes.
- Release asset SHA-256: `103d412ee3c59676488f044cc9f99c1866a8da1de54895228cb69bf2a3491627`.
- Downloaded the public release asset again after publication; SHA-256 matched and the packaged `AltTaber.exe` reports `FileVersion=0.6.3.0` / `ProductVersion=0.6.3.0`.

### Remove one-frame fallback-icon flash before live previews

- User visually confirmed that the previously reported window-adhesion issue now appears fixed on the physical display.
- New regression reported: immediately after invoking Alt+Tab, fallback application icons are visible for a brief frame before DWM live previews appear.
- Root cause: `forceShow()` restored the top-level window opacity to `1` immediately after `showNormal()`, while DWM thumbnail registration intentionally occurs on the next Qt event-loop turn to avoid the prior cross-session adhesion race.
- Changed show sequencing so the switcher remains effectively invisible at opacity `0.005` until:
  1. the final QListWidget layout is committed;
  2. DWM thumbnails are registered;
  3. `PreviewAvailableRole` has been reflected in a synchronous viewport repaint;
  4. DWM composition is flushed.
- Only then is top-level opacity restored to `1`, so the first visible switcher frame should already contain live previews rather than fallback icons.
- Added repository workflow rules in `AGENTS.md`: commit validated modifications promptly; wait for user approval before push; push using Codex identity.
- Added `AGENTD.md` requiring continuous maintenance of `.local/CHATGPT_WEB_HANDOFF.md` and `CHANGELOG_DEV.md` throughout development.

Validation:

- Recorded the desktop at 60 FPS while invoking Alt+Tab. Frame 49 was still the source application; frame 50 was the first visible AltTaber frame and already contained DWM live previews. No fallback-icon-only frame was present in the recorded transition.
- Re-ran 21 independent Alt+Tab sessions across seven foreground applications after the timing change. Every capture was owned by the test-build PID and no card-adhesion regression was visible in the reviewed frames.

### Windows 11 Alt+Tab parity / DWM thumbnail stability

- Reconfirmed the native Windows 11 25H2 layout on the current machine with the same seven visible task windows: **7 windows are arranged as 3 + 4 rows**, not a single seven-card row.
- Reconfirmed that row wrapping is width-sensitive; it must not be hard-coded purely by item count.
- Thickened the current-item accent outline to **3 logical px** (about 4 physical px at 125% DPI), using the current Windows system accent color.
- Kept the rule that **only the current Alt+Tab target** receives the accent outline.
- Hardened DWM thumbnail teardown between independent Alt+Tab sessions:
  - mark every registered thumbnail invisible before unregistering it;
  - unregister all thumbnail handles;
  - call `DwmFlush()` after teardown;
  - invalidate delayed refreshes with `thumbnailGeneration` whenever a session hides;
  - defer new thumbnail registration to the next Qt event-loop turn after `showNormal()` and layout commit.
- Purpose of the DWM lifecycle change: prevent old destination surfaces from surviving briefly into the next Alt+Tab session and appearing as vertical strips / card adhesion between adjacent previews.

### Validation performed

- 60 consecutive **independent** Alt+Tab sessions: press Alt+Tab once, release Alt, wait, repeat. Every captured session was owned by the test-build process; no fallback to native switcher occurred.
- 28 additional independent Alt+Tab sessions with different source windows (Chrome, Windows Terminal, Explorer, WeChat, VS Code, Clash Party, UniGetUI) to force different MRU orders and card arrangements.
- Exact 7-window regression: temporarily hid the AgentDock control panel, leaving seven task windows, then ran 14 independent Alt+Tab sessions from seven different source windows. Layout remained 3 + 4 and no card adhesion was visible in the captured frames.
- Native comparison screenshot on the same machine confirmed the same 3 + 4 seven-window layout.
- Final detached elevated test-build launch was smoke-tested after all changes; the Alt+Tab foreground PID matched the test-build PID and the process remained responsive after the automation command exited.

### Current local artifact

- Test executable: `D:\Workspace\general\AltTaber\test-build\AltTaber.exe`
- Build tree: `D:\Workspace\general\AltTaber\build-codex-ucrt64-nosys`
- Implementation commit: `ce48f0e1c6406d4a32e2d8b716682eff68a8142c` (`Codex <codex@openai.com>`).
- Commit is local only and has **not been pushed**; wait for user confirmation before push.

## 2026-08-22 and earlier (summary)

- Replaced the light Mica-style background with Windows 11 transient Desktop Acrylic behavior so dark mode renders correctly.
- Added Windows 11 dark/light theme refresh before switcher display.
- Reworked the fixed equal-width grid into a variable-width flow layout based on source window aspect ratio.
- Added width-balanced row partitioning and per-row centering.
- Corrected minimized-window aspect ratios by using `WINDOWPLACEMENT.rcNormalPosition` instead of tiny iconic rectangles.
- Switched the selected outline to the actual Windows accent color (`AccentColorMenu`) rather than a fixed derived purple palette entry.
- Removed accent outlines from non-current cards.
- Changed selected-card painting so the accent is drawn entirely inside the card.
- Added explicit old/new current-item repainting and delegate clipping.
- Preserved the original fork goal: fix the Microsoft IME / Chinese input Alt+Tab issue while making the UI closely resemble native Windows 11.
