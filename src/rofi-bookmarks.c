#define G_LOG_DOMAIN "Bookmarks"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <gmodule.h>
#include <rofi/mode.h>
#include <rofi/helper.h>
#include <rofi/mode-private.h>
#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>

G_MODULE_EXPORT Mode mode;

// Bookmark structure
typedef struct {
    char *name;
    char *url;
} Bookmark;

// Plugin private data
typedef struct {
    Bookmark *bookmarks;
    int num_bookmarks;
    int bookmark_capacity;
    bool edit_mode;
    bool settings_mode;
    int selected_bookmark_idx;
    char *encrypted_file;
    char *temp_file;
} BookmarkModePrivateData;

// Key types for detection
typedef enum {
    KEY_NONE,
    KEY_ENTER,
    KEY_CUSTOM_ACTION
} BMKey;

// Function declarations
static int bookmarks_init(Mode *sw);
static unsigned int bookmarks_get_num_entries(const Mode *sw);
static ModeMode bookmarks_result(Mode *sw, int mretv, char **input, unsigned int selected_line);
static void bookmarks_destroy(Mode *sw);
static char* bookmarks_get_display_value(const Mode *sw, unsigned int index, int *state, GList **attr_list, int get_entry);
static int bookmarks_token_match(const Mode *sw, rofi_int_matcher **tokens, unsigned int index);
static char* bookmarks_get_message(const Mode *sw);

// Get password from rbw
static char* get_password() {
    FILE *fp = popen("rbw get Default 2>/dev/null", "r");
    if (!fp) return NULL;
    
    char *password = g_malloc0(256);
    if (fgets(password, 256, fp) == NULL) {
        g_free(password);
        pclose(fp);
        return NULL;
    }
    
    password[strcspn(password, "\n")] = 0;
    pclose(fp);
    
    if (password[0] == '\0') {
        g_free(password);
        return NULL;
    }
    
    return password;
}

// Decrypt bookmarks file
static bool decrypt_bookmarks(BookmarkModePrivateData *pd) {
    if (!g_file_test(pd->encrypted_file, G_FILE_TEST_EXISTS)) {
        return true; // No file yet
    }
    
    char *password = get_password();
    if (!password) return false;
    
    // Create temp file
    char template[] = "/tmp/rofi-bookmarks-XXXXXX";
    int fd = mkstemp(template);
    if (fd == -1) {
        g_free(password);
        return false;
    }
    close(fd);
    
    pd->temp_file = g_strdup(template);
    
    // Decrypt
    char *cmd = g_strdup_printf(
        "gpg --batch --yes --passphrase-fd 0 --decrypt '%s' > '%s' 2>/dev/null",
        pd->encrypted_file, pd->temp_file
    );
    
    FILE *gpg = popen(cmd, "w");
    g_free(cmd);
    
    if (!gpg) {
        g_free(password);
        return false;
    }
    
    fprintf(gpg, "%s\n", password);
    g_free(password);
    pclose(gpg);
    
    return true;
}

// Encrypt bookmarks file
static void encrypt_bookmarks(BookmarkModePrivateData *pd) {
    if (!pd->temp_file || !g_file_test(pd->temp_file, G_FILE_TEST_EXISTS)) {
        return;
    }
    
    char *password = get_password();
    if (!password) return;
    
    // Ensure directory exists
    char *dir = g_path_get_dirname(pd->encrypted_file);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    
    // Encrypt
    char *cmd = g_strdup_printf(
        "gpg --batch --yes --passphrase-fd 0 --symmetric --cipher-algo AES256 -o '%s' '%s' 2>/dev/null",
        pd->encrypted_file, pd->temp_file
    );
    
    FILE *gpg = popen(cmd, "w");
    g_free(cmd);
    
    if (gpg) {
        fprintf(gpg, "%s\n", password);
        pclose(gpg);
    }
    
    g_free(password);
}

// Load bookmarks from temp file
static void load_bookmarks(BookmarkModePrivateData *pd) {
    if (!pd->temp_file || !g_file_test(pd->temp_file, G_FILE_TEST_EXISTS)) {
        return;
    }
    
    FILE *fp = fopen(pd->temp_file, "r");
    if (!fp) return;
    
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *sep = strchr(line, '|');
        if (!sep) continue;
        
        *sep = '\0';
        char *name = line;
        char *url = sep + 1;
        
        // Remove newline
        url[strcspn(url, "\n")] = 0;
        
        // Add bookmark
        if (pd->num_bookmarks >= pd->bookmark_capacity) {
            pd->bookmark_capacity = pd->bookmark_capacity == 0 ? 10 : pd->bookmark_capacity * 2;
            pd->bookmarks = g_realloc(pd->bookmarks, pd->bookmark_capacity * sizeof(Bookmark));
        }
        
        pd->bookmarks[pd->num_bookmarks].name = g_strdup(name);
        pd->bookmarks[pd->num_bookmarks].url = g_strdup(url);
        pd->num_bookmarks++;
    }
    
    fclose(fp);
}

// Save bookmarks to temp file
static void save_bookmarks(BookmarkModePrivateData *pd) {
    if (!pd->temp_file) return;
    
    FILE *fp = fopen(pd->temp_file, "w");
    if (!fp) return;
    
    for (int i = 0; i < pd->num_bookmarks; i++) {
        fprintf(fp, "%s|%s\n", pd->bookmarks[i].name, pd->bookmarks[i].url);
    }
    
    fclose(fp);
}

// Cleanup temp file
static void cleanup_temp_file(BookmarkModePrivateData *pd) {
    if (pd->temp_file) {
        if (g_file_test(pd->temp_file, G_FILE_TEST_EXISTS)) {
            char *cmd = g_strdup_printf("shred -u '%s' 2>/dev/null", pd->temp_file);
            system(cmd);
            g_free(cmd);
        }
        g_free(pd->temp_file);
        pd->temp_file = NULL;
    }
}

// Delete bookmark
static void delete_bookmark(BookmarkModePrivateData *pd, int index) {
    if (index < 0 || index >= pd->num_bookmarks) return;
    
    // Free bookmark
    g_free(pd->bookmarks[index].name);
    g_free(pd->bookmarks[index].url);
    
    // Shift bookmarks
    for (int i = index; i < pd->num_bookmarks - 1; i++) {
        pd->bookmarks[i] = pd->bookmarks[i + 1];
    }
    pd->num_bookmarks--;
    
    // Save and encrypt
    save_bookmarks(pd);
    encrypt_bookmarks(pd);
}

// Get key from mretv (same as rofi-file-browser-extended)
static BMKey get_key_from_mretv(int mretv) {
    if ((mretv & MENU_CUSTOM_ACTION) == MENU_CUSTOM_ACTION) {
        return KEY_CUSTOM_ACTION;
    } else if ((mretv & MENU_OK) == MENU_OK) {
        return KEY_ENTER;
    }
    return KEY_NONE;
}

// Initialize mode
static int bookmarks_init(Mode *sw) {
    if (mode_get_private_data(sw) != NULL) {
        return true;
    }
    
    BookmarkModePrivateData *pd = g_malloc0(sizeof(*pd));
    mode_set_private_data(sw, pd);
    
    // Setup paths - store in Dropbox
    const char *home = g_get_home_dir();
    char *dropbox_dir = g_build_filename(home, "Dropbox", "rofi-bookmarks", NULL);
    g_mkdir_with_parents(dropbox_dir, 0700);
    
    pd->encrypted_file = g_build_filename(dropbox_dir, "bookmarks.gpg", NULL);
    g_free(dropbox_dir);
    
    // Create symlink from old location for compatibility
    const char *data_home = g_get_user_data_dir();
    char *old_dir = g_build_filename(data_home, "rofi-bookmarks", NULL);
    char *old_file = g_build_filename(old_dir, "bookmarks.gpg", NULL);
    
    // Create symlink if old location doesn't exist
    if (!g_file_test(old_file, G_FILE_TEST_EXISTS)) {
        g_mkdir_with_parents(old_dir, 0700);
        symlink(pd->encrypted_file, old_file);
    }
    
    g_free(old_dir);
    g_free(old_file);
    
    pd->edit_mode = false;
    pd->settings_mode = false;
    pd->selected_bookmark_idx = -1;
    
    // Load bookmarks
    if (g_file_test(pd->encrypted_file, G_FILE_TEST_EXISTS)) {
        decrypt_bookmarks(pd);
        load_bookmarks(pd);
    }
    
    return true;
}

// Cleanup and destroy
static void bookmarks_destroy(Mode *sw) {
    BookmarkModePrivateData *pd = (BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd) return;
    
    // Free bookmarks
    for (int i = 0; i < pd->num_bookmarks; i++) {
        g_free(pd->bookmarks[i].name);
        g_free(pd->bookmarks[i].url);
    }
    g_free(pd->bookmarks);
    
    cleanup_temp_file(pd);
    g_free(pd->encrypted_file);
    g_free(pd);
    
    mode_set_private_data(sw, NULL);
}

// Get number of entries
static unsigned int bookmarks_get_num_entries(const Mode *sw) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    
    if (pd->edit_mode) {
        return 2; // Edit, Delete
    } else if (pd->settings_mode) {
        return 2; // Purge, Import from Firefox
    } else {
        return pd->num_bookmarks + 2; // +1 for "Add Bookmark", +1 for "Settings"
    }
}

// Handle result selection
static ModeMode bookmarks_result(Mode *sw, int mretv, char **input, unsigned int selected_line) {
    BookmarkModePrivateData *pd = (BookmarkModePrivateData *)mode_get_private_data(sw);
    BMKey key = get_key_from_mretv(mretv);
    
    // Handle edit mode
    if (pd->edit_mode) {
        if (mretv & MENU_OK) {
            if (selected_line == 0) { // Edit
                // Launch helper script for editing
                if (pd->selected_bookmark_idx >= 0 && pd->selected_bookmark_idx < pd->num_bookmarks) {
                    Bookmark *bm = &pd->bookmarks[pd->selected_bookmark_idx];
                    char *helper = g_find_program_in_path("rofi-bookmarks-helper");
                    if (!helper) {
                        helper = g_strdup("/usr/local/bin/rofi-bookmarks-helper");
                    }
                    char *cmd = g_strdup_printf("'%s' edit '%s'", helper, bm->name);
                    helper_execute_command(NULL, cmd, false, NULL);
                    g_free(cmd);
                    g_free(helper);
                    return MODE_EXIT; // Exit so helper can relaunch
                }
            } else if (selected_line == 1) { // Delete
                delete_bookmark(pd, pd->selected_bookmark_idx);
                // Reload bookmarks
                for (int i = 0; i < pd->num_bookmarks; i++) {
                    g_free(pd->bookmarks[i].name);
                    g_free(pd->bookmarks[i].url);
                }
                pd->num_bookmarks = 0;
                load_bookmarks(pd);
            }
            pd->edit_mode = false;
            pd->selected_bookmark_idx = -1;
            return RESET_DIALOG;
        } else if (mretv & MENU_CANCEL) {
            pd->edit_mode = false;
            pd->selected_bookmark_idx = -1;
            return RESET_DIALOG;
        }
    }
    
    // Handle settings mode
    if (pd->settings_mode) {
        if (mretv & MENU_OK) {
            if (selected_line == 0) { // Purge
                char *helper = g_find_program_in_path("rofi-bookmarks-helper");
                if (!helper) {
                    helper = g_strdup("/usr/local/bin/rofi-bookmarks-helper");
                }
                char *cmd = g_strdup_printf("'%s' purge", helper);
                helper_execute_command(NULL, cmd, false, NULL);
                g_free(cmd);
                g_free(helper);
                return MODE_EXIT;
            } else if (selected_line == 1) { // Import from Firefox
                char *helper = g_find_program_in_path("rofi-bookmarks-helper");
                if (!helper) {
                    helper = g_strdup("/usr/local/bin/rofi-bookmarks-helper");
                }
                char *cmd = g_strdup_printf("'%s' import-firefox", helper);
                helper_execute_command(NULL, cmd, false, NULL);
                g_free(cmd);
                g_free(helper);
                return MODE_EXIT;
            }
        } else if (mretv & MENU_CANCEL) {
            pd->settings_mode = false;
            return RESET_DIALOG;
        }
    }
    
    // Handle kb-accept-alt (Shift+Return) in main mode - show edit/delete dialog
    if (key == KEY_CUSTOM_ACTION && selected_line > 1) {
        pd->edit_mode = true;
        pd->selected_bookmark_idx = selected_line - 2;
        return RESET_DIALOG;
    }
    
    // Handle main mode - Enter key
    if (mretv & MENU_OK) {
        if (selected_line == 0) {
            // Add Bookmark - launch helper script
            char *helper = g_find_program_in_path("rofi-bookmarks-helper");
            if (!helper) {
                helper = g_strdup("/usr/local/bin/rofi-bookmarks-helper");
            }
            char *cmd = g_strdup_printf("'%s' add", helper);
            helper_execute_command(NULL, cmd, false, NULL);
            g_free(cmd);
            g_free(helper);
            return MODE_EXIT; // Exit so helper can relaunch
        } else if (selected_line == 1) {
            // Open Settings submenu
            pd->settings_mode = true;
            return RESET_DIALOG;
        } else {
            // Open bookmark
            int idx = selected_line - 2;
            if (idx >= 0 && idx < pd->num_bookmarks) {
                char *cmd = g_strdup_printf("xdg-open '%s'", pd->bookmarks[idx].url);
                helper_execute_command(NULL, cmd, false, NULL);
                g_free(cmd);
                return MODE_EXIT;
            }
        }
    } else if (mretv & MENU_CANCEL) {
        return MODE_EXIT;
    }
    
    return RELOAD_DIALOG;
}

// Token matching
static int bookmarks_token_match(const Mode *sw, rofi_int_matcher **tokens, unsigned int index) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    
    if (pd->edit_mode) {
        const char *options[] = {"Edit", "Delete"};
        return helper_token_match(tokens, options[index]);
    } else if (pd->settings_mode) {
        const char *options[] = {"Purge All Bookmarks", "Import from Firefox"};
        return helper_token_match(tokens, options[index]);
    } else {
        if (index == 0) {
            return helper_token_match(tokens, "Add Bookmark");
        } else if (index == 1) {
            return helper_token_match(tokens, "Bookmark Settings");
        } else {
            return helper_token_match(tokens, pd->bookmarks[index - 2].name);
        }
    }
}

// Get display value
static char* bookmarks_get_display_value(const Mode *sw, unsigned int index, int *state, G_GNUC_UNUSED GList **attr_list, int get_entry) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    
    if (pd->edit_mode) {
        const char *options[] = {"Edit", "Delete"};
        return get_entry ? g_strdup(options[index]) : NULL;
    } else if (pd->settings_mode) {
        const char *options[] = {"🗑️ Purge All Bookmarks", "🦊 Import from Firefox"};
        return get_entry ? g_strdup(options[index]) : NULL;
    } else {
        if (index == 0) {
            return get_entry ? g_strdup("➕ Add Bookmark") : NULL;
        } else if (index == 1) {
            return get_entry ? g_strdup("⚙️ Bookmark Settings") : NULL;
        } else {
            int idx = index - 2;
            if (idx < pd->num_bookmarks) {
                return get_entry ? g_strdup_printf("🔖 %s", pd->bookmarks[idx].name) : NULL;
            }
        }
    }
    
    return NULL;
}

// Get message
static char* bookmarks_get_message(const Mode *sw) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    
    if (pd->edit_mode) {
        return g_strdup("Select action");
    } else if (pd->settings_mode) {
        return g_strdup("Select setting");
    } else {
        return g_strdup("Enter: Open/Add | Shift+Enter: Edit/Delete");
    }
}

// Mode definition
Mode mode = {
    .abi_version = ABI_VERSION,
    .name = "bookmarks",
    .cfg_name_key = "display-bookmarks",
    ._init = bookmarks_init,
    ._get_num_entries = bookmarks_get_num_entries,
    ._result = bookmarks_result,
    ._destroy = bookmarks_destroy,
    ._token_match = bookmarks_token_match,
    ._get_display_value = bookmarks_get_display_value,
    ._get_message = bookmarks_get_message,
    ._get_completion = NULL,
    ._preprocess_input = NULL,
    .private_data = NULL,
    .free = NULL
};
