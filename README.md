# 🔖 Rofi Bookmarks Manager

A secure bookmark manager using rofi plugin with GPG encrypted storage.

## ✨ Features

- 🔐 **Encrypted Storage**: Bookmarks stored in GPG-encrypted file (AES256)
- 🔑 **Password from Bitwarden**: Encryption password automatically retrieved from `rbw get Default`
- 📂 **Hierarchical Organization**: Organize bookmarks with folders (e.g., `Work > Projects > MyApp`)
- ➕ **Easy Management**: Add, edit, delete bookmarks through intuitive rofi interface
- 🚀 **Native Plugin**: Built as proper rofi plugin in C for performance
- 🌐 **Browser Integration**: Opens URLs in your default browser
- ��️ **Secure Deletion**: Temporary files are shredded after use
- 🎯 **Intuitive UX**: 
  - `Enter` to open bookmark
  - `Shift+Enter` for edit/delete menu
  - Inline "Add Bookmark" option

## 📋 Requirements

- `rofi` (>= 1.6.1) - Menu system
- `gpg` - GPG encryption
- `rbw` - Bitwarden CLI client (for password retrieval)
- `xdg-open` - Open URLs in default browser
- `cmake` (>= 3.10) - Build system
- `glib-2.0` - Development libraries
- `cairo` - Graphics library

## 📦 Installation

### Prerequisites

1. Ensure you have a password stored in rbw named "Default":
   ```bash
   rbw add Default
   # Enter a strong encryption password when prompted
   ```

### Building from Source

```bash
cd rofi-bookmarks
cmake .
make
sudo make install
sudo install -m 755 rofi-bookmarks-helper /usr/local/bin/
```

### Arch Linux (PKGBUILD)

```bash
makepkg -si
```

## 🚀 Usage

### Running the Plugin

```bash
rofi -show bookmarks
```

Or use the launcher script:
```bash
./rofi-bookmarks-launcher
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Enter` | Open bookmark or select option |
| `Shift+Enter` | Show edit/delete menu |
| `Esc` | Cancel/Exit |

### Adding a Bookmark

1. Select "➕ Add Bookmark" (first option)
2. Enter the bookmark name
   - Use `Folder > Name` for hierarchy
   - Example: `Work > Projects > MyApp`
3. Enter the bookmark URL

### Editing a Bookmark

1. Select a bookmark
2. Press `Shift+Enter`
3. Choose "Edit"
4. Modify the name and/or URL

### Deleting a Bookmark

1. Select a bookmark
2. Press `Shift+Enter`
3. Choose "Delete"
4. Confirm deletion

## 📁 Data Storage

Bookmarks are stored at:
```
~/.local/share/rofi-bookmarks/bookmarks.gpg
```

The file is only decrypted when needed and immediately re-encrypted after modification.

## 🔒 Security

- **Encryption**: AES256 via GPG symmetric encryption
- **Password Storage**: Encryption key stored in Bitwarden (via rbw)
- **Secure Cleanup**: Temporary decrypted files are shredded after use
- **No Plain Text**: Bookmarks never stored unencrypted on disk

### Security Workflow

1. Plugin requests password from `rbw get Default`
2. File is decrypted to temporary location
3. User performs action (add/edit/delete)
4. File is immediately re-encrypted
5. Temporary file is securely shredded

## 🗂️ File Structure

```
rofi-bookmarks/
├── src/
│   └── rofi-bookmarks-clean.c    # Main plugin source (C)
├── rofi-bookmarks                # Standalone script version
├── rofi-bookmarks-helper         # Helper for add/edit operations
├── rofi-bookmarks-launcher       # Convenience launcher
├── CMakeLists.txt               # Build configuration
├── PKGBUILD                     # Arch Linux package
├── README.md                    # This file
└── LICENSE                      # MIT License
```

## 🗑️ Uninstall

```bash
sudo rm $(pkg-config --variable=pluginsdir rofi)/bookmarks.so
sudo rm /usr/local/bin/rofi-bookmarks-helper
```

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details

## 🙏 Credits

- Built with [rofi](https://github.com/davatorium/rofi)
- Uses [rbw](https://github.com/doy/rbw) for Bitwarden integration
- Encryption via [GnuPG](https://gnupg.org/)
