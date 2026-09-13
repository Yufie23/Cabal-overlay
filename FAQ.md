# FAQ

## Windows says "This app has been blocked" / SmartScreen warns about an unknown publisher

This is expected for now, and it is not a sign of danger. The installer
is **not code-signed yet** (see below), it is brand new, and SmartScreen
builds trust from download volume — a small project starts at zero
reputation, like every developer's first release.

To install anyway:

1. On the blue "Windows protected your PC" screen, click
   **More info**.
2. Click **Run anyway**.

That's it — two extra clicks, once per installer version.

## My antivirus flags the installer or one of the DLLs

Almost certainly a false positive. The installer bundles 61 GTK
runtime DLLs and the app keeps itself on top of the game with
always-on-top window styles plus a global hotkey — that *shape* matches
generic "game hack" signatures that antivirus heuristics hunt for. The
behavior is nothing like one: the overlay is a separate process that is
never injected into the game and never reads its memory (the source is
right here in the repo, and the CI builds it in the open).

What to do:

- Restore the file from quarantine and add an exclusion for the
  install folder.
- Report the false positive to your AV vendor — most have a submission
  page (Microsoft: <https://www.microsoft.com/en-us/wdsi/filesubmission>).
  Every report helps the next user.

## Is this a cheat? Will it trigger the game's anti-cheat (XIGNCODE3)?

No, and that is a design constraint, not an accident. The overlay:

- never injects code into the game process,
- never reads or writes the game's memory,
- never sends input to the game.

It only draws its own windows on top, watches which window has focus
(so it can hide itself when you alt-tab), and observes your own clicks
in one screen zone you calibrate yourself (for the DG Check counter).
The server's anti-cheat sees a normal game client and an unrelated
window floating above it — because that is all there is.

## Why is the installer not signed? Will it be?

Code signing certificates cost money and/or a vetting process, and for
a small clan tool the warning is a two-click nuisance, not a blocker.
If the project outgrows that, signing is on the table — the CI pipeline
already has a SignPath step wired in, ready to activate.

## How do I know the downloaded binary matches this source?

You don't have to trust anyone: every build runs on GitHub Actions in
a public, reproducible pipeline (see the Actions tab and
`.github/workflows/build.yml`). The release files are the exact output
of that build, and the Windows installer was produced by the NSIS
script in `dist/windows/`.

## Linux: the overlay doesn't show up at all

The overlay needs a Wayland compositor with the wlr-layer-shell
protocol: **KDE Plasma and wlroots-based compositors (Sway, Hyprland,
…) work**. GNOME (Wayland or X11) and plain X11 sessions are not
supported — see the Requirements table in the README.

## Something else is wrong / I have an idea

Open an issue on the repository's Issues tab. Include your OS, session
type (Wayland/X11), and the output of running `cabal-overlay` from a
terminal — that log answers most questions immediately.
