# POD native port

- Correct standalone recomp behavior is the primary requirement. Preparation
  for Libretro must preserve standalone functionality and be verified there.
- Structure current work to facilitate eventual standalone ports, including
  Linux. This is preparation, not a request to implement those ports. Separate
  shared game services, reusable standalone SDL adapters, and OS-specific
  lifecycle/packaging code. Avoid adding macOS assumptions to shared modules;
  verify each platform before claiming support.
- Organize each change so the game services can be reused by a future Libretro
  frontend. Keep CLI parsing, application/window ownership, scripted tests and
  process termination in the standalone host. Do not start a Libretro port
  merely to satisfy this structural guidance.
- Keep guest ABI and POD-specific compatibility policy in service modules.
  Translate frontend input at the boundary; DirectInput accepts DIK transitions
  independently of SDL scancodes.
- Prefer small, concrete interfaces over a speculative frontend framework.
  Existing SDL/HLE global-state dependencies remain limitations to document and
  gradually isolate; do not describe the current library as a Libretro core.
- Preserve the existing Windows host. Do not edit generated lifted code, embed
  game/codec/disc assets, or change the pinned toolkit without specific evidence.
- See docs/FRONTEND_ARCHITECTURE.md for current module ownership and remaining
  work toward a pull-driven frontend.
