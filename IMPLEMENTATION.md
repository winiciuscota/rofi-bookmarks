# rofi-bookmarks: Browser-Backed Bookmark Manager

A modern rofi plugin for browsing and managing bookmarks directly from your installed browsers (Firefox, Waterfox, Chromium) without requiring external password managers or encryption.

## Features

- **Direct Browser Reading**: Reads bookmarks directly from browser storage files
  - Firefox/Waterfox: `places.sqlite` with SQLite recursive CTE queries
  - Chromium: JSON-based Bookmarks file (in development)
- **Automatic Profile Selection**: Uses the most recently modified Firefox/Waterfox profile
- **Multi-Browser Support**: Automatically detects installed browsers and allows switching
- **Hierarchical Folder Display**: Shows folder structure using path separators (`Folder > Subfolder > Bookmark`)
- **Quick Actions Menu**: 
  - Shift+Enter opens action menu (Open, Copy URL)
  - Navigate with Esc to go back or exit
- **Settings View**: Change browser, refresh bookmarks
- **User-Local Installation**: No sudo required, installs to `~/.local/share/rofi/plugins`

## Requirements

### System Dependencies
```bash
# Debian/Ubuntu
sudo apt install libglib2.0-dev libcairo2-dev libsqlite3-dev libjson-glib-dev rofi cmake pkg-config

# Arch Linux
sudo pacman -S glib2 cairo sqlite json-glib rofi cmake pkg-config

# Fedora
sudo dnf install glib2-devel cairo-devel sqlite-devel json-glib-devel rofi cmake pkg-config
```

### Browser Requirements
- Firefox or Waterfox (for SQLite bookmark support)
- Chromium/Google Chrome (planned)

## Installation

### Quick Start
```bash
cd rofi-bookmarks
bash install.sh
```

This will:
1. Build the plugin from source
2. Install to `~/.local/share/rofi/plugins`
3. Create `~/.local/bin` if needed

### Manual Installation
```bash
mkdir -p build && cd build
cmake ..
make
mkdir -p ~/.local/share/rofi/plugins
cp bookmarks.so ~/.local/share/rofi/plugins/
```

## Usage

### Command Line
```bash
# Quick launch
rofi -show bookmarks -plugin-path ~/.local/share/rofi/plugins

# With all modes
rofi -show bookmarks -modi "window,run,drun,bookmarks" -plugin-path ~/.local/share/rofi/plugins
```

### rofi Configuration
Add to `~/.config/rofi/config.rasi`:
```rasi
configuration {
    modi: "window,run,drun,bookmarks";
    plugins-directory: "~/.local/share/rofi/plugins";
    display-bookmarks: "📌";
}
```

Then launch with:
```bash
rofi -show bookmarks
```

## Navigation

### Main View (Bookmarks List)
- **Enter**: Open selected bookmark in default browser
- **Shift+Enter**: Open action menu for bookmark
- **Esc**: Exit plugin

### Action Menu
- **Enter (Open)**: Open URL in default browser
- **Enter (Copy URL)**: Copy URL to clipboard
- **Esc**: Back to bookmarks list

### Settings View
- **Enter (Change browser)**: Switch between detected browsers
- **Enter (Refresh)**: Reload bookmarks
- **Esc**: Back to bookmarks list

### Browser Selection View
- **Enter**: Select highlighted browser
- **Esc**: Back to settings

## Implementation Details

### Architecture

The plugin is a compiled rofi mode (shared library) implementing the rofi mode API:

```c
// Core structures
Browser {
    type: BROWSER_FIREFOX | BROWSER_WATERFOX | BROWSER_CHROMIUM
    name, base_dir, profile_dir, db_path, bookmarks_path
}

Bookmark {
    display: "Folder > Subfolder > Title"
    title: "Title or URL fallback"
    url: "https://..."
    browser_type, ff_bookmark_id, chromium_guid
}

BookmarkModePrivateData {
    browsers: GPtrArray of detected browsers
    bookmarks: GPtrArray of loaded bookmarks
    view: current view (MAIN, SETTINGS, BROWSERS, ACTIONS)
    selected_browser, selected_bookmark_idx
    pref_file: last selected browser preference
    status_msg: error reporting
}
```

### Firefox/Waterfox Bookmark Loading

1. **Profile Detection**: Parse `~/.mozilla/firefox/profiles.ini` and find most recent profile by `moz_bookmarks.sqlite` mtime
2. **Database Copy**: Copy `places.sqlite` to `/tmp/` to avoid locking Firefox
3. **SQL Query**: Recursive CTE query to build folder hierarchy:
   ```sql
   WITH RECURSIVE roots AS (...),
       folders AS (... UNION ALL ...)
   SELECT bookmarks with folder paths
   ```
4. **Result**: Bookmarks with display format like `"Documentation > Setup > Quick Start"`

### View Navigation

- **VIEW_MAIN**: Shows bookmarks + Settings option
- **VIEW_SETTINGS**: Shows "Change browser" and "Refresh" options
- **VIEW_BROWSERS**: Lists detected browsers with current selection marked
- **VIEW_ACTIONS**: Shows "Open" and "Copy URL" actions

Key bindings handled in `bookmarks_result()`:
- `MENU_OK` (Enter): Navigate or execute current view action
- `MENU_CUSTOM_ACTION` (Shift+Enter): Open action menu
- `MENU_CANCEL` (Esc): Navigate back or exit

### Error Handling

All errors are propagated to the rofi UI via `set_status()`:
- Failed to copy `places.sqlite`
- Failed to open SQLite database (includes `sqlite3_errmsg()`)
- Failed to prepare SQL query (includes error string)
- No Firefox profile found with bookmarks
- No supported browsers detected

### Browser Preference Persistence

Selected browser is saved to `~/.local/share/rofi-bookmarks/browser_preference` (plain text name) and reloaded on next plugin launch.

## Development Notes

### Testing Firefox Bookmarks Directly

```bash
# Find most recent profile
PROFILE=$(ls -dt ~/.mozilla/firefox/*/places.sqlite | head -1 | xargs dirname)

# Copy to temp file to avoid locks
cp "$PROFILE/places.sqlite" /tmp/test.db

# Test the SQL query
sqlite3 /tmp/test.db <<'EOF'
WITH RECURSIVE roots(id) AS (
  SELECT id FROM moz_bookmarks WHERE parent=1 AND type=2 
    AND title IN ('menu','toolbar','unfiled')
), folders(id, parent, path, level) AS (
  ...
)
SELECT ... FROM moz_bookmarks mb ...
EOF
```

### Building in Debug Mode

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Chromium Support (Planned)

Chromium bookmarks are stored in `~/.config/chromium/Default/Bookmarks` as JSON. Implementation in progress using json-glib.

## Troubleshooting

### "Failed to query bookmarks" Error

1. Ensure Firefox profile contains bookmarks: `sqlite3 ~/.mozilla/firefox/*/places.sqlite "SELECT COUNT(*) FROM moz_bookmarks WHERE type=1;"`
2. Check plugin is built: `ls -lh ~/.local/share/rofi/plugins/bookmarks.so`
3. Verify rofi finds plugin: `rofi -show bookmarks -plugin-path ~/.local/share/rofi/plugins -dump-config 2>&1 | grep bookmarks`

### "No supported browsers found"

1. Install Firefox/Waterfox: `firefox` or `waterfox` binary must be in `$PATH`
2. Verify profile directory exists: `ls -d ~/.mozilla/firefox` or `ls -d ~/.waterfox`

### Plugin Won't Load

```bash
# Check for missing dependencies
pkg-config --cflags --libs glib-2.0 cairo sqlite3 json-glib-1.0

# Rebuild completely
cd rofi-bookmarks
rm -rf build
bash install.sh
```

## Phase 2: Planned Features

- [ ] Chromium/Chrome bookmark support (JSON parsing)
- [ ] Add bookmark via helper script + UI indicator
- [ ] Edit bookmark title/URL
- [ ] Delete bookmark
- [ ] Import bookmarks from HTML/other formats
- [ ] Search/filter bookmarks (rofi native fuzzy matching)
- [ ] Bookmark categories/tags

## Technical Stack

- **Language**: C
- **Build System**: CMake
- **Rofi Integration**: Rofi mode API (mode.h, helper.h)
- **Dependencies**:
  - `glib-2.0`: Memory, data structures, file I/O
  - `cairo`: Graphics (not used directly in bookmarks mode)
  - `sqlite3`: Firefox bookmark database access
  - `json-glib-1.0`: Chromium bookmark parsing (planned)
- **Installation**: User-local plugin system (~/.local/share/rofi/plugins)

## License

Same as parent project Scripts collection

## Author

Created as replacement for rbw-based encrypted bookmark manager with direct browser integration.
