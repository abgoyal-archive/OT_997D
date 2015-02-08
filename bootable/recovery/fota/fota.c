/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>

#include "fota.h"
#include "bootloader.h"
#include "common.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "install.h"

#include "cutils/log.h"
#undef LOG_TAG
#define LOG_TAG "fota"

#include "vRM_PublicDefines.h"
#include "RB_ImageUpdate.h"
#include "RB_vRM_ImageUpdate.h"
#include "RbErrors.h"

// ----------------------------------------------------------------------------
//
// Global Config
//

#define BOOT_DELTA_FILE     "boot.delta"
#define SYSTEM_DELTA_FILE   "system.delta"
#define RECOVERY_DELTA_FILE "recovery.delta"

#define BOOT_IMAGE_FILE     "boot.img"
#define SYSTEM_IMAGE_FILE   "system.img"
#define RECOVERY_IMAGE_FILE "recovery.img"

#define BOOT_DELTA_PARTITION_NAME       "boot"
#define SYSTEM_DELTA_PARTITION_NAME     "system"

#define FOTA_TEMP_PATH           "/data/fota"

#define UPDATE_RESULT_FILE      "/data/data/com.mediatek.dm/files/updateResult"
//#define DEFAULT_DELTA_FOLDER    "/data/delta"

#define VERIFY_BOOT_SOURCE          0
#define VERIFY_BOOT_TARGET          0
#define VERIFY_SYSTEM_SOURCE        0
#define VERIFY_SYSTEM_TARGET        0
#define VERIFY_RECOVERY_SOURCE      0
#define VERIFY_RECOVERY_TARGET      0


// ----------------------------------------------------------------------------
//
// Debug Options
//

//#define FOTA_COMPARE_GOLDEN
//#define FOTA_UPDATE_RECOVERY
//#define FOTA_UI_DEBUG_MESSAGE
#define FOTA_UI_MESSAGE

#ifdef FOTA_UI_DEBUG_MESSAGE
#define LOG_UI       ui_print
#else
#define LOG_UI(...)
#endif

#ifdef FOTA_UI_MESSAGE
#define UI_PRINT    ui_print
#else
#define UI_PRINT(...)
#endif


// ----------------------------------------------------------------------------
//
// Partition Name
//

#ifdef MTK_EMMC_SUPPORT
#define BOOT_PART_NAME      "bootimg"
#define SYSTEM_PART_NAME    "android"
#define DATA_PART_NAME      "usrdata"
#else
#define BOOT_PART_NAME      "boot"
#define SYSTEM_PART_NAME    "system"
#define DATA_PART_NAME      "userdata"
#endif

#define RECOVERY_PART_NAME  "recovery"
#define BACKUP_PART_NAME    "expdb"

#define MAX_PATH  256  // merge with fota_fs.c

#define NAND_START      0
#define NAND_END        0x20000000

// UPI Operation

#define UPI_OP_SCOUT_UPDATE     0x0
#define UPI_OP_VERIFY_SOURCE    0x1
#define UPI_OP_VERIFY_TARGET    0x2
#define UPI_OP_UPDATE           0x3

#define UPI_WORKING_BUFFER_SIZE     20L * 1024 * 1024
#define UPI_BACKUP_BUFFER_NUM       4

//#define RB_UPI_VERSION  "6,3,9,111"
#define RB_UPI_VERSION  "6,3,9,125"

enum FOTA_UPGRADE_MODE {
    FOTA_UPGRADE_MODE_UNKNOWN,
    FOTA_UPGRADE_MODE_DELTA,
    FOTA_UPGRADE_MODE_IMAGE
};

typedef struct PartitionInfo
{
    unsigned int  size;
    unsigned int  write_size;
    unsigned int  erase_size;
} PartitionInfo;

struct FotaGlobals
{
    const struct MtdPartition *boot_partition;
    const struct MtdPartition *system_partition;
    const struct MtdPartition *recovery_partition;
    const struct MtdPartition *data_partition;
    const struct MtdPartition *backup_partition;

    PartitionInfo boot_partition_info;
    PartitionInfo system_partition_info;
    PartitionInfo recovery_partition_info;
    PartitionInfo backup_partition_info;

    char  *boot_delta_file;
    char  *system_delta_file;
    char  *recovery_delta_file;

    const struct MtdPartition  *partition;
    PartitionInfo partition_info;
    FILE   *delta_file;

    unsigned long  uProgress;
    unsigned long  uRound;

    enum FOTA_UPGRADE_MODE  mode;
};

struct FotaGlobals  gFota;
unsigned char *upi_working_buffer = NULL;

unsigned long BackupBuffer[4] = {
    0,
    0x20000,
    0x40000,
    0x80000
};

#if 0
unsigned long BackupBuffer[4] = {
    0xC840000,
    0xC840000 + 0x40000,
    0xC840000 + 0x80000,
    0xC840000 + 0xC0000
};
#endif


static bool fota_init(void)
{
    LOG_UI("[%s]\n", __func__);
    LOG_INFO("[%s]", __func__);

    memset((void *) &gFota, 0, sizeof(struct FotaGlobals));

    return true;
}

static bool check_upi_version(void)
{
    char buf[32];


    if (S_RB_SUCCESS != RB_GetUPIVersion((unsigned char *) buf))  {
        LOG_ERROR("Can't get UPI Version");
        return false;
    }
    LOG_INFO("UPI Version : %s (%s)", buf, RB_UPI_VERSION);
    LOG_UI("UPI Version : %s\n", buf);

    if (!strcmp(RB_UPI_VERSION, buf))  {
        return true;
    }

    return false;
}

static bool mount_partitions(void)
{
    size_t total_size, erase_size, write_size;


    LOG_INFO("[%s]", __func__);

    if (fota_scan_partitions() <= 0)  {
        LOG_ERROR("[%s] error scanning partitions", __func__);
        return false;
    }

    LOG_INFO("[%s] Mount DATA partition", __func__);
    gFota.data_partition = fota_find_partition_by_name(DATA_PART_NAME);
    if (gFota.data_partition == NULL)  {
        LOG_ERROR("[%s] Can't find DATA partition", __func__);
        return false;
    }

    LOG_INFO("[%s] Mount BOOT partition", __func__);
    gFota.boot_partition = fota_find_partition_by_name(BOOT_PART_NAME);
    if (gFota.boot_partition)  {
        if (fota_partition_info(gFota.boot_partition, &total_size, &erase_size, &write_size) != 0)  {
           LOG_ERROR("[%s] Can't stat BOOT partition", __func__);
            return false;
        }
    } else  {
        LOG_ERROR("[%s] Can't find BOOT partition", __func__);
        return false;
    }
    gFota.boot_partition_info.size = total_size;
    gFota.boot_partition_info.erase_size = erase_size;
    gFota.boot_partition_info.write_size = write_size;

    LOG_INFO("[%s] BOOT partition : total_size=0x%X, erase_size=0x%X, write_size=0x%X",
                __func__, total_size, erase_size, write_size);

    LOG_INFO("[%s] Mount SYSTEM partition", __func__);
    gFota.system_partition = fota_find_partition_by_name(SYSTEM_PART_NAME);
    if (gFota.system_partition)  {
        if (fota_partition_info(gFota.system_partition, &total_size, &erase_size, &write_size) != 0)  {
           LOG_ERROR("[%s] Can't stat SYSTEM partition", __func__);
            return false;
        }
    } else  {
        LOG_ERROR("Can't find SYSTEM partition : %s", SYSTEM_PART_NAME);
        return false;
    }

    gFota.system_partition_info.size = total_size;
    gFota.system_partition_info.erase_size = erase_size;
    gFota.system_partition_info.write_size = write_size;

    LOG_INFO("[%s] SYSTEM partition : total_size=0x%X, erase_size=0x%X, write_size=0x%X",
                __func__, total_size, erase_size, write_size);

    LOG_INFO("[%s] Mount RECOVERY partition", __func__);
    gFota.recovery_partition = fota_find_partition_by_name(RECOVERY_PART_NAME);
    if (gFota.recovery_partition)  {
        if (fota_partition_info(gFota.recovery_partition, &total_size, &erase_size, &write_size) != 0)  {
           LOG_ERROR("[%s] Can't stat RECOVERY partition", __func__);
            return false;
        }
    } else  {
        LOG_ERROR("[%s] Can't find RECOVERY partition", __func__);
        return false;
    }

    gFota.recovery_partition_info.size = total_size;
    gFota.recovery_partition_info.erase_size = erase_size;
    gFota.recovery_partition_info.write_size = write_size;

    //LOG_INFO("[%s] RECOVERY partition : total_size=0x%X, erase_size=0x%X, write_size=0x%X",
    //            __func__, total_size, erase_size, write_size);

    if (gFota.boot_partition_info.erase_size != gFota.system_partition_info.erase_size)  {
        LOG_ERROR("boot erase size != system erase size");
        return false;
    }

    //-------------------------------------------------------------------------

    //LOG_INFO("[%s] Mount UBOOT partition", __func__);
    //gFota.uboot_partition = fota_find_partition_by_name(UBOOT_PART_NAME);
    //if (gFota.uboot_partition)  {
    //    if (fota_partition_info(gFota.uboot_partition, &total_size, &erase_size, &write_size) != 0)  {
    //       LOG_ERROR("[%s] Can't stat UBOOT partition", __func__);
    //        return false;
    //    }
    //} else  {
    //    LOG_ERROR("[%s] Can't find UBOOT partition", __func__);
    //    return false;
    //}
    //gFota.uboot_partition_info.size = total_size;
    //gFota.uboot_partition_info.erase_size = erase_size;
    //gFota.uboot_partition_info.write_size = write_size;

    //LOG_INFO("[%s] Mount BACKUP partition", __func__);
    //gFota.backup_partition = fota_find_partition_by_name(BACKUP_PART_NAME);
    //if (gFota.backup_partition)  {
    //    if (fota_partition_info(gFota.backup_partition, &total_size, &erase_size, &write_size) != 0)  {
    //       LOG_ERROR("[%s] Can't stat BACKUP partition", __func__);
    //        return false;
    //    }
    //} else  {
    //    LOG_ERROR("[%s] Can't find BACKUP partition", __func__);
    //    return false;
    //}
    //
    //gFota.backup_partition_info.size = total_size;
    //gFota.backup_partition_info.erase_size = erase_size;
    //gFota.backup_partition_info.write_size = write_size;

    //-------------------------------------------------------------------------

    LOG_INFO("Partition Info");
    //LOG_INFO("  UBOOT : %X %X",
    //    gFota.uboot_partition_info.size, gFota.uboot_partition_info.erase_size, gFota.uboot_partition_info.write_size);
    LOG_INFO("  Boot     : %X %X %X",
        gFota.boot_partition_info.size, gFota.boot_partition_info.erase_size, gFota.boot_partition_info.write_size);
    LOG_INFO("  System   : %X %X %X",
        gFota.system_partition_info.size, gFota.system_partition_info.erase_size, gFota.system_partition_info.write_size);
    LOG_INFO("  Recovery : %X %X %X",
        gFota.recovery_partition_info.size, gFota.recovery_partition_info.erase_size, gFota.recovery_partition_info.write_size);


    UI_PRINT("mount partitions done\n");

    return true;
}

static bool is_file_exist(const char *folder, const char *file)
{
    struct stat st;
    char  path[MAX_PATH];

    strcpy(path, folder);
    strcat(path, file);

    if (lstat(path, &st) < 0) {
        LOG_INFO("[%s] %s not exist", __func__, path);
        return false;
    }

    return true;
}

static bool find_delta_update_file(const char *folder)
{
    int  folder_len = strlen(folder);

    if (is_file_exist(folder, BOOT_DELTA_FILE))  {
        gFota.boot_delta_file = malloc(folder_len + strlen(BOOT_DELTA_FILE) + 1);
        strcpy(gFota.boot_delta_file, folder);
        strcat(gFota.boot_delta_file, BOOT_DELTA_FILE);
    }

    if (is_file_exist(folder, SYSTEM_DELTA_FILE))  {
        gFota.system_delta_file   = malloc(folder_len + strlen(SYSTEM_DELTA_FILE) + 1);
        strcpy(gFota.system_delta_file, folder);
        strcat(gFota.system_delta_file, SYSTEM_DELTA_FILE);
    }

    if (is_file_exist(folder, RECOVERY_DELTA_FILE))  {
        gFota.recovery_delta_file = malloc(folder_len + strlen(RECOVERY_DELTA_FILE) + 1);
        strcpy(gFota.recovery_delta_file, folder);
        strcat(gFota.recovery_delta_file, RECOVERY_DELTA_FILE);
    }

    if (!(gFota.boot_delta_file || gFota.system_delta_file || gFota.recovery_delta_file))  {
        LOG_ERROR("[%s] Can not find any update files", __func__);
        return false;
    }

    return true;
}

static bool find_image_update_file(const char *folder)
{
    int  folder_len = strlen(folder);

    if (is_file_exist(folder, BOOT_IMAGE_FILE))  {
        gFota.boot_delta_file = malloc(folder_len + strlen(BOOT_IMAGE_FILE) + 1);
        strcpy(gFota.boot_delta_file, folder);
        strcat(gFota.boot_delta_file, BOOT_IMAGE_FILE);
    }

    if (is_file_exist(folder, SYSTEM_IMAGE_FILE))  {
        gFota.system_delta_file   = malloc(folder_len + strlen(SYSTEM_IMAGE_FILE) + 1);
        strcpy(gFota.system_delta_file, folder);
        strcat(gFota.system_delta_file, SYSTEM_IMAGE_FILE);
    }

    if (is_file_exist(folder, RECOVERY_IMAGE_FILE))  {
        gFota.recovery_delta_file = malloc(folder_len + strlen(RECOVERY_IMAGE_FILE) + 1);
        strcpy(gFota.recovery_delta_file, folder);
        strcat(gFota.recovery_delta_file, RECOVERY_IMAGE_FILE);
    }

    if (!(gFota.boot_delta_file || gFota.system_delta_file || gFota.recovery_delta_file))  {
        LOG_ERROR("[%s] Can not find any update files", __func__);
        return false;
    }

    return true;
}

static bool find_all_update_file(const char *root_path)
{
    //if (!find_update_file(DELTA_CHECK))  {
    //    LOG_ERROR("failed to open check file (%s)", strerror(errno));
    //    return false;
    //}

    if (ensure_path_mounted(root_path)) {
        LOG_ERROR("[%s] Can't mount %s", __func__, root_path);
        return false;
    }

    // ----------------------------------------------------------------------------

    char  folder[MAX_PATH];

    strcpy(folder, root_path);
    if (root_path[strlen(root_path) - 1] != '/')  {
        strcat(folder, "/");
    }

    if (find_delta_update_file(folder))  {
        gFota.mode = FOTA_UPGRADE_MODE_DELTA;
    }
    else if (find_image_update_file(folder))  {
        gFota.mode = FOTA_UPGRADE_MODE_IMAGE;
    } else  {
        LOG_ERROR("[%s] Can not find any update files", __func__);
        return false;
    }

    LOG_INFO("[%s] boot delta = %s", __func__, gFota.boot_delta_file);
    LOG_INFO("[%s] system delta = %s", __func__, gFota.system_delta_file);
    LOG_INFO("[%s] recovery delta = %s", __func__, gFota.recovery_delta_file);

    return true;
}

static int fota_update_boot(int operation)
{
    if (!gFota.boot_delta_file)  {
        LOG_ERROR("[%s] Can't find boot.delta", __func__);
        return E_RB_FAILURE;
    }

    long result = 0;

    UI_PRINT("Update BOOT partition : %s\n", BOOT_DELTA_PARTITION_NAME);
    LOG_INFO("[%s] %d", __func__, operation);

    //if (UPI_OP_SCOUT_UPDATE == operation)  {
    //    ui_print("Format BOOT partition\n");
    //    format_volume("/boot");
    //    ui_print("Format BOOT partition done\n");
    //}

    unsigned short partition_name[128] = { '\0' };
	char_to_unicode(BOOT_DELTA_PARTITION_NAME, partition_name);

	unsigned short temp_path[128] = {'\0'};
	unsigned short delta_path[128] = {'\0'};
	unsigned short mount_point[128] = {'\0'};
	//char_to_unicode("/data/fota", temp_path);
	char_to_unicode(FOTA_TEMP_PATH, temp_path);
	//char_to_unicode("/data/boot.delta", delta_path);
	char_to_unicode(gFota.boot_delta_file, delta_path);
	char_to_unicode("/", mount_point);

    CustomerPartitionData partition_data;
    vRM_DeviceData device_data;

    unsigned long InstallerTypes[1] = { 0L };
    unsigned char upi_supplementary_info[1] = { 0 };
    unsigned long upi_supplementary_info_size = 1;

	//setting the 1st structure - partition
    partition_data.partition_name           = (unsigned short *) &partition_name[0];
    partition_data.base_partition_name      = partition_name;
    partition_data.sector_size              = gFota.boot_partition_info.erase_size; //0x20000;
    partition_data.page_size                = gFota.boot_partition_info.erase_size; //0x20000;
    partition_data.rom_start_address        = NAND_START;   // 0;
    partition_data.rom_end_address          = NAND_END;     // 0x20000000;
    partition_data.dir_tree_offset          = 0;
    partition_data.mount_point              = mount_point;
    partition_data.ui16StrSourcePath        = 0;
    partition_data.ui16StrTargetPath        = 0;
	partition_data.ui16StrSourceFileAttr    = 0;
	partition_data.ui16StrTargetFileAttr    = 0;
    partition_data.partition_type           = PT_FOTA;
    partition_data.file_system_type         = 0;
    partition_data.rom_type                 = ROM_TYPE_EMPTY;
	partition_data.compression_type	        = NO_COMPRESSION;

	//setting the 2nd structure - device data
	device_data.ui32Operation               = operation; // UPI_OP_UPDATE; //0;
	device_data.ui32DeviceCaseSensitive     = 1;    // case sensitive
	device_data.pRam                        = upi_working_buffer;
	device_data.ui32RamSize                 = UPI_WORKING_BUFFER_SIZE;
    device_data.ui32NumberOfBuffers         = 4;
    device_data.pBufferBlocks			    = (unsigned long *) (&BackupBuffer[0]);
	device_data.ui32NumberOfPartitions      = 1;        // 1 for FOTA
	device_data.pFirstPartitionData         = &partition_data;
	device_data.ui32NumberOfLangs           = 0;        // 0 for FOTA
	device_data.pLanguages				    = 0;        // NULL for FOTA
	device_data.pTempPath				    = temp_path;  // "/data/update.tmp";
	device_data.pSupplementaryInfo          = (unsigned char **) &upi_supplementary_info;
	device_data.pSupplementaryInfoSize      = &upi_supplementary_info_size;
	device_data.pComponentInstallerTypes    = InstallerTypes;
	device_data.ui32ComponentInstallerTypesNum = 1;
	device_data.enmUpdateType			    = UT_NO_SELF_UPDATE;
	device_data.ui32Flags				    = 0;
	device_data.pDeltaPath				    = delta_path; // "/data/boot.delta";
	device_data.pbUserData				    = 0;

	long ret = RB_vRM_Update(&device_data);
    UI_PRINT("[%s] ret=0x%lX\n", __func__, ret);
	LOG_INFO("[%s] ret=0x%lX ", __func__, ret);

	return ret;
}

static int fota_update_system(int operation)
{
	int ret = 0;
	vRM_DeviceData  device_data;
	CustomerPartitionData pCustomerPartData;
	unsigned int SupplementaryInfoSize = 4;
	unsigned char SupplementaryInfoArray[SupplementaryInfoSize];
	unsigned char* pSupplementaryInfo = (unsigned char*)SupplementaryInfoArray;

	unsigned long ComponentInstallerType[1]={0};
	unsigned short partition_name[MAX_PATH] = {'\0'};
    unsigned short mount_point[MAX_PATH] = {'\0'};
    unsigned short temp_path[MAX_PATH] = {'\0'};
    unsigned short delta_path[MAX_PATH] = {'\0'};

    UI_PRINT("Update SYSTEM partition\n");
    LOG_INFO("[%s]", __func__);

    if (!ensure_path_mounted("/system"))  {
        LOG_INFO("[%s] can not mount system partition", __func__);
    }

    //char_to_unicode("YAFFS", partition_name);
    char_to_unicode(SYSTEM_DELTA_PARTITION_NAME, partition_name);

    char_to_unicode("/", mount_point);
    //char_to_unicode("/data/fota", temp_path);
    char_to_unicode(FOTA_TEMP_PATH, temp_path);
    //char_to_unicode("/data/system.delta", delta_path);
    char_to_unicode(gFota.system_delta_file, delta_path);

	pCustomerPartData.partition_name        = partition_name;
	pCustomerPartData.base_partition_name   = partition_name;
	pCustomerPartData.page_size             = 0x20000;
	pCustomerPartData.sector_size           = 0x20000;
    pCustomerPartData.rom_start_address     = 0;
    pCustomerPartData.rom_end_address       = 0x20000000;
	pCustomerPartData.mount_point           = mount_point;
    pCustomerPartData.ui16StrSourcePath     = 0;
    pCustomerPartData.ui16StrTargetPath     = 0;
    pCustomerPartData.ui16StrSourceFileAttr = 0;
    pCustomerPartData.ui16StrTargetFileAttr = 0;
    pCustomerPartData.partition_type        = PT_FS;
    pCustomerPartData.file_system_type      = FS_JOURNALING_RW;
    pCustomerPartData.rom_type              = ROM_TYPE_READ_WRITE;
    pCustomerPartData.compression_type      = NO_COMPRESSION;

	device_data.ui32Operation                   = operation; // 0;
	device_data.ui32DeviceCaseSensitive         = 1;
    device_data.pRam                            = upi_working_buffer;
    device_data.ui32RamSize                     = UPI_WORKING_BUFFER_SIZE;
    device_data.ui32NumberOfBuffers             = 4;
    device_data.pBufferBlocks                   = (unsigned long *) (&BackupBuffer[0]);
	device_data.ui32NumberOfPartitions          = 1;
	device_data.pFirstPartitionData             = &pCustomerPartData;
    device_data.ui32NumberOfLangs               = 0;
    device_data.pLanguages                      = 0;
    device_data.pTempPath                       = temp_path;
    device_data.pSupplementaryInfo              = (unsigned char**) &pSupplementaryInfo;
    device_data.pSupplementaryInfoSize          = (unsigned long *) &SupplementaryInfoSize;
    device_data.pComponentInstallerTypes        = (unsigned long *) &ComponentInstallerType;
	device_data.ui32ComponentInstallerTypesNum  = 1;
	device_data.enmUpdateType					= UT_NO_SELF_UPDATE;
    device_data.ui32Flags						= 0;
    device_data.pDeltaPath						= delta_path;
    device_data.pbUserData						= NULL;

    ret = RB_vRM_Update(&device_data);

    UI_PRINT("[%s] done 0x%X\n", __func__, ret);
    LOG_INFO("[%s] 0x%X", __func__, ret);

	return ret;
}

static int fota_write_boot(void)
{
    return INSTALL_SUCCESS;
#if 0
    int  ret = -1;

    char  *buffer = malloc(gFota.partition_info.write_size);
    int fd = open(gFota.boot_delta_file, O_RDONLY);
    int r_size;
    int r_total;

    if (!buffer || (fd == -1))  {
        goto bail;
    }

    ui_print("Format BOOT partition\n");
    format_volume("/boot");
    ui_print("Format BOOT partition done\n");

    MtdWriteContext *out = fota_write_partition(gFota.partition);

    r_total = 0;
    while (r_size = read(fd, buffer, gFota.partition_info.write_size))  {
        if (-1 == fota_write_data(out, (char *) buffer, gFota.partition_info.write_size))  {
            LOG_ERROR("RB_WriteBlock Error");
            return E_RB_FAILURE;
        }
        r_total += r_size;
        LOG_UI("write boot : %X\n", r_total);
    }
    fota_write_close(out);

    ret = INSTALL_SUCCESS;

bail:

    if (buffer)
        free(buffer);
    if (fd != -1)
        close(fd);

    return ret;
#endif
}

static int fota_write_system(void)
{
    return INSTALL_SUCCESS;
#if 0
    int  ret = -1;

    char  *buffer = malloc(gFota.partition_info.erase_size);
    //char  *buffer = malloc(0x20000);
    int fd = open(gFota.system_delta_file, O_RDONLY);
    int r_size;
    int r_total;

    if (!buffer || (fd == -1))  {
        goto bail;
    }

    ui_print("Format SYSTEM partition\n");
    int xx = format_volume("/system");
    ui_print("Format SYSTEM partition done : %d\n", xx);

    LOG_INFO("fota_write_system : %X", gFota.partition_info.erase_size);

    MtdWriteContext *out = fota_write_partition(gFota.partition);

    r_total = 0;
#if 1
    memset(buffer, 0xFF, gFota.partition_info.erase_size);
    while (r_size = read(fd, buffer, gFota.partition_info.erase_size))  {
        //if (-1 == fota_write_data(out, (char *) buffer, r_size))  {
        if (-1 == fota_write_data(out, (char *) buffer, r_size))  {
            LOG_ERROR("RB_WriteBlock Error");
            return E_RB_FAILURE;
        }
        LOG_UI("write system : %X %X\n", r_total, r_size);
        LOG_INFO("write system : %X %X\n", r_total, r_size);
        for (xx = 0; xx < 128; xx+=16)  {
            LOG_HEX("      ", &buffer[xx], 16);
        }
        r_total += r_size;
        memset(buffer, 0xFF, gFota.partition_info.erase_size);
        if (r_size < gFota.partition_info.erase_size)
            break;
        //if (r_total >= 0x9000000)
        //    break;
    }
#else
    memset(buffer, 0xFF, gFota.partition_info.erase_size);
    r_size = read(fd, buffer, gFota.partition_info.erase_size);
    if (-1 == fota_write_data(out, (char *) buffer, r_size))  {
        LOG_ERROR("RB_WriteBlock Error");
        LOG_UI("RB_WriteBlock Error");
        ret = E_RB_FAILURE;
        goto bail;
    }
    memset(buffer, 0xFF, gFota.partition_info.erase_size);
    r_size = read(fd, buffer, gFota.partition_info.erase_size);
    if (-1 == fota_write_data(out, (char *) buffer, r_size))  {
        LOG_ERROR("RB_WriteBlock Error");
        LOG_UI("RB_WriteBlock Error");
        ret = E_RB_FAILURE;
        goto bail;
    }
#endif
    //fota_write_close(out);

    ret = INSTALL_SUCCESS;

bail:

    fota_write_close(out);

    if (buffer)
        free(buffer);
    if (fd != -1)
        close(fd);

    return ret;
#endif
}

static bool remove_file(const char *root_path, const char *delta_file)
{
    int  ret = 0;
    char path[256];

    strcpy(path, root_path);
    strcat(path, delta_file);

    LOG_INFO("[%s] %s\n", __func__, path);

    ret = unlink(path);

    if (ret == 0)
		return true;

	if (ret < 0 && errno == ENOENT)	//if file does not exist then we can say that we deleted it successfully
		return true;

    return false;
}

void remove_fota_delta_files(const char *root_path)
{
    char path[256];


    strcpy(path, root_path);
    if (root_path[strlen(root_path) - 1] != '/')  {
        strcat(path, "/");
    }

    LOG_INFO("[%s] root_path=%s, path=%s, %d%d%d\n",
            __func__, root_path, path,
            (gFota.boot_delta_file) ? 1 : 0,
            (gFota.system_delta_file) ? 1 : 0,
            (gFota.recovery_delta_file) ? 1 : 0);

    //if (gFota.boot_delta_file)  {
        remove_file(path, BOOT_DELTA_FILE);
    //}
    //if (gFota.system_delta_file)  {
        remove_file(path, SYSTEM_DELTA_FILE);
    //}
    //if (gFota.recovery_delta_file)  {
        remove_file(path, RECOVERY_DELTA_FILE);
    //}
        unlink(path);
}

#if 0
static void write_result_file(int result)
{
    int result_fd = open(UPDATE_RESULT_FILE, O_RDWR | O_CREAT, 0644);

    if (result_fd < 0) {
        LOG_ERROR("[%s] cannot open '%s' for output", __func__, UPDATE_RESULT_FILE);
        return;
    }

    char buf[4];
    if (S_RB_SUCCESS == result)
        strcpy(buf, "1");
    else
        strcpy(buf, "0");
    write(result_fd, buf, 1);
    close(result_fd);
}
#endif

int find_fota_delta_package(const char *root_path)
{
    if (find_all_update_file(root_path))  {
        return 1;
    }

    return 0;
}

int install_fota_delta_package(const char *root_path)
{
    int  ret = INSTALL_ERROR;

    UI_PRINT("[%s]\n", __func__);

    if (!fota_init())  {
        goto FAIL;
    }

    if (!check_upi_version())  {
        goto FAIL;
    }

    if (!mount_partitions())  {
        goto FAIL;
    }

    if (!find_all_update_file(root_path))  {
        goto FAIL;
    }

    upi_working_buffer = (unsigned char *) malloc(UPI_WORKING_BUFFER_SIZE);
    if (0 == upi_working_buffer)  {
        LOG_ERROR("Can not alloc working buffer");
        goto FAIL;
    }

    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_reset_progress();
    ui_show_progress(1, 0);

    if (gFota.mode == FOTA_UPGRADE_MODE_DELTA)  {

        gFota.uRound = 0;
        if (gFota.boot_delta_file)  {
    #if VERIFY_BOOT_SOURCE
            gFota.uRound++;
    #endif
    #if VERIFY_BOOT_TARGET
            gFota.uRound++;
    #endif
            gFota.uRound++;
        }

        if (gFota.system_delta_file)  {
    #if VERIFY_SYSTEM_SOURCE
            gFota.uRound++;
    #endif
    #if VERIFY_SYSTEM_TARGET
            gFota.uRound++;
    #endif
            gFota.uRound++;
        }

        //
    #if (VERIFY_BOOT_SOURCE || VERIFY_BOOT_TARGET)
        if (gFota.boot_delta_file)  {
            UI_PRINT("verify boot.delta\n");
            gFota.partition = gFota.boot_partition;
            gFota.partition_info = gFota.boot_partition_info;
            gFota.delta_file = fopen(gFota.boot_delta_file, "rb");

            if (!gFota.delta_file)
                goto FAIL;

        #if VERIFY_BOOT_SOURCE
            ret = fota_update_boot(UPI_OP_VERIFY_SOURCE);
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] verify boot source error : 0x%X", __func__, ret);
                goto FAIL;
            }
        #endif

        #if VERIFY_BOOT_TARGET
            ret = fota_update_boot(UPI_OP_VERIFY_TARGET);
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] verify boot target error : 0x%X", __func__, ret);
                goto FAIL;
            }
        #endif

            fclose(gFota.delta_file);
        }
    #endif


    #if (VERIFY_SYSTEM_SOURCE || VERIFY_SYSTEM_TARGET)
        if (gFota.system_delta_file)  {
            UI_PRINT("verify system.delta\n");
            gFota.partition = gFota.system_partition;
            gFota.partition_info = gFota.system_partition_info;
            gFota.delta_file = fopen(gFota.system_delta_file, "rb");

            if (!gFota.delta_file)
                goto FAIL;

        #if VERIFY_SYSTEM_SOURCE
            ret = fota_update_system(UPI_OP_VERIFY_SOURCE);
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] verify system source error : 0x%X", __func__, ret);
                goto FAIL;
            }
        #endif
        #if VERIFY_SYSTEM_TARGET
            ret = fota_update_system(UPI_OP_VERIFY_TARGET);
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] verify system target error : 0x%X", __func__, ret);
                goto FAIL;
            }
        #endif

            fclose(gFota.delta_file);
        }
    #endif

        if (gFota.boot_delta_file)  {
            gFota.partition = gFota.boot_partition;
            gFota.partition_info = gFota.boot_partition_info;
            gFota.delta_file = fopen(gFota.boot_delta_file, "rb");
            //ret = fota_update_boot(UPI_OP_SCOUT_UPDATE);
            ret = fota_update_boot(UPI_OP_UPDATE);
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] update boot error : 0x%X", __func__, ret);
                UI_PRINT("[%s] update boot error : 0x%X", __func__, ret);
                goto FAIL;
            }
        }

        if (gFota.system_delta_file)  {
            gFota.partition = gFota.system_partition;
            gFota.partition_info = gFota.system_partition_info;
            gFota.delta_file = fopen(gFota.system_delta_file, "rb");
            //ret = fota_update_system(UPI_OP_SCOUT_UPDATE);
            ret = fota_update_system(UPI_OP_UPDATE);
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] update system error : 0x%X", __func__, ret);
                goto FAIL;
            }
        }

        ret = INSTALL_SUCCESS;
    }
    else if (gFota.mode == FOTA_UPGRADE_MODE_IMAGE)  {
#if 0
        if (gFota.boot_delta_file)  {
            gFota.partition = gFota.boot_partition;
            gFota.partition_info = gFota.boot_partition_info;
            //gFota.delta_file = fopen(gFota.boot_delta_file, "rb");
            ret = fota_write_boot();
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] update boot error : 0x%X", __func__, ret);
                UI_PRINT("[%s] update boot error : 0x%X", __func__, ret);
                goto FAIL;
            }
        }

        if (gFota.system_delta_file)  {
            gFota.partition = gFota.system_partition;
            gFota.partition_info = gFota.system_partition_info;
            //gFota.delta_file = fopen(gFota.system_delta_file, "rb");
            ret = fota_write_system();
            if (S_RB_SUCCESS != ret)  {
                LOG_ERROR("[%s] update system error : 0x%X", __func__, ret);
                goto FAIL;
            }
        }

        ret = INSTALL_SUCCESS;
#endif
    }

FAIL:
    if (gFota.boot_delta_file)  {
        free(gFota.boot_delta_file);
        gFota.boot_delta_file = NULL;
    }
    if (gFota.system_delta_file)  {
        free(gFota.system_delta_file);
        gFota.system_delta_file = NULL;
    }
    if (gFota.recovery_delta_file)  {
        free(gFota.recovery_delta_file);
        gFota.recovery_delta_file = NULL;
    }

    if (gFota.delta_file)  {
        fclose(gFota.delta_file);
        gFota.delta_file = NULL;
    }

    if (upi_working_buffer)
        free(upi_working_buffer);

    //remove_delta_files(root_path);
    //write_result_file(ret);

    //struct bootloader_message boot;
    //memset(&boot, 0, sizeof(boot));
    //strcpy(boot.command, "FOTA");
    //sprintf(boot.status, "%d", ret);
    //if (S_RB_SUCCESS != ret)
    //    sprintf(boot.recovery, "%s", get_error_string(ret));
    //set_bootloader_message(&boot);

    return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// NAND Interface
//

/* RB_GetBlockSize() - Returns the size of one memory block.
 *
 * This function must be called before any block reading or writing can be done.
 * Note: For a given update, the sequence of blocks that is being updated is always the same.
 * This assumption is made in order to be able to successfully continue update after failure situation.
 */
long RB_GetBlockSize(void)
{
    LOG_INFO("[%s] : %d", __func__, gFota.partition_info.erase_size);

    return gFota.partition_info.erase_size;
}


/* RB_ReadImage() - Reads data from flash.
 *
 * Number of bytes to read should be less or equal to block size.
 * The data is used to create new block based on read data and diff file information
 */
long RB_ReadImage(
	void *pbUserData,					/* User data passed to all porting routines */
	unsigned char *pbBuffer,			/* pointer to user buffer */
    unsigned long dwStartAddress,		/* Location in Flash */
    unsigned long dwSize) 				/* number of bytes to copy */
{
    LOG_INFO("[%s] from 0x%X to 0x%X, size = 0x%X", __func__, dwStartAddress, (int) pbBuffer, dwSize);

    if (dwStartAddress >= gFota.partition_info.size)  {
        LOG_ERROR("[%s] exceed the partition size : %X %X", __func__, dwStartAddress, gFota.partition_info.size);
        return E_RB_BAD_PARAMS;
    }

    LOG_INFO("[%s] addr=0x%X", __func__, dwStartAddress);

    MtdReadContext *in = fota_read_partition(gFota.partition);

    unsigned long block_addr = dwStartAddress & (~0x1FFFF);

    if (in == NULL)  {
        LOG_ERROR("[%s] can not read partition", __func__);
    }

#ifdef MTK_EMMC_SUPPORT
    ssize_t size = fota_read_data_ex(in, (char *) pbBuffer, dwSize, dwStartAddress);
    if (size != (ssize_t) dwSize)  {
        LOG_ERROR("[%s] align read len error : %X %X", __func__, size, dwSize);
        return E_RB_FAILURE;
    }
#else
    if (block_addr == dwStartAddress)  {
        ssize_t size = fota_read_data_ex(in, (char *) pbBuffer, dwSize, dwStartAddress);
        if (size != (ssize_t) dwSize)  {
            LOG_ERROR("[%s] align read len error : %X %X", __func__, size, dwSize);
            return E_RB_FAILURE;
        }
    }
    else  {
        char tmp_buf[0x20000];
        unsigned long offset = dwStartAddress - block_addr;
        unsigned long prev_read = 0x20000 - offset;

        //               offset          prev_read
        //        |------------------+--------------|
        //     block_addr           addr

        LOG_INFO("[%s] unalign read : 0x%X 0x%X 0x%X %X", __func__,
                 dwStartAddress, block_addr, offset, prev_read);

        ssize_t size = fota_read_data_ex(in, tmp_buf, 0x20000, block_addr);
        if (size != (ssize_t) 0x20000)  {
            LOG_ERROR("[%s] unalign read len error : %X %X", __func__, size, 0x20000);
            return E_RB_FAILURE;
        }
        memcpy((char *) pbBuffer, tmp_buf + offset, prev_read);

        if (dwSize > prev_read)  {
            size = fota_read_data_ex(in, (char *) pbBuffer + prev_read, dwSize - prev_read, dwStartAddress+prev_read);
            if (size != (ssize_t) (dwSize - prev_read))  {
                LOG_ERROR("[%s] unalign read len error : %X %X", __func__, size, dwSize - prev_read);
                return E_RB_FAILURE;
            }
        }
    }
#endif

    fota_read_close(in);

#ifdef FOTA_COMPARE_GOLDEN
    FILE *fp = fopen("/data/boot_1048p27_phone.img", "rb");
    unsigned char  *pTmp = malloc(dwSize);
    int  i;
    if (fp && pTmp)  {
        fseek(fp, addr, SEEK_SET);
        fread(pTmp, sizeof(char), dwSize, fp);
        for (i = 0; i < dwSize; i++)  {
            if (pbBuffer[i] != pTmp[i])
                break;
        }
        if (i != dwSize)  {
            LOG_ERROR("[%s] Compare Golden Fail : 0x%X.", __func__, i);
            LOG_ERROR("[%s] Y : %X %X %X %X %X %X %X %X", __func__,
                        pTmp[i+0], pTmp[i+1], pTmp[i+2], pTmp[i+3],
                        pTmp[i+4], pTmp[i+5], pTmp[i+6], pTmp[i+7]);
            LOG_ERROR("[%s] N : %X %X %X %X %X %X %X %X", __func__,
                        pbBuffer[i+0], pbBuffer[i+1], pbBuffer[i+2], pbBuffer[i+3],
                        pbBuffer[i+4], pbBuffer[i+5], pbBuffer[i+6], pbBuffer[i+7]);
        }
        else  {
            LOG_INFO("[%s] Compare Golden OK.", __func__);
        }
    }
    else   {
        LOG_ERROR("[%s] Can not open golden file.", __func__);
    }

    if (pTmp)   free(pTmp);
    if (fp)     fclose(fp);
#endif  // FOTA_COMPARE_GOLDEN

    if (dwSize > 16)
        LOG_HEX("[RB_ReadImage] ", (const char *) pbBuffer, 16);
    else
        LOG_HEX("[RB_ReadImage] ", (const char *) pbBuffer, dwSize);

    return S_RB_SUCCESS;
}

/* RB_WriteBlock() - Writes one block to a given location in flash.
 *
 * Erases the image location and writes the data to that location
 */
long RB_WriteBlock(
	void *pbUserData,					/* User data passed to all porting routines */
	unsigned long dwBlockAddress,		/* address of the block to be updated */
	unsigned char *pbBuffer) 			/* pointer to data to be written */

{
    LOG_INFO("[%s] addr=0x%X(0x%X), size=0x%X", __func__,
            dwBlockAddress, dwBlockAddress, gFota.partition_info.erase_size);

    if (pbBuffer == NULL)  {
        LOG_ERROR("[%s] no data", __func__);
        return E_RB_BAD_PARAMS;
    }

    if (dwBlockAddress % gFota.partition_info.erase_size)  {
        LOG_ERROR("[%s] address not block alignment", __func__);
        return E_RB_BAD_PARAMS;
    }

    if (dwBlockAddress >= gFota.partition_info.size)  {
        LOG_ERROR("[%s] exceed the partition size : %X %X", __func__, dwBlockAddress, gFota.partition_info.size);
        return E_RB_BAD_PARAMS;
    }

    LOG_HEX("[RB_WriteBlock] ", (const char *) pbBuffer, 16);

#ifdef FOTA_COMPARE_GOLDEN
    FILE *fp = fopen("/data/boot_1048nr9_phone.img", "rb");
    size_t size = gFota.partition_info.erase_size;
    unsigned char  *pTmp = malloc(size);
    if (fp && pTmp)  {
        fseek(fp, addr, SEEK_SET);
        fread(pTmp, sizeof(char), size, fp);
        if (memcmp(pbBuffer, pTmp, size) != 0) {
            LOG_ERROR("[%s] Compare Golden Fail.", __func__);
        }
        else  {
            LOG_INFO("[%s] Compare Golden OK.", __func__);
        }
    }
    else   {
        LOG_ERROR("[%s] Can not open golden file.", __func__);
    }

    if (pTmp)   free(pTmp);
    if (fp)     fclose(fp);
#endif  // FOTA_COMPARE_GOLDEN


    MtdWriteContext *out = fota_write_partition(gFota.partition);

#ifdef MTK_EMMC_SUPPORT
    if (-1 == fota_write_data_ex(out, (char *) pbBuffer, 0x20000, dwBlockAddress))  {
        LOG_ERROR("RB_WriteBlock Error");
        return E_RB_FAILURE;
    }
#else
    if (-1 == fota_write_block_ex(out, (char *) pbBuffer, dwBlockAddress))  {
        LOG_ERROR("RB_WriteBlock Error");
        return E_RB_FAILURE;
    }
#endif

    fota_write_close(out);

    return S_RB_SUCCESS;
}

/* RB_WriteMetadataOfBlock() - Write one metadata of a given block in flash
 *
 * writes the metadata in the matching place according to the represented block location
 */
long RB_WriteMetadataOfBlock(
	void *pbUserData,					/* User data passed to all porting routines */
	unsigned long dwBlockAddress,		/* address of the block of that the metadata describe */
	unsigned long dwMdSize,				/* Size of the data that the metadata describe*/
	unsigned char *pbMDBuffer) 			/* pointer to metadata to be written */
{
    LOG_INFO("[%s]", __func__);
    LOG_ERROR("%s : addr = 0x%X, size = 0x%X", __func__, dwBlockAddress, dwMdSize);

    // Unused in Redbend solution.

    return E_RB_FAILURE;
}

/* RB_GetDelta() - Get the Delta either as a whole or in block pieces */
long RB_GetDelta(
	void *pbUserData,				    /* User data passed to all porting routines */
	unsigned char *pbBuffer,			/* pointer to user buffer */
    unsigned long dwStartAddressOffset, /* offset from start of delta file */
    unsigned long dwSize)               /* buffer size in bytes */
{

    LOG_INFO("[%s] 0x%X 0x%X",  __func__, dwStartAddressOffset, dwSize);

    fseek(gFota.delta_file, dwStartAddressOffset, SEEK_SET);
    int n = ftell(gFota.delta_file);
    if (n != (int) dwStartAddressOffset)  {
        LOG_ERROR("[%s] Can not move file pointer : 0x%X 0x%X", __func__, n, (int) dwStartAddressOffset);
        return E_RB_FAILURE;
    }

    n = fread(pbBuffer, sizeof(char), dwSize, gFota.delta_file);
    if (n != (int) dwSize) {
        LOG_ERROR("[%s] Can not read enough data %X", __func__, n);
        return E_RB_FAILURE;
    }

    return S_RB_SUCCESS;
}

long RB_GetRBDeltaOffset(
    void *pbUserData,
    unsigned long delta_ordinal,
    unsigned long* delta_offset,
    unsigned long *installer_types,
    unsigned long installer_types_num,
    UpdateType update_type)
{
    LOG_INFO("[%s] 0x%X 0x%X", __func__, delta_ordinal, *delta_offset);

    // RB default
    //RB_UInt32 size = 0;
    unsigned long size = 0;
    return RB_GetSignedDeltaOffset(delta_ordinal, delta_offset, &size, installer_types, installer_types_num, update_type);
}

/* RB_ReadBackupBlock() - Copy data from backup block to RAM.
 *
 * Can copy data of specified size from any location in one of the buffer blocks and into specified RAM location
 */
long RB_ReadBackupBlock(
	void *pbUserData,					/* User data passed to all porting routines */
	unsigned char *pbBuffer,			/* pointer to user buffer in RAM where the data will be copied */
	unsigned long dwBlockAddress,		/* address of data to read into RAM. Must be inside one of the backup buffer blocks */
	unsigned long dwSize)				/* buffer size in bytes */
{
    LOG_INFO("[%s] addr = 0x%X, size = 0x%X", __func__, dwBlockAddress, dwSize);

    if (!gFota.backup_partition)  {
        LOG_ERROR("[%s] No backup partition", __func__);
        return E_RB_FAILURE;
    }

    if (dwBlockAddress % gFota.backup_partition_info.erase_size)  {
        LOG_ERROR("[%s] address not block alignment", __func__);
    }

    MtdReadContext *in = fota_read_partition(gFota.backup_partition);
    int retry = 2, len;
    while (retry--)  {
        len = fota_read_data_ex(in, (char *) pbBuffer, dwSize, dwBlockAddress);
        if (len != (int) dwSize)  {
            LOG_ERROR("[%s] Can not read enough data %X", __func__, len);
        }
    }
    fota_read_close(in);

    if (!retry)  {
        return E_RB_FAILURE;
    }

    return S_RB_SUCCESS;
}

/* RB_WriteBackupBlock() - Copy data from specified address in RAM to a backup block.
 *
 * This function copies data from specified address in RAM to a backup buffer block.
 * This will always write a complete block (sector) and the address will always be one of the addresses
*  provided in RB_ImageUpdate() call.
 */
long RB_WriteBackupBlock(
    void *pbUserData,					/* User data passed to all porting routines */
    unsigned long dwBlockStartAddress,	/* address of the block to be updated */
    unsigned char *pbBuffer)  	        /* RAM to copy data from */
{
    LOG_INFO("[%s] addr = 0x%X", __func__, dwBlockStartAddress);

    if (!gFota.backup_partition)  {
        LOG_ERROR("[%s] No backup partition", __func__);
        return E_RB_FAILURE;
    }

    if (dwBlockStartAddress % gFota.backup_partition_info.erase_size)  {
        LOG_ERROR("[%s] address not block alignment", __func__);
    }

    MtdWriteContext *out = fota_write_partition(gFota.backup_partition);
    int retry = 2, len;
    while (retry--)  {
        len = fota_write_block_ex(out, (char *) pbBuffer, dwBlockStartAddress);
        if (len != (int) gFota.backup_partition_info.erase_size)  {
            LOG_ERROR("[%s] Can not write enough data %X", __func__, len);
        }
    }
    fota_write_close(out);

    if (!retry)  {
        return E_RB_FAILURE;
    }

    return S_RB_SUCCESS;
}

/* RB_EraseBackupBlock() - Erase a specific backup block.
 *
 * This function will erase the specified block starting from dwStartAddress.  The
 * block address must be aligned to sector size and a single sector should be erased.
 */
long RB_EraseBackupBlock(
    void *pbUserData,					/* User data passed to all porting routines */
    unsigned long dwStartAddress)		/* block start address in flash to erase */
{
    LOG_ERROR("[%s]", __func__);

    // Unused function

    return S_RB_SUCCESS;
}

/* RB_WriteBackupPartOfBlock() - Copy data from specified address in RAM to part of a backup block.
 *
 * This function copies data from specified address in RAM to a backup buffer block.
 * This will write part of a block (sector) that was already written with RB_WriteBackupBlock().
 */
long RB_WriteBackupPartOfBlock(
    void *pbUserData,					/* User data passed to all porting routines */
    unsigned long dwStartAddress,		/* Start address in flash to write to */
    unsigned long dwSize,				/* Size of data to write */
    unsigned char* pbBuffer)			/* Buffer in RAM to write from */
{
    LOG_INFO("[%s] addr = 0x%X, size = 0x%X", __func__, dwStartAddress, dwSize);

    if (!gFota.backup_partition)  {
        LOG_ERROR("[%s] No backup partition", __func__);
        return E_RB_FAILURE;
    }

    if (dwStartAddress % gFota.backup_partition_info.erase_size)  {
        LOG_ERROR("[%s] address not block alignment", __func__);
    }

    MtdWriteContext *out = fota_write_partition(gFota.backup_partition);
    int retry = 2, len;
    while (retry--)  {
        len = fota_write_data_ex(out, (char *) pbBuffer, dwSize, dwStartAddress);
        if (len != (int) dwSize)  {
            LOG_ERROR("[%s] Can not write enough data %X", __func__, len);
        }
    }
    fota_write_close(out);

    if (!retry)  {
        return E_RB_FAILURE;
    }

    return S_RB_SUCCESS;
}

/* RB_ResetTimerA() - Reset watchdog timer A
 *
 * This function is being called periodically within the 30-second period
 */
long RB_ResetTimerA(void)
{
	LOG_INFO("[%s]", __func__);
	return S_RB_SUCCESS;
}

/* RB_Trace() - Display trace messages on the console for debug purposes
 *
 * Format and print data using the C printf() format
 */
unsigned long RB_Trace(
    void *pbUserData,                   /* User data passed to all porting routines */
    const char *aFormat,...)            /* format string to printf */
{
    int err = errno;
    va_list args;
    va_start(args, aFormat);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), aFormat, args);
    va_end(args);

    fprintf(stdout, "[%s] %s", __func__, buf);
    clearerr(stdout);
    fflush(stdout);

    return S_RB_SUCCESS;
}

/* RB_Progress() - Report the current stage of the update
 *
 * Provides information in percents on the update process progress
 */
void RB_Progress(
    void *pbUserData,           /* User data passed to all porting routines */
    unsigned long uPercent)     /* progress info in percents */
{
    float fPercent;

    fPercent = (float) (gFota.uProgress + uPercent) / ((float) gFota.uRound * 100.0);

    LOG_INFO("[%s] %ld %ld %f", __func__, uPercent, gFota.uProgress, fPercent);
    //ui_print("%d:%d:%d ", gFota.uProgress, uPercent, (int) (fPercent * 100));

    ui_set_progress(fPercent);

    if (uPercent == 100)  {
        gFota.uProgress += 100;
    }
    //ui_print("%02d ", uPercent);
}


// Tmp
long RB_GetAvailableFreeSpace(void *pbUserData, unsigned short* partition_name, unsigned long* available_flash_size)
{
    char name[MAX_PATH] = {'\0'};

    unicode_to_char(partition_name, name);

    struct statfs st;

    if (!strcmp(name, "YAFFS"))
    {
        if (statfs("/system", &st) < 0) {
            LOG_ERROR("[%s] Can not get system stat (%s)", __func__, strerror(errno));
        } else {
            LOG_INFO("[%s] bfree=0x%llX, bsize=0x%llX", __func__, st.f_bfree, st.f_bsize);
        }

        *available_flash_size = st.f_bfree * st.f_bsize ;
    }
    else
    {
        *available_flash_size = 0xEFFFFFFF;
    }

    LOG_INFO("[%s] %s, ret = 0x%lX", __func__, name, *available_flash_size);

    return S_RB_SUCCESS;
}


// Not in Spec
long RB_LockFile(unsigned short* file_full_path)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_UnlockFile(unsigned short* file_full_path)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_GetDPProtocolVersion(void* pbUserData, void* pbyRAM, unsigned long dwRAMSize,
							 unsigned long *installer_types, unsigned long installer_types_num, UpdateType update_type,
							 unsigned long *dpProtocolVersion)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}


long RB_GetDPScoutProtocolVersion(void* pbUserData, void* pbyRAM, unsigned long dwRAMSize,
								  unsigned long *installer_types, unsigned long installer_types_num, UpdateType update_type,
								  unsigned long *dpScoutProtocolVersion)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

RB_RETCODE RB_ReadSourceBytes(
	void *pbUserData,				/* User data passed to all porting routines. Could be NULL */
    unsigned long address,			/* address of page in flash to retrieve */
    unsigned long size,				/* size of page in flash to retrieve */
    unsigned char *buff,			/* buffer large enough to contain size bytes */
	long section_id)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_ReadImageNewKey(
	void *pbUserData,					/* User data passed to all porting routines */
	unsigned char *pbBuffer,
	unsigned long dwStartAddress,
	unsigned long dwSize)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_ResetTimerB(void)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_SemaphoreInit(void *pbUserData, RB_SEMAPHORE semaphore, long num_resources)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_SemaphoreDestroy(void *pbUserData, RB_SEMAPHORE semaphore)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_SemaphoreDown(void *pbUserData, RB_SEMAPHORE semaphore,long num)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

long RB_SemaphoreUp(void *pbUserData, RB_SEMAPHORE semaphore,long num)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

void *RB_Malloc(unsigned long size)
{
    LOG_INFO("[%s]", __func__);
    return 0;
}

void RB_Free(void *pMemBlock)
{
    LOG_INFO("[%s]", __func__);
}

