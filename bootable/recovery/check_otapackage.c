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
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/vfs.h>
#include <stdarg.h>
#include <libgen.h>
#include "minzip/SysUtil.h"
#include "minzip/DirUtil.h"
#include "minzip/Zip.h"
#include "backup_restore.h"

int main(int argc, char **argv)
{
    int ret = 0;
    if (argc == 2) {
        ret = check_ota(argv[1]);
        printf("%d", ret);
        return ret; 
    } else {
        ret = ERROR_INVALID_ARGS;
        printf("%d", ret);
        return ret;
    }
}

