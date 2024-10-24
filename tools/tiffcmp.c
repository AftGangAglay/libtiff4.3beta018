/* $Header: /usr/people/sam/tiff/tools/RCS/tiffcmp.c,v 1.27 1995/07/19 00:39:51 sam Exp $ */

/*
 * Copyright (c) 1988-1995 Sam Leffler
 * Copyright (c) 1991-1995 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and 
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Sam Leffler and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Sam Leffler and Silicon Graphics.
 * 
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
 * 
 * IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF 
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE 
 * OF THIS SOFTWARE.
 */

#if defined(unix) || defined(__unix)
#include "port.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "tiffio.h"

static	int stopondiff = 1;
static	int stoponfirsttag = 1;
static	uint16 bitspersample = 1;
static	uint16 samplesperpixel = 1;
static	uint32 imagewidth;
static	uint32 imagelength;

static	int tiffcmp(TIFF*, TIFF*);
static	int cmptags(TIFF*, TIFF*);
static	void ContigCompare(int, uint32, unsigned char*, unsigned char*, int);
static	void PrintDiff(uint32, int, uint32, int, int);
static	void SeparateCompare(int, int, uint32, unsigned char*, unsigned char*);
static	void eof(const char*, uint32, int);

static void
usage(void)
{
	fprintf(stderr, "Usage: tiffcmp [-l] [-t] [-z] file1 file2\n");
	exit(-3);
}

int
main(int argc, char* argv[])
{
	TIFF *tif1, *tif2;
	int c, dirnum;
	extern int optind;

	while ((c = getopt(argc, argv, "ltz")) != -1)
		switch (c) {
		case 'l':
			stopondiff = 0;
			break;
		case 'z':
			stopondiff += 100;
			break;
		case 't':
			stoponfirsttag = 0;
			break;
		case '?':
			usage();
			/*NOTREACHED*/
		}
	if (argc - optind < 2)
		usage();
	tif1 = TIFFOpen(argv[optind], "r");
	if (tif1 == NULL)
		return (-1);
	tif2 = TIFFOpen(argv[optind+1], "r");
	if (tif2 == NULL)
		return (-2);
	dirnum = 0;
	while (tiffcmp(tif1, tif2)) {
		if (!TIFFReadDirectory(tif1)) {
			if (!TIFFReadDirectory(tif2))
				break;
			printf("No more directories for %s\n",
			    TIFFFileName(tif1));
			return (1);
		} else if (!TIFFReadDirectory(tif2)) {
			printf("No more directories for %s\n",
			    TIFFFileName(tif2));
			return (1);
		}
		printf("Directory %d:\n", ++dirnum);
	}
	return (0);
}

#define	checkEOF(tif, row, sample) { \
	eof(TIFFFileName(tif), row, sample); \
	goto bad; \
}

static	int CheckShortTag(TIFF*, TIFF*, int, char*);
static	int CheckShort2Tag(TIFF*, TIFF*, int, char*);
static	int CheckShortArrayTag(TIFF*, TIFF*, int, char*);
static	int CheckLongTag(TIFF*, TIFF*, int, char*);
static	int CheckFloatTag(TIFF*, TIFF*, int, char*);
static	int CheckStringTag(TIFF*, TIFF*, int, char*);

static int
tiffcmp(TIFF* tif1, TIFF* tif2)
{
	uint16 config1, config2;
	tsize_t size1;
	uint32 s, row;
	unsigned char *buf1, *buf2;

	if (!CheckShortTag(tif1, tif2, TIFFTAG_BITSPERSAMPLE, "BitsPerSample"))
		return (0);
	if (!CheckShortTag(tif1, tif2, TIFFTAG_SAMPLESPERPIXEL, "SamplesPerPixel"))
		return (0);
	if (!CheckLongTag(tif1, tif2, TIFFTAG_IMAGEWIDTH, "ImageWidth"))
		return (0);
	if (!cmptags(tif1, tif2))
		return (1);
	(void) TIFFGetField(tif1, TIFFTAG_BITSPERSAMPLE, &bitspersample);
	(void) TIFFGetField(tif1, TIFFTAG_SAMPLESPERPIXEL, &samplesperpixel);
	(void) TIFFGetField(tif1, TIFFTAG_IMAGEWIDTH, &imagewidth);
	(void) TIFFGetField(tif1, TIFFTAG_IMAGELENGTH, &imagelength);
	(void) TIFFGetField(tif1, TIFFTAG_PLANARCONFIG, &config1);
	(void) TIFFGetField(tif2, TIFFTAG_PLANARCONFIG, &config2);
	buf1 = (unsigned char *)_TIFFmalloc(size1 = TIFFScanlineSize(tif1));
	buf2 = (unsigned char *)_TIFFmalloc(TIFFScanlineSize(tif2));
	if (buf1 == NULL || buf2 == NULL) {
		fprintf(stderr, "No space for scanline buffers\n");
		exit(-1);
	}
	if (config1 != config2 && bitspersample != 8 && samplesperpixel > 1) {
		fprintf(stderr,
"Can't handle different planar configuration w/ different bits/sample\n");
		goto bad;
	}
#define	pack(a,b)	((a)<<8)|(b)
	switch (pack(config1, config2)) {
	case pack(PLANARCONFIG_SEPARATE, PLANARCONFIG_CONTIG):
		for (row = 0; row < imagelength; row++) {
			if (TIFFReadScanline(tif2, buf2, row, 0) < 0)
				checkEOF(tif2, row, -1)
			for (s = 0; s < samplesperpixel; s++) {
				if (TIFFReadScanline(tif1, buf1, row, s) < 0)
					checkEOF(tif1, row, s)
				SeparateCompare(1, s, row, buf2, buf1);
			}
		}
		break;
	case pack(PLANARCONFIG_CONTIG, PLANARCONFIG_SEPARATE):
		for (row = 0; row < imagelength; row++) {
			if (TIFFReadScanline(tif1, buf1, row, 0) < 0)
				checkEOF(tif1, row, -1)
			for (s = 0; s < samplesperpixel; s++) {
				if (TIFFReadScanline(tif2, buf2, row, s) < 0)
					checkEOF(tif2, row, s)
				SeparateCompare(0, s, row, buf1, buf2);
			}
		}
		break;
	case pack(PLANARCONFIG_SEPARATE, PLANARCONFIG_SEPARATE):
		for (s = 0; s < samplesperpixel; s++)
			for (row = 0; row < imagelength; row++) {
				if (TIFFReadScanline(tif1, buf1, row, s) < 0)
					checkEOF(tif1, row, s)
				if (TIFFReadScanline(tif2, buf2, row, s) < 0)
					checkEOF(tif2, row, s)
				ContigCompare(s, row, buf1, buf2, size1);
			}
		break;
	case pack(PLANARCONFIG_CONTIG, PLANARCONFIG_CONTIG):
		for (row = 0; row < imagelength; row++) {
			if (TIFFReadScanline(tif1, buf1, row, 0) < 0)
				checkEOF(tif1, row, -1)
			if (TIFFReadScanline(tif2, buf2, row, 0) < 0)
				checkEOF(tif2, row, -1)
			ContigCompare(-1, row, buf1, buf2, size1);
		}
		break;
	}
	if (buf1) _TIFFfree(buf1);
	if (buf2) _TIFFfree(buf2);
	return (1);
bad:
	if (stopondiff)
		exit(1);
	if (buf1) _TIFFfree(buf1);
	if (buf2) _TIFFfree(buf2);
	return (0);
}

#define	CmpShortField(tag, name) \
	if (!CheckShortTag(tif1, tif2, tag, name) && stoponfirsttag) return (0)
#define	CmpShortField2(tag, name) \
	if (!CheckShort2Tag(tif1, tif2, tag, name) && stoponfirsttag) return (0)
#define	CmpLongField(tag, name) \
	if (!CheckLongTag(tif1, tif2, tag, name) && stoponfirsttag) return (0)
#define	CmpFloatField(tag, name) \
	if (!CheckFloatTag(tif1, tif2, tag, name) && stoponfirsttag) return (0)
#define	CmpStringField(tag, name) \
	if (!CheckStringTag(tif1, tif2, tag, name) && stoponfirsttag) return (0)
#define	CmpShortArrayField(tag, name) \
	if (!CheckShortArrayTag(tif1, tif2, tag, name) && stoponfirsttag) return (0)

static int
cmptags(TIFF* tif1, TIFF* tif2)
{
	CmpLongField(TIFFTAG_SUBFILETYPE,	"SubFileType");
	CmpLongField(TIFFTAG_IMAGEWIDTH,	"ImageWidth");
	CmpLongField(TIFFTAG_IMAGELENGTH,	"ImageLength");
	CmpShortField(TIFFTAG_BITSPERSAMPLE,	"BitsPerSample");
	CmpShortField(TIFFTAG_COMPRESSION,	"Compression");
	CmpShortField(TIFFTAG_PREDICTOR,	"Predictor");
	CmpShortField(TIFFTAG_PHOTOMETRIC,	"PhotometricInterpretation");
	CmpShortField(TIFFTAG_THRESHHOLDING,	"Thresholding");
	CmpShortField(TIFFTAG_FILLORDER,	"FillOrder");
	CmpShortField(TIFFTAG_ORIENTATION,	"Orientation");
	CmpShortField(TIFFTAG_SAMPLESPERPIXEL,	"SamplesPerPixel");
	CmpShortField(TIFFTAG_MINSAMPLEVALUE,	"MinSampleValue");
	CmpShortField(TIFFTAG_MAXSAMPLEVALUE,	"MaxSampleValue");
	CmpFloatField(TIFFTAG_XRESOLUTION,	"XResolution");
	CmpFloatField(TIFFTAG_YRESOLUTION,	"YResolution");
	CmpLongField(TIFFTAG_GROUP3OPTIONS,	"Group3Options");
	CmpLongField(TIFFTAG_GROUP4OPTIONS,	"Group4Options");
	CmpShortField(TIFFTAG_RESOLUTIONUNIT,	"ResolutionUnit");
	CmpShortField(TIFFTAG_PLANARCONFIG,	"PlanarConfiguration");
	CmpLongField(TIFFTAG_ROWSPERSTRIP,	"RowsPerStrip");
	CmpFloatField(TIFFTAG_XPOSITION,	"XPosition");
	CmpFloatField(TIFFTAG_YPOSITION,	"YPosition");
	CmpShortField(TIFFTAG_GRAYRESPONSEUNIT, "GrayResponseUnit");
	CmpShortField(TIFFTAG_COLORRESPONSEUNIT, "ColorResponseUnit");
#ifdef notdef
	{ uint16 *graycurve;
	  CmpField(TIFFTAG_GRAYRESPONSECURVE, graycurve);
	}
	{ uint16 *red, *green, *blue;
	  CmpField3(TIFFTAG_COLORRESPONSECURVE, red, green, blue);
	}
	{ uint16 *red, *green, *blue;
	  CmpField3(TIFFTAG_COLORMAP, red, green, blue);
	}
#endif
	CmpShortField2(TIFFTAG_PAGENUMBER,	"PageNumber");
	CmpStringField(TIFFTAG_ARTIST,		"Artist");
	CmpStringField(TIFFTAG_IMAGEDESCRIPTION,"ImageDescription");
	CmpStringField(TIFFTAG_MAKE,		"Make");
	CmpStringField(TIFFTAG_MODEL,		"Model");
	CmpStringField(TIFFTAG_SOFTWARE,	"Software");
	CmpStringField(TIFFTAG_DATETIME,	"DateTime");
	CmpStringField(TIFFTAG_HOSTCOMPUTER,	"HostComputer");
	CmpStringField(TIFFTAG_PAGENAME,	"PageName");
	CmpStringField(TIFFTAG_DOCUMENTNAME,	"DocumentName");
	CmpShortField(TIFFTAG_MATTEING,		"Matteing");
	CmpShortArrayField(TIFFTAG_EXTRASAMPLES,"ExtraSamples");
	return (1);
}

static void
ContigCompare(int sample, uint32 row, unsigned char* p1, unsigned char* p2, int size)
{
	register uint32 pix;
	register int ppb = 8/bitspersample;

	if (memcmp(p1, p2, size) == 0)
		return;
	switch (bitspersample) {
	case 1: case 2: case 4: case 8: {
		register unsigned char *pix1 = p1, *pix2 = p2;

		for (pix = 0; pix < imagewidth; pix1++, pix2++, pix += ppb)
			if (*pix1 != *pix2)
				PrintDiff(row, sample, pix,
				    *pix1, *pix2);
		break;
	}
	case 16: {
		register uint16 *pix1 = (uint16 *)p1, *pix2 = (uint16 *)p2;

		for (pix = 0; pix < imagewidth; pix1++, pix2++, pix++)
			if (*pix1 != *pix2)
				PrintDiff(row, sample, pix,
				    *pix1, *pix2);
		break;
	}
	}
}

static void
PrintDiff(uint32 row, int sample, uint32 pix, int w1, int w2)
{
	register int mask1, mask2, s, bps;

	if (sample < 0)
		sample = 0;
	bps = bitspersample;
	switch (bps) {
	case 1:
	case 2:
	case 4:
		mask1 =  ~((-1)<<bps);
		s = (8-bps);
		mask2 = mask1<<s;
		for (; mask2; mask2 >>= bps, s -= bps, pix++) {
			if ((w1 & mask2) ^ (w2 & mask2)) {
				printf(
			"Scanline %lu, pixel %lu, sample %d: %01x %01x\n",
	    				row, pix, sample, (w1 >> s) & mask1,
					(w2 >> s) & mask1 );
				if (--stopondiff == 0)
					exit(1);
			}
		}
		break;
	case 8: 
		printf("Scanline %lu, pixel %lu, sample %d: %02x %02x\n",
		    row, pix, sample, w1, w2);
		if (--stopondiff)
			exit(1);
		break;
	case 16:
		printf("Scanline %lu, pixel %lu, sample %d: %04x %04x\n",
		    row, pix, sample, w1, w2);
		if (--stopondiff)
			exit(1);
		break;
	}
}

static void
SeparateCompare(int reversed, int sample, uint32 row, unsigned char* cp1, unsigned char* p2)
{
	uint32 npixels = imagewidth;
	register int pixel;

	cp1 += sample;
	for (pixel = 0; npixels-- > 0; pixel++, cp1 += samplesperpixel, p2++)
		if (*cp1 != *p2) {
			printf("Scanline %lu, pixel %lu, sample %d: ",
			    row, pixel, sample);
			if (reversed)
				printf("%02x %02x\n", *p2, *cp1);
			else
				printf("%02x %02x\n", *cp1, *p2);
			if (--stopondiff)
				exit(1);
		}
}

static int
checkTag(TIFF* tif1, TIFF* tif2, int tag, char* name, void* p1, void* p2)
{

	if (TIFFGetField(tif1, tag, p1)) {
		if (!TIFFGetField(tif2, tag, p2)) {
			printf("%s tag appears only in %s\n",
			    name, TIFFFileName(tif1));
			return (0);
		}
		return (1);
	} else if (TIFFGetField(tif2, tag, p2)) {
		printf("%s tag appears only in %s\n", name, TIFFFileName(tif2));
		return (0);
	}
	return (-1);
}

#define	CHECK(cmp, fmt) {				\
	switch (checkTag(tif1,tif2,tag,name,&v1,&v2)) {	\
	case 1:	if (cmp)				\
	case -1:	return (1);			\
		printf(fmt, name, v1, v2);		\
	}						\
	return (0);					\
}

static int
CheckShortTag(TIFF* tif1, TIFF* tif2, int tag, char* name)
{
	uint16 v1, v2;
	CHECK(v1 == v2, "%s: %u %u\n");
}

static int
CheckShort2Tag(TIFF* tif1, TIFF* tif2, int tag, char* name)
{
	uint16 v11, v12, v21, v22;

	if (TIFFGetField(tif1, tag, &v11, &v12)) {
		if (!TIFFGetField(tif2, tag, &v21, &v22)) {
			printf("%s tag appears only in %s\n",
			    name, TIFFFileName(tif1));
			return (0);
		}
		if (v11 == v21 && v12 == v22)
			return (1);
		printf("%s: <%u,%u> <%u,%u>\n", name, v11, v12, v21, v22);
	} else if (TIFFGetField(tif2, tag, &v21, &v22))
		printf("%s tag appears only in %s\n", name, TIFFFileName(tif2));
	else
		return (1);
	return (0);
}

static int
CheckShortArrayTag(TIFF* tif1, TIFF* tif2, int tag, char* name)
{
	uint16 n1, *a1;
	uint16 n2, *a2;

	if (TIFFGetField(tif1, tag, &n1, &a1)) {
		if (!TIFFGetField(tif2, tag, &n2, &a2)) {
			printf("%s tag appears only in %s\n",
			    name, TIFFFileName(tif1));
			return (0);
		}
		if (n1 == n2) {
			char* sep;
			uint16 i;

			if (memcmp(a1, a2, n1) == 0)
				return (1);
			printf("%s: value mismatch, <%u:", name, n1);
			sep = "";
			for (i = 0; i < n1; i++)
				printf("%s%u", sep, a1[i]), sep = ",";
			printf("> and <%u: ", n2);
			sep = "";
			for (i = 0; i < n2; i++)
				printf("%s%u", sep, a2[i]), sep = ",";
			printf(">\n");
		} else
			printf("%s: %u items in %s, %u items in %s", name,
			    n1, TIFFFileName(tif1),
			    n2, TIFFFileName(tif2)
			);
	} else if (TIFFGetField(tif2, tag, &n2, &a2))
		printf("%s tag appears only in %s\n", name, TIFFFileName(tif2));
	else
		return (1);
	return (0);
}

static int
CheckLongTag(TIFF* tif1, TIFF* tif2, int tag, char* name)
{
	uint32 v1, v2;
	CHECK(v1 == v2, "%s: %lu %lu\n");
}

static int
CheckFloatTag(TIFF* tif1, TIFF* tif2, int tag, char* name)
{
	float v1, v2;
	CHECK(v1 == v2, "%s: %g %g\n");
}

static int
CheckStringTag(TIFF* tif1, TIFF* tif2, int tag, char* name)
{
	char *v1, *v2;
	CHECK(strcmp(v1, v2) == 0, "%s: \"%s\" \"%s\"\n");
}

static void
eof(const char* name, uint32 row, int s)
{

	printf("%s: EOF at scanline %lu", name, row);
	if (s >= 0)
		printf(", sample %d", s);
	printf("\n");
}
