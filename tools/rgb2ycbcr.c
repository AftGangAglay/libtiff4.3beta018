/* $Header: /usr/people/sam/tiff/tools/RCS/rgb2ycbcr.c,v 1.25 1995/06/06 23:45:26 sam Exp $ */

/*
 * Copyright (c) 1991-1995 Sam Leffler
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
#include <string.h>
#include <stdlib.h>
#endif

#include "tiffio.h"

#define	streq(a,b)	(strcmp(a,b) == 0)
#define	CopyField(tag, v) \
    if (TIFFGetField(in, tag, &v)) TIFFSetField(out, tag, v)

#define	howmany(x, y)	(((x)+((y)-1))/(y))
#define	roundup(x, y)	(howmany(x,y)*((uint32)(y)))

#define	LumaRed		ycbcrCoeffs[0]
#define	LumaGreen	ycbcrCoeffs[1]
#define	LumaBlue	ycbcrCoeffs[2]

uint16	compression = COMPRESSION_LZW;
uint32	rowsperstrip = (uint32) -1;

uint16	horizSubSampling = 2;		/* YCbCr horizontal subsampling */
uint16	vertSubSampling = 2;		/* YCbCr vertical subsampling */
float	ycbcrCoeffs[3] = { .299, .587, .114 };
/* default coding range is CCIR Rec 601-1 with no headroom/footroom */
float	refBlackWhite[6] = { 0., 255., 128., 255., 128., 255. };

static	int tiffcvt(TIFF* in, TIFF* out);
static	void usage(void);
static	void setupLumaTables(void);

int
main(int argc, char* argv[])
{
	TIFF *in, *out;
	int c;
	extern int optind;
	extern char *optarg;

	while ((c = getopt(argc, argv, "c:h:r:v:z")) != -1)
		switch (c) {
		case 'c':
			if (streq(optarg, "none"))
			    compression = COMPRESSION_NONE;
			else if (streq(optarg, "packbits"))
			    compression = COMPRESSION_PACKBITS;
			else if (streq(optarg, "lzw"))
			    compression = COMPRESSION_LZW;
			else if (streq(optarg, "jpeg"))
			    compression = COMPRESSION_JPEG;
			else
			    usage();
			break;
		case 'h':
			horizSubSampling = atoi(optarg);
			break;
		case 'v':
			vertSubSampling = atoi(optarg);
			break;
		case 'r':
			rowsperstrip = atoi(optarg);
			break;
		case 'z':	/* CCIR Rec 601-1 w/ headroom/footroom */
			refBlackWhite[0] = 16.;
			refBlackWhite[1] = 235.;
			refBlackWhite[2] = 128.;
			refBlackWhite[3] = 240.;
			refBlackWhite[4] = 128.;
			refBlackWhite[5] = 240.;
			break;
		case '?':
			usage();
			/*NOTREACHED*/
		}
	if (argc - optind < 2)
		usage();
	out = TIFFOpen(argv[argc-1], "w");
	if (out == NULL)
		return (-2);
	setupLumaTables();
	for (; optind < argc-1; optind++) {
		in = TIFFOpen(argv[optind], "r");
		if (in != NULL) {
			do {
				if (!tiffcvt(in, out) ||
				    !TIFFWriteDirectory(out)) {
					(void) TIFFClose(out);
					return (1);
				}
			} while (TIFFReadDirectory(in));
			(void) TIFFClose(in);
		}
	}
	(void) TIFFClose(out);
	return (0);
}

float	*lumaRed;
float	*lumaGreen;
float	*lumaBlue;
float	D1, D2;
int	Yzero;

static float*
setupLuma(float c)
{
	float *v = (float *)_TIFFmalloc(256 * sizeof (float));
	int i;
	for (i = 0; i < 256; i++)
		v[i] = c * i;
	return (v);
}

static unsigned
V2Code(float f, float RB, float RW, int CR)
{
	unsigned int c = (unsigned int)((((f)*(RW-RB)/CR)+RB)+.5);
	return (c > 255 ? 255 : c);
}

static void
setupLumaTables(void)
{
	lumaRed = setupLuma(LumaRed);
	lumaGreen = setupLuma(LumaGreen);
	lumaBlue = setupLuma(LumaBlue);
	D1 = 1./(2 - 2*LumaBlue);
	D2 = 1./(2 - 2*LumaRed);
	Yzero = V2Code(0, refBlackWhite[0], refBlackWhite[1], 255);
}

static void
cvtClump(unsigned char* op, uint32* raster, uint32 ch, uint32 cw, uint32 w)
{
	float Y, Cb = 0, Cr = 0;
	int j, k;
	/*
	 * Convert ch-by-cw block of RGB
	 * to YCbCr and sample accordingly.
	 */
	for (k = 0; k < ch; k++) {
		for (j = 0; j < cw; j++) {
			uint32 RGB = (raster - k*w)[j];
			Y = lumaRed[TIFFGetR(RGB)] +
			    lumaGreen[TIFFGetG(RGB)] +
			    lumaBlue[TIFFGetB(RGB)];
			/* accumulate chrominance */
			Cb += (TIFFGetB(RGB) - Y) * D1;
			Cr += (TIFFGetR(RGB) - Y) * D2;
			/* emit luminence */
			*op++ = V2Code(Y,
			    refBlackWhite[0], refBlackWhite[1], 255);
		}
		for (; j < horizSubSampling; j++)
			*op++ = Yzero;
	}
	for (; k < vertSubSampling; k++) {
		for (j = 0; j < horizSubSampling; j++)
			*op++ = Yzero;
	}
	/* emit sampled chrominance values */
	*op++ = V2Code(Cb / (ch*cw), refBlackWhite[2], refBlackWhite[3], 127);
	*op++ = V2Code(Cr / (ch*cw), refBlackWhite[4], refBlackWhite[5], 127);
}
#undef LumaRed
#undef LumaGreen
#undef LumaBlue
#undef V2Code

/*
 * Convert a strip of RGB data to YCbCr and
 * sample to generate the output data.
 */
static void
cvtStrip(unsigned char* op, uint32* raster, uint32 nrows, uint32 width)
{
	uint32 x;
	int clumpSize = vertSubSampling * horizSubSampling + 2;
	uint32 *tp;

	for (; nrows >= vertSubSampling; nrows -= vertSubSampling) {
		tp = raster;
		for (x = width; x >= horizSubSampling; x -= horizSubSampling) {
			cvtClump(op, tp,
			    vertSubSampling, horizSubSampling, width);
			op += clumpSize;
			tp += horizSubSampling;
		}
		if (x > 0) {
			cvtClump(op, tp, vertSubSampling, x, width);
			op += clumpSize;
		}
		raster -= vertSubSampling*width;
	}
	if (nrows > 0) {
		tp = raster;
		for (x = width; x >= horizSubSampling; x -= horizSubSampling) {
			cvtClump(op, tp, nrows, horizSubSampling, width);
			op += clumpSize;
			tp += horizSubSampling;
		}
		if (x > 0)
			cvtClump(op, tp, nrows, x, width);
	}
}

static int
cvtRaster(TIFF* tif, uint32* raster, uint32 width, uint32 height)
{
	uint32 y;
	tstrip_t strip = 0;
	tsize_t cc, acc;
	unsigned char* buf;
	uint32 rwidth = roundup(width, horizSubSampling);
	uint32 rheight = roundup(height, vertSubSampling);
	uint32 nrows = (rowsperstrip > rheight ? rheight : rowsperstrip);

	cc = nrows*rwidth +
	    2*((nrows*rwidth) / (horizSubSampling*vertSubSampling));
	buf = (unsigned char*)_TIFFmalloc(cc);
	for (y = height; (int32) y > 0; y -= nrows) {
		uint32 nr = (y > nrows ? nrows : y);
		cvtStrip(buf, raster + (y-1)*width, nr, width);
		nr = roundup(nr, vertSubSampling);
		acc = nr*rwidth +
			2*((nr*rwidth)/(horizSubSampling*vertSubSampling));
		if (!TIFFWriteEncodedStrip(tif, strip++, buf, acc)) {
			_TIFFfree(buf);
			return (0);
		}
	}
	_TIFFfree(buf);
	return (1);
}

static int
tiffcvt(TIFF* in, TIFF* out)
{
	uint32 width, height;		/* image width & height */
	uint32* raster;			/* retrieve RGBA image */
	uint16 shortv;
	float floatv;
	char *stringv;
	uint32 longv;

	TIFFGetField(in, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(in, TIFFTAG_IMAGELENGTH, &height);
	raster = (uint32*)_TIFFmalloc(width * height * sizeof (uint32));
	if (raster == 0) {
		TIFFError(TIFFFileName(in), "No space for raster buffer");
		return (0);
	}
	if (!TIFFReadRGBAImage(in, width, height, raster, 0)) {
		_TIFFfree(raster);
		return (0);
	}

	CopyField(TIFFTAG_SUBFILETYPE, longv);
	TIFFSetField(out, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField(out, TIFFTAG_IMAGELENGTH, height);
	TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(out, TIFFTAG_COMPRESSION, compression);
	TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);
	if (compression == COMPRESSION_JPEG)
		TIFFSetField(out, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RAW);
	CopyField(TIFFTAG_FILLORDER, shortv);
	TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, 3);
	CopyField(TIFFTAG_XRESOLUTION, floatv);
	CopyField(TIFFTAG_YRESOLUTION, floatv);
	CopyField(TIFFTAG_RESOLUTIONUNIT, shortv);
	TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	{ char buf[2048];
	  char *cp = strrchr(TIFFFileName(in), '/');
	  sprintf(buf, "YCbCr conversion of %s", cp ? cp+1 : TIFFFileName(in));
	  TIFFSetField(out, TIFFTAG_IMAGEDESCRIPTION, buf);
	}
	TIFFSetField(out, TIFFTAG_SOFTWARE, TIFFGetVersion());
	CopyField(TIFFTAG_DOCUMENTNAME, stringv);

	TIFFSetField(out, TIFFTAG_REFERENCEBLACKWHITE, refBlackWhite);
	TIFFSetField(out, TIFFTAG_YCBCRSUBSAMPLING,
	    horizSubSampling, vertSubSampling);
	TIFFSetField(out, TIFFTAG_YCBCRPOSITIONING, YCBCRPOSITION_CENTERED);
	TIFFSetField(out, TIFFTAG_YCBCRCOEFFICIENTS, ycbcrCoeffs);
	rowsperstrip = TIFFDefaultStripSize(out, rowsperstrip);
	TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, rowsperstrip);

	return (cvtRaster(out, raster, width, height));
}

static char* usageMsg[] = {
    "usage: rgb2ycbcr [-c comp] [-r rows] [-h N] [-v N] input... output\n",
    "where comp is one of the following compression algorithms:\n",
    " jpeg\t\tJPEG encoding\n",
    " lzw\t\tLempel-Ziv & Welch encoding\n",
    " packbits\tPackBits encoding\n",
    " none\t\tno compression\n",
    "and the other options are:\n",
    " -r\trows/strip\n",
    " -h\thorizontal sampling factor (1,2,4)\n",
    " -v\tvertical sampling factor (1,2,4)\n",
    NULL
};

static void
usage(void)
{
	int i;
	for (i = 0; usageMsg[i]; i++)
		fprintf(stderr, "%s", usageMsg[i]);
	exit(-1);
}
