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
static char* bookmarks_get_icon(const Mode *sw, unsigned int selected_line, unsigned int *height);

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
    
    // Setup paths
    const char *data_home = g_get_user_data_dir();
    char *data_dir = g_build_filename(data_home, "rofi-bookmarks", NULL);
    g_mkdir_with_parents(data_dir, 0700);
    
    pd->encrypted_file = g_build_filename(data_dir, "bookmarks.gpg", NULL);
    g_free(data_dir);
    
