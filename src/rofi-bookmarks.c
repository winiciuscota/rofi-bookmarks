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
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>
#include <sys/stat.h>

G_MODULE_EXPORT Mode mode;

typedef enum {
    BROWSER_FIREFOX,
    BROWSER_WATERFOX,
    BROWSER_CHROMIUM
} BrowserType;

typedef enum {
    VIEW_MAIN,
    VIEW_SETTINGS,
    VIEW_BROWSERS,
    VIEW_ACTIONS
} View;

typedef struct {
    BrowserType type;
    char *name;
    char *base_dir;
    char *profile_dir;
    char *db_path;
    char *bookmarks_path;
} Browser;

typedef struct {
    char *display;
    char *title;
    char *url;
    BrowserType browser_type;
    long long ff_bookmark_id;
    char *chromium_guid;
} Bookmark;

typedef struct {
    GPtrArray *browsers;
    int selected_browser;
    
    GPtrArray *bookmarks;
    View view;
    int selected_bookmark_idx;
    
    char *data_dir;
    char *pref_file;
    char *status_msg;
} BookmarkModePrivateData;

// ----- UTILITIES -----

static void set_status(BookmarkModePrivateData *pd, const char *fmt, ...) {
    if (!pd) return;
    g_free(pd->status_msg);
    va_list ap;
    va_start(ap, fmt);
    pd->status_msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
}

static void bookmark_free(Bookmark *bm) {
    if (!bm) return;
    g_free(bm->display);
    g_free(bm->title);
    g_free(bm->url);
    g_free(bm->chromium_guid);
    g_free(bm);
}

static void browser_free(Browser *b) {
    if (!b) return;
    g_free(b->name);
    g_free(b->base_dir);
    g_free(b->profile_dir);
    g_free(b->db_path);
    g_free(b->bookmarks_path);
    g_free(b);
}

static bool file_mtime(const char *path, time_t *out_mtime) {
    struct stat st;
    if (g_stat(path, &st) != 0) return false;
    if (out_mtime) *out_mtime = st.st_mtime;
    return true;
}

static char *read_file_trim(const char *path) {
    g_autofree char *content = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &content, &len, NULL) || !content) return NULL;
    g_strstrip(content);
    return g_strdup(content);
}

static void write_file_atomic(const char *path, const char *content) {
    g_autofree char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_file_set_contents(path, content, -1, NULL);
}

static void open_url_async(const char *url) {
    if (!url || url[0] == 0) return;
    g_autofree char *q = g_shell_quote(url);
    g_autofree char *cmd = g_strdup_printf("xdg-open %s &", q);
    helper_execute_command(NULL, cmd, false, NULL);
}

static void copy_to_clipboard_async(const char *text) {
    if (!text) return;
    g_autofree char *q = g_shell_quote(text);
    g_autofree char *cmd = g_strdup_printf(
        "sh -c \"if command -v wl-copy >/dev/null 2>&1; then printf '%%s' %s | wl-copy; "
        "elif command -v xclip >/dev/null 2>&1; then printf '%%s' %s | xclip -selection clipboard; fi\" &",
        q, q
    );
    helper_execute_command(NULL, cmd, false, NULL);
}

// ----- BROWSER DETECTION -----

static void detect_browsers(BookmarkModePrivateData *pd) {
    if (!pd) return;
    if (!pd->browsers) {
        pd->browsers = g_ptr_array_new_with_free_func((GDestroyNotify)browser_free);
    } else {
        g_ptr_array_set_size(pd->browsers, 0);
    }

    const char *home = g_get_home_dir();

    // Firefox
    {
        g_autofree char *dir = g_build_filename(home, ".mozilla", "firefox", NULL);
        g_autofree char *bin = g_find_program_in_path("firefox");
        if (bin && g_file_test(dir, G_FILE_TEST_IS_DIR)) {
            Browser *b = g_malloc0(sizeof(*b));
            b->type = BROWSER_FIREFOX;
            b->name = g_strdup("Firefox");
            b->base_dir = g_strdup(dir);
            g_ptr_array_add(pd->browsers, b);
        }
    }

    // Waterfox
    {
        g_autofree char *dir = g_build_filename(home, ".waterfox", NULL);
        g_autofree char *bin = g_find_program_in_path("waterfox");
        if (bin && g_file_test(dir, G_FILE_TEST_IS_DIR)) {
            Browser *b = g_malloc0(sizeof(*b));
            b->type = BROWSER_WATERFOX;
            b->name = g_strdup("Waterfox");
            b->base_dir = g_strdup(dir);
            g_ptr_array_add(pd->browsers, b);
        }
    }

    // Chromium
    {
        g_autofree char *dir = g_build_filename(home, ".config", "chromium", NULL);
        g_autofree char *bin = g_find_program_in_path("chromium");
        if (bin && g_file_test(dir, G_FILE_TEST_IS_DIR)) {
            Browser *b = g_malloc0(sizeof(*b));
            b->type = BROWSER_CHROMIUM;
            b->name = g_strdup("Chromium");
            b->base_dir = g_strdup(dir);
            g_ptr_array_add(pd->browsers, b);
        }
    }
}

static Browser *get_selected_browser(BookmarkModePrivateData *pd) {
    if (!pd || !pd->browsers || pd->browsers->len == 0) return NULL;
    if (pd->selected_browser < 0 || (guint)pd->selected_browser >= pd->browsers->len) {
        pd->selected_browser = 0;
    }
    return g_ptr_array_index(pd->browsers, (guint)pd->selected_browser);
}

static int find_browser_index_by_name(BookmarkModePrivateData *pd, const char *name) {
    if (!pd || !pd->browsers || !name) return -1;
    for (guint i = 0; i < pd->browsers->len; i++) {
        Browser *b = g_ptr_array_index(pd->browsers, i);
        if (b && g_strcmp0(b->name, name) == 0) return (int)i;
    }
    return -1;
}

static void load_selected_browser_pref(BookmarkModePrivateData *pd) {
    if (!pd) return;
    g_autofree char *saved = read_file_trim(pd->pref_file);
    if (!saved) {
        pd->selected_browser = 0;
        return;
    }
    int idx = find_browser_index_by_name(pd, saved);
    pd->selected_browser = (idx >= 0) ? idx : 0;
}

static void save_selected_browser_pref(BookmarkModePrivateData *pd) {
    Browser *b = get_selected_browser(pd);
    if (!pd || !b) return;
    write_file_atomic(pd->pref_file, b->name);
}

// ----- PROFILE RESOLUTION -----

static char *find_most_recent_firefox_profile_dir(const char *base_dir) {
    if (!base_dir) return NULL;
    g_autofree char *ini_path = g_build_filename(base_dir, "profiles.ini", NULL);
    if (!g_file_test(ini_path, G_FILE_TEST_EXISTS)) return NULL;

    g_autoptr(GKeyFile) kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, ini_path, G_KEY_FILE_NONE, NULL)) return NULL;

    gsize n_groups = 0;
    g_autofree char **groups = g_key_file_get_groups(kf, &n_groups);

    time_t best_mtime = 0;
    g_autofree char *best = NULL;

    for (gsize i = 0; i < n_groups; i++) {
        const char *grp = groups[i];
        if (!g_str_has_prefix(grp, "Profile")) continue;
        g_autofree char *path = g_key_file_get_string(kf, grp, "Path", NULL);
        if (!path) continue;
        gboolean is_rel = g_key_file_get_boolean(kf, grp, "IsRelative", NULL);

        g_autofree char *prof_dir = is_rel ? g_build_filename(base_dir, path, NULL) : g_strdup(path);
        g_autofree char *db = g_build_filename(prof_dir, "places.sqlite", NULL);
        time_t mt = 0;
        if (!file_mtime(db, &mt)) continue;
        if (mt >= best_mtime) {
            best_mtime = mt;
            g_free(best);
            best = g_strdup(prof_dir);
        }
    }
    return best ? g_strdup(best) : NULL;
}

static void resolve_browser_paths(BookmarkModePrivateData *pd, Browser *b) {
    if (!pd || !b) return;
    g_free(b->profile_dir);
    g_free(b->db_path);
    g_free(b->bookmarks_path);
    b->profile_dir = NULL;
    b->db_path = NULL;
    b->bookmarks_path = NULL;

    if (b->type == BROWSER_FIREFOX || b->type == BROWSER_WATERFOX) {
        b->profile_dir = find_most_recent_firefox_profile_dir(b->base_dir);
        if (b->profile_dir) {
            b->db_path = g_build_filename(b->profile_dir, "places.sqlite", NULL);
        }
    }
}

// ----- BOOKMARK LOADING -----

static void clear_bookmarks(BookmarkModePrivateData *pd) {
    if (!pd) return;
    if (!pd->bookmarks) {
        pd->bookmarks = g_ptr_array_new_with_free_func((GDestroyNotify)bookmark_free);
        return;
    }
    g_ptr_array_set_size(pd->bookmarks, 0);
}

static char *copy_to_temp_file(const char *src_path) {
    if (!src_path || !g_file_test(src_path, G_FILE_TEST_EXISTS)) return NULL;

    g_autofree char *tmpl = g_strdup("/tmp/rofi-bookmarks-db-XXXXXX");
    int fd = g_mkstemp(tmpl);
    if (fd < 0) return NULL;
    close(fd);

    g_autoptr(GError) err = NULL;
    g_autoptr(GFile) src = g_file_new_for_path(src_path);
    g_autoptr(GFile) dst = g_file_new_for_path(tmpl);
    if (!g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &err)) {
        g_remove(tmpl);
        return NULL;
    }
    return g_strdup(tmpl);
}

static void load_firefox_bookmarks(BookmarkModePrivateData *pd, Browser *b) {
    if (!pd || !b || !b->db_path) return;
    clear_bookmarks(pd);

    g_autofree char *tmp_db = copy_to_temp_file(b->db_path);
    if (!tmp_db) {
        set_status(pd, "Failed to copy places.sqlite");
        return;
    }

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(tmp_db, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        set_status(pd, "Failed to open places.sqlite: %s", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        g_remove(tmp_db);
        return;
    }

    const char *sql =
        "WITH RECURSIVE roots(id) AS ("
        "  SELECT id FROM moz_bookmarks WHERE parent=1 AND type=2 AND title IN ('menu','toolbar','unfiled')"
        "), folders(id, parent, path, level) AS ("
        "  SELECT id, parent, '' AS path, 0 AS level FROM moz_bookmarks WHERE id IN (SELECT id FROM roots)"
        "  UNION ALL"
        "  SELECT b.id, b.parent,"
        "         CASE WHEN f.path='' THEN b.title ELSE f.path || ' > ' || b.title END,"
        "         f.level + 1"
        "  FROM moz_bookmarks b JOIN folders f ON b.parent = f.id"
        "  WHERE b.type=2 AND b.title IS NOT NULL AND b.title!='' AND f.level < 25"
        ")"
        "SELECT mb.id,"
        "       CASE WHEN COALESCE(f.path,'')='' THEN COALESCE(NULLIF(mb.title,''), mp.url) ELSE f.path || ' > ' || COALESCE(NULLIF(mb.title,''), mp.url) END AS display_title,"
        "       COALESCE(NULLIF(mb.title,''), mp.url) AS title, mp.url"
        " FROM moz_bookmarks mb"
        " JOIN moz_places mp ON mp.id = mb.fk"
        " LEFT JOIN folders f ON f.id = mb.parent"
        " WHERE mb.type=1 AND mp.url IS NOT NULL AND mp.url!=''"
        " ORDER BY display_title COLLATE NOCASE;";

    sqlite3_stmt *st = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        set_status(pd, "Failed to prepare query: %s", sqlite3_errmsg(db));
        if (st) sqlite3_finalize(st);
        sqlite3_close(db);
        g_remove(tmp_db);
        return;
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        long long bkid = sqlite3_column_int64(st, 0);
        const unsigned char *disp = sqlite3_column_text(st, 1);
        const unsigned char *title = sqlite3_column_text(st, 2);
        const unsigned char *url = sqlite3_column_text(st, 3);
        if (!disp || !url) continue;

        Bookmark *bm = g_malloc0(sizeof(*bm));
        bm->browser_type = b->type;
        bm->ff_bookmark_id = bkid;
        bm->display = g_strdup((const char *)disp);
        bm->title = title ? g_strdup((const char *)title) : g_strdup((const char *)disp);
        bm->url = g_strdup((const char *)url);
        g_ptr_array_add(pd->bookmarks, bm);
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    g_remove(tmp_db);
}

static void reload_selected_bookmarks(BookmarkModePrivateData *pd) {
    if (!pd) return;
    clear_bookmarks(pd);
    Browser *b = get_selected_browser(pd);
    if (!b) {
        set_status(pd, "No supported browsers found");
        return;
    }

    resolve_browser_paths(pd, b);

    if ((b->type == BROWSER_FIREFOX || b->type == BROWSER_WATERFOX) && (!b->profile_dir || !b->db_path)) {
        set_status(pd, "%s: no profile with places.sqlite found", b->name);
        return;
    }

    if (b->type == BROWSER_FIREFOX || b->type == BROWSER_WATERFOX) {
        load_firefox_bookmarks(pd, b);
    }
}

// ----- ROFI PLUGIN INTERFACE -----

typedef enum {
    KEY_NONE,
    KEY_ENTER,
    KEY_CUSTOM_ACTION
} BMKey;

static BMKey get_key_from_mretv(int mretv) {
    if ((mretv & MENU_CUSTOM_ACTION) == MENU_CUSTOM_ACTION) {
        return KEY_CUSTOM_ACTION;
    }
    if ((mretv & MENU_OK) == MENU_OK) {
        return KEY_ENTER;
    }
    return KEY_NONE;
}

static int bookmarks_init(Mode *sw) {
    if (mode_get_private_data(sw) != NULL) {
        return true;
    }

    BookmarkModePrivateData *pd = g_malloc0(sizeof(*pd));
    mode_set_private_data(sw, pd);

    pd->data_dir = g_build_filename(g_get_user_data_dir(), "rofi-bookmarks", NULL);
    g_mkdir_with_parents(pd->data_dir, 0700);
    pd->pref_file = g_build_filename(pd->data_dir, "browser_preference", NULL);

    pd->bookmarks = g_ptr_array_new_with_free_func((GDestroyNotify)bookmark_free);
    pd->browsers = g_ptr_array_new_with_free_func((GDestroyNotify)browser_free);
    pd->selected_browser = 0;
    pd->view = VIEW_MAIN;
    pd->selected_bookmark_idx = -1;

    detect_browsers(pd);
    load_selected_browser_pref(pd);
    reload_selected_bookmarks(pd);

    return true;
}

static void bookmarks_destroy(Mode *sw) {
    BookmarkModePrivateData *pd = (BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd) return;

    if (pd->bookmarks) g_ptr_array_free(pd->bookmarks, TRUE);
    if (pd->browsers) g_ptr_array_free(pd->browsers, TRUE);
    g_free(pd->data_dir);
    g_free(pd->pref_file);
    g_free(pd->status_msg);
    g_free(pd);
    mode_set_private_data(sw, NULL);
}

static unsigned int bookmarks_get_num_entries(const Mode *sw) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd) return 0;

    switch (pd->view) {
        case VIEW_SETTINGS:
            return 2; // Change browser, Refresh
        case VIEW_BROWSERS:
            return pd->browsers ? pd->browsers->len : 0;
        case VIEW_ACTIONS:
            return 2; // Open, Copy URL
        case VIEW_MAIN:
        default:
            return (pd->bookmarks ? pd->bookmarks->len : 0) + 1; // Settings + bookmarks
    }
}

static ModeMode bookmarks_result(Mode *sw, int mretv, char **input, unsigned int selected_line) {
    BookmarkModePrivateData *pd = (BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd) return MODE_EXIT;

    BMKey key = get_key_from_mretv(mretv);

    // Esc: back/exit
    if (mretv & MENU_CANCEL) {
        if (pd->view == VIEW_MAIN) {
            return MODE_EXIT;
        }
        if (pd->view == VIEW_ACTIONS) {
            pd->view = VIEW_MAIN;
            pd->selected_bookmark_idx = -1;
            return RESET_DIALOG;
        }
        if (pd->view == VIEW_BROWSERS) {
            pd->view = VIEW_SETTINGS;
            return RESET_DIALOG;
        }
        if (pd->view == VIEW_SETTINGS) {
            pd->view = VIEW_MAIN;
            return RESET_DIALOG;
        }
    }

    if (pd->view == VIEW_MAIN) {
        // Shift+Enter: open action menu
        if (key == KEY_CUSTOM_ACTION && selected_line > 0) {
            pd->selected_bookmark_idx = (int)selected_line - 1;
            pd->view = VIEW_ACTIONS;
            return RESET_DIALOG;
        }

        // Enter: open bookmark or settings
        if (mretv & MENU_OK) {
            if (selected_line == 0) {
                pd->view = VIEW_SETTINGS;
                return RESET_DIALOG;
            }
            int idx = (int)selected_line - 1;
            if (idx >= 0 && pd->bookmarks && (guint)idx < pd->bookmarks->len) {
                Bookmark *bm = g_ptr_array_index(pd->bookmarks, (guint)idx);
                open_url_async(bm->url);
                return MODE_EXIT;
            }
        }
        return RELOAD_DIALOG;
    }

    if (pd->view == VIEW_ACTIONS) {
        if (!(mretv & MENU_OK)) return RELOAD_DIALOG;
        if (!pd->bookmarks || pd->selected_bookmark_idx < 0 || (guint)pd->selected_bookmark_idx >= pd->bookmarks->len) {
            pd->view = VIEW_MAIN;
            return RESET_DIALOG;
        }
        Bookmark *bm = g_ptr_array_index(pd->bookmarks, (guint)pd->selected_bookmark_idx);
        if (!bm) {
            pd->view = VIEW_MAIN;
            return RESET_DIALOG;
        }

        if (selected_line == 0) {
            open_url_async(bm->url);
            return MODE_EXIT;
        }
        if (selected_line == 1) {
            copy_to_clipboard_async(bm->url);
            return MODE_EXIT;
        }
    }

    if (pd->view == VIEW_SETTINGS) {
        if (!(mretv & MENU_OK)) return RELOAD_DIALOG;
        if (selected_line == 0) {
            pd->view = VIEW_BROWSERS;
            return RESET_DIALOG;
        }
        if (selected_line == 1) {
            set_status(pd, NULL);
            reload_selected_bookmarks(pd);
            pd->view = VIEW_MAIN;
            return RESET_DIALOG;
        }
    }

    if (pd->view == VIEW_BROWSERS) {
        if (!(mretv & MENU_OK)) return RELOAD_DIALOG;
        if (!pd->browsers || pd->browsers->len == 0) {
            pd->view = VIEW_SETTINGS;
            return RESET_DIALOG;
        }
        if (selected_line < pd->browsers->len) {
            pd->selected_browser = (int)selected_line;
            save_selected_browser_pref(pd);
            set_status(pd, NULL);
            reload_selected_bookmarks(pd);
        }
        pd->view = VIEW_SETTINGS;
        return RESET_DIALOG;
    }

    (void)input;
    return RELOAD_DIALOG;
}

static int bookmarks_token_match(const Mode *sw, rofi_int_matcher **tokens, unsigned int index) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd) return false;

    if (pd->view == VIEW_ACTIONS) {
        const char *opts[] = {"Open", "Copy URL"};
        return helper_token_match(tokens, opts[index]);
    }
    if (pd->view == VIEW_SETTINGS) {
        const char *opts[] = {"Change browser", "Refresh"};
        return helper_token_match(tokens, opts[index]);
    }
    if (pd->view == VIEW_BROWSERS) {
        if (!pd->browsers) return false;
        Browser *b = g_ptr_array_index(pd->browsers, index);
        return helper_token_match(tokens, b ? b->name : "");
    }

    // main
    if (index == 0) return helper_token_match(tokens, "Settings");
    guint idx = index - 1;
    if (!pd->bookmarks || idx >= pd->bookmarks->len) return false;
    Bookmark *bm = g_ptr_array_index(pd->bookmarks, idx);
    return helper_token_match(tokens, bm ? bm->display : "");
}

static char* bookmarks_get_display_value(const Mode *sw, unsigned int index, int *state, G_GNUC_UNUSED GList **attr_list, int get_entry) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd || !get_entry) return NULL;

    if (pd->view == VIEW_ACTIONS) {
        const char *opts[] = {"🌐 Open", "📋 Copy URL"};
        return g_strdup(opts[index]);
    }

    if (pd->view == VIEW_SETTINGS) {
        Browser *b = get_selected_browser((BookmarkModePrivateData *)pd);
        const char *current = b ? b->name : "None";
        if (index == 0) return g_strdup_printf("🌐 Change browser (%s)", current);
        if (index == 1) return g_strdup("🔄 Refresh");
        return NULL;
    }

    if (pd->view == VIEW_BROWSERS) {
        if (!pd->browsers || index >= pd->browsers->len) return NULL;
        Browser *b = g_ptr_array_index(pd->browsers, index);
        if (!b) return NULL;
        if ((int)index == pd->selected_browser) {
            return g_strdup_printf("✓ %s", b->name);
        }
        return g_strdup_printf("  %s", b->name);
    }

    // main
    if (index == 0) return g_strdup("⚙ Settings");
    guint idx = index - 1;
    if (!pd->bookmarks || idx >= pd->bookmarks->len) return NULL;
    Bookmark *bm = g_ptr_array_index(pd->bookmarks, idx);
    if (!bm) return NULL;
    (void)state;
    return g_strdup_printf("📌 %s", bm->display);
}

static char* bookmarks_get_message(const Mode *sw) {
    const BookmarkModePrivateData *pd = (const BookmarkModePrivateData *)mode_get_private_data(sw);
    if (!pd) return g_strdup("");
    if (pd->status_msg && pd->status_msg[0] != 0) {
        return g_strdup(pd->status_msg);
    }
    return g_strdup("Enter: Open | Shift+Enter: Actions | Esc: Back/Close");
}

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
