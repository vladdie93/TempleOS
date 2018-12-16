/*
	Author:	Alan Cai
	Github:	flowac
	Email:	alan@ld50.bid
	License:Public Domain

	Description:	Light weight version of terry's RedSea file unzip program.
			Recursively decompress all files in a given path.
			Audio, image, and video decompress removed.

	Note:		To convert tabs to spaces, run:	expand -t2 FILE > TEMP
			To convert back, run:		unexpand -t2 TEMP > FILE
			Please only commit this file in tabbed form.
*/

#include <dirent.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#pragma pack(1)

#define TRUE		1
#define FALSE		0
#define BYTE_MAX	0xFF
#define CHAR_MIN	(-0x80)
#define CHAR_MAX	0x7F
#define SHORT_MIN	(-0x8000)
#define SHORT_MAX	0x7FFF
#define INT_MIN		(-0x80000000)
#define INT_MAX		0x7FFFFFFF
#define LONG_LONG_MIN		(-0x8000000000000000l)
#define LONG_LONG_MAX		0x7FFFFFFFFFFFFFFFl
#define DWORD_MIN		0
#define DWORD_MAX		0xFFFFFFFF

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned char BOOL;

void *CAlloc(DWORD size)
{
	BYTE *res=(BYTE *)malloc(size);
	memset(res,0,size);
	return res;
}

void Free(void *ptr)
{
	if (ptr) free(ptr);
}

int FSize(FILE *f)
{
	int res,original=ftell(f);
	fseek(f,0,SEEK_END);
	res=ftell(f);
	fseek(f,original,SEEK_SET);
	return res;
}

#define ARC_MAX_BITS 12

#define CT_NONE		1
#define CT_7_BIT	2
#define CT_8_BIT	3

class CArcEntry
{
public:
	CArcEntry *next;
	WORD basecode;
	BYTE ch,pad;
};

class CArcCtrl //control structure
{
public:
	DWORD src_pos,src_size,
	dst_pos,dst_size;
	BYTE *src_buf,*dst_buf;
	DWORD min_bits,min_table_entry;
	CArcEntry *cur_entry,*next_entry;
	DWORD cur_bits_in_use,next_bits_in_use;
	BYTE *stk_ptr,*stk_base;
	DWORD free_idx,free_limit,
	saved_basecode,
	entry_used,
	last_ch;
	CArcEntry compress[1<<ARC_MAX_BITS],
	*hash[1<<ARC_MAX_BITS];
};

class CArcCompress
{
public:
	DWORD compressed_size,compressed_size_hi,
	expanded_size,expanded_size_hi;
	BYTE	compression_type;
	BYTE body[1];
};

int Bt(int bit_num, BYTE *bit_field)
{
	bit_field+=bit_num>>3;
	bit_num&=7;
	return (*bit_field & (1<<bit_num)) ? 1:0;
}

int Bts(int bit_num, BYTE *bit_field)
{
	int res;
	bit_field+=bit_num>>3;
	bit_num&=7;
	res=*bit_field & (1<<bit_num);
	*bit_field|=(1<<bit_num);
	return (res) ? 1:0;
}

DWORD BFieldExtDWORD(BYTE *src,DWORD pos,DWORD bits)
{
	DWORD i,res=0;
	for (i=0;i<bits;i++)
		if (Bt(pos+i,src))
			Bts(i,(BYTE *)&res);
	return res;
}

void ArcEntryGet(CArcCtrl *c)
{
	DWORD i;
	CArcEntry *tmp,*tmp1;

	if (!c->entry_used) return;

	i=c->free_idx;
	c->entry_used=FALSE;
	c->cur_entry=c->next_entry;
	c->cur_bits_in_use=c->next_bits_in_use;
	if (c->next_bits_in_use<ARC_MAX_BITS) {
		c->next_entry = &c->compress[i++];
		if (i==c->free_limit) {
			c->next_bits_in_use++;
			c->free_limit=1<<c->next_bits_in_use;
		}
	} else {
		do if (++i==c->free_limit) i=c->min_table_entry;
		while (c->hash[i]);
		tmp=&c->compress[i];
		c->next_entry=tmp;
		tmp1=(CArcEntry *)&c->hash[tmp->basecode];
		while (tmp1 && tmp1->next!=tmp)
			tmp1=tmp1->next;
		if (tmp1)
			tmp1->next=tmp->next;
	}
	c->free_idx=i;
}

void ArcExpandBuf(CArcCtrl *c)
{
	BYTE *dst_ptr,*dst_limit;
	DWORD basecode,lastcode,code;
	CArcEntry *tmp,*tmp1;

	dst_ptr=c->dst_buf+c->dst_pos;
	dst_limit=c->dst_buf+c->dst_size;

	while (dst_ptr<dst_limit && c->stk_ptr!=c->stk_base)
		*dst_ptr++ = * -- c->stk_ptr;

	if (c->stk_ptr==c->stk_base && dst_ptr<dst_limit) {
		if (c->saved_basecode==0xFFFFFFFFl) {
			lastcode=BFieldExtDWORD(c->src_buf,c->src_pos,
			c->next_bits_in_use);
			c->src_pos=c->src_pos+c->next_bits_in_use;
			*dst_ptr++=lastcode;
			ArcEntryGet(c);
			c->last_ch=lastcode;
		} else
			lastcode=c->saved_basecode;
		while (dst_ptr<dst_limit && c->src_pos+c->next_bits_in_use<=c->src_size) {
			basecode=BFieldExtDWORD(c->src_buf,c->src_pos,
			c->next_bits_in_use);
			c->src_pos=c->src_pos+c->next_bits_in_use;
			if (c->cur_entry==&c->compress[basecode]) {
				*c->stk_ptr++=c->last_ch;
				code=lastcode;
			} else
				code=basecode;
			while (code>=c->min_table_entry) {
				*c->stk_ptr++=c->compress[code].ch;
				code=c->compress[code].basecode;
			}
			*c->stk_ptr++=code;
			c->last_ch=code;

			c->entry_used=TRUE;
			tmp=c->cur_entry;
			tmp->basecode=lastcode;
			tmp->ch=c->last_ch;
			tmp1=(CArcEntry *)&c->hash[lastcode];
			tmp->next=tmp1->next;
			tmp1->next=tmp;

			ArcEntryGet(c);
			while (dst_ptr<dst_limit && c->stk_ptr!=c->stk_base)
				*dst_ptr++ = * -- c->stk_ptr;
			lastcode=basecode;
		}
		c->saved_basecode=lastcode;
	}
	c->dst_pos=dst_ptr-c->dst_buf;
}

CArcCtrl *ArcCtrlNew(DWORD expand,DWORD compression_type)
{
	CArcCtrl *c;
	c=(CArcCtrl *)CAlloc(sizeof(CArcCtrl));
	if (expand) {
		c->stk_base=(BYTE *)malloc(1<<ARC_MAX_BITS);
		c->stk_ptr=c->stk_base;
	}
	if (compression_type==CT_7_BIT)
		c->min_bits=7;
	else
		c->min_bits=8;
	c->min_table_entry=1<<c->min_bits;
	c->free_idx=c->min_table_entry;
	c->next_bits_in_use=c->min_bits+1;
	c->free_limit=1<<c->next_bits_in_use;
	c->saved_basecode=0xFFFFFFFFl;
	c->entry_used=TRUE;
	ArcEntryGet(c);
	c->entry_used=TRUE;
	return c;
}

void ArcCtrlDel(CArcCtrl *c)
{
	Free(c->stk_base);
	Free(c);
}

BYTE *ExpandBuf(CArcCompress *arc)
{
	CArcCtrl *c;
	BYTE *res;

	if (!(CT_NONE<=arc->compression_type && arc->compression_type<=CT_8_BIT) ||
	arc->expanded_size>=0x20000000l)
		return NULL;

	res=(BYTE *)malloc(arc->expanded_size+1);
	res[arc->expanded_size]=0; //terminate
	switch (arc->compression_type) {
		case CT_NONE:
			memcpy(res,arc->body,arc->expanded_size);
			break;
		case CT_7_BIT:
		case CT_8_BIT:
			c=ArcCtrlNew(TRUE,arc->compression_type);
			c->src_size=arc->compressed_size*8;
			c->src_pos=(sizeof(CArcCompress)-1)*8;
			c->src_buf=(BYTE *)arc;
			c->dst_size=arc->expanded_size;
			c->dst_buf=res;
			c->dst_pos=0;
			ArcExpandBuf(c);
			ArcCtrlDel(c);
			break;
	}
	return res;
}

BOOL Cvt(char *in_name,char *out_name,BOOL cvt_ascii)
{
	DWORD out_size,i,j,in_size;
	CArcCompress *arc;
	BYTE *out_buf;
	FILE *io_file;
	BOOL okay=FALSE;
	if (io_file=fopen(in_name,"rb")) {
		in_size=FSize(io_file);
		arc=(CArcCompress *)malloc(in_size);
		fread(arc,1,in_size,io_file);
		out_size=arc->expanded_size;
		printf("%-45s %d-->%d\r\n",in_name,(DWORD) in_size,out_size);
		fclose(io_file);
		if (arc->compressed_size==in_size &&
		arc->compression_type && arc->compression_type<=3) {
			if (out_buf=ExpandBuf(arc)) {
				if (cvt_ascii) {
					j=0;
					for (i=0;i<out_size;i++)
						if (out_buf[i]==31)
							out_buf[j++]=32;
						else if (out_buf[i]!=5)
							out_buf[j++]=out_buf[i];
					out_size=j;
				}
				if (io_file=fopen(out_name,"wb")) {
					fwrite(out_buf,1,out_size,io_file);
					fclose(io_file);
					okay=TRUE;
				}
				Free(out_buf);
			}
		}
		Free(arc);
	}
	return okay;
}

//Recursively executes the Cvt command
//Assumes compressed files end with .Z
//Args: input_path, input prefix, output prefix
int Cvt_r(const char *in0, const char *in_pfx, const char *out_pfx)
{
	char in1[256], out[256];
	DIR *in2;
	struct dirent *in3;
	int ret = 0;

	if (strlen(in0) > 0) {
		sprintf(in1, "%s/%s", in_pfx, in0);
		sprintf(out, "%s/%s", out_pfx, in0);
	} else {
		strcpy(in1, in_pfx);
		strcpy(out, out_pfx);
	}
	if (strstr(in0, ".Z")) {
		out[strstr(out, ".Z")-out] = 0;
		//printf("    %s\r\n", in1);
		ret += Cvt(in1, out, TRUE);
	} else {
		in2 = opendir(in1);
		if (!in2) return 0;
		while (in3 = readdir(in2)) {
			if (!strcmp(in3->d_name, ".") ||
			!strcmp(in3->d_name, "..")) continue;

			mkdir(out, 0755);
			ret += Cvt_r(in3->d_name, in1, out);
		}
	}
	return ret;
}

int main(int argc, char* argv[])
{
	char in_name[256], out_name[256];
	int loc;

	if (argc < 2) {
		printf(	"%s in_path [out_path]\r\n"
			"Expands a TempleOS directory.\r\n", argv[0]);
		return -1;
	}

	strcpy(in_name, argv[1]);
	loc = strlen(in_name) - 1;
	if (in_name[loc] == '/')
		in_name[loc] = 0;

	if (argc == 3)
		strcpy(out_name, argv[2]);
	else
		sprintf(out_name, "%s_unzipped", in_name);
	loc = strlen(out_name) - 1;
	if (out_name[loc] == '/')
		out_name[loc] = 0;

	if (mkdir(out_name, 0755) != 0)
		printf("Create output directory maybe failed\r\n");
	if ((loc = Cvt_r("", in_name, out_name)) == 0)
		printf("Fail: %s %s\r\n", in_name, out_name);
	else
		printf("%d files converted", loc);

	return 0;
}

