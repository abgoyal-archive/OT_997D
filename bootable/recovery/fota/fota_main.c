#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"


extern bool fota_process(void);

int main(int argc, char **argv)
{
    time_t start = time(NULL);


    // If these fail, there's not really anywhere to complain...
    //freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
    //freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);

    //fprintf(stderr, "Starting recovery on %s", ctime(&start));

    //ui_init();

    if (INSTALL_SUCCESS != install_fota_delta_package("/data"))  {
        //reboot(RB_AUTOBOOT);
        return EXIT_SUCCESS;
    }

    return EXIT_SUCCESS;
}
