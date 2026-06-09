/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2002 Daniel Stenberg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "string-extra.h"
#include "panic.h"

#include "applimits.h"
#include "dir.h"
#include "file.h"
#include "lcd.h"
#include "font.h"
#include "button.h"
#include "kernel.h"
#include "usb.h"
#include "tree.h"
#include "audio.h"
#include "playlist.h"
#include "menu.h"
#include "skin_engine/skin_engine.h"
#include "settings.h"
#include "debug.h"
#include "storage.h"
#include "rolo.h"
#include "icons.h"
#include "lang.h"
#include "screens.h"
#include "keyboard.h"
#include "bookmark.h"
#include "onplay.h"
#include "core_alloc.h"
#include "power.h"
#include "action.h"
#include "talk.h"
#include "filetypes.h"
#include "misc.h"
#include "pathfuncs.h"
#include "filetree.h"
#include "tagtree.h"
#ifdef HAVE_RECORDING
#include "recorder/recording.h"
#endif
#include "rtc.h"
#include "dircache.h"
#ifdef HAVE_TAGCACHE
#include "tagcache.h"
#include "metadata.h"
#include "recorder/albumart.h"
#include "recorder/bmp.h"
#ifdef HAVE_JPEG
#include "recorder/jpeg_load.h"
#endif
#include "crc32.h"
#endif /* HAVE_TAGCACHE */
#include "yesno.h"
#include "eeprom_settings.h"
#include "playlist_catalog.h"

/* gui api */
#include "list.h"
#include "splash.h"
#include "quickscreen.h"
#include "shortcuts.h"
#include "appevents.h"

#include "root_menu.h"

static struct gui_synclist tree_lists;

/* I put it here because other files doesn't use it yet,
 * but should be elsewhere since it will be used mostly everywhere */
static struct tree_context tc;

char lastfile[MAX_PATH];
static char lastdir[MAX_PATH];
#ifdef HAVE_TAGCACHE
static int lasttable, lastextra;
#endif

static bool reload_dir = false;

static bool start_wps = false;
static int curr_context = false;/* id3db or tree*/

static int dirbrowse(void);
static int ft_play_dirname(char* name);
static int ft_play_filename(char *dir, char *file, int attr);
static void say_filetype(int attr);

struct entry* tree_get_entries(struct tree_context *t)
{
    return core_get_data(t->cache.entries_handle);
}

struct entry* tree_get_entry_at(struct tree_context *t, int index)
{
    if(index < 0 || index >= t->cache.max_entries)
        return NULL; /* no entry */
    struct entry* entries = tree_get_entries(t);
    return &entries[index];
}

static struct entry *get_valid_entry(const char* funcname,
                                     struct tree_context *t, int index)
{
    struct entry *entry = tree_get_entry_at(t, index);
    if (!entry)
        panicf("Invalid tree entry %s", funcname);
    /*DEBUGF("%s tc: %x idx: %d\n", funcname, t, index);*/
    return entry;
}

static bool ext_stripit(bool isdir, int attr, int dirfilter)
{
    if((dirfilter != SHOW_ID3DB) && !isdir)
    {
        switch(global_settings.show_filename_ext)
        {
            case 0:
                /* show file extension: off */
                return true;
                break;
            case 1:
                /* show file extension: on */
                break;
            case 2:
                /* show file extension: only unknown types */
                return filetype_supported(attr);
            case 3:
            default:
                /* show file extension: only when viewing all */
                return (dirfilter != SHOW_ALL);
        }
    }
    return false;
}

static const char* tree_get_filename(int selected_item, void *data,
                                     char *buffer, size_t buffer_len)
{
    struct tree_context * local_tc=(struct tree_context *)data;
    char *name;
    int attr=0;
#ifdef HAVE_TAGCACHE
    bool id3db = *(local_tc->dirfilter) == SHOW_ID3DB;

    if (id3db)
    {
        return tagtree_get_entry_name(&tc, selected_item, buffer, buffer_len);
    }
    else
#endif
    {
        struct entry *entry = get_valid_entry(__func__, local_tc, selected_item);
        name = entry->name;
        attr = entry->attr;
    }

    if(ext_stripit((attr & ATTR_DIRECTORY), attr, *(local_tc->dirfilter)))
    {
        return(strip_extension(buffer, buffer_len, name));
    }
    return(name);
}

#ifdef HAVE_LCD_COLOR
static int tree_get_filecolor(int selected_item, void * data)
{
    if (*tc.dirfilter == SHOW_ID3DB)
        return -1;
    struct tree_context * local_tc=(struct tree_context *)data;
    struct entry *entry = get_valid_entry(__func__, local_tc, selected_item);

    return filetype_get_color(entry->name, entry->attr);
}
#endif

static enum themable_icons tree_get_fileicon(int selected_item, void * data)
{
    struct tree_context * local_tc=(struct tree_context *)data;
#ifdef HAVE_TAGCACHE
    bool id3db = *(local_tc->dirfilter) == SHOW_ID3DB;
    if (id3db) {
        return tagtree_get_icon(&tc);
    }
    else
#endif
    {
        struct entry *entry = get_valid_entry(__func__, local_tc, selected_item);

        return filetype_get_icon(entry->attr);
    }
}

static int tree_voice_cb(int selected_item, void * data)
{
    struct tree_context * local_tc=(struct tree_context *)data;
    unsigned char *name;
    int attr=0;
    int customaction = ONPLAY_NO_CUSTOMACTION;
#ifdef HAVE_TAGCACHE
    bool id3db = *(local_tc->dirfilter) == SHOW_ID3DB;
    char buf[AVERAGE_FILENAME_LENGTH*2];

    if (id3db)
    {
        attr = tagtree_get_attr(local_tc);
        name = tagtree_get_entry_name(local_tc, selected_item, buf, sizeof(buf));
        customaction = tagtree_get_custom_action(local_tc);

        /* See if name is an encoded ID, if it is, then speak it normally */
        int lang_id = P2ID(name);
        /*debugf("%s Found name %s id %d\n", __func__, P2STR(name), lang_id);*/
        if (lang_id >= 0) {
            if (global_settings.talk_menu)
                talk_id(lang_id, true);
            return 0;
        }

        /* Otherwise, it is either a custom "header" or a database entry,
           so try to look up a talk clip for it. */

        // XXX this needs further work, so disable it for now
        // -- need to distinguish between "headers" and entries
        // each entry type ("artist", "album", etc) should be delineated
        // so we can split the clips into subdirs.
#if 0
        if (global_settings.talk_file_clip) {
            if (talk_file(LANG_DIR"/database/", NULL,
                          P2STR(name), file_thumbnail_ext, NULL, true) > 0)
                return 0;

            // XXX fall back to spelling it out?
            return 0;
        }
#endif
    }
    else
#endif
    {
        struct entry *entry = get_valid_entry(__func__, local_tc, selected_item);
        name = entry->name;
        attr = entry->attr;
    }
    bool is_dir = (attr & ATTR_DIRECTORY);
    bool did_clip = false;
    /* First the .talk clip case */
    if(is_dir)
    {
        if(global_settings.talk_dir_clip)
        {
            did_clip = true;
            if (ft_play_dirname(name) <= 0)
                /* failed, not existing */
                did_clip = false;
        }
    } else { /* it's a file */
        if (global_settings.talk_file_clip && (attr & FILE_ATTR_THUMBNAIL))
        {
            did_clip = true;
            if (ft_play_filename(local_tc->currdir, name, attr) <= 0)
                /* failed, not existing */
                did_clip = false;
        }
    }
    bool spell_name = (customaction == ONPLAY_CUSTOMACTION_FIRSTLETTER);
    if(!did_clip)
    {
        /* say the number or spell if required or as a fallback */
        switch (is_dir ? global_settings.talk_dir : global_settings.talk_file)
        {
        case 1: /* as numbers */
            talk_id(is_dir ? VOICE_DIR : VOICE_FILE, false);
            talk_number(selected_item+1        - (is_dir ? 0 : local_tc->dirsindir),
                        true);
            break;
        case 2: /* spelled */
            talk_shutup();
            if(global_settings.talk_filetype)
            {
                if(is_dir)
                    talk_id(VOICE_DIR, true);
            }
            spell_name = true;
            break;
        }
    }

    if(global_settings.talk_filetype && !is_dir
       && *local_tc->dirfilter < NUM_FILTER_MODES)
    {
        say_filetype(attr);
    }

    /* spell name AFTER voicing filetype */
    if (spell_name) {
            bool stripit = ext_stripit(is_dir, attr, *(local_tc->dirfilter));
            char *ext = NULL;

            /* Don't spell the extension if it's not displayed */

            if (stripit) {
                ext = strrchr(name, '.');
                if (ext)
                    *ext = 0;
            }

        talk_spell(name, true);

        if (stripit && ext)
            *ext = '.';
    }

    return 0;
}

bool check_rockboxdir(void)
{
    if(!dir_exists(ROCKBOX_DIR))
    {   /* No need to localise this message.
           If .rockbox is missing, it wouldn't work anyway */
        FOR_NB_SCREENS(i)
            screens[i].clear_display();
        splash(HZ*2, "No .rockbox directory");
        FOR_NB_SCREENS(i)
            screens[i].clear_display();
        splash(HZ*2, "Installation incomplete");
        return false;
    }
    return true;
}

/* do this really late in the init sequence */
#ifdef HAVE_TAGCACHE
static void aa_thread_init(void);
#endif

void tree_init(void)
{
    check_rockboxdir();
    strcpy(tc.currdir, "/");
#ifdef HAVE_TAGCACHE
    aa_thread_init();
#endif
}

struct tree_context* tree_get_context(void)
{
    return &tc;
}

void tree_lock_cache(struct tree_context *t)
{
    core_pin(t->cache.name_buffer_handle);
    core_pin(t->cache.entries_handle);
}

void tree_unlock_cache(struct tree_context *t)
{
    core_unpin(t->cache.name_buffer_handle);
    core_unpin(t->cache.entries_handle);
}

/*
 * Returns the position of a given file in the current directory
 * returns -1 if not found
 */
static int tree_get_file_position(char * filename)
{
    int i, ret = -1;/* no file match, return undefined */

    tree_lock_cache(&tc);
    struct entry *entries = tree_get_entries(&tc);

    /* use lastfile to determine the selected item (default=0) */
    for (i=0; i < tc.filesindir; i++)
    {
#if ((CONFIG_PLATFORM & PLATFORM_NATIVE) || defined(__APPLE__) || defined(_WIN32) || defined(__CYGWIN__))
        if (!strcasecmp(entries[i].name, filename))
#else
        if (!strcmp(entries[i].name, filename))
#endif
        {
            ret = i;
            break;
        }
    }
    tree_unlock_cache(&tc);
    return(ret);
}

#ifdef HAVE_TAGCACHE
#define AA_THUMB            128  /* max decoded thumbnail size (buffer ceiling) */
#define AA_SLOTS            32   /* LRU cache slots */
#define AA_PREFETCH_AHEAD   5    /* items to prefetch in the scroll direction */
#define AA_THUMBCACHE_DIR   ROCKBOX_DIR "/thumbcache"

/* Standard art sizes matching the menu options table */
static const int aa_standard_sizes[] = {16,20,24,28,32,36,40,44,64,96,128};
#define AA_NUM_STD_SIZES ((int)(sizeof(aa_standard_sizes)/sizeof(aa_standard_sizes[0])))

/* Decode-thread message IDs */
#define AA_Q_DECODE      1
#define AA_Q_QUIT        2
#define AA_Q_REFRESH     3
#define AA_Q_REFRESH_ALL 4
#define AA_Q_PRUNE       5

typedef struct {
    int  item_idx;                  /* -1 = unused */
    bool has_art;                   /* pixels[] is valid */
    bool pending;                   /* decode in progress on bg thread */
    int  width, height;             /* actual decoded dimensions */
    char track_path[MAX_PATH];      /* resolved by main thread before queuing */
    char album_name[MAX_PATH];      /* used by find_albumart as a hint */
    fb_data pixels[AA_THUMB * AA_THUMB];
} aa_entry_t;

static aa_entry_t         aa_cache[AA_SLOTS];
static int                aa_cache_table = -1;
static int                aa_cache_extra = -1;
static char               aa_cache_dir[MAX_PATH] = "";
static int                aa_evict      = 0;
static int                aa_scroll_dir = 0;   /* +1 down, -1 up, 0 unknown */
static int                aa_prev_start = -1;  /* start_item from previous frame */
static int                aa_thumb_sz   = AA_THUMB;
static int                aa_art_pad    = 0;   /* per-side padding around artwork */

static struct mutex       aa_mutex;
static struct event_queue aa_queue;
static long               aa_thread_stack[(DEFAULT_STACK_SIZE + 0x2000) / sizeof(long)];
static unsigned int       aa_thread_id = 0;
static volatile bool      aa_redraw_needed = false;
static volatile bool      aa_build_stop    = false; /* set by aa_thumbcache_build_stop() */
static aa_entry_t         aa_gen_slot;              /* reusable slot for bulk thumb generation */

/* Live build progress — updated by decode thread, read by UI thread (no lock needed;
 * values are informational and short-tearing is harmless).
 * struct aa_build_stat is declared in tree.h. */
static struct aa_build_stat aa_bstat;

/* Fallback image: theme-supplied BMP drawn for items with no art.
 * Source path comes from global_settings.thumb_fallback_file (theme-configurable).
 * A scaled .bin is cached in thumbcache/%dp/, keyed on CRC32(path). */
#define AA_FALLBACK_CACHE_FMT  AA_THUMBCACHE_DIR "/%dp/fallback_%08x.bin"
static fb_data           aa_fallback_pixels[AA_THUMB * AA_THUMB];
static volatile int      aa_fallback_sz  = 0; /* 0 = not loaded; set by decode thread */
static volatile uint32_t aa_fallback_crc = 0; /* CRC32 of the loaded source path */

/* Large enough for JPEG decode overhead + pixel data; owned by decode thread */
#define AA_DECODE_BUF_SIZE (AA_THUMB * AA_THUMB * sizeof(fb_data) + 50000)
static unsigned char aa_decode_buf[AA_DECODE_BUF_SIZE];

/* Clear all cache slots. Caller must hold aa_mutex. */
static void _aa_do_invalidate(void)
{
    for (int i = 0; i < AA_SLOTS; i++)
    {
        aa_cache[i].item_idx = -1;
        aa_cache[i].pending  = false;
    }
    aa_cache_table  = -1;
    aa_cache_extra  = -1;
    aa_cache_dir[0] = '\0';
    aa_evict        = 0;
    aa_scroll_dir   = 0;
    aa_prev_start   = -1;
}

/* Invalidate from outside a mutex-held context (e.g. update_dir). */
static void aa_invalidate(void)
{
    mutex_lock(&aa_mutex);
    _aa_do_invalidate();
    mutex_unlock(&aa_mutex);
}

/* True if name is a "no value for this tag" placeholder rather than a real
 * album: either the raw tagcache sentinel ("<Untagged>", stored verbatim in
 * a track's album_name) or the localized [Untagged] display string (used as
 * the entry name when browsing albums). Such groupings have no consistent
 * artwork, so they're treated the same as non-album/non-track items. */
static bool aa_name_is_untagged(const char *name)
{
    return name && name[0] &&
           (strcmp(name, UNTAGGED) == 0 ||
            strcmp(name, (const char *)str(LANG_TAGNAVI_UNTAGGED)) == 0);
}

/* Resolve item_idx → album_name for use as the cache key. Returns true only
 * for items that represent a real album or a real track with album info —
 * i.e. items eligible for cached art and the fallback image alike.
 * Only looks up the name from the in-memory tagtree buffer — no tagcache
 * searches, so this is safe and fast to call from the draw callback.
 * track_path is left empty; the full tagcache lookup happens only in the
 * decode thread (aa_generate_to_slot / aa_build_thumbcache). */
static bool aa_resolve_path(int item_idx, char *track_path, char *album_name)
{
    track_path[0] = '\0';
    album_name[0] = '\0';

    if (*(tc.dirfilter) != SHOW_ID3DB)
        return false; /* file browser: no art */

    /* Virtual navigation entries ([By Album], [All Tracks], [Random], …) occupy
     * the first special_entry_count slots and have no associated album art. */
    if (item_idx < tc.special_entry_count)
        return false;

    int attr = tagtree_get_attr(&tc);
    if (attr == FILE_ATTR_AUDIO)
    {
        /* Track (TABLE_NAVIBROWSE tag_title, TABLE_ALLSUBENTRIES*): use the
         * per-entry album_name field which is populated from tag_album for every
         * track regardless of which table we're in.  This is correct even in
         * [All Tracks] / [By Album] views where tagtree_get_title() returns
         * the artist name, not the album name. */
        if (!tagtree_get_entry_album(&tc, item_idx, album_name, MAX_PATH))
            return false;
    }
    else
    {
        /* Album view (TABLE_NAVIBROWSE tag_album): the entry name IS the album. */
        if (tagtree_browse_tag(&tc) != tag_album) return false;
        if (!tagtree_get_entry_name(&tc, item_idx, album_name, MAX_PATH))
            return false;
    }

    if (aa_name_is_untagged(album_name))
        return false;

    return true;
}

/* Ensure the per-size subdirectory exists (no-op if already present). */
static void aa_ensure_size_dir(int sz)
{
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), AA_THUMBCACHE_DIR "/%dp", sz);
    mkdir(dir);
}

/* Build the disk-cache path keyed on the album name; thumbs live in Np/ subdirs. */
static void aa_thumb_cache_path(char *buf, const char *album_name, int sz)
{
    uint32_t hash = crc_32(album_name, strlen(album_name), 0xffffffff);
    snprintf(buf, MAX_PATH, AA_THUMBCACHE_DIR "/%dp/t%08x.bin",
             sz, (unsigned)hash);
}

static void aa_fallback_cache_path(char *buf, const char *source_path, int sz)
{
    uint32_t hash = crc_32(source_path, strlen(source_path), 0xffffffff);
    snprintf(buf, MAX_PATH, AA_FALLBACK_CACHE_FMT, sz, (unsigned)hash);
}

/* Read a pre-built thumbnail from the disk cache into slot->pixels.
 * Browse-time path: no JPEG decode, no fallback — cache or nothing. */
static bool aa_load_from_cache(aa_entry_t *slot, int thumb_sz)
{
    char cache_path[MAX_PATH];
    aa_thumb_cache_path(cache_path, slot->album_name, thumb_sz);
    int expected_bytes = thumb_sz * thumb_sz * (int)sizeof(fb_data);

    int cfd = open(cache_path, O_RDONLY);
    if (cfd < 0) return false;

    bool ok = (filesize(cfd) == expected_bytes &&
               read(cfd, aa_decode_buf, expected_bytes) == expected_bytes);
    close(cfd);
    if (!ok) return false;

    slot->width  = thumb_sz;
    slot->height = thumb_sz;
    memcpy(slot->pixels, aa_decode_buf, expected_bytes);
    return true;
}

/* Decode art from slot->track_path via JPEG/BMP, write the disk cache, and
 * populate slot->pixels. Only called from aa_build_thumbcache, never during
 * live browsing. */
static bool aa_generate_to_slot(aa_entry_t *slot, int thumb_sz)
{
    char cache_path[MAX_PATH];
    aa_thumb_cache_path(cache_path, slot->album_name, thumb_sz);
    int expected_bytes = thumb_sz * thumb_sz * (int)sizeof(fb_data);

    /* Skip if already cached at this size */
    int cfd = open(cache_path, O_RDONLY);
    if (cfd >= 0)
    {
        bool ok = (filesize(cfd) == expected_bytes);
        close(cfd);
        if (ok) return true;
    }

    struct mp3entry id3;
    memset(&id3, 0, sizeof(id3));
    strmemccpy(id3.path, slot->track_path, sizeof(id3.path));
    id3.album = slot->album_name;

    const struct dim dim = { thumb_sz, thumb_sz };
    char art_path[MAX_PATH];
    bool found_file = find_albumart(&id3, art_path, sizeof(art_path), &dim);
    bool use_embedded = false;

    if (!found_file)
    {
        if (get_metadata(&id3, -1, slot->track_path))
        {
#ifdef HAVE_JPEG
            if (id3.has_embedded_albumart &&
                (id3.albumart.type & AA_CLEAR_FLAGS_MASK) == AA_TYPE_JPG)
                use_embedded = true;
#endif
        }
        if (!use_embedded)
            return false;
    }

    struct bitmap bm;
    memset(&bm, 0, sizeof(bm));
    bm.width  = thumb_sz;
    bm.height = thumb_sz;
    bm.data   = aa_decode_buf;

    int rc = -1;
    if (use_embedded)
    {
#ifdef HAVE_JPEG
        int fd = open(slot->track_path, O_RDONLY);
        if (fd >= 0)
        {
            lseek(fd, id3.albumart.pos, SEEK_SET);
            rc = clip_jpeg_fd(fd, id3.albumart.type, id3.albumart.size,
                              &bm, sizeof(aa_decode_buf),
                              FORMAT_NATIVE | FORMAT_RESIZE, NULL);
            close(fd);
        }
#endif
    }
    else
    {
        const char *ext = strrchr(art_path, '.');
#if defined(HAVE_JPEG)
        if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0))
            rc = read_jpeg_file(art_path, &bm, sizeof(aa_decode_buf),
                                FORMAT_NATIVE | FORMAT_RESIZE, NULL);
        else
#endif
            rc = read_bmp_file(art_path, &bm, sizeof(aa_decode_buf),
                               FORMAT_NATIVE | FORMAT_RESIZE, NULL);
    }

    if (rc < 0) return false;

    slot->width  = bm.width;
    slot->height = bm.height;
    int copy_bytes = bm.width * bm.height * sizeof(fb_data);
    if (copy_bytes > (int)sizeof(slot->pixels))
        copy_bytes = sizeof(slot->pixels);
    memcpy(slot->pixels, aa_decode_buf, copy_bytes);

    if (bm.width == thumb_sz && bm.height == thumb_sz)
    {
        aa_ensure_size_dir(thumb_sz);
        int wfd = open(cache_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (wfd >= 0)
        {
            write(wfd, aa_decode_buf, expected_bytes);
            close(wfd);
        }
    }

    return true;
}

/* Delete every file in every Np/ subdir of the thumbcache (and the subdirs
 * themselves).  Plain files at the top level are also removed for safety. */
void aa_thumbcache_clear(void)
{
    DIR *topdir = opendir(AA_THUMBCACHE_DIR);
    if (!topdir) return;
    struct dirent *de;
    char path[MAX_PATH];
    while ((de = readdir(topdir)) != NULL)
    {
        if (de->d_name[0] == '.') continue;
        snprintf(path, MAX_PATH, AA_THUMBCACHE_DIR "/%s", de->d_name);
        DIR *subdir = opendir(path);
        if (subdir)
        {
            struct dirent *sde;
            char subpath[MAX_PATH];
            while ((sde = readdir(subdir)) != NULL)
            {
                if (sde->d_name[0] == '.') continue;
                snprintf(subpath, sizeof(subpath), "%s/%s", path, sde->d_name);
                remove(subpath);
            }
            closedir(subdir);
            rmdir(path);
        }
        else
        {
            remove(path);
        }
    }
    closedir(topdir);
}

/* Decode the BMP at source_path, scale to sz, and write a .bin to thumbcache.
 * Caller must have already confirmed source_path exists with open().
 * Used by aa_build_thumbcache (bulk) and aa_load_fallback (on-demand). */
static void aa_build_fallback_cache(const char *source_path, int sz)
{
    char cache_path[MAX_PATH];
    aa_fallback_cache_path(cache_path, source_path, sz);
    int expected = sz * sz * (int)sizeof(fb_data);

    int cfd = open(cache_path, O_RDONLY);
    if (cfd >= 0)
    {
        bool ok = (filesize(cfd) == expected);
        close(cfd);
        if (ok) return;
    }

    struct bitmap bm;
    memset(&bm, 0, sizeof(bm));
    bm.width  = sz;
    bm.height = sz;
    bm.data   = aa_decode_buf;
    int rc = read_bmp_file(source_path, &bm, sizeof(aa_decode_buf),
                           FORMAT_NATIVE | FORMAT_RESIZE, NULL);
    if (rc < 0 || bm.width != sz || bm.height != sz) return;

    aa_ensure_size_dir(sz);
    int wfd = open(cache_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (wfd >= 0)
    {
        write(wfd, aa_decode_buf, expected);
        close(wfd);
    }
}

/* Load the fallback image for thumb_sz into aa_fallback_pixels.
 * Source path comes from global_settings.thumb_fallback_file.
 * Tries the pre-built .bin in thumbcache first; generates it on-demand if
 * absent.  Silently does nothing if no fallback file is configured or the
 * source BMP is missing.  Runs in the decode thread only. */
static void aa_load_fallback(int thumb_sz)
{
    aa_fallback_sz  = 0;
    aa_fallback_crc = 0;

    const char *source_path = (const char *)global_settings.thumb_fallback_file;
    if (!source_path[0]) return;

    uint32_t path_crc = crc_32(source_path, strlen(source_path), 0xffffffff);
    char cache_path[MAX_PATH];
    aa_fallback_cache_path(cache_path, source_path, thumb_sz);
    int expected = thumb_sz * thumb_sz * (int)sizeof(fb_data);

    int cfd = open(cache_path, O_RDONLY);
    if (cfd >= 0)
    {
        bool ok = (filesize(cfd) == expected &&
                   read(cfd, (void *)aa_fallback_pixels, expected) == expected);
        close(cfd);
        if (ok) { aa_fallback_sz = thumb_sz; aa_fallback_crc = path_crc; return; }
    }

    /* .bin absent — check source exists to avoid DEBUGF from read_bmp_file */
    int bfd = open(source_path, O_RDONLY);
    if (bfd < 0) return;
    close(bfd);

    aa_build_fallback_cache(source_path, thumb_sz);

    cfd = open(cache_path, O_RDONLY);
    if (cfd < 0) return;
    bool ok = (filesize(cfd) == expected &&
               read(cfd, (void *)aa_fallback_pixels, expected) == expected);
    close(cfd);
    if (ok) { aa_fallback_sz = thumb_sz; aa_fallback_crc = path_crc; }
}

/* Return the effective art size: explicit user setting > theme hint > live value. */
static int aa_effective_size(void)
{
    if (global_settings.thumb_art_size > 0)
        return MIN(global_settings.thumb_art_size, AA_THUMB);
    if (global_settings.thumb_theme_size > 0)
        return MIN(global_settings.thumb_theme_size, AA_THUMB);
    return aa_thumb_sz;
}

/* Generate thumbnails for all albums at a single explicit size.
 * Runs in the decode thread; updates aa_bstat; honours aa_build_stop. */
static void aa_build_thumbcache_at_sz(int sz)
{
    if (sz < 8 || !tagcache_is_usable()) return;

    aa_ensure_size_dir(sz);
    aa_bstat.current_sz = sz;
    aa_bstat.processed  = 0;

    struct tagcache_search tcs;
    if (!tagcache_search(&tcs, tag_album))
        return;

    char prev_album[MAX_PATH] = "";

    while (tagcache_get_next(&tcs, aa_gen_slot.album_name, MAX_PATH))
    {
        if (aa_build_stop) break;

        if (strcmp(aa_gen_slot.album_name, prev_album) == 0)
        {
            yield();
            continue;
        }
        strmemccpy(prev_album, aa_gen_slot.album_name, MAX_PATH);
        strmemccpy(aa_bstat.current_album, aa_gen_slot.album_name,
                   sizeof(aa_bstat.current_album));

        struct tagcache_search tcs2;
        struct tagcache_search_clause clause;
        memset(&clause, 0, sizeof(clause));
        clause.tag     = tag_album;
        clause.type    = clause_is;
        clause.numeric = false;
        clause.source  = source_constant;
        clause.str     = aa_gen_slot.album_name;

        aa_gen_slot.track_path[0] = '\0';
        if (tagcache_search(&tcs2, tag_filename))
        {
            tagcache_search_add_clause(&tcs2, &clause);
            tagcache_get_next(&tcs2, aa_gen_slot.track_path, MAX_PATH);
            tagcache_search_finish(&tcs2);
        }

        if (!aa_gen_slot.track_path[0]) { yield(); continue; }

        aa_generate_to_slot(&aa_gen_slot, sz);
        aa_bstat.processed++;
        yield();
    }
    tagcache_search_finish(&tcs);

    /* Generate fallback .bin for this size */
    const char *fb_path = (const char *)global_settings.thumb_fallback_file;
    if (fb_path[0] && !aa_build_stop)
    {
        int bfd = open(fb_path, O_RDONLY);
        if (bfd >= 0) { close(bfd); aa_build_fallback_cache(fb_path, sz); }
    }
}

/* Build at the currently active/configured size. */
static void aa_build_thumbcache(void)
{
    int sz = aa_effective_size();
    if (sz < 8) return;
    aa_build_stop      = false;
    aa_bstat.active    = true;
    aa_bstat.all_sizes = false;
    aa_build_thumbcache_at_sz(sz);
    aa_bstat.active = false;
    aa_bstat.current_album[0] = '\0';
}

/* Build at every standard size in sequence. */
static void aa_build_all_sizes(void)
{
    aa_build_stop      = false;
    aa_bstat.active    = true;
    aa_bstat.all_sizes = true;
    for (int i = 0; i < AA_NUM_STD_SIZES && !aa_build_stop; i++)
        aa_build_thumbcache_at_sz(aa_standard_sizes[i]);
    aa_bstat.active = false;
    aa_bstat.current_album[0] = '\0';
}

/* Public: post a single-size build to the decode thread. */
void aa_thumbcache_build_start(void)
{
    aa_build_stop = false;
    queue_post(&aa_queue, AA_Q_REFRESH, 0);
}

/* Public: post an all-sizes build to the decode thread. */
void aa_thumbcache_build_all_start(void)
{
    aa_build_stop = false;
    queue_post(&aa_queue, AA_Q_REFRESH_ALL, 0);
}

/* Public: cancel the current build (checked per album in the build loop). */
void aa_thumbcache_build_stop(void)
{
    aa_build_stop = true;
}

/* Public: read-only pointer to live build progress (informational; no lock). */
const struct aa_build_stat *aa_get_build_stat(void)
{
    return &aa_bstat;
}

/* --- Orphan prune --------------------------------------------------------
 * Remove .bin files in the thumbcache whose album is no longer in the DB.
 * Runs in the decode thread.  Uses a temporary sorted array of valid CRC32
 * hashes; skips pruning if the allocation fails. */
static int aa_cmp_u32(const void *a, const void *b)
{
    uint32_t ua = *(const uint32_t*)a, ub = *(const uint32_t*)b;
    return (ua > ub) - (ua < ub);
}

static void _aa_do_prune(void)
{
    if (!tagcache_is_usable()) return;

    /* Phase 1: collect valid album hashes */
    const int max_albums = 8192;
    int handle = core_alloc((size_t)max_albums * sizeof(uint32_t));
    if (handle < 0) return;
    uint32_t *valid = core_get_data(handle);
    int n_valid = 0;

    struct tagcache_search tcs;
    if (tagcache_search(&tcs, tag_album))
    {
        char album_name[MAX_PATH];
        char prev[MAX_PATH] = "";
        while (tagcache_get_next(&tcs, album_name, MAX_PATH) && n_valid < max_albums)
        {
            if (strcmp(album_name, prev) == 0) { yield(); continue; }
            strmemccpy(prev, album_name, MAX_PATH);
            valid[n_valid++] = crc_32(album_name, strlen(album_name), 0xffffffff);
            yield();
        }
        tagcache_search_finish(&tcs);
    }
    qsort(valid, (size_t)n_valid, sizeof(uint32_t), aa_cmp_u32);

    /* Phase 2: scan every Np/ subdir; delete .bin files not in valid set */
    for (int i = 0; i < AA_NUM_STD_SIZES; i++)
    {
        if (aa_build_stop) break;
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), AA_THUMBCACHE_DIR "/%dp", aa_standard_sizes[i]);
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) != NULL)
        {
            if (de->d_name[0] != 't') continue;
            unsigned hash_val;
            if (sscanf(de->d_name + 1, "%08x", &hash_val) != 1) continue;
            uint32_t hash = (uint32_t)hash_val;
            /* Binary search in sorted valid[] for hash */
            bool found = false;
            {
                int lo = 0, hi = n_valid - 1;
                while (lo <= hi) {
                    int mid = lo + (hi - lo) / 2;
                    if (valid[mid] == hash)      { found = true; break; }
                    else if (valid[mid] < hash)    lo = mid + 1;
                    else                           hi = mid - 1;
                }
            }
            if (!found)
            {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
                remove(path);
            }
            yield();
        }
        closedir(d);
    }

    core_free(handle);
}

/* Public: post a prune job to the decode thread. */
void aa_thumbcache_prune(void)
{
    queue_post(&aa_queue, AA_Q_PRUNE, 0);
}

/* Background decode thread: waits for requests, decodes, writes result to cache. */
static void aa_decode_thread(void)
{
    struct queue_event ev;
    while (1)
    {
        queue_wait(&aa_queue, &ev);
        if (ev.id == AA_Q_QUIT)        break;
        if (ev.id == AA_Q_REFRESH)     { aa_build_thumbcache();  continue; }
        if (ev.id == AA_Q_REFRESH_ALL) { aa_build_all_sizes();   continue; }
        if (ev.id == AA_Q_PRUNE)       { _aa_do_prune();         continue; }
        if (ev.id != AA_Q_DECODE) continue;

        int item_idx = (int)ev.data;
        int thumb_sz = aa_effective_size();
        if (thumb_sz < 8) thumb_sz = aa_thumb_sz;

        /* Reload fallback if size or configured source path changed */
        {
            const char *fb = (const char *)global_settings.thumb_fallback_file;
            uint32_t fb_crc = fb[0]
                ? crc_32(fb, strlen(fb), 0xffffffff) : 0;
            if (aa_fallback_sz != thumb_sz || aa_fallback_crc != fb_crc)
                aa_load_fallback(thumb_sz);
        }

        /* Find the pending slot (brief lock to read pointer) */
        mutex_lock(&aa_mutex);
        aa_entry_t *slot = NULL;
        for (int i = 0; i < AA_SLOTS; i++)
        {
            if (aa_cache[i].item_idx == item_idx && aa_cache[i].pending)
            {
                slot = &aa_cache[i];
                break;
            }
        }
        mutex_unlock(&aa_mutex);

        if (!slot) continue; /* evicted before we could decode */

        /* Load from disk cache only — no JPEG decode during live browsing */
        bool has_art = aa_load_from_cache(slot, thumb_sz);

        /* Write result under lock, only if slot still belongs to this item */
        mutex_lock(&aa_mutex);
        if (slot->item_idx == item_idx && slot->pending)
        {
            slot->has_art = has_art;
            slot->pending = false;
        }
        else if (!slot->pending)
        {
            /* Slot was reassigned; discard — pixels were written but won't be read */
        }
        mutex_unlock(&aa_mutex);

        /* Wake the main thread so it redraws without waiting for the HZ/2 timeout */
        aa_redraw_needed = true;
        button_queue_try_post(BUTTON_NONE, 0);
    }
}

/* Allocate a cache slot for item_idx and post a decode request.
 * Caller must NOT hold aa_mutex. track_path/album_name must already be set.
 * Eviction priority: empty > pending (abandon) > no-art > decoded (LRU). */
static void aa_queue_decode(int item_idx,
                            const char *track_path, const char *album_name)
{
    /* Don't evict a decoded slot for a decode we can't even queue */
    if (queue_full(&aa_queue))
        return;

    mutex_lock(&aa_mutex);

    aa_entry_t *slot = NULL;
    /* 1. Empty slot */
    for (int i = 0; i < AA_SLOTS && !slot; i++)
        if (aa_cache[i].item_idx == -1) slot = &aa_cache[i];
    /* 2. Pending slot — decode thread discards stale result via item_idx check */
    for (int i = 0; i < AA_SLOTS && !slot; i++)
        if (aa_cache[i].pending) slot = &aa_cache[i];
    /* 3. Confirmed no-art slot */
    for (int i = 0; i < AA_SLOTS && !slot; i++)
        if (!aa_cache[i].has_art) slot = &aa_cache[i];
    /* 4. LRU eviction of a decoded slot */
    if (!slot) {
        slot = &aa_cache[aa_evict % AA_SLOTS];
        aa_evict++;
    }

    slot->item_idx = item_idx;
    slot->has_art  = false;
    slot->pending  = true;
    strmemccpy(slot->track_path, track_path, MAX_PATH);
    strmemccpy(slot->album_name, album_name, MAX_PATH);

    mutex_unlock(&aa_mutex);
    queue_post(&aa_queue, AA_Q_DECODE, (intptr_t)item_idx);
}

/* Queue a prefetch decode for item_idx if it is not already cached or pending. */
static void aa_try_prefetch(int item_idx)
{
    if (item_idx < 0 || item_idx >= tc.filesindir)
        return;

    mutex_lock(&aa_mutex);
    for (int i = 0; i < AA_SLOTS; i++)
        if (aa_cache[i].item_idx == item_idx)
        {
            mutex_unlock(&aa_mutex);
            return;
        }
    mutex_unlock(&aa_mutex);

    char track_path[MAX_PATH], album_name[MAX_PATH];
    if (!aa_resolve_path(item_idx, track_path, album_name))
        return;

    aa_queue_decode(item_idx, track_path, album_name);
}

/* Returns decoded pixels for item_idx, or NULL if not yet available.
 * On a cache miss the decode is queued asynchronously; NULL is returned
 * immediately and the list will show art on the next natural redraw. */
static const fb_data *aa_get(int item_idx)
{
    if (item_idx < 0 || item_idx >= tc.filesindir)
        return NULL;

    bool id3db = *(tc.dirfilter) == SHOW_ID3DB;

    mutex_lock(&aa_mutex);

    /* Invalidate on navigation change */
    if (id3db)
    {
        if (tc.currtable != aa_cache_table || tc.currextra != aa_cache_extra)
        {
            _aa_do_invalidate();
            aa_cache_table = tc.currtable;
            aa_cache_extra = tc.currextra;
        }
    }
    else
    {
        if (strcmp(tc.currdir, aa_cache_dir) != 0)
        {
            _aa_do_invalidate();
            strmemccpy(aa_cache_dir, tc.currdir, sizeof(aa_cache_dir));
        }
    }

    /* Cache hit (includes pending slots — return NULL while decoding) */
    for (int i = 0; i < AA_SLOTS; i++)
    {
        if (aa_cache[i].item_idx == item_idx)
        {
            const fb_data *result =
                (!aa_cache[i].pending && aa_cache[i].has_art)
                    ? aa_cache[i].pixels : NULL;
            mutex_unlock(&aa_mutex);
            return result;
        }
    }

    mutex_unlock(&aa_mutex);

    /* Cache miss — resolve path on main thread, then queue async decode */
    char track_path[MAX_PATH], album_name[MAX_PATH];
    if (!aa_resolve_path(item_idx, track_path, album_name))
        return NULL;

    aa_queue_decode(item_idx, track_path, album_name);
    return NULL;
}

static void aa_thread_init(void)
{
    mutex_init(&aa_mutex);
    queue_init(&aa_queue, false);
    _aa_do_invalidate();
    mkdir(AA_THUMBCACHE_DIR); /* no-op if already exists */
    aa_thread_id = create_thread(aa_decode_thread, aa_thread_stack,
                                 sizeof(aa_thread_stack), 0,
                                 "aa_decode" IF_PRIO(, PRIORITY_BACKGROUND)
                                 IF_COP(, CPU));
}

/* Draw art, fallback image, or centered icon inside the artwork container.
 * art_x/art_y is the top-left of the container (sz + 2*pad wide/tall).
 * pad adds inset space between the container edge and the actual artwork.
 * linedes drives the icon drawmode so it matches line.c's put_icon(). */
static void aa_draw_art_area(struct screen *display, int art_x, int art_y,
                             int sz, int pad, int line_h,
                             enum themable_icons icon, const fb_data *thumb,
                             const struct line_desc *linedes, bool allow_fallback)
{
    /* Artwork sits pad pixels inside the container; vertical centre accounts
     * for both the row margin (2px) and the padding naturally via line_h. */
    int bx = art_x + pad;
    int by = art_y + (line_h - sz) / 2;

    if (thumb)
    {
        if (bx >= 0 && by >= 0 &&
            bx + sz <= display->lcdwidth && by + sz <= display->lcdheight)
            display->bitmap_part((const unsigned char *)thumb,
                                 0, 0, sz, bx, by, sz, sz);
        return;
    }
    if (allow_fallback && aa_fallback_sz == sz)
    {
        if (bx >= 0 && by >= 0 &&
            bx + sz <= display->lcdwidth && by + sz <= display->lcdheight)
            display->bitmap_part((const unsigned char *)aa_fallback_pixels,
                                 0, 0, sz, bx, by, sz, sz);
        return;
    }
    /* No art and no fallback: center the item icon inside the container.
     * DRMODE_FG keeps the icon background transparent (doesn't overwrite
     * the selection bar).  STYLE_INVERT+mono uses SOLID|INVERSEVID so the
     * icon remains visible on an inverted selection bar, matching line.c. */
    if (icon <= Icon_NOICON) return;
    unsigned drmode = DRMODE_FG;
    if (get_icon_format(display->screen_type) == FORMAT_MONO &&
        (linedes->style & STYLE_INVERT))
        drmode = DRMODE_SOLID | DRMODE_INVERSEVID;
    int iw = get_icon_width(SCREEN_MAIN);
    int ih = get_icon_height(SCREEN_MAIN);
    int container_w = sz + 2 * pad;
    int ix = art_x + (container_w - iw) / 2;
    int iy = art_y + (line_h - ih) / 2;
    display->set_drawmode(drmode);
    screen_put_iconxy(display, ix, iy, icon);
    display->set_drawmode(DRMODE_SOLID);
}

/* Draw item: consistent art/icon area on the left, text after it for every row. */
static void albumart_list_draw_item(struct list_putlineinfo_t *info)
{
    if (!info || info->line < 0 || info->line >= tc.filesindir)
    {
        gui_list_default_draw_item(info);
        return;
    }

    const int sz = aa_thumb_sz;
    if (sz < 8)
    {
        gui_list_default_draw_item(info);
        return;
    }

    struct screen *display = info->display;
    const int line_h = info->linedes->height;
    const fb_data *thumb = aa_get(info->line);

    /* saved_indent accounts for scrollbar-left, RTL offset, and tab indentation.
     * All art positioning must start from info->x + saved_indent, not info->x. */
    const int saved_indent = info->item_indent;

    const int pad = aa_art_pad;
    /* Container width = sz + 2*pad.  Text gap is 4px after container + 2px before. */
    const int container_w = sz + 2 * pad;

    /* Fallback image only for items that aa_resolve_path recognises as a
     * genuine album or track — this is the same eligibility test used to pick
     * the art cache key, so menus, [By Album]/[All Tracks]/[Random], and
     * [Untagged] groupings never show it, only real albums/tracks do. */
    char fb_scratch_path[MAX_PATH];
    char fb_scratch_album[MAX_PATH];
    bool allow_fallback = aa_resolve_path(info->line, fb_scratch_path, fb_scratch_album);

    if (info->show_cursor)
    {
        /* Pointer mode: [indent][cursor][container][4px gap][text] */
        display->put_line(info->x, info->y, info->linedes,
            "$*s$1I$*s$*t",
            saved_indent,
            info->is_selected ? Icon_Cursor : Icon_NOICON,
            container_w + 4,
            info->item_offset, info->dsp_text);

        int art_x = info->x + saved_indent + info->icon_width - 1;
        aa_draw_art_area(display, art_x, info->y, sz, pad, line_h,
                         info->icon, thumb, info->linedes, allow_fallback);
    }
    else
    {
        /* Bar/gradient mode: [2px][container][4px gap][text] */
        bool saved_icons = info->have_icons;
        info->item_indent = saved_indent + container_w + 6;
        info->have_icons  = false;
        gui_list_default_draw_item(info);
        info->item_indent = saved_indent;
        info->have_icons  = saved_icons;

        int art_x = info->x + saved_indent + 2;
        aa_draw_art_area(display, art_x, info->y, sz, pad, line_h,
                         info->icon, thumb, info->linedes, allow_fallback);
    }

    /* Direction detection: runs once per frame on the first visible item */
    int cur_start = info->list->start_item[SCREEN_MAIN];
    if (info->line == cur_start)
    {
        if (aa_prev_start >= 0 && cur_start != aa_prev_start)
            aa_scroll_dir = (cur_start > aa_prev_start) ? 1 : -1;
        aa_prev_start = cur_start;
    }

    /* Direction-aware prefetch */
    if (aa_scroll_dir >= 0)
        for (int i = 1; i <= AA_PREFETCH_AHEAD; i++)
            aa_try_prefetch(info->line + i);
    if (aa_scroll_dir <= 0)
        for (int i = 1; i <= AA_PREFETCH_AHEAD; i++)
            aa_try_prefetch(info->line - i);
}
#endif /* HAVE_TAGCACHE */

/*
 * Called when a new dir is loaded (for example when returning from other apps ...)
 * also completely redraws the tree
 */
static int update_dir(void)
{
    struct gui_synclist * const list = &tree_lists;
    int show_path_in_browser = global_settings.show_path_in_browser;
    bool changed = false;

    const char* title = NULL;/* Must clear the title as the list is reused */
    int icon = NOICON;

#ifdef HAVE_TAGCACHE
    bool id3db = *tc.dirfilter == SHOW_ID3DB;
#else
    const bool id3db = false;
#endif

    /* Ensure that list is initialized before update_dir returns */
    gui_synclist_init(list, &tree_get_filename, &tc, false, 1, NULL);

    /* Artwork sizing (DB view only):
     *   art_sz  = thumbnail pixel size (what the user/theme actually set)
     *   base_h  = art_sz + 4  (row height = art + 2px top/bottom margin)
     *   effective line height = base_h + 2*pad (row grows to fit container)
     *
     * Priority: explicit user size > theme hint (when auto) > font-derived auto.
     * When show_album_art is off we skip all of this and use the normal
     * draw callback — the list looks exactly like the standard view. */
#if LCD_DEPTH > 1 && defined(HAVE_TAGCACHE)
    const bool show_art = global_settings.show_album_art;
#else
    const bool show_art = false;
#endif
    {
        int pad = (id3db && show_art) ? global_settings.thumb_art_padding : 0;

        int art_sz, base_h;
        if (id3db && show_art && global_settings.thumb_art_size > 0)
        {
            art_sz = MIN(global_settings.thumb_art_size, AA_THUMB);
            base_h = art_sz + 4;
        }
        else if (id3db && show_art && global_settings.thumb_theme_size > 0)
        {
            art_sz = MIN(global_settings.thumb_theme_size, AA_THUMB);
            base_h = art_sz + 4;
        }
        else
        {
            base_h = list->line_height[SCREEN_MAIN]; /* theme default */
            art_sz = (base_h > 4) ? MIN(base_h - 4, AA_THUMB) : 0;
        }

        if (id3db && show_art)
            FOR_NB_SCREENS(i)
                list->line_height[i] = base_h + 2 * pad;

        int new_sz = show_art ? art_sz : 0;
        if (new_sz < 0) new_sz = 0;
        aa_art_pad = pad;
        if (new_sz != aa_thumb_sz)
        {
            aa_invalidate();
            aa_thumb_sz = new_sz;
        }
    }
    list->callback_draw_item = show_art ? albumart_list_draw_item : NULL;

#ifdef HAVE_TAGCACHE

    /* Checks for changes */
    if (id3db) {
        if (tc.currtable != lasttable ||
            tc.currextra != lastextra ||
            reload_dir)
        {
            if (tagtree_load(&tc) < 0)
                return -1;

            aa_invalidate(); /* new directory/table — thumbnail cache is stale */
            lasttable = tc.currtable;
            lastextra = tc.currextra;
            changed = true;
        }
    }
    else
#endif
    {
        tc.sort_dir = global_settings.sort_dir;
        /* if the tc.currdir has been changed, reload it ...*/
        if (reload_dir || strncmp(tc.currdir, lastdir, sizeof(lastdir)))
        {
            if (ft_load(&tc, NULL) < 0)
                return -1;
#ifdef HAVE_TAGCACHE
            aa_invalidate(); /* new directory — thumbnail cache is stale */
#endif
            strmemccpy(lastdir, tc.currdir, MAX_PATH);
            changed = true;
        }
    }
    /* if selected item is undefined */
    if (tc.selected_item == -1)
    {
        if (!id3db)
            /* use lastfile to determine the selected item */
            tc.selected_item = tree_get_file_position(lastfile);

        /* If the file doesn't exists, select the first one (default) */
        if(tc.selected_item < 0)
            tc.selected_item = 0;
        changed = true;
    }
    if (changed)
    {
        if( !id3db && tc.dirfull )
        {
            splash(HZ, ID2P(LANG_SHOWDIR_BUFFER_FULL));
        }
    }

#ifdef HAVE_TAGCACHE
    if (id3db)
    {
        if (show_path_in_browser == SHOW_PATH_FULL
            || show_path_in_browser == SHOW_PATH_CURRENT)
        {
            title = tagtree_get_title(&tc);
            icon = filetype_get_icon(ATTR_DIRECTORY);
        }
    }
    else
#endif
    {
        if (tc.browse && tc.browse->title)
        {
            title = tc.browse->title;
            icon = tc.browse->icon;
            if (icon == NOICON)
                icon = filetype_get_icon(ATTR_DIRECTORY);
            /* display sub directories in the title of plugin browser */
            if (tc.dirlevel > 0 && *tc.dirfilter == SHOW_PLUGINS)
            {
                char *subdir = strrchr(tc.currdir, '/');
                if (subdir != NULL)
                    title = subdir + 1; /* step past the separator */
            }
        }
        else
        {
            if (show_path_in_browser == SHOW_PATH_FULL)
            {
                title = tc.currdir;
                icon = filetype_get_icon(ATTR_DIRECTORY);
            }
            else if (show_path_in_browser == SHOW_PATH_CURRENT)
            {
                title = strrchr(tc.currdir, '/');
                if (title != NULL)
                {
                    title++; /* step past the separator */
                    if (*title == '\0')
                    {
                        /* Display "Files" for the root dir */
                        title = ID2P(LANG_DIR_BROWSER);
                    }
                    icon = filetype_get_icon(ATTR_DIRECTORY);
                }
            }
        }
    }

    /* set title and icon, if nothing is set, clear the title
     * with NULL and icon as NOICON as the list is reused */
    gui_synclist_set_title(list, P2STR((unsigned char*)title), icon);

    gui_synclist_set_nb_items(list, tc.filesindir);
    gui_synclist_set_icon_callback(list,
                            global_settings.show_icons?tree_get_fileicon:NULL);
    gui_synclist_set_voice_callback(list, &tree_voice_cb);
#ifdef HAVE_LCD_COLOR
    gui_synclist_set_color_callback(list, &tree_get_filecolor);
#endif
    if( tc.selected_item >= tc.filesindir)
        tc.selected_item=tc.filesindir-1;

    gui_synclist_select_item(list, tc.selected_item);
    gui_synclist_draw(list);
    gui_synclist_speak_item(list);
    return tc.filesindir;
}

/* load tracks from specified directory to resume play */
void resume_directory(const char *dir)
{
    int dirfilter = *tc.dirfilter;
    int ret;
#ifdef HAVE_TAGCACHE
    bool id3db = *tc.dirfilter == SHOW_ID3DB;
#else
    const bool id3db = false;
#endif
    /* make sure the dirfilter is sane. The only time it should be possible
     * thats its not is when resume playlist is called from a plugin
     */
    if (!id3db)
        *tc.dirfilter = global_settings.dirfilter;
    ret = ft_load(&tc, dir);
    *tc.dirfilter = dirfilter;
    if (ret < 0)
        return;
    lastdir[0] = 0;

    ft_build_playlist(&tc, 0);

#ifdef HAVE_TAGCACHE
    if (id3db)
        tagtree_load(&tc);
#endif
}

/* Returns the current working directory and also writes cwd to buf if
   non-NULL.  In case of error, returns NULL. */
#ifdef CTRU
char *__wrap_getcwd(char *buf, getcwd_size_t size)
#else
char *getcwd(char *buf, getcwd_size_t size)
#endif
{
    if (!buf)
        return tc.currdir;
    else if (size)
    {
        if (strmemccpy(buf, tc.currdir, size) != NULL)
            return buf;
    }
    /* size == 0, or truncation in strmemccpy */
    return NULL;
}

/* Force a reload of the directory next time directory browser is called */
void reload_directory(void)
{
    reload_dir = true;
}

char* get_current_file(char* buffer, size_t buffer_len)
{
#ifdef HAVE_TAGCACHE
    /* in ID3DB mode it is a bad idea to call this function */
    /* (only happens with `follow playlist') */
    if( *tc.dirfilter == SHOW_ID3DB )
        return NULL;
#endif

    struct entry *entry = tree_get_entry_at(&tc, tc.selected_item);
    if (entry && getcwd(buffer, buffer_len))
    {
        if (!tc.dirlength)
            return buffer;

        size_t usedlen = strlen(buffer);

        if (usedlen + 2 < buffer_len) /* ensure enough room for '/' + '\0' */
        {
            if (buffer[usedlen-1] != '/')
            {
                buffer[usedlen] = '/';
                /* strmemccpy will zero terminate if we run out of space after */
                usedlen++;
            }
            buffer_len -= usedlen;
            if (strmemccpy(buffer + usedlen, entry->name, buffer_len) != NULL)
                return buffer;
        }
    }
    return NULL;
}

/* Allow apps to change our dirfilter directly (required for sub browsers)
   if they're suddenly going to become a file browser for example */
void set_dirfilter(int l_dirfilter)
{
    *tc.dirfilter = l_dirfilter;
}

/* Selects a path + file and update tree context properly */
static void set_current_file_ex(const char *path, const char *filename)
{
    int i;

#ifdef HAVE_TAGCACHE
    /* in ID3DB mode it is a bad idea to call this function */
    /* (only happens with `follow playlist') */
    if( *tc.dirfilter == SHOW_ID3DB )
        return;
#endif

    if (!filename) /* path and filename supplied combined */
    {
        /* separate directory from filename */
        /* gets the directory's name and put it into tc.currdir */
        filename = strrchr(path+1,'/');
        size_t endpos = filename - path;
        if (filename && endpos < MAX_PATH - 1)
        {
            strmemccpy(tc.currdir, path, endpos + 1);
            filename++;
        }
        else
        {
            strcpy(tc.currdir, "/");
            filename = path+1;
        }
    }
    else /* path and filename came in separate ensure an ending '/' */
    {
        char *end_p = strmemccpy(tc.currdir, path, MAX_PATH);
        size_t endpos = end_p - tc.currdir;
        if (endpos < MAX_PATH)
        {
            if (tc.currdir[endpos - 2] != '/')
            {
                tc.currdir[endpos - 1] = '/';
                tc.currdir[endpos] = '\0';
            }
        }
    }
    strmemccpy(lastfile, filename, MAX_PATH);


    /* If we changed dir we must recalculate the dirlevel
       and adjust the selected history properly */
    if (strncmp(tc.currdir,lastdir,sizeof(lastdir)))
    {
        tc.dirlevel =  0;
        tc.selected_item_history[tc.dirlevel] = -1;

        /* use '/' to calculate dirlevel */
        for (i = 1; path[i] != '\0'; i++)
        {
            if (path[i] == '/')
            {
                tc.dirlevel++;
                tc.selected_item_history[tc.dirlevel] = -1;
            }
        }
    }
    if (ft_load(&tc, NULL) >= 0)
    {
        tc.selected_item = tree_get_file_position(lastfile);
        if (!tc.is_browsing && tc.out_of_tree == 0)
        {
            /* the browser is closed */
            /* don't allow the previous items to overwrite what we just loaded */
            tc.out_of_tree = tc.selected_item + 1;
        }
    }
}

/* Selects a file and update tree context properly */
void set_current_file(const char *path)
{
    set_current_file_ex(path, NULL);
}


static int exit_to_new_screen(int screen)
{
    gui_synclist_scroll_stop(&tree_lists);
    return screen;
}

/* main loop, handles key events */
static int dirbrowse(void)
{
    int numentries=0;
    char buf[MAX_PATH];
    int button;
    int oldbutton;
    bool reload_root = false;
    int lastfilter = *tc.dirfilter;
    bool lastsortcase = global_settings.sort_case;
    bool exit_func = false;

    char* currdir = tc.currdir; /* just a shortcut */
#ifdef HAVE_TAGCACHE
    bool id3db = *tc.dirfilter == SHOW_ID3DB;

    if (id3db)
        curr_context=CONTEXT_ID3DB;
    else
#endif
        curr_context=CONTEXT_TREE;
    if (tc.selected_item < 0)
        tc.selected_item = 0;
#ifdef HAVE_TAGCACHE
    lasttable = -1;
    lastextra = -1;
#endif

    start_wps = false;
    numentries = update_dir();
    reload_dir = false;
    if (numentries == -1)
        return exit_to_new_screen(GO_TO_PREVIOUS);  /* currdir is not a directory */

    if (*tc.dirfilter > NUM_FILTER_MODES && numentries==0)
    {
        splash(HZ*2, *tc.dirfilter == SHOW_M3U ?
                     ID2P(LANG_CATALOG_NO_PLAYLISTS) : ID2P(LANG_NO_FILES));
        return exit_to_new_screen(GO_TO_PREVIOUS);  /* No files found for rockbox_browse() */
    }

    while(tc.browse && tc.is_browsing) {
        bool restore = false;
        if (tc.dirlevel < 0)
            tc.dirlevel = 0; /* shouldnt be needed.. this code needs work! */

        keyclick_set_callback(gui_synclist_keyclick_callback, &tree_lists);
        button = get_action(CONTEXT_TREE|ALLOW_SOFTLOCK,
                            list_do_action_timeout(&tree_lists, HZ/2));
        oldbutton = button;
        gui_synclist_do_button(&tree_lists, &button);
        tc.selected_item = gui_synclist_get_sel_pos(&tree_lists);
#ifdef HAVE_TAGCACHE
        if (aa_redraw_needed) {
            aa_redraw_needed = false;
            gui_synclist_draw(&tree_lists);
        }
#endif
        int customaction = ONPLAY_NO_CUSTOMACTION;
        bool do_restore_display = true;
        #ifdef HAVE_TAGCACHE
            if (id3db && (button == ACTION_STD_OK || button == ACTION_STD_CONTEXT))
            {
                customaction = tagtree_get_custom_action(&tc);
                if (customaction == ONPLAY_CUSTOMACTION_SHUFFLE_SONGS)
                {
                    /* The code to insert shuffled is on the context branch of the switch so we always go here */
                    button = ACTION_STD_CONTEXT;
                    do_restore_display = false;
                }
            }
        #endif
        switch ( button ) {
            case ACTION_STD_OK:
                /* nothing to do if no files to display */
                if ( numentries == 0 )
                    break;
                if (tc.browse->flags & BROWSE_SELECTONLY)
                {
                    struct entry *entry =
                                get_valid_entry(__func__, &tc, tc.selected_item);
                    short attr = entry->attr;
                    if(!(attr & ATTR_DIRECTORY))
                    {
                        tc.browse->flags |= BROWSE_SELECTED;
                        get_current_file(tc.browse->buf, tc.browse->bufsize);
                        return exit_to_new_screen(GO_TO_PREVIOUS);
                    }
                }
#ifdef HAVE_TAGCACHE
                switch (id3db ? tagtree_enter(&tc, true) : ft_enter(&tc))
#else
                switch (ft_enter(&tc))
#endif
                {
                    case GO_TO_FILEBROWSER: reload_dir = true; break;
                    case GO_TO_PLUGIN:
                        return exit_to_new_screen(GO_TO_PLUGIN);
                    case GO_TO_WPS:
                        return exit_to_new_screen(GO_TO_WPS);
#if CONFIG_TUNER
                    case GO_TO_FM:
                        return exit_to_new_screen(GO_TO_FM);
#endif
                    case GO_TO_ROOT: exit_func = true; break;
                    default:
                        break;
                }
                restore = do_restore_display;
                break;

            case ACTION_STD_CANCEL:
                exit_to_new_screen(0);
                if (*tc.dirfilter > NUM_FILTER_MODES && tc.dirlevel < 1) {
                    exit_func = true;
                    break;
                }
                if ((*tc.dirfilter == SHOW_ID3DB && tc.dirlevel == 0) ||
                    ((*tc.dirfilter != SHOW_ID3DB && !strcmp(currdir,"/"))))
                {
                    if (oldbutton == ACTION_TREE_PGLEFT)
                        break;
                    else
                        return exit_to_new_screen(GO_TO_ROOT);
                }

#ifdef HAVE_TAGCACHE
                if (id3db)
                    tagtree_exit(&tc, true);
                else
#endif
                    if (ft_exit(&tc) == 3)
                        exit_func = true;

                restore = do_restore_display;
                break;

            case ACTION_TREE_STOP:
                if (list_stop_handler())
                    restore = do_restore_display;
                break;

            case ACTION_STD_MENU:
                return exit_to_new_screen(GO_TO_ROOT);
                break;

#ifdef HAVE_RECORDING
            case ACTION_STD_REC:
                return exit_to_new_screen(GO_TO_RECSCREEN);
#endif

            case ACTION_TREE_WPS:
                return exit_to_new_screen(GO_TO_PREVIOUS_MUSIC);
                break;
#ifdef HAVE_QUICKSCREEN
            case ACTION_STD_QUICKSCREEN:
            {
                bool enter_shortcuts_menu = global_settings.shortcuts_replaces_qs;
                if (enter_shortcuts_menu && *tc.dirfilter >= NUM_FILTER_MODES)
                    break;
                else if (!enter_shortcuts_menu)
                {
                    int ret = quick_screen_quick(button);
                    if (ret == QUICKSCREEN_IN_USB)
                        reload_dir = true;
                    else if (ret == QUICKSCREEN_GOTO_SHORTCUTS_MENU)
                        enter_shortcuts_menu = true;
                }

                if (enter_shortcuts_menu && *tc.dirfilter < NUM_FILTER_MODES)
                {
                    int last_screen = global_status.last_screen;
                    global_status.last_screen = GO_TO_SHORTCUTMENU;
                    int shortcut_ret = do_shortcut_menu(NULL);
                    if (shortcut_ret == GO_TO_PREVIOUS)
                        global_status.last_screen = last_screen;
                    else
                        return exit_to_new_screen(shortcut_ret);
                }
                else if (enter_shortcuts_menu) /* currently disabled */
                {
                    /* QuickScreen defers skin updates, popping its activity, when
                       switching to Shortcuts Menu, so make up for that here:   */
                    FOR_NB_SCREENS(i)
                        skin_update(CUSTOM_STATUSBAR, i, SKIN_REFRESH_ALL);
                }

                restore = do_restore_display;
                break;
            }
#endif

#ifdef HAVE_HOTKEY
            case ACTION_TREE_HOTKEY:
                if (!global_settings.hotkey_tree)
                    break;
                /* fall through */
#endif
            case ACTION_STD_CONTEXT:
            {
                bool hotkey = button == ACTION_TREE_HOTKEY;
                int onplay_result;
                int attr = 0;

                if (tc.browse->flags & BROWSE_NO_CONTEXT_MENU)
                    break;

                if(!numentries)
                    onplay_result = onplay(NULL, 0, curr_context, hotkey, customaction);
                else {
#ifdef HAVE_TAGCACHE
                    if (id3db)
                    {
                        if (tagtree_get_attr(&tc) == FILE_ATTR_AUDIO)
                        {
                            attr = FILE_ATTR_AUDIO;

                            /* Look up the filename only once it is needed, so we
                               don't have to wait for the disk to wake up here. */
                            buf[0] = '\0';
                        }
                        else
                        {
                            attr = ATTR_DIRECTORY;
                            int title_len = 0;

                            /* In case of "special entries", add table title as
                               prefix, e.g. "The Beatles [All Tracks]", instead
                               of just "[All Tracks]", to improve the suggested
                               playlist filename.
                            */
                            if (tc.selected_item < tc.special_entry_count)
                            {
                                title_len = snprintf(buf, sizeof(buf), "%s ",
                                                     tagtree_get_title(&tc));
                                if (title_len < 0)
                                    title_len = 0;
                            }

                            if (title_len < (int) sizeof(buf))
                                tagtree_get_entry_name(&tc, tc.selected_item,
                                                       buf + title_len,
                                                       sizeof(buf) - title_len);

                            fix_path_part(buf, 0, sizeof(buf) - 1);
                        }
                    }
                    else
#endif
                    {
                        struct entry *entry =
                               get_valid_entry(__func__, &tc, tc.selected_item);

                        attr = entry->attr;

                        ft_assemble_path(buf, sizeof(buf), currdir, entry->name);

                    }
                    onplay_result = onplay(buf, attr, curr_context, hotkey, customaction);
                }
                switch (onplay_result)
                {
                    case ONPLAY_MAINMENU:
                        return exit_to_new_screen(GO_TO_ROOT);
                        break;

                    case ONPLAY_REVEAL_FILE:
                        return exit_to_new_screen(GO_TO_FILEBROWSER);
                        break;

                    case ONPLAY_OK:
                        restore = do_restore_display;
                        break;

                    case ONPLAY_RELOAD_DIR:
                        reload_dir = true;
                        break;

                    case ONPLAY_START_PLAY:
                        return exit_to_new_screen(GO_TO_WPS);
                        break;

                    case ONPLAY_PLUGIN:
                        return exit_to_new_screen(GO_TO_PLUGIN);
                        break;
                }
                break;
            }

#ifdef HAVE_HOTSWAP
            case SYS_FS_CHANGED:
#ifdef HAVE_TAGCACHE
                if (!id3db)
#endif
                    reload_dir = true;
                /* The 'dir no longer valid' situation will be caught later
                 * by checking the showdir() result. */
                break;
#endif

            default:
                if (default_event_handler(button) == SYS_USB_CONNECTED)
                {
                    if(*tc.dirfilter > NUM_FILTER_MODES)
                        /* leave sub-browsers after usb, doing otherwise
                           might be confusing to the user */
                        exit_func = true;
                    else
                        reload_dir = true;
                }
                break;
        }
        if (start_wps)
            return exit_to_new_screen(GO_TO_WPS);
        if (button && !IS_SYSEVENT(button))
        {
            storage_spin();
        }


    check_rescan:
        /* do we need to rescan dir? */
        if (reload_dir || reload_root ||
            lastfilter != *tc.dirfilter ||
            lastsortcase != global_settings.sort_case)
        {
            if (reload_root) {
                strcpy(currdir, "/");
                tc.dirlevel = 0;
#ifdef HAVE_TAGCACHE
                tc.currtable = 0;
                tc.currextra = 0;
                lasttable = -1;
                lastextra = -1;
#endif
                reload_root = false;
            }

            if (!reload_dir)
            {
                gui_synclist_select_item(&tree_lists, 0);
                gui_synclist_draw(&tree_lists);
                tc.selected_item = 0;
                lastdir[0] = 0;
            }

            lastfilter = *tc.dirfilter;
            lastsortcase = global_settings.sort_case;
            restore = do_restore_display;
        }

        if (exit_func)
            return exit_to_new_screen(GO_TO_PREVIOUS);

        if (restore || reload_dir) {
            FOR_NB_SCREENS(i)
                screens[i].scroll_stop();
            /* restore display */
            numentries = update_dir();
            reload_dir = false;
            if (currdir[1] && (numentries < 0))
            {   /* not in root and reload failed */
                reload_root = true; /* try root */
                goto check_rescan;
            }
        }
    }
    return exit_to_new_screen(GO_TO_ROOT);
}

int create_playlist(void)
{
    bool ret;
    trigger_cpu_boost();
    ret = catalog_add_to_a_playlist(PATH_ROOTSTR, ATTR_DIRECTORY, true, NULL, NULL);
    cancel_cpu_boost();

    return (ret) ? 1 : 0;
}

#define NUM_TC_BACKUP   3
static struct tree_context backups[NUM_TC_BACKUP];
/* do not make backup if it is not recursive call */
static int backup_count = -1;
int rockbox_browse(struct browse_context *browse)
{
    tc.is_browsing = (browse != NULL);
    int ret_val = 0;
    int dirfilter = SHOW_ALL;
    if (tc.is_browsing)
        dirfilter = browse->dirfilter;
    else
    {
        DEBUGF("%s browse is [NULL] \n", __func__);
        browse = tc.browse;
    }
    if (backup_count >= NUM_TC_BACKUP)
        return GO_TO_PREVIOUS;
    if (backup_count >= 0)
        backups[backup_count] = tc;
    backup_count++;
    int *prev_dirfilter = tc.dirfilter;
    tc.dirfilter = &dirfilter;
    tc.sort_dir = global_settings.sort_dir;

    reload_dir = true;

    if (tc.out_of_tree > 0)
    {
        /* an item has already been loaded out_of_tree holds the selected index
         * what happens with the item is dependent on the browse context */
        tc.selected_item = tc.out_of_tree - 1;
        tc.out_of_tree = 0;
        ret_val = ft_enter(&tc);
    }
    else
    {
        if (*tc.dirfilter >= NUM_FILTER_MODES)
        {
            int last_context;
            /* don't reset if its the same browse already loaded */
            if (tc.browse != browse ||
                !(tc.currdir[1] && strstr(tc.currdir, browse->root) != NULL))
            {
                tc.browse = browse;
                tc.selected_item = 0;
                tc.dirlevel = 0;

                strmemccpy(tc.currdir, browse->root, sizeof(tc.currdir));
            }

            start_wps = false;
            last_context = curr_context;

            if (browse->selected)
            {
                set_current_file_ex(browse->root, browse->selected);
                /* set_current_file changes dirlevel, change it back */
                tc.dirlevel = 0;
            }

            ret_val = dirbrowse();
            curr_context = last_context;
        }
        else
        {
            if (dirfilter != SHOW_ID3DB && (browse->flags & BROWSE_DIRFILTER) == 0)
                tc.dirfilter = &global_settings.dirfilter;
            tc.browse = browse;
            set_current_file(browse->root);
            if (browse->flags&BROWSE_RUNFILE)
                ret_val = ft_enter(&tc);
            else
                ret_val = dirbrowse();
        }
    }

    tc.is_browsing = false;
    tc.dirfilter = prev_dirfilter; /* Bugfix restore dirfilter*/

    backup_count--;
    if (backup_count >= 0)
        tc = backups[backup_count];

    return ret_val;
}

static int move_callback(int handle, void* current, void* new)
{
    struct tree_cache* cache = &tc.cache;
    ptrdiff_t diff = new - current;
    /* FIX_PTR makes sure to not accidentally update static allocations */
#define FIX_PTR(x) \
    { if ((void*)x >= current && (void*)x < (current+cache->name_buffer_size)) x+= diff; }

    if (handle == cache->name_buffer_handle)
    {   /* update entry structs, *even if they are struct tagentry */
        struct entry *this = core_get_data(cache->entries_handle);
        struct entry *last = this + cache->max_entries;
        for(; this < last; this++)
            FIX_PTR(this->name);
    }
    /* nothing to do if entries moved */
    return BUFLIB_CB_OK;
}

static struct buflib_callbacks ops = {
    .move_callback = move_callback,
    .shrink_callback = NULL,
};

void tree_mem_init(void)
{
    /* initialize tree context struct */
    struct tree_cache* cache = &tc.cache;
    memset(&tc, 0, sizeof(tc));
    tc.dirfilter = &global_settings.dirfilter;
    tc.sort_dir = global_settings.sort_dir;

    cache->name_buffer_size = AVERAGE_FILENAME_LENGTH *
        global_settings.max_files_in_dir;
    cache->name_buffer_handle = core_alloc_ex(cache->name_buffer_size, &ops);

    cache->max_entries = global_settings.max_files_in_dir;
    cache->entries_handle =
            core_alloc_ex(cache->max_entries*(sizeof(struct entry)), &ops);
}

bool bookmark_play(char *resume_file, int index, unsigned long elapsed,
                   unsigned long offset, int seed, char *filename)
{
    int i;
    char* suffix = strrchr(resume_file, '.');
    bool started = false;

    if (suffix != NULL && !strncasecmp(suffix, ".m3u", sizeof(".m3u") - 1)) /* gets m3u8 too */
    {
        /* Playlist playback */
        char* slash;
        /* check that the file exists */
        if (!file_exists(resume_file))
            return false;

        slash = strrchr(resume_file,'/');
        if (slash)
        {
            char* cp;
            *slash=0;

            cp=resume_file;
            if (!cp[0])
                cp="/";

            if (playlist_create(cp, slash+1) != -1)
            {
                if (global_settings.playlist_shuffle)
                    playlist_shuffle(seed, -1);
                started = true;
            }
            *slash='/';
        }
    }
    else
    {
        /* Directory playback */
        lastdir[0]='\0';
        if (playlist_create(resume_file, NULL) != -1)
        {
            char filename_buf[MAX_PATH + 1];
            const char* peek_filename;
            resume_directory(resume_file);
            if (global_settings.playlist_shuffle)
                playlist_shuffle(seed, -1);

            /* Check if the file is at the same spot in the directory,
               else search for it */
            int amt = playlist_amount();
            for ( i=0; i < amt; i++ )
            {
                int modidx = (i + index) % amt;
                peek_filename = playlist_peek(modidx, filename_buf,
                    sizeof(filename_buf));

                if (peek_filename == NULL)
                {
                    if (index == 0) /* searched every entry didn't find a match */
                        return false;
                    /* playlist has shrunk, search from the top */
                    i = 0;
                    amt = index;
                    index = 0;
                }
                else if (!strcmp(strrchr(peek_filename, '/') + 1, filename))
                {
                    started = true;
                    index = modidx;
                    break;
                }
            }
        }
    }

    if (started)
    {
        playlist_start(index, elapsed, offset);
        start_wps = true;
    }
    return started;
}

static void say_filetype(int attr)
{
    talk_id(tree_get_filetype_voiceclip(attr), true);
}

static int ft_play_dirname(char* name)
{
#ifdef HAVE_MULTIVOLUME
    int vol = path_get_volume_id(name);
    if (talk_volume_id(vol))
        return 1;
#endif

    return talk_file(tc.currdir, name, dir_thumbnail_name, NULL,
                     global_settings.talk_filetype ?
                     TALK_IDARRAY(VOICE_DIR) : NULL,
                     false);
}

static int ft_play_filename(char *dir, char *file, int attr)
{
    if (strlen(file) >= strlen(file_thumbnail_ext)
        && strcasecmp(&file[strlen(file) - strlen(file_thumbnail_ext)],
                      file_thumbnail_ext))
        /* file has no .talk extension */
        return talk_file(dir, NULL, file, file_thumbnail_ext,
                         TALK_IDARRAY(tree_get_filetype_voiceclip(attr)), false);

    /* it already is a .talk file, play this directly, but prefix it. */
    return talk_file(dir, NULL, file, NULL,
                     TALK_IDARRAY(LANG_VOICE_DIR_HOVER), false);
}

/* These two functions are called by the USB and shutdown handlers */
void tree_flush(void)
{
     tc.is_browsing = false;/* clear browse to prevent reentry to a possibly missing file */
#ifdef HAVE_TAGCACHE
    tagcache_shutdown();
#endif

#ifdef HAVE_TC_RAMCACHE
    tagcache_unload_ramcache();
#endif

#ifdef HAVE_DIRCACHE
    int old_val = global_status.dircache_size;
#ifdef HAVE_EEPROM_SETTINGS
    bool savecache = false;
#endif

    if (global_settings.dircache)
    {
        dircache_suspend();

        struct dircache_info info;
        dircache_get_info(&info);

        global_status.dircache_size = info.last_size;
    #ifdef HAVE_EEPROM_SETTINGS
        savecache = firmware_settings.initialized;
    #endif
    }
    else
    {
        global_status.dircache_size = 0;
    }

    if (old_val != global_status.dircache_size)
        status_save(true);

    #ifdef HAVE_EEPROM_SETTINGS
        if (savecache)
            dircache_save();
    #endif
#endif /* HAVE_DIRCACHE */
}

void tree_restore(void)
{
#ifdef HAVE_EEPROM_SETTINGS
    firmware_settings.disk_clean = false;
#endif

#ifdef HAVE_TC_RAMCACHE
    tagcache_remove_statefile();
#endif

#ifdef HAVE_DIRCACHE
    if (global_settings.dircache && dircache_resume() > 0)
    {
        /* Print "Scanning disk..." to the display. */
        splash(0, str(LANG_SCANNING_DISK));
        dircache_wait();
    }
#endif

#ifdef HAVE_TAGCACHE
    tagcache_start_scan();
#endif
}
