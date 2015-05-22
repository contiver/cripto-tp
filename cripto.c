#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BMP_HEADER_SIZE        14
#define DIB_HEADER_SIZE_OFFSET 14
#define FILESIZE_OFFSET        2	
#define WIDTH_OFFSET           18
#define HEIGHT_OFFSET          22

typedef enum {
	BITMAPCOREHEADER   = 12,
	OS21XBITMAPHEADER  = 12,
	OS22XBITMAPHEADER  = 64,
	BITMAPINFOHEADER   = 40,
	BITMAPV2INFOHEADER = 52,
	BITMAPV3INFOHEADER = 56,
	BITMAPV4HEADER     = 108,
	BITMAPV5HEADER     = 124,
} DIBheadersize;

#pragma pack(1)
typedef struct {
	uint8_t id[2];    /* magic number to identify the BMP format */
	uint32_t size;    /* size of the BMP file in bytes */
	uint16_t unused1; /* reserved */
	uint16_t unused2; /* reserved */
	uint32_t offset;  /* starting address of the pixel array (bitmap data) */
} BMPheader;
#pragma pack()

/* 40 bytes BITMAPINFOHEADER */
typedef struct {
  uint32_t size;        /* the size of this header (40 bytes) */
  uint32_t width;       /* the bitmap width in pixels */
  uint32_t height;      /* the bitmap height in pixels */
  uint16_t nplanes;     /* number of color planes used; Must set to 1 */
  uint16_t depth;       /* bpp number. Usually: 1, 4, 8, 16, 24 or 32 */
  uint32_t compression; /* compression method used */
  uint32_t rawbmpsize;  /* size of the raw bitmap (pixel) data */
  uint32_t hres;        /* horizontal resolution (pixel per meter) */
  uint32_t vres;        /* vertical resolution (pixel per meter) */
  uint32_t ncolors;     /* number of colors in the palette. 0 means 2^n */
  uint32_t nimpcolors;  /* number of important colors used, usually ignored */
} DIBheader;

typedef struct {
	BMPheader bmpheader; /* 14 bytes bmp starting header */
	DIBheader dibheader; /* 40 bytes dib header */
	uint8_t *palette;    /* color palette */
	uint8_t *data;       /* image pixels */
} Bitmap;

/* prototypes */ 
static void     randomize(int num);
static double   randnormalize(void);
static long     randint(long int max);
static uint32_t bmpfilesize(FILE *fp);
static uint32_t bmpfilewidth(FILE *fp);
static uint32_t bmpfileheight(FILE *fp);
static uint32_t get32bitsfromheader(FILE *fp, char offset);
static void     freebitmap(Bitmap *bp);
static void     die(const char *errstr, ...);
static void     *xmalloc(size_t size);
static FILE     *xfopen(const char *filename, const char *mode);
static void     xfclose(FILE *fp);
static void     bmptofile(Bitmap *bp, const char *filename);

void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void *
xmalloc(size_t size) {
	void *p = malloc(size);

	if(!p)
		die("Out of memory: couldn't malloc %d bytes\n", size);

	return p;
}

FILE *
xfopen (const char *filename, const char *mode){
	FILE *fp = fopen(filename, mode);

	if(!fp) 
		die("couldn't open %s", filename);

	return fp;
}

void
xfclose (FILE *fp){
	if(fclose(fp) == EOF)
		die("couldn't close file\n");
}

void 
randomize(int num){
	srand((int) num);
}

double 
randnormalize(void){
	return rand()/((double)RAND_MAX+1);
}

long
randint(long int max){
	return (long) (randnormalize()*(max+1)); /*returns num in [0,max]*/
} 

uint32_t
get32bitsfromheader(FILE *fp, char offset){
	uint32_t value;
	uint32_t pos = ftell(fp);

	fseek(fp, offset, SEEK_SET);
	fread(&value, 1, 4, fp);
	fseek(fp, pos, SEEK_SET);

	return value;
}

uint32_t
bmpfilewidth(FILE *fp){
	return get32bitsfromheader(fp, WIDTH_OFFSET);
}

uint32_t
bmpfileheight(FILE *fp){
	return get32bitsfromheader(fp, HEIGHT_OFFSET);
}

uint32_t
bmpfilesize(FILE *fp){
	return get32bitsfromheader(fp, FILESIZE_OFFSET);
}

uint32_t
bmpfiledibheadersize(FILE *fp){
	uint32_t size = get32bitsfromheader(fp, DIB_HEADER_SIZE_OFFSET);
	if(size != BITMAPINFOHEADER)
		die("unsupported dib header format\n");
	return size;
}


void
freebitmap(Bitmap *bp){
	free(bp->palette);
	free(bp->data);
	free(bp);
}

void
bmpheaderdebug(Bitmap *bp){
	printf("ID: %c%-15c size: %-16d offset: %-16d\n", bp->bmpheader.id[0],
	bp->bmpheader.id[1], bp->bmpheader.size, bp->bmpheader.offset);
}

void
dibheaderdebug(Bitmap *bp){
	printf("dibsize: %-16d width: %-16d height: %-16d\n" 
			"nplanes: %-16d depth: %-16d compression:%-16d\n"
			"rawbmpsize: %-16d hres: %-16d vres:%-16d\n"
			"ncolors: %-16d nimpcolors: %-16d\n", bp->dibheader.size,
			bp->dibheader.width, bp->dibheader.height, bp->dibheader.nplanes,
			bp->dibheader.depth, bp->dibheader.compression,
			bp->dibheader.rawbmpsize, bp->dibheader.hres, bp->dibheader.vres,
			bp->dibheader.ncolors, bp->dibheader.nimpcolors);
}

Bitmap *
bmpfromfile(char *filename){
	FILE *fp = xfopen(filename, "r");
	Bitmap *bp = xmalloc(sizeof(*bp));

	/* read BMP header */
	int i = 0;
	while(i < BMP_HEADER_SIZE)
		i += fread(&(bp->bmpheader) + i, 1, BMP_HEADER_SIZE, fp);

	/* read DIB header */
	int dibheadersize = bmpfiledibheadersize(fp);
	i = 0;
	while(i < dibheadersize)
		i += fread(&bp->dibheader + i, 1, dibheadersize, fp);

	bmpheaderdebug(bp);
	dibheaderdebug(bp);

	/* read color palette */
	int palettesize = bp->bmpheader.offset - (BMP_HEADER_SIZE + bp->dibheader.size);
	bp->palette = xmalloc(palettesize);
	i = 0;
	while(i < palettesize)
		i += fread(bp->palette + i, 1, palettesize, fp);

	/* read pixel data */
	int imagesize = bp->bmpheader.size - bp->bmpheader.offset;
	bp->data = xmalloc(imagesize);
	i = 0;
	while(i < imagesize)
		i += fread(bp->data + i, 1, imagesize, fp);

	xfclose(fp);
	return bp;
}

void
bmptofile(Bitmap *bp, const char *filename){
	FILE *fp = xfopen(filename, "w");
	int palettesize = bp->bmpheader.offset - (BMP_HEADER_SIZE + bp->dibheader.size);
	int imagesize = bp->bmpheader.size - bp->bmpheader.offset;

	fwrite(&bp->bmpheader, sizeof(bp->bmpheader), 1, fp);
	fwrite(&bp->dibheader, sizeof(bp->dibheader), 1, fp);
	fwrite(bp->palette, palettesize, 1, fp);
	fwrite(bp->data, imagesize, 1, fp);
	xfclose(fp);
}

void
changebmppixel(FILE *fp, int pixel, char bit){
	uint8_t byte;

	// TODO
	//fseek(fp, HEIGHT_OFFSET, SEEK_SET);
	fread(&byte, 1, 1, fp);
	byte &= bit;
	fseek(fp, -1, SEEK_CUR);
	fwrite(&byte, 1, 1, fp);
}

int
main(int argc, char *argv[]){
	char *filename;
	char *arg;
	FILE *fp;

	/*
	   if(strcmp("-secret", arg) == 0){
	   }
	   */

	filename = argv[1];
	Bitmap *bp = bmpfromfile(filename);
	bmptofile(bp, "output.bmp");
	freebitmap(bp);
}
