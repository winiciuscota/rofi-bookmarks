# rofi-bookmarks

Browse, add, update and delete Firefox bookmarks via rofi — no sync account required.

Reads directly from Firefox's local `places.sqlite` database.

## Features

- Reads from the most recently used Firefox profile (auto-detected)
- Displays full folder path: `Folder > Subfolder > Bookmark`
- `Enter` — open bookmark in default browser
- `Shift+Enter` — edit or delete the selected bookmark
- `⚙ Settings` entry — add new bookmarks
- `Folder/Name` syntax — create nested folders under the Bookmarks Toolbar
- Handles DB lock: closes Firefox, saves changes, relaunches

## Requirements

- `bash`
- `python3` (stdlib only — `sqlite3`, `configparser`, `uuid`)
- `rofi`
- `firefox`
- `xdg-utils`
- `libnotify`

## Installation

### Arch Linux (PKGBUILD)

```bash
makepkg -si
```

### Manual

```bash
install -Dm755 rofi-bookmarks ~/.local/bin/rofi-bookmarks
```

## Usage

```bash
rofi-bookmarks
```

Or bind `rofi-bookmarks` to a keyboard shortcut in your desktop environment.

### Keyboard shortcuts

| Key            | Action                        |
|----------------|-------------------------------|
| `Enter`        | Open bookmark in browser      |
| `Shift+Enter`  | Edit / Delete menu            |
| `Esc`          | Cancel / go back              |

### Adding a bookmark

1. Select `⚙ Settings` → `➕ Add Bookmark`
2. Enter name — use `Folder/Name` to place inside a toolbar folder
3. Enter URL

### Editing / deleting

Select a bookmark and press `Shift+Enter`, then choose **Update** or **Delete**.

## File structure

```
rofi-bookmarks   — main bash+python script
PKGBUILD         — Arch Linux package
LICENSE          — MIT
```

## License

MIT — see [LICENSE](LICENSE).
