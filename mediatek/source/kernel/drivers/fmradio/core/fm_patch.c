

#include <linux/fs.h>
#include <asm/uaccess.h>

#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"

fm_s32 fm_file_exist(const fm_s8 *filename)
{
    fm_s32 ret = 0;
    mm_segment_t old_fs;
    struct file *fp = NULL;

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    fp = filp_open(filename, O_RDONLY, 0);

    if (IS_ERR(fp)) {
        WCN_DBG(FM_ERR | CHIP, "open \"%s\" failed\n", filename);
        set_fs(old_fs);
        return -FM_EPATCH;
    } else {
        WCN_DBG(FM_NTC | CHIP, "open \"%s\" ok\n", filename);
    }

    if (fp) {
        filp_close(fp, NULL);
    }

    set_fs(old_fs);

    return ret;
}


fm_s32 fm_file_read(const fm_s8 *filename, fm_u8* dst, fm_s32 len, fm_s32 position)
{
    fm_s32 ret = 0;
    loff_t pos = position;
    mm_segment_t old_fs;
    struct file *fp = NULL;

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    fp = filp_open(filename, O_RDONLY, 0);

    if (IS_ERR(fp)) {
        WCN_DBG(FM_ERR | CHIP, "open \"%s\" failed\n", filename);
        set_fs(old_fs);
        return -FM_EPATCH;
    } else {
        WCN_DBG(FM_NTC | CHIP, "open \"%s\" ok\n", filename);
    }

    ret = vfs_read(fp, (char __user *)dst, len, &pos);

    if (ret < 0) {
        WCN_DBG(FM_ERR | CHIP, "read \"%s\" failed\n", filename);
    } else if (ret < len) {
        WCN_DBG(FM_NTC | CHIP, "read \"%s\" part data\n", filename);
    } else {
        WCN_DBG(FM_NTC | CHIP, "read \"%s\" full data\n", filename);
    }

    if (fp) {
        filp_close(fp, NULL);
    }

    set_fs(old_fs);

    return ret;
}
