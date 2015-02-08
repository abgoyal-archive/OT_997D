#ifndef SEC_H_
#define SEC_H_

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *  INCLUDING
 ******************************************************************************/
#include "../minzip/Zip.h"

/******************************************************************************
 *  DEFINITION
 ******************************************************************************/
#define SEC_OK                                  0x0000
#define SEC_SBOOT_NOT_ENABLED                   0x9007
#define SEC_SUSBDL_NOT_ENABLED                  0x9009


/******************************************************************************
 *  EXPORT FUNCTION
 ******************************************************************************/
extern int sec_boot_init (bool, bool);
extern int sec_verify_img_info (ZipArchive *zip,bool bDebug);
extern int sec_boot_mark_status (void);
extern int sec_boot_recovery_update(void);
extern int sec_boot_invalid (void);

#ifdef __cplusplus
}
#endif

#endif  // SEC_H_
