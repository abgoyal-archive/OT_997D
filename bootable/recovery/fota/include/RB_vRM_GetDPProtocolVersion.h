#ifndef _RB_VRM_GET_DP_PROTOCOL_VERSION_H
#define _RB_VRM_GET_DP_PROTOCOL_VERSION_H

    /*********************************************************
     **               C O N F I D E N T I A L               **
     **                  Copyright ® 2010                   **
     **    Red Bend Software Inc.    All rights reserved    **
     *********************************************************/ 

#include "vRM_PublicDefines.h"

#ifdef __cplusplus
extern "C" {
#endif


/* --------------------------------------------- 
   Additional vRM functions - exported functions
   --------------------------------------------- */

// returns the DP protocol version - that is, the protocol version of the first delta in the first valid component. 
// A valid component is one that has valid installer type and update type, and contains at least one delta. 
// (Similar to RB_GetDeltaProtocolVersion, but for the vRM case). 
long RB_GetDPProtocolVersion(void* pbUserData, void* pbyRAM, unsigned long dwRAMSize, 
							 unsigned long *installer_types, unsigned long installer_types_num, UpdateType update_type,
							 unsigned long *dpProtocolVersion);

// returns the DP Scout protocol version - that is, the scout protocol version of the first delta in the first valid component. 
// A valid component is one that has valid installer type and update type, and contains at least one delta.
// (Similar to RB_GetDeltaScoutProtocolVersion, but for the vRM case). 
long RB_GetDPScoutProtocolVersion(void* pbUserData, void* pbyRAM, unsigned long dwRAMSize, 
								  unsigned long *installer_types, unsigned long installer_types_num, UpdateType update_type,
								  unsigned long *dpScoutProtocolVersion);


#ifdef __cplusplus
}
#endif



#endif // _RB_VRM_GET_DP_PROTOCOL_VERSION_H
