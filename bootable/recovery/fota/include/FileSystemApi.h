#ifndef _FILE_SYSTEM_API_H_
#define _FILE_SYSTEM_API_H_



	   /*********************************************************
	  **					C O N F I D E N T I A L             **
	 **						  Copyright 2002-2008                **
	  **                         Red Bend Ltd.                  **
	   **********************************************************/



/*
 *   Part of Red Bend's API definition for the UPdate Installer
 */


/*!
 ************************************************************
 *  
 * @date		April 2008
 *
 * @brief
 *	Redbend Interface for the Mutil files update product.
 *
 *
 *	Below you can find the API for the basic IO operation
 *  that needs to be implemented by the customer as a glue layer
 *  between the Redbend library and the customer Operating & File Systems
 *  and the library entry point API that should be called in order
 *  to invoke the update procedure.
 *  
 *	Glue Layer Operations:
 *
 *	1. RB_OpenFile		- Open any given file in the file system
 *	2. RB_CloseFile		- Close an open file in the file system
 *	3. RB_WriteFile		- Writes block of data to an open file
 *	4. RB_ReadFile		- Reads any size of data from an open file
 *	5. RB_FSTrace		- Send any kind of indication to the application.
 *	6. RB_FSProgress	- An update procedure indicator
 *
 *	The above Operation should be implememnted by the customer.
 *
 *	Invocation API:
 *		RB_FileSystemUpdate - Should be invoked be the customer's 
 *                       application, provided with system parameters
 *     
 *
 *  @note
 *  All the operations that are part of the glue layer
 *  has the ability to get a user defined structure that may
 *  contain any user defined data or pointer to functions.
 *  The library code doesn't do any use with it. 
 *  It flexable, and allow the user the freedom to implement
 *  the glue functions according to his needs.
 *  
 *  The calling application creates the data struct and supply,
 *  it to RB_FileSystemUpdate when it decides to invoke the update procedure.
 *
 ************************************************************
 */


#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/*
 ************************************************************
 *               STRUCTURES DECLARATION
 ************************************************************
 */


/*
 ************************************************************
 *               REDBEND UPDATE API DECLARATION
 ************************************************************
 */


/*! 
 ************************************************************
 *                     RB_FileSystemUpdate
 ************************************************************
 *
 * @brief
 *	The Update procedure invoker
 *
 *	Call this function with the proper parameters in order to run the Update
 *	procedure.
 * 
 *	@param pbUserData			Any user data structure, that may be useful for the user
 *								implementation, if not needed set to NULL.
 *								While in run time the Update procedure supply this given 
 *								structure to the caller such as RB_OpenFile, RB_CloseFile etc.
 *
 *	@param strRootSourcePath	Pointer to a NULL-terminated Unicode (wide char) string 
 *								with the root directory of the File System to be updated.
 *
 *	@param strRootTargetPath	Pointer to a NULL-terminated Unicode (wide char) string 
 *								with the root directory where the updated File System will 
 *								be created. Can be the same as strRootSourcePath.
 *
 *	@param strTempPath			Pointer to a NULL-terminated Unicode (wide char) string with 
 *								the directory that will be used for temporary files during the update process.
 *
 *	@param strDeltaPath			If the File System delta-update is stored in the file system, 
 *								this parameter should hold a pointer to a NULL-terminated 
 *								Unicode (wide char) string with the full path of the delta-update file.
 *
 *	@param pbyRAM				Pointer to the RAM area that is used for FS update process.
 *
 *	@param dwRAMSize			Size of available RAM to be used by UPI.
 *
 *	@operation					Type of operation for FS UPI to preform.
 *
 *	@return				One of the return codes as defined in RbErrors.h
 * 
 ************************************************************ 
 */


long
RB_FileSystemUpdate(
	void*					pbUserData,
	const unsigned short*	strRootSourcePath,
	const unsigned short*	strRootTargetPath,
	const unsigned short*	strTempPath,
	const unsigned short*	strDeltaPath,
	unsigned char*			pbyRAM, 
	unsigned long			dwRAMSize,
	unsigned long			wOperation
);


#ifdef __cplusplus
	}
#endif	/* __cplusplus */

#endif
