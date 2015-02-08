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

#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include <pixelflinger/pixelflinger.h>
#include <cutils/memory.h>
#include <cutils/log.h>

#include "minui.h"

extern GGLContext *gr_context;

typedef struct {
	GGLSurface texture;
	unsigned cwidth;
	unsigned cheight;
	unsigned ascent;
} GRFont;

typedef struct {
	unsigned long long utf8_char;
	unsigned int width;
	unsigned int height;
	unsigned char *data;
	GRFont *p_gr_font;
} pgm_font;

typedef struct {
	char c;
	char str[15];
	int strlen;
} special_char_map;

//FIXME: need to be more flexible. 
static const char TAR_PATH[] = "/res/images/recovery_fonts.tar"; 
static int tar_len = -1;
static char *tar;
static int pgm_len = 200;
static pgm_font g_pgm_font[200];

//FIXME: we assume the U+FFFF(as CJK) is about twice wider. 
#define WIDE_CHAR_IDENTIFY 	(2)
#define WIDE_CHAR_FACTOR	(1.7)	

#define TAR_DIR		"recovery_fonts"
#define TAR_DIR_LEN	(14)
#define TAR_FN_OFFSET	(15)


static special_char_map g_special_char[] = {
	{'/', "slash", 5},
	{'*', "asterisk", 8},
	{'?', "question", 8},
	{0, "", 0}
};

void utf8_init_font(void)
{
	FILE *fp;

	memset(g_pgm_font, 0, sizeof(g_pgm_font));

	fp = fopen(TAR_PATH, "r");
	
	fseek(fp, 0, SEEK_END); 
	tar_len = ftell(fp); 
	fseek(fp, 0, SEEK_SET); 

	tar = malloc(tar_len);
	fread(tar, tar_len, 1, fp);
	fclose(fp);
}

//return uc and length.
static unsigned long long utf8_get_ch(char *p, int *len)
{
	char t = 0x80;
	int i, j;
	unsigned long long uc = 0;

	for(i=0; i<8; i++){
		if((*p & t) == 0){
			break;
		}
		t = t>>1; 	
	}

	if(i == 0){
		*len = 1;
		uc = p[0];
	}else if(i > 6){ //FIXME: error handle.
		*len = 1;
		uc = '?';
	}else{
		*len = i; //i is length.
		for(j=0;j<i;j++){
			uc = (uc << 8) + p[j];
		}
	}

	return uc;
}

//calculate the byte num of truncated char_num.
//char_num is length of (string with '\0').
//return truncated byte-length of (utf8 string with '\0').
int utf8_bytes_constraint(char *p, int char_num)
{
	int l = 0, total = 0;
	float width = 0.0;

	while(*p != '\0'){
		if(l){ //valid utf8.
			if((p[0] & 0xC0) != 0x80){
				break;
			}
		}else{
			utf8_get_ch(p, &l);

			width += (l > WIDE_CHAR_IDENTIFY)?WIDE_CHAR_FACTOR:1;
			if(width > char_num - 1){
				break;
			}

			total += l;
		}

		p++;
		l--;
	}

	return total + 1;
}


//return status.
//1, is special
//0, is not special
static inline int get_special_char_map(char c, special_char_map **call)
{
	int i;

	for(i=0; g_special_char[i].c!=0; i++){
		if(c == g_special_char[i].c){
			*call = &g_special_char[i];
			return 1;
		}
	}
	
	return 0;
}

//return pointer of pgm.
static char* lookup_tar(unsigned long long inuc)
{
	int tu = 512; //tar unit size.
	char *p = tar; 
	char *p_max = tar + tar_len - tu;
	int i;
	
	special_char_map *scm = NULL;
	int is_special = 0;
	
	unsigned long long uc = 0;
	int uc_len;

	is_special = get_special_char_map(inuc, &scm);

	for(i=0; p<p_max; i++, p+=tu){
		if(strncmp(p, TAR_DIR, TAR_DIR_LEN)){
			continue;
		}else if(p[TAR_FN_OFFSET] == 0){ //skip dir itself.
			continue;
		}
		
		if(is_special){
			if(0 == strncmp(&p[TAR_FN_OFFSET], scm->str, scm->strlen)){
				return p + tu;
			}
		}else{
			uc = utf8_get_ch(&p[TAR_FN_OFFSET], &uc_len);
			if((uc == inuc) && (p[TAR_FN_OFFSET + uc_len] == '.')){
				return p + tu;
			}
		}
	}

	return NULL;
}

//return status.
//0000400: 5035 0a31 3420 3332 0a32 3535 0aff ffff  P5.14 32.255....
static int pgm_get_data(pgm_font *pf, char* buf)
{
	char *p = buf;
	int i, k=0;
	int v = 0;
	
	//we assume the max string is 32 bytes.
	int const max = 32;

	for(i=0; i<max; i++){
		if(p[i] == '\n' || p[i] == ' '){
			k++;
			if((k == 1) && (v != 325)){
				//check magic of P5.
				return -1;
			}
			if(k == 2)
				pf->width = v;
			if(k == 3)
				pf->height = v;
			if(k == 4){
				pf->data = &p[i + 1];
				break;
			}
			v = 0;
			continue;
		}

		v = v* 10 + p[i] - '0';
	}

	if(i >= max){
		return -2;
	}

	return 0;
}

static void utf8_make_gr_font(int index)
{
	GGLSurface *ftex;
	GRFont *gr_font;
	
	gr_font = calloc(sizeof(*gr_font), 1);
	ftex = &gr_font->texture;
	
	//many gr_font.
	g_pgm_font[index].p_gr_font = gr_font;

	ftex->version = sizeof(*ftex);
	ftex->width = g_pgm_font[index].width;
	ftex->height = g_pgm_font[index].height;
	ftex->stride = g_pgm_font[index].width;
	ftex->data = g_pgm_font[index].data;
	ftex->format = GGL_PIXEL_FORMAT_A_8;

	gr_font->cwidth = g_pgm_font[index].width;
	gr_font->cheight = g_pgm_font[index].height;
	gr_font->ascent = g_pgm_font[index].height - 2;
}

//return g_pgm_font index.
static int utf8_lookup_char(unsigned long long inuc)
{
	int i; 
	int current_index;
	
	char *p = NULL;
	pgm_font *pf;

	//look for existed items.
	for(i=0; i<pgm_len; i++){
		if(g_pgm_font[i].utf8_char == 0x0){
			break; //no more data.
		}

		if(g_pgm_font[i].utf8_char == inuc){
			return i; //found.
		}
	}
	
	if(i>=pgm_len){
		LOGE("g_pgm_font is full. in=0x%llx\n", inuc);
		return 0;
	}

	current_index = i;

	p = lookup_tar(inuc);
	if(p == NULL){
		LOGE("not find resource of %llx\n", inuc);
		return 0;
	}else{
		pf = malloc(sizeof(pgm_font));
		i = pgm_get_data(pf, p);
		if(i){
			return 0;
		}

		g_pgm_font[current_index].utf8_char = inuc;
		g_pgm_font[current_index].width = pf->width;
		g_pgm_font[current_index].height = pf->height;
		g_pgm_font[current_index].data = pf->data;

		utf8_make_gr_font(current_index);
	}

	//NOTE: return 0 to make sure show a '?' as an error mark.
	return current_index;
}

int utf8_text(int x, int y, const char *s)
{
	GGLContext *gl = gr_context;
	GRFont *font;
	unsigned off;
	unsigned long long uc;
	int index;
	int minus_ascent = 1;
	int uc_len = 1;

	//we always put '?' at place 0, as an error mark.
	static int error_mark_init = 0;
	if(error_mark_init == 0){ 
		LOGI("setup an error mark.\n");
		utf8_lookup_char('?');
		error_mark_init = 1;
	}

	while((off = *s)) {
		uc = utf8_get_ch((char*)s, &uc_len);
		//printf("text s 0x%llx, %c ", uc, off);
		s += uc_len;

		index = utf8_lookup_char(uc);
		font = g_pgm_font[index].p_gr_font;
		if(minus_ascent){
			y -= font->ascent;
			minus_ascent = 0;
		}

		gl->bindTexture(gl, &font->texture);
		gl->texEnvi(gl, GGL_TEXTURE_ENV, GGL_TEXTURE_ENV_MODE, GGL_REPLACE);
		gl->texGeni(gl, GGL_S, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
		gl->texGeni(gl, GGL_T, GGL_TEXTURE_GEN_MODE, GGL_ONE_TO_ONE);
		gl->enable(gl, GGL_TEXTURE_2D);
			
		//printf("at %d, %d\n", x, y);
		gl->texCoord2i(gl, -x, -y); //TODO
		gl->recti(gl, x, y, x + font->cwidth, y + font->cheight);
		x += font->cwidth;
	}

	return x;
}


