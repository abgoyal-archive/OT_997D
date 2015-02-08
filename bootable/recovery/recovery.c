/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"
#ifdef SUPPORT_FOTA
#include "fota/fota.h"
#endif

#include "private/android_filesystem_config.h"

#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 
#include "minzip/Zip.h"
#include "backup_restore.h"
#endif //SUPPORT_DATA_BACKUP_RESTORE

#ifdef SUPPORT_SBOOT_UPDATE
#include "sec/sec.h"
#endif


static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 's' },
  { "update_package", required_argument, NULL, 'u' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { "show_text", no_argument, NULL, 't' },
#ifdef SUPPORT_FOTA
  { "fota_delta_path", required_argument, NULL, 'f' },
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
  { "restore_data", required_argument, NULL, 'r' },
#endif //SUPPORT_DATA_BACKUP_RESTORE
  { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";
static const char *SIDELOAD_TEMP_DIR = "/tmp/sideload";
static const char *UPDATE_TEMP_FILE = "/data/update.zip";
static const char *DEFAULT_MOTA_FILE = "/data/data/com.mediatek.GoogleOta/files/update.zip";
static const char *DEFAULT_FOTA_FOLDER = "/data/data/com.mediatek.dm/files";
static const char *MOTA_RESULT_FILE = "/data/data/com.mediatek.GoogleOta/files/updateResult";
static const char *FOTA_RESULT_FILE = "/data/data/com.mediatek.dm/files/updateResult";
static const char *JRD_FOTA_RESULT_FILE = "/data/system/fota.status";

//Dan added for pr 330505
static const char *FILE_OM =  "/data/nvram/md/om";
static const char *FILE_TI = "/mobile_info/s/ti";

static const char *FILE_FLG = "/mobile_info/s/flg";
static const char *FILE_ST33A004 = "/data/nvram/md/NVRAM/IMPORTNT/ST33A004";
static const char *FILE_ST33B004 = "/data/nvram/md/NVRAM/IMPORTNT/ST33B004";
static const char *FILE_BACKUP_ST33A004 = "/mobile_info/s/ST33A004";
static const char *FILE_BACKUP_ST33B004 = "/mobile_info/s/ST33B004";
//End

extern UIParameters ui_parameters;    // from ui.c

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;
#if SUPPORT_SD_HOTPLUG
static const int MAX_TEMP_FILE_SIZE = 10 * 1024 * 1024;
#endif

static bool check_otaupdate_done(void)
{
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
    const char *arg = strtok(boot.recovery, "\n");

    if (arg != NULL && !strcmp(arg, "sdota"))
    {
        LOGI("Got arguments from boot message is %s\n", boot.recovery);
        return true;
    }
    else
    {
        LOGI("no boot messages %s\n", boot.recovery);
        return false;
    }
}


// open a given path, mounting partitions as necessary
FILE*
fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1);

    FILE *fp = fopen(path, mode);
	if(fp == NULL)
        LOGE("Can mount but Can't open  %s\n", path);
    return fp;
}

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    } else {
        ensure_path_mounted("/cache");
    }

    if (!check_otaupdate_done()) {

        // --> write the arguments we have back into the bootloader control block
        // always boot into recovery after this (until finish_recovery() is called)
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        LOGI("check otaupdate is done clear boot message\n");
        int i;
        for (i = 1; i < *argc; ++i) {
            strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
            strlcat(boot.recovery, "\n", sizeof(boot.recovery));
        }
        set_bootloader_message(&boot);
    }
}

static void
set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "sdota\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
}

// How much of the temp log we have copied to the copy in cache.
static long tmplog_offset = 0;

static void
copy_log_file(const char* source, const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else 
    {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) {
            if (append)
	     {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, source);
        }
        check_and_fclose(log, destination);
    }
}


//Dan added for pr 330505
static void
copy_file(const char* source, const char* destination, int append) {
 //   FILE *log = fopen_path(destination, append ? "a" : "w");
	    FILE *log = fopen_path(destination, "w");

	int read_len = 0;
char data[128] = {0};
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) 
	{
            if (append) 
	     {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            //char buf[4096];
            //while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
			while(!feof(tmplog))
			{//LOGE("Source file opened");

			read_len = fread(data,sizeof(char),128,tmplog);
        		fwrite(data,sizeof(char),read_len,log);
			}
            if (append)
	    {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, source);
        }
	else 
	{
		LOGE("Source file can not open\n");
	}
        check_and_fclose(log, destination);
    }
}
//End pr330505

//Dan added for pr 330505
static void
copy_file_2(const char* source, const char* destination, int append) {
 //   FILE *log = fopen_path(destination, append ? "a" : "w");
        FILE *tmplog = fopen_path(source, "r");
	int read_len = 0;
char data[128] = {0};

	if(tmplog!= NULL)
	{	    FILE *log = fopen_path(destination, "w");

		    if (log == NULL) {
		        LOGE("Can't open destination file %s\n", destination);
		    } else {
		        if (tmplog != NULL) 
			{
		            if (append) 
			     {
		                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
		            }
					while(!feof(tmplog))
					{
					read_len = fread(data,sizeof(char),128,tmplog);
		        		fwrite(data,sizeof(char),read_len,log);
					}
		            if (append)
			    {
		                tmplog_offset = ftell(tmplog);
		            }
		            check_and_fclose(tmplog, source);
		        }
		
		        check_and_fclose(log, destination);
		    }
	}
	else 
	{
		LOGE("Source file can not be opened\n");
	}
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    // Reset to mormal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

#if 0 //wschen 2012-03-18
    ensure_path_unmounted(CACHE_ROOT);
#endif
    sync();  // For good measure.
}

static int
erase_volume(const char *volume) {
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_show_indeterminate_progress();
    ui_print("Formatting %s...\n", volume);

    ensure_path_unmounted(volume);

    if (strcmp(volume, "/cache") == 0) {
        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
    }

    return format_volume(volume);
}

static char*
copy_sideloaded_package(const char* original_path) {
  if (ensure_path_mounted(original_path) != 0) {
    LOGE("Can't mount %s\n", original_path);
    return NULL;
  }

  if (ensure_path_mounted(SIDELOAD_TEMP_DIR) != 0) {
    LOGE("Can't mount %s\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }

  if (mkdir(SIDELOAD_TEMP_DIR, 0700) != 0) {
    if (errno != EEXIST) {
      LOGE("Can't mkdir %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
      return NULL;
    }
  }

  // verify that SIDELOAD_TEMP_DIR is exactly what we expect: a
  // directory, owned by root, readable and writable only by root.
  struct stat st;
  if (stat(SIDELOAD_TEMP_DIR, &st) != 0) {
    LOGE("failed to stat %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
    return NULL;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOGE("%s isn't a directory\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }
  if ((st.st_mode & 0777) != 0700) {
    LOGE("%s has perms %o\n", SIDELOAD_TEMP_DIR, st.st_mode);
    return NULL;
  }
  if (st.st_uid != 0) {
    LOGE("%s owned by %lu; not root\n", SIDELOAD_TEMP_DIR, st.st_uid);
    return NULL;
  }

  char copy_path[PATH_MAX];
  strcpy(copy_path, SIDELOAD_TEMP_DIR);
  strcat(copy_path, "/package.zip");

  char* buffer = malloc(BUFSIZ);
  if (buffer == NULL) {
    LOGE("Failed to allocate buffer\n");
    return NULL;
  }

  size_t read;
  FILE* fin = fopen(original_path, "rb");
  if (fin == NULL) {
    LOGE("Failed to open %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }
  FILE* fout = fopen(copy_path, "wb");
  if (fout == NULL) {
    LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  while ((read = fread(buffer, 1, BUFSIZ, fin)) > 0) {
    if (fwrite(buffer, 1, read, fout) != read) {
      LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
      return NULL;
    }
  }

  free(buffer);

  if (fclose(fout) != 0) {
    LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  if (fclose(fin) != 0) {
    LOGE("Failed to close %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }

  // "adb push" is happy to overwrite read-only files when it's
  // running as root, but we'll try anyway.
  if (chmod(copy_path, 0400) != 0) {
    LOGE("Failed to chmod %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  return strdup(copy_path);
}

#if SUPPORT_SD_HOTPLUG
static bool copy_update_package_to_temp(const char* original_path)
{
    FILE  *fi, *fo;
    bool   ret = true;
    unsigned int    len;

    struct statfs st;

    if (ensure_path_mounted(original_path) != 0) {
        LOGE("Can't mount %s\n", original_path);
        return false;
    }

    if (ensure_path_mounted(UPDATE_TEMP_FILE) != 0) {
        LOGE("Can't mount %s\n", UPDATE_TEMP_FILE);
        return false;
    }

    if ((fi = fopen(original_path, "rb")) == NULL)  {
        LOGE("Can't open %s\n", original_path);
        return false;
    }

    fseek(fi, 0, SEEK_END);
    len = ftell(fi);
    fseek(fi, 0, SEEK_SET);

    if (len > MAX_TEMP_FILE_SIZE)  {
        LOGE("The update.zip is too large\n");
        return false;
    }

    if (statfs("/data", &st) < 0) {
        LOGE("Can't stat %s\n", UPDATE_TEMP_FILE);
        return false;
    }

    if (len > st.f_bfree * st.f_bsize)  {
        LOGE("%s size not enough\n", UPDATE_TEMP_FILE);
        return false;
    }

    if ((fo = fopen(UPDATE_TEMP_FILE, "wb")) == NULL)  {
        LOGE("Can't create %s\n", UPDATE_TEMP_FILE);
        return false;
    }

    char buf[8192];
    int done = 0;
    while ((len = fread(buf, 1, sizeof(buf), fi)))  {
        done += len;
        if (done >= 512 * 1024)  {
            done = 0;
            ui_print(".");
        }
        if (fwrite(buf, 1, sizeof(buf), fo) != len)  {
            ret = false;
            break;
        }
    }

    fclose(fo);
    fclose(fi);

    return ret;
}
#endif

static char**
prepend_title(const char** headers) {
    char* title[] = { "Android system recovery <"
                          EXPAND(RECOVERY_API_VERSION) "e>",
                      "",
                      NULL };

    // count the number of lines in our title, plus the
    // caller-provided headers.
    int count = 0;
    char** p;
    for (p = title; *p; ++p, ++count);
    for (p = (char **) headers; *p; ++p, ++count);

    char** new_headers = malloc((count+1) * sizeof(char*));
    char** h = new_headers;
    for (p = title; *p; ++p, ++h) *h = *p;
    for (p = (char **) headers; *p; ++p, ++h) *h = *p;
    *h = NULL;

    return new_headers;
}

static int
get_menu_selection(char** headers, char** items, int menu_only,
                   int initial_selection) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui_clear_key_queue();

    ui_start_menu(headers, items, initial_selection);
    int selected = initial_selection;
    int chosen_item = -1;

    while (chosen_item < 0) {
        int key = ui_wait_key();
        int visible = ui_text_visible();

        if (key == -1) {   // ui_wait_key() timed out
            if (ui_text_ever_visible()) {
                continue;
            } else {
                LOGI("timed out waiting for key input; rebooting.\n");
                ui_end_menu();
                return ITEM_REBOOT;
            }
        }

        int action = device_handle_key(key, visible);

        if (action < 0) {
            switch (action) {
                case HIGHLIGHT_UP:
                    --selected;
                    selected = ui_menu_select(selected);
                    break;
                case HIGHLIGHT_DOWN:
                    ++selected;
                    selected = ui_menu_select(selected);
                    break;
                case SELECT_ITEM:
                    chosen_item = selected;
                    break;
                case NO_ACTION:
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui_end_menu();
    return chosen_item;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 

static int sdcard_restore_directory(const char *path) {

    const char *MENU_HEADERS[] = { "Choose a backup file to restore:",
                                   path,
                                   "",
                                   NULL };
    DIR *d;
    struct dirent *de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        ensure_path_unmounted(SDCARD_ROOT);
        return 0;
    }

    char **headers = prepend_title(MENU_HEADERS);

    int d_size = 0;
    int d_alloc = 10;
    char **dirs = malloc(d_alloc * sizeof(char *));
    int z_size = 1;
    int z_alloc = 10;
    char **zips = malloc(z_alloc * sizeof(char *));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') {
                continue;
            }
            if (name_len == 2 && de->d_name[0] == '.' && de->d_name[1] == '.') {
                continue;
            }

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 7 &&
                   strncasecmp(de->d_name + (name_len - 7), ".backup", 7) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char *), compare_string);
    qsort(zips, z_size, sizeof(char *), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = realloc(zips, z_alloc * sizeof(char *));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char *));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is sdcard_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = sdcard_restore_directory(new_path);
            if (result >= 0) {
                break;
            }
        } else {
            // selected a backup file:  attempt to restore it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
#if 1 //wschen 2011-05-16
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
#endif
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

            ui_print("\n-- Restore %s ...\n", item);
            //do restore and update result
#if 1 //wschen 2011-05-16
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--restore_data=", sizeof(boot.recovery));
            strlcat(boot.recovery, new_path, sizeof(boot.recovery));
            strlcat(boot.recovery, "\n", sizeof(boot.recovery));
            set_bootloader_message(&boot);
            sync();
#endif
            result = userdata_restore(new_path, 0);

            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) {
        free(zips[i]);
    }
    free(zips);
    free(headers);

    if (result != -1) {
        ensure_path_unmounted(SDCARD_ROOT);
    }
    return result;
}

#endif //SUPPORT_DATA_BACKUP_RESTORE

static int
update_directory(const char* path, const char* unmount_when_done, int *wipe_cache) {
    ensure_path_mounted(path);

    const char* MENU_HEADERS[] = { "Choose a package to install:",
                                   path,
                                   "",
                                   NULL };
    DIR* d;
    struct dirent* de;
    d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        if (unmount_when_done != NULL) {
            ensure_path_unmounted(unmount_when_done);
        }
        return 0;
    }

#ifdef SUPPORT_UTF8_MULTILINGUAL
    char* headers[4] = {NULL, path, "", NULL};
    char **head;
    char **menu;
    ml_string_fetch("updater", &head, &menu);
    headers[0] = head[0];
#else
    char** headers = prepend_title(MENU_HEADERS);
#endif

    int d_size = 0;
    int d_alloc = 10;
    char** dirs = malloc(d_alloc * sizeof(char*));
    int z_size = 1;
    int z_alloc = 10;
    char** zips = malloc(z_alloc * sizeof(char*));
    zips[0] = strdup("../");

    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') continue;
            if (name_len == 2 && de->d_name[0] == '.' &&
                de->d_name[1] == '.') continue;

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 4 &&
                   strncasecmp(de->d_name + (name_len-4), ".zip", 4) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char*), compare_string);
    qsort(zips, z_size, sizeof(char*), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = realloc(zips, z_alloc * sizeof(char*));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char*));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    int result;
    int chosen_item = 0;
    do {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is update_directory)
            result = -1;
            break;
        } else if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = update_directory(new_path, unmount_when_done, wipe_cache);
            if (result >= 0) break;
        } else {
            // selected a zip file:  attempt to install it, and return
            // the status to the caller.
            char new_path[PATH_MAX];
            strlcpy(new_path, path, PATH_MAX);
            strlcat(new_path, "/", PATH_MAX);
            strlcat(new_path, item, PATH_MAX);

            ui_print("\n-- Install %s ...\n", path);
            set_sdcard_update_bootloader_message();
            char* copy = copy_sideloaded_package(new_path);
            if (unmount_when_done != NULL) {
                ensure_path_unmounted(unmount_when_done);
            }
            if (copy) {
                result = install_package(copy, wipe_cache, TEMPORARY_INSTALL_FILE);
                free(copy);
            } else {
                result = INSTALL_ERROR;
            }
            break;
        }
    } while (true);

    int i;
    for (i = 0; i < z_size; ++i) free(zips[i]);
    free(zips);
#ifdef SUPPORT_UTF8_MULTILINGUAL
#else
    free(headers);
#endif

    if (unmount_when_done != NULL) {
        ensure_path_unmounted(unmount_when_done);
    }
    return result;
}

static void
wipe_data(int confirm) {
#if 1 //wschen 2012-04-12
    struct phone_encrypt_state ps;
#endif
#if 1 //wschen 2011-05-16
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
#endif
    if (confirm) {

#ifdef SUPPORT_UTF8_MULTILINGUAL
	char **head;
	char **menu;
        ml_string_fetch("wipe_data", &head, &menu);
	int chosen_item = get_menu_selection(head, menu, 1, 0);
#else
        static char** title_headers = NULL;

        if (title_headers == NULL) {
            char* headers[] = { "Confirm wipe of all user data?",
                                "  THIS CAN NOT BE UNDONE.",
                                "",
                                NULL };
            title_headers = prepend_title((const char**)headers);
        }

        char* items[] = { " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " No",
                          " Yes -- delete all user data",   // [7]
                          " No",
                          " No",
                          " No",
                          NULL };

        int chosen_item = get_menu_selection(title_headers, items, 1, 0);
#endif
        if (chosen_item != 7) {
            return;
        }
    }

    ui_print("\n-- Wiping data...\n");
#if 1 //wschen 2011-05-16
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
    sync();
#endif
    device_wipe_data();
#ifdef SPECIAL_FACTORY_RESET //wschen 2011-06-16
    if (special_factory_reset()) {
        return;
    }
#else
    erase_volume("/data");
#endif //SPECIAL_FACTORY_RESET
    erase_volume("/cache");
#if 1 //wschen 2011-05-16
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);
#if 1 //wschen 2012-04-12
    ps.state = 0;
    set_phone_encrypt_state(&ps);
#endif
    sync();
#endif
    ui_print("Data wipe complete.\n");
}

static void prompt_error_message(int reason)
{
    switch (reason)  {
        case INSTALL_NO_SDCARD:
            ui_print("No SD-Card.\n");
            break;
        case INSTALL_NO_UPDATE_PACKAGE:
            ui_print("Can not find update.zip.\n");
            break;
        case INSTALL_NO_KEY:
            ui_print("Failed to load keys\n");
            break;
        case INSTALL_SIGNATURE_ERROR:
            ui_print("Signature verification failed\n");
            break;
        case INSTALL_CORRUPT:
            ui_print("The update.zip is corrupted\n");
            break;
        case INSTALL_FILE_SYSTEM_ERROR:
            ui_print("Can't create/copy file\n");
            break;
        case INSTALL_ERROR:
            ui_print("Update.zip is not correct\n");
            break;
    }
}

#ifdef SUPPORT_UTF8_MULTILINGUAL
extern char *g_ml_str_hint[];
static void ml_select()
{
	int chosen_item = 0;
	static char** title_headers = NULL;
	
	if (title_headers == NULL) {
		char* headers[] = { "=>", "", NULL };
		title_headers = prepend_title(headers);
	}

	chosen_item = get_menu_selection(title_headers, g_ml_str_hint, 0 ,0);
	ml_select_language(chosen_item);
	ui_print("Select lang %s.\n", g_ml_str_hint[chosen_item]);
}
#endif

static void
prompt_and_wait() {
    char** headers = prepend_title((const char**)MENU_HEADERS);

    for (;;) {
        ui_print("\n");
        if (!check_otaupdate_done()) {
            LOGI("[1]check the otaupdate is done!\n");
            finish_recovery(NULL);
            ui_reset_progress();

#ifdef SUPPORT_UTF8_MULTILINGUAL
            char **menu = NULL;
            char **head = NULL;
            ml_string_fetch("main", &head, &menu);
            int chosen_item = get_menu_selection(head, menu, 0, 0);
#else
            int chosen_item = get_menu_selection(headers, MENU_ITEMS, 0, 0);
#endif

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            chosen_item = device_perform_action(chosen_item);
            // clear screen
            ui_print("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");


            int status;
            int wipe_cache;
            switch (chosen_item) {
#ifdef SUPPORT_UTF8_MULTILINGUAL
            case ITEM_TO_ML:
                ml_select();
                break;
#endif
                case ITEM_REBOOT:
                    return;

                case ITEM_WIPE_DATA:
                    wipe_data(ui_text_visible());
                    if (!ui_text_visible()) return;
                    break;

                case ITEM_WIPE_CACHE:
                    {
#if 1 //wschen 2011-05-16
                        struct bootloader_message boot;
#endif
                        ui_print("\n-- Wiping cache...\n");
#if 1 //wschen 2011-05-16
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                        set_bootloader_message(&boot);
                        sync();
#endif
                        erase_volume("/cache");
                        ui_print("Cache wipe complete.\n");
#if 1 //wschen 2011-05-16
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
#endif
                        if (!ui_text_visible()) return;
                    }
                    break;

                case ITEM_APPLY_SDCARD:
                    status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache);
                    if ((status == INSTALL_SUCCESS) && wipe_cache) {
                        struct bootloader_message boot;
                        ui_print("\n-- Wiping cache (at package request)...\n");
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                        set_bootloader_message(&boot);
                        sync();
                        if (erase_volume("/cache")) {
                            ui_print("Cache wipe failed.\n");
                        } else {
                            ui_print("Cache wipe complete.\n");
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui_set_background(BACKGROUND_ICON_ERROR);
                            prompt_error_message(status);
                            ui_print("Installation aborted.\n");
                        } else if (!ui_text_visible()) {
                            finish_recovery(NULL);
                            return;  // reboot if logs aren't visible
                        } else {
                            ui_print("\nInstall from sdcard complete.\n");
                            finish_recovery(NULL);
                        }
                    }
                    break;
                case ITEM_APPLY_CACHE:
                    // Don't unmount cache at the end of this.
                    status = update_directory(CACHE_ROOT, NULL, &wipe_cache);
                    if (status == INSTALL_SUCCESS && wipe_cache) {
                        struct bootloader_message boot;
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                        set_bootloader_message(&boot);
                        sync();
                        ui_print("\n-- Wiping cache (at package request)...\n");
                        if (erase_volume("/cache")) {
                            ui_print("Cache wipe failed.\n");
                        } else {
                            ui_print("Cache wipe complete.\n");
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }
                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui_set_background(BACKGROUND_ICON_ERROR);
                            ui_print("Installation aborted.\n");
                        } else if (!ui_text_visible()) {
                            return;  // reboot if logs aren't visible
                        } else {
                            ui_print("\nInstall from cache complete.\n");
                        }
                    }
                    break;
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-03-09 

                case ITEM_BACKUP:
                    {
                        int status;

                        ui_set_background(BACKGROUND_ICON_INSTALLING);
                        ui_reset_progress();
                        ui_show_progress(1.0, 0);

                        status = userdata_backup();

                        if (status == INSTALL_SUCCESS) {
                            ui_set_background(BACKGROUND_ICON_NONE);
                            ui_reset_progress();
                            ui_print("Backup user data complete.\n");
                            finish_recovery(NULL);
                        } else {
                            ui_set_background(BACKGROUND_ICON_ERROR);
                        }
                    }
                    break;
                case ITEM_RESTORE:
                    {
                        int status;

                        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
                            ui_set_background(BACKGROUND_ICON_ERROR);
                            ui_print("No SD-Card.\n");
                            break;
                        }

                        status = sdcard_restore_directory(SDCARD_ROOT);

                        if (status >= 0) {
                            if (status != INSTALL_SUCCESS) {
                                ui_set_background(BACKGROUND_ICON_ERROR);
                            } else {
#if 1 //wschen 2011-05-16
                                struct bootloader_message boot;
                                memset(&boot, 0, sizeof(boot));
                                set_bootloader_message(&boot);
                                sync();
#endif
                                ui_set_background(BACKGROUND_ICON_NONE);
                                ui_reset_progress();
                                if (!ui_text_visible()) {
                                    finish_recovery(NULL);
                                    return;  // reboot if logs aren't visible
                                } else {
                                    ui_print("Restore user data complete.\n");
                                    finish_recovery(NULL);
                                }
                            }
                        }
                    }
                    break;
#endif

            }
        } else {
            ui_reset_progress();
            ui_print("\n Please continue to update your system !\n");
            int chosen_item = get_menu_selection(headers, MENU_FORCE_ITEMS, 0, 0);

            // device-specific code may take some action here.  It may
            // return one of the core actions handled in the switch
            // statement below.
            chosen_item = device_perform_action(chosen_item);

            switch (chosen_item)
            {
                case ITEM_REBOOT:
                    return;

                case ITEM_FORCE_APPLY_SDCARD:
                    ;
                    int wipe_cache;
                    //M Removed by QA's resutest.
                    //ui_print("\n-- Install from sdcard...\n");
                    set_sdcard_update_bootloader_message();
                    int status = update_directory(SDCARD_ROOT, SDCARD_ROOT, &wipe_cache);
                    if ((status == INSTALL_SUCCESS) && wipe_cache) {
                        struct bootloader_message boot;
                        ui_print("\n-- Wiping cache (at package request)...\n");
                        memset(&boot, 0, sizeof(boot));
                        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                        set_bootloader_message(&boot);
                        sync();
                        if (erase_volume("/cache")) {
                            ui_print("Cache wipe failed.\n");
                        } else {
                            ui_print("Cache wipe complete.\n");
                        }
                        memset(&boot, 0, sizeof(boot));
                        set_bootloader_message(&boot);
                        sync();
                    }

                    if (status >= 0) {
                        if (status != INSTALL_SUCCESS) {
                            ui_set_background(BACKGROUND_ICON_ERROR);
                            prompt_error_message(status);
                            ui_print("Installation aborted.\n");
                        } else if (!ui_text_visible()) {
                            finish_recovery(NULL);
                            return;  // reboot if logs aren't visible
                        } else {
                            ui_print("\nInstall from sdcard complete.\n");
                            finish_recovery(NULL);
                        }
                    }
                    break;
            }

        }
    }
}

static void
print_property(const char *key, const char *name, void *cookie) {
    fprintf(stdout, "%s=%s\n", key, name);
}

static bool remove_mota_file(const char *file_name)
{
    int  ret = 0;

    //LOG_INFO("[%s] %s\n", __func__, file_name);

    ret = unlink(file_name);

    if (ret == 0)
		return true;

	if (ret < 0 && errno == ENOENT)
		return true;

    return false;
}

static void write_result_file(const char *file_name, int result)
{
#ifdef MTK_EMMC_SUPPORT

    if (INSTALL_SUCCESS == result) {
        set_ota_result(1);
    } else {
        set_ota_result(0);
    }

#else
    char  dir_name[256];

    ensure_path_mounted("/data");

    strcpy(dir_name, file_name);
    char *p = strrchr(dir_name, '/');
    *p = 0;

    fprintf(stdout, "dir_name = %s\n", dir_name);

    if (opendir(dir_name) == NULL)  {
        fprintf(stdout, "dir_name = '%s' does not exist, create it.\n", dir_name);
        if (mkdir(dir_name, 0666))  {
            fprintf(stdout, "can not create '%s' : %s\n", dir_name, strerror(errno));
            return;
        }
    }

    int result_fd = open(file_name, O_RDWR | O_CREAT, 0644);

    if (result_fd < 0) {
        fprintf(stdout, "cannot open '%s' for output : %s\n", file_name, strerror(errno));
        return;
    }

    //LOG_INFO("[%s] %s %d\n", __func__, file_name, result);

    char buf[4];
    if (INSTALL_SUCCESS == result)
        strcpy(buf, "1");
    else
        strcpy(buf, "0");
    write(result_fd, buf, 1);
    close(result_fd);
#endif	
}

static void write_file(const char *file_name, char *result)
{
    char  dir_name[256];

    ensure_path_mounted("/data");

    strcpy(dir_name, file_name);
    char *p = strrchr(dir_name, '/');
    *p = 0;

    fprintf(stdout, "dir_name = %s\n", dir_name);

    if (opendir(dir_name) == NULL)  {
        fprintf(stdout, "dir_name = '%s' does not exist, create it.\n", dir_name);
        if (mkdir(dir_name, 0777))  {
            fprintf(stdout, "can not create '%s' : %s\n", dir_name, strerror(errno));
            return;
        }
    }

    int result_fd = open(file_name, O_RDWR | O_CREAT, 0644);
    fchown(result_fd, AID_SYSTEM, AID_SYSTEM);

    if (result_fd < 0) {
        fprintf(stdout, "cannot open '%s' for output : %s\n", file_name, strerror(errno));
        return;
    }

    //LOG_INFO("[%s] %s %d\n", __func__, file_name, result);

    char buf[256];
    strcpy(buf, "package install result:");
    strcat(buf, result);
    write(result_fd, buf, strlen(buf));
    close(result_fd);
}

static char *install_result = NULL;
static void set_install_result(char *s)
{
    install_result = s;
}

static void write_result(int reason)
{
    switch (reason) {
    case INSTALL_NO_SDCARD:
        set_install_result("No SD-Card.\n");
        break;
    case INSTALL_NO_UPDATE_PACKAGE:
        set_install_result("Can not find update.zip.\n");
        break;
    case INSTALL_NO_KEY:
        set_install_result("Failed to load keys\n");
        break;
    case INSTALL_SIGNATURE_ERROR:
        set_install_result("Signature verification failed\n");
        break;
    case INSTALL_CORRUPT:
        set_install_result("The update.zip is corrupted\n");
        break;
    case INSTALL_FILE_SYSTEM_ERROR:
        set_install_result("Can't create/copy file\n");
        break;
    case INSTALL_ERROR:
        set_install_result("Update.zip is not correct\n");
        break;
    }

    if (reason == INSTALL_SUCCESS) {
        set_install_result("INSTALL SUCCESS\n");
    }

    fprintf(stdout, "package install result:%s", install_result);

    write_file(JRD_FOTA_RESULT_FILE, install_result);
}

int
main(int argc, char **argv) {
    time_t start = time(NULL);

    // If these fail, there's not really anywhere to complain...
    freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
    freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);
    printf("Starting recovery on %s", ctime(&start));

    device_ui_init(&ui_parameters);
    ui_init();
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    load_volume_table();
    get_args(&argc, &argv);

    int previous_runs = 0;
    const char *send_intent = NULL;
    const char *update_package = NULL;
    int wipe_data = 0, wipe_cache = 0;
#ifdef SUPPORT_FOTA
    const char *fota_delta_path = NULL;
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
    const char *restore_data_path = NULL;
#endif //SUPPORT_DATA_BACKUP_RESTORE

    int arg;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) {
        switch (arg) {
        case 'p': previous_runs = atoi(optarg); break;
        case 's': send_intent = optarg; break;
        case 'u': update_package = optarg; break;
        case 'w': wipe_data = wipe_cache = 1; break;
        case 'c': wipe_cache = 1; break;
        case 't': ui_show_text(1); break;
#ifdef SUPPORT_FOTA
        case 'f': fota_delta_path = optarg; break;
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
        case 'r': restore_data_path = optarg; break;
#endif //SUPPORT_DATA_BACKUP_RESTORE
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }

    device_recovery_start();

    fprintf(stdout, "Command:");
    for (arg = 0; arg < argc; arg++) {
        fprintf(stdout, " \"%s\"", argv[arg]);
    }
    fprintf(stdout, "\n");
//Dan added for pr330505
	ensure_path_mounted(FILE_OM);
	ensure_path_mounted(FILE_TI);
	ensure_path_mounted(FILE_ST33A004);
	ensure_path_mounted(FILE_ST33B004);
	ensure_path_mounted(FILE_FLG);
	ensure_path_mounted(FILE_BACKUP_ST33A004);
	ensure_path_mounted(FILE_BACKUP_ST33B004);
	copy_file_2(FILE_OM, FILE_TI, false);
	copy_file_2(FILE_OM, FILE_FLG, false);
	copy_file_2(FILE_ST33A004, FILE_BACKUP_ST33A004, false);
	copy_file_2(FILE_ST33B004, FILE_BACKUP_ST33B004, false);
	ensure_path_unmounted(FILE_OM);
	ensure_path_unmounted(FILE_FLG);
	ensure_path_unmounted(FILE_TI);
	ensure_path_unmounted(FILE_ST33A004);
	ensure_path_unmounted(FILE_ST33B004);
	ensure_path_unmounted(FILE_BACKUP_ST33A004);
	ensure_path_unmounted(FILE_BACKUP_ST33B004);
//End pr330505
    /* ----------------------------- */
    /* SECURE BOOT INIT              */    
    /* ----------------------------- */    
#ifdef SUPPORT_SBOOT_UPDATE
    sec_init(false);
#endif

    if (update_package) {
        // For backwards compatibility on the cache partition only, if
        // we're given an old 'root' path "CACHE:foo", change it to
        // "/cache/foo".
        if (strncmp(update_package, "CACHE:", 6) == 0) {
            int len = strlen(update_package) + 10;
            char* modified_path = malloc(len);
            strlcpy(modified_path, "/cache/", len);
            strlcat(modified_path, update_package+6, len);
            fprintf(stdout, "(replacing path \"%s\" with \"%s\")\n",
                   update_package, modified_path);
            update_package = modified_path;
        }

        if (strncmp(update_package, "/mnt", 4) == 0) {
            int len = strlen(update_package);
            char* modified_path = malloc(len);
            memset(modified_path, 0, len);
            strlcpy(modified_path, update_package+4, len-4+1);
            fprintf(stdout, "(replacing path \"%s\" with \"%s\")\n",
                   update_package, modified_path);
            update_package = modified_path;
        }
    }
    fprintf(stdout, "\n");

    property_list(print_property, NULL);
    fprintf(stdout, "\n");

#ifdef SUPPORT_FOTA
    struct stat st;

    fprintf(stdout, "check update_package or fota_delta_path.\n");

    if (update_package || fota_delta_path)  {
#ifdef FOTA_FIRST
        if (fota_delta_path)  {
            fprintf(stdout, "fota_delta_path exist\n");
            if (find_fota_delta_package(fota_delta_path))  {
                fprintf(stdout, "FOTA_FIRST : fota_delta_path\n");
                update_package = 0;
            }
            else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER))  {
                fprintf(stdout, "FOTA_FIRST : DEFAULT_FOTA_FOLDER\n");
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            }
            else if (!lstat(DEFAULT_MOTA_FILE, &st))  {
                fprintf(stdout, "FOTA_FIRST : DEFAULT_MOTA_FILE\n");
                update_package = strdup(DEFAULT_MOTA_FILE);
                fota_delta_path = 0;
            }
        } else if (update_package)  {
            fprintf(stdout, "update_package exist\n");
            if (!lstat(update_package, &st))  {
                fprintf(stdout, "FOTA_SECOND : update_package\n");
            }
            else if (!lstat(DEFAULT_MOTA_FILE, &st))  {
                fprintf(stdout, "FOTA_SECOND : DEFAULT_MOTA_FILE\n");
                fota_delta_path = 0;
                update_package = strdup(DEFAULT_MOTA_FILE);
            }
            else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER))  {
                fprintf(stdout, "FOTA_SECOND : DEFAULT_FOTA_FOLDER\n");
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            }
        }
#elif defined(MOTA_FIRST)
        if (update_package)  {
            if (!lstat(update_package, &st))  {
                fota_delta_path = 0;
            }
            else if (!lstat(DEFAULT_MOTA_FILE, &st))  {
                fota_delta_path = 0;
            }
            else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER))  {
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            }
        }
        else if (fota_delta_path)  {
            if (find_fota_delta_package(fota_delta_path))  {
                update_package = 0;
            }
            else if (find_fota_delta_package(DEFAULT_FOTA_FOLDER))  {
                update_package = 0;
                fota_delta_path = strdup(DEFAULT_FOTA_FOLDER);
            }
            else if (!lstat(DEFAULT_MOTA_FILE, &st))  {
                fota_delta_path = 0;
                update_package = strdup(DEFAULT_MOTA_FILE);
            }
        }
#else
        #error must specify FOTA_FIRST or MOTA_FIRST
#endif
    }
#endif

    fprintf(stdout, "update_package = %s\n", update_package ? update_package : "NULL");
#ifdef SUPPORT_FOTA
    fprintf(stdout, "fota_delta_path = %s\n", fota_delta_path ? fota_delta_path : "NULL");
#endif

    int status = INSTALL_SUCCESS;

    if (update_package != NULL) {
        if (ensure_path_mounted(JRD_FOTA_RESULT_FILE)) {
          LOGE("can't mount %s rootdir",JRD_FOTA_RESULT_FILE);
        }else{
          struct stat st;
          if (stat(JRD_FOTA_RESULT_FILE, &st)) {
            LOGI("can't stat %s",JRD_FOTA_RESULT_FILE);
          }else if (unlink(JRD_FOTA_RESULT_FILE)){
            LOGE("can't erase %s",JRD_FOTA_RESULT_FILE);
          }
        }
#if SUPPORT_SD_HOTPLUG
        int copy_success = 0;
        if (!strncmp(update_package, "/sdcard", strlen("/sdcard")) &&
             copy_update_package_to_temp(update_package))
        {
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--update_package=", sizeof(boot.recovery));
            strlcat(boot.recovery, update_package, sizeof(boot.recovery));
            set_bootloader_message(&boot);
            sync();

            status = install_package(UPDATE_TEMP_FILE, &wipe_cache, TEMPORARY_INSTALL_FILE);
            copy_success = 1;
            if (status == INSTALL_SUCCESS && wipe_cache) {
                struct bootloader_message boot;
                memset(&boot, 0, sizeof(boot));
                strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                set_bootloader_message(&boot);
                sync();

                if (erase_volume("/cache")) {
                    LOGE("Cache wipe (request by package) failed.");
                }

                memset(&boot, 0, sizeof(boot));
                set_bootloader_message(&boot);
                sync();
            }
        }
        else
#endif
        {
            struct bootloader_message boot;
            memset(&boot, 0, sizeof(boot));
            strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
            strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
            strlcat(boot.recovery, "--update_package=", sizeof(boot.recovery));
            strlcat(boot.recovery, update_package, sizeof(boot.recovery));
            set_bootloader_message(&boot);
            sync();

            status = install_package(update_package, &wipe_cache, TEMPORARY_INSTALL_FILE);
            if (status == INSTALL_SUCCESS && wipe_cache) {
                struct bootloader_message boot;
                memset(&boot, 0, sizeof(boot));
                strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
                strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
                strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
                set_bootloader_message(&boot);
                sync();

                if (erase_volume("/cache")) {
                    LOGE("Cache wipe (request by package) failed.");
                }

                memset(&boot, 0, sizeof(boot));
                set_bootloader_message(&boot);
                sync();
            }
        }
        prompt_error_message(status);
        if (status != INSTALL_SUCCESS) ui_print("Installation aborted.\n");
#if SUPPORT_SD_HOTPLUG
        if (!strncmp(update_package, "/sdcard", strlen("/sdcard")))  {
            LOGE("Please re-insert the sd-card and try again\n");
        }
        if (copy_success) {
            unlink(UPDATE_TEMP_FILE);
        }
#endif
    } else if (wipe_data) {

#if 1 //wschen 2012-04-12
        struct phone_encrypt_state ps;
#endif
#if 1 //wschen 2012-03-21
        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--wipe_data\n", sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();
#endif
        if (device_wipe_data()) status = INSTALL_ERROR;
#ifdef SPECIAL_FACTORY_RESET //wschen 2011-06-16
        if (special_factory_reset()) status = INSTALL_ERROR;
#else
        if (erase_volume("/data")) status = INSTALL_ERROR;
#endif //SPECIAL_FACTORY_RESET
        if (wipe_cache && erase_volume("/cache")) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
        if (status != INSTALL_SUCCESS) {
            ui_print("Data wipe failed.\n");
        } else {
            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
#if 1 //wschen 2012-04-12
            ps.state = 0;
            set_phone_encrypt_state(&ps);
#endif
            sync();
        }
#endif
    } else if (wipe_cache) {
#if 1 //wschen 2012-03-21
        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--wipe_cache\n", sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();
#endif
        if (erase_volume("/cache")) status = INSTALL_ERROR;
#if 1 //wschen 2012-04-19 write to /dev/misc and reboot soon, it may not really write to flash
        if (status != INSTALL_SUCCESS) {
            ui_print("Cache wipe failed.\n");
        } else {
            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
        }
#endif
#ifdef SUPPORT_FOTA
    } else if (fota_delta_path)  {

        struct bootloader_message boot;
        memset(&boot, 0, sizeof(boot));
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
        strlcat(boot.recovery, "--fota_delta_path=", sizeof(boot.recovery));
        strlcat(boot.recovery, fota_delta_path, sizeof(boot.recovery));
        set_bootloader_message(&boot);
        sync();

        status = install_fota_delta_package(fota_delta_path);

        /* ----------------------------- */
        /* SECURE BOOT UPDATE            */    
        /* ----------------------------- */            
#ifdef SUPPORT_SBOOT_UPDATE
        if (INSTALL_SUCCESS == status) {
            sec_update(false);
        }
#endif
#endif
#ifdef SUPPORT_DATA_BACKUP_RESTORE //wschen 2011-05-16 
    } else if (restore_data_path) {
        if (ensure_path_mounted(SDCARD_ROOT) != 0) {
            ui_set_background(BACKGROUND_ICON_ERROR);
            ui_print("No SD-Card.\n");
            status = INSTALL_ERROR;
        } else {
            struct bootloader_message boot;

            status = userdata_restore(restore_data_path, 0);

            memset(&boot, 0, sizeof(boot));
            set_bootloader_message(&boot);
            sync();
            ensure_path_unmounted(SDCARD_ROOT);
        }
#endif //SUPPORT_DATA_BACKUP_RESTORE
    } else {
        status = INSTALL_ERROR;  // No command specified
    }

    if (update_package 
#ifdef SUPPORT_FOTA
            || fota_delta_path
#endif
       ) {
        fprintf(stdout, "write result : MOTA_RESULT_FILE\n");
        write_result_file(MOTA_RESULT_FILE, status);
#ifdef SUPPORT_FOTA
        fprintf(stdout, "write result : FOTA_RESULT_FILE\n");
        write_result_file(FOTA_RESULT_FILE, status);
#endif

#ifdef SUPPORT_FOTA
        fprintf(stdout, "write result : remove_fota_delta_files\n");
        if (fota_delta_path)
            remove_fota_delta_files(fota_delta_path);
#endif
        fprintf(stdout, "write result : remove_mota_file\n");
        //if (update_package)
        //    remove_mota_file(update_package);
#ifdef SUPPORT_FOTA
        fprintf(stdout, "write result : remove_fota_delta_files(DEFAULT_FOTA_FOLDER)\n");
        remove_fota_delta_files(DEFAULT_FOTA_FOLDER);
#endif
        fprintf(stdout, "write result : remove_mota_file(DEFAULT_MOTA_FILE)\n");
        //remove_mota_file(DEFAULT_MOTA_FILE);
        
        write_result(status);
    }

    if (status != INSTALL_SUCCESS) ui_set_background(BACKGROUND_ICON_ERROR);
    if (status != INSTALL_SUCCESS) {
#ifdef SUPPORT_UTF8_MULTILINGUAL
        ui_set_background(BACKGROUND_ICON_GUIDE);
        ml_select();
#endif
        prompt_and_wait();
    }

    // Otherwise, get ready to boot the main system...
    finish_recovery(send_intent);
    ui_print("Rebooting...\n");
    android_reboot(ANDROID_RB_RESTART, 0, 0);
    return EXIT_SUCCESS;
}
