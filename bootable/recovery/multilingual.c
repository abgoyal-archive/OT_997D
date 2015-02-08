/*
 * For multilingual support.
 */


#include <stdio.h>
#include <unistd.h>
#include "multilingual.h"
#include "ml_string.h"

static int language = DEFAULT_LANG;

static inline int ml_set_language(int l)
{
	if(l > supported_lang_cnt){
		language = DEFAULT_LANG;
		return 0;
	}

	language = l;
	return 0;
}

int ml_select_language(int i)
{
	ml_set_language(supported_lang[i]);
	return 0;
}


int ml_string_fetch(char *name, char*** header, char*** menu)
{
	int i;
	struct ml_strings *mls = g_ml_str;
	int max = supported_lang_cnt * PAGE_COUNT; 
	
	for(i=0;i<max;i++){
		if((mls[i].lang == language) && 
			(0 == strncmp(mls[i].name , name, sizeof(name)))){
			
			*header = mls[i].header;
			*menu = mls[i].menu;
			break;
		}
	}

	return 0;
}


