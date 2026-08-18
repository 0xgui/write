# Write

A tiny native writing app for the simple pleasure of putting words on a page.

Write opens onto one calm, centered sheet. It saves plain text, gets out of the
way, and keeps the controls off-screen until you ask for them.

## What it does

- Writes and saves plain UTF-8 `.txt` files.
- Autosaves silently after you pause.
- Lets you choose any local save location.
- Keeps one recovery copy of the preceding version (`your-file.txt~`).
- Restores your cursor position for each document.
- Uses the bundled Literata font for long-form screen writing, with Find,
  focus mode, and a warm dark paper theme—all shortcut-only.

There are no accounts, cloud services, databases, web views, formatting
toolbars, document libraries, or telemetry.

## Build and run

Write uses C++20 and GTK4 (version 4.10 or newer).

On Linux, install a C++ compiler, GNU Make, `pkg-config`, and the GTK4
development package. Then run:

```sh
make run
```

To install the binary and desktop launcher system-wide:

```sh
sudo make install
```

Use `PREFIX=... make install` to install to a different prefix.

## Keyboard shortcuts

- `Enter` — new paragraph (with breathing room)
- `Shift` + `Enter` — new line
- `Ctrl` + `O` — open a text file
- `Ctrl` + `S` — save immediately
- `Ctrl` + `Shift` + `S` — choose where to save
- `Ctrl` + `F` — find text; `Enter` finds next, `Shift` + `Enter` finds previous, `Esc` closes
- `Ctrl` + `+` / `Ctrl` + `=` — increase text size
- `Ctrl` + `-` — decrease text size
- `Ctrl` + `0` — reset text size
- `Ctrl` + `Shift` + `D` — switch warm light/dark paper
- `Ctrl` + `Shift` + `I` — show the active writing typeface
- `F11` — focus mode (fullscreen, no header)

## Data and portability

Your writing is ordinary text and remains portable independently of the app.
Until you use Save As, the default document is stored at
`~/.local/share/write/document.txt`; its cursor-position metadata lives only in
the matching app-data folder.

The source is portable to platforms supported by GTK4. This repository includes
a small Linux Makefile and desktop launcher; releases for other platforms need
their platform-appropriate GTK runtime packaging.

## License

MIT. See [LICENSE](LICENSE).

Literata is bundled under the SIL Open Font License 1.1; see
[assets/OFL.txt](assets/OFL.txt).
