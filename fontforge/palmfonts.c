/* Copyright (C) 2005 by George Williams */
/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.

 * The name of the author may not be used to endorse or promote products
 * derived from this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "pfaeditui.h"
#include <stdio.h>
#include <math.h>
#include "splinefont.h"
#include <string.h>
#include <ustring.h>

/* Palm bitmap fonts are a bastardized version of the mac 'FONT' resource
 (which is the same format as the newer but still obsolete 'NFNT' resource).
  http://www.palmos.com/dev/support/docs/palmos/PalmOSReference/Font.html
They are stored in a resource database (pdb file)
  http://www.palmos.com/dev/support/docs/fileformats/Introl.html#970318
*/

/* Palm resource dbs have no magic numbers, nor much of anything I can use
    to validate that I have opened a real db file. Their records don't have
    much in the way of identification either, so I can't tell if a record
    is supposed to be a font of an image or a twemlo. Fonts have no names
    (usually. FontBucket has their own non-standard format with names), so
    I can't provide the user with a list of fonts in the file.
    So do the best we can to guess.
*/

struct font {
    int ascent;
    int leading;
    int frectheight;
    int rowwords;
    int first, last;
    struct chars {
	uint16 start;
	int16 width;
    } chars[258];
};

static SplineFont *MakeContainer(struct font *fn, char *family, char *style) {
    SplineFont *sf;
    int em;
    int i;

    sf = SplineFontBlank(FindOrMakeEncoding("win"),256);
    free(sf->familyname); free(sf->fontname); free(sf->fullname);
    sf->familyname = copy(family);
    sf->fontname = galloc(strlen(family)+strlen(style)+2);
    strcpy(sf->fontname,family);
    if ( *style!='\0' ) {
	strcat(sf->fontname,"-");
	strcat(sf->fontname,style);
    }
    sf->fullname = copy(sf->fontname);

    free(sf->copyright); sf->copyright = NULL;

    em = sf->ascent+sf->descent;
    sf->ascent = fn->ascent*em/fn->frectheight;
    sf->descent = em - sf->ascent;
    sf->onlybitmaps = true;

    for ( i=fn->first; i<=fn->last; ++i ) if ( fn->chars[i].width!=-1 ) {
	SplineChar *sc = SFMakeChar(sf,i);
	sc->width = fn->chars[i].width*em/fn->frectheight;
    }
    if ( fn->first!=0 ) {		/* .notdef */
	SplineChar *sc = SFMakeChar(sf,0);
	sc->width = fn->chars[i].width*em/fn->frectheight;
    }
return( sf );
}

/* scale all metric info by density/72. Not clear what happens for non-integral results */
static void PalmReadBitmaps(SplineFont *sf,FILE *file,int imagepos,
	struct font *fn,int density) {
    int pixelsize = density*fn->frectheight/72;
    BDFFont *bdf;
    uint16 *fontImage;
    int imagesize, index, i;

    for ( bdf = sf->bitmaps; bdf!=NULL && bdf->pixelsize!=pixelsize; bdf=bdf->next );
    if ( bdf!=NULL )
return;

    imagesize = (density*fn->rowwords/72)*(density*fn->frectheight/72);
    fontImage = galloc(2*imagesize);
    fseek(file,imagepos,SEEK_SET);
    for ( i=0; i<imagesize; ++i )
	fontImage[i] = getushort(file);
    if ( feof(file) ) {
	free(fontImage);
return;
    }

    bdf = gcalloc(1,sizeof(BDFFont));
    bdf->sf = sf;
    bdf->next = sf->bitmaps;
    sf->bitmaps = bdf;
    bdf->charcnt = sf->charcnt;
    bdf->pixelsize = pixelsize;
    bdf->chars = gcalloc(sf->charcnt,sizeof(BDFChar *));
    bdf->ascent = density*fn->ascent/72;
    bdf->descent = pixelsize - bdf->ascent;
    bdf->encoding_name = sf->encoding_name;
    bdf->res = 72;

    for ( index=fn->first; index<=fn->last+1; ++index ) if ( fn->chars[index].width!=-1 ) {
	BDFChar *bdfc;
	int i,j, bits, bite, bit;
	int enc = index;

	if ( index==fn->last+1 ) {
	    enc=0;
	    if ( fn->first==0 )
	break;
	}

	bdfc = chunkalloc(sizeof(BDFChar));
	bdfc->xmin = 0;
	bdfc->xmax = density*(fn->chars[index+1].start-fn->chars[index].start)/72-1;
	bdfc->ymin = -bdf->descent;
	bdfc->ymax = bdf->ascent-1;
	bdfc->width = density*fn->chars[index].width/72;
	bdfc->bytes_per_line = ((bdfc->xmax-bdfc->xmin)>>3) + 1;
	bdfc->bitmap = gcalloc(bdfc->bytes_per_line*(density*fn->frectheight)/72,sizeof(uint8));
	bdfc->enc = enc;
	bdfc->sc = sf->chars[enc];
	bdf->chars[enc] = bdfc;

	bits = density*fn->chars[index].start/72; bite = density*fn->chars[index+1].start/72;
	for ( i=0; i<density*fn->frectheight/72; ++i ) {
	    uint16 *test = fontImage + i*density*fn->rowwords/72;
	    uint8 *bpt = bdfc->bitmap + i*bdfc->bytes_per_line;
	    for ( bit=bits, j=0; bit<bite; ++bit, ++j ) {
		if ( test[bit>>4]&(0x8000>>(bit&0xf)) )
		    bpt[j>>3] |= (0x80>>(j&7));
	    }
	}
	BCCompressBitmap(bdfc);
    }
}

static SplineFont *PalmTestFont(FILE *file,int end, char *family,char *style) {
    int type;
    int frectwidth, descent;
    int owtloc;
    int pos = ftell(file);
    struct density {
	int density;
	int offset;
    } density[10];
    int dencount, i;
    struct font fn;
    int imagepos=0;
    SplineFont *sf;
    int maxbit;

    type = getushort(file);
    if ( type==0x0090 || type==0x0092 ) {
	fprintf( stderr, "Warning: Byte swapped font mark in palm font.\n" );
	type = type<<8;
    }
    if ( (type&0x9000)!=0x9000 )
return( NULL );
    memset(&fn,0,sizeof(fn));
    fn.first = getushort(file);
    fn.last = getushort(file);
    /* maxWidth = */ (void) getushort(file);
    /* kernmax = */ (void) getushort(file);
    /* ndescent = */ (void) getushort(file);
    frectwidth = getushort(file);
    fn.frectheight = getushort(file);
    owtloc = ftell(file);
    owtloc += 2*getushort(file);
    fn.ascent = getushort(file);
    descent = getushort(file);
    fn.leading = getushort(file);
    fn.rowwords = getushort(file);
    if ( feof(file) || ftell(file)>=end || fn.first>fn.last || fn.last>255 ||
	    pos+(fn.last-fn.first+2)*2+2*fn.rowwords*fn.frectheight>end ||
	    owtloc+2*(fn.last-fn.first+2)>end )
return( NULL );
    dencount = 0;
    if ( type&0x200 ) {
	if ( getushort(file)!=1 )	/* Extended data version number */
return(NULL);
	dencount = getushort(file);
	if ( dencount>6 )		/* only a few sizes allowed */
return( NULL );
	for ( i=0; i<dencount; ++i ) {
	    density[i].density = getushort(file);
	    density[i].offset = getlong(file);
	    if ( ftell(file)>end ||
		    (density[i].density!=72 &&
		     density[i].density!=108 &&
		     density[i].density!=144 &&
		     density[i].density!=216 &&	/*Documented, but not supported*/
		     density[i].density!=288 ))	/*Documented, but not supported*/
return( NULL );
	}
    } else {
	imagepos = ftell(file);
	fseek(file,2*fn.rowwords*fn.frectheight,SEEK_CUR);
    }

    /* Bitmap location table */
    /*  two extra entries. One gives loc of .notdef glyph, one points just after it */
    maxbit = fn.rowwords*16;
    for ( i=fn.first; i<=fn.last+2; ++i ) {
	fn.chars[i].start = getushort(file);
	if ( fn.chars[i].start>maxbit || (i!=0 && fn.chars[i].start<fn.chars[i-1].start))
return( NULL );
    }

    fseek(file,owtloc,SEEK_SET);
    for ( i=fn.first; i<=fn.last+1; ++i ) {
	int offset, width;
	offset = (int8) getc(file);
	width = (int8) getc(file);
	if ( offset==-1 && width==-1 )
	    /* Skipped glyph */;
	else if ( offset!=0 )
return(NULL);
	fn.chars[i].width = width;
    }
    if ( feof(file) || ftell(file)>end )
return( NULL );

    sf = MakeContainer(&fn,family,style);
    if ( type&0x200 ) {
	for ( i=0; i<dencount; ++i )
	    PalmReadBitmaps(sf,file,pos+density[i].offset,&fn,density[i].density);
    } else {
	PalmReadBitmaps(sf,file,imagepos,&fn,72);
    }
return( sf );
}

static char *palmreadstring(FILE *file) {
    int pos = ftell(file);
    int i, ch;
    char *str, *pt;

    for ( i=0; (ch=getc(file))!=0 && ch!=EOF; ++i);
    str = pt = galloc(i+1);
    fseek(file,pos,SEEK_SET);
    while ( (ch=getc(file))!=0 && ch!=EOF )
	*pt++ = ch;
    *pt = '\0';
return( str );
}

static SplineFont *PalmTestRecord(FILE *file,int start, int end, char *name) {
    int here = ftell(file);
    int type, size, pos;
    SplineFont *sf = NULL;
    char *family=NULL, *style=NULL;
    int version;

    if ( end<=start )
return( NULL );

    fseek(file,start,SEEK_SET);
    type = getushort(file);
    if ( feof(file))
  goto ret;
    fseek(file,start,SEEK_SET);
    if ( (type&0x9000)==0x9000 || type==0x0090 || type==0x0092 ) {
	sf = PalmTestFont(file,end,name,"");
	if ( sf!=NULL )
  goto ret;
    }
    /* Now test for a font bucket structure */
    version = getc(file); /* version number of font bucket format. currently 0 */
    if ( version==4 ) {
	fprintf( stderr, "Warning: Font Bucket version 4 treated as 0.\n" );
	version=0;
    }
    if ( version!=0 )
  goto ret;
    if ( getc(file)!=0 )	/* not interested in system fonts */
  goto ret;
    (void) getushort(file);	/* Skip the pixel height */
    (void) getushort(file);	/* blank bits */
    size = getlong(file);
    pos = ftell(file);		/* Potential start of font data */
    if ( pos+size > end )
  goto ret;
    fseek(file,size,SEEK_CUR);
    family = palmreadstring(file);
    style = palmreadstring(file);
    if ( feof(file) || ftell(file)>end )
  goto ret;
    fseek(file,pos,SEEK_SET);
    sf = PalmTestFont(file,pos+size,family,style);

  ret:
    free(family); free(style);
    fseek(file,here,SEEK_SET);
return( sf );
}

SplineFont *SFReadPalmPdb(char *filename,int toback) {
    char name[32];
    FILE *file;
    int num_records, i, file_end;
    int offset, next_offset;
    SplineFont *sf;

    file = fopen(filename,"rb");
    if ( file==NULL )
return( NULL );

    fseek(file,0,SEEK_END);
    file_end = ftell(file);
    fseek(file,0,SEEK_SET);

    if ( fread(name,1,32,file)==-1 )
  goto fail;
    fseek(file,0x2c,SEEK_CUR);		/* Find start of record list */
    num_records = getushort(file);
    if ( num_records<=0 )
  goto fail;
    offset = getlong(file);		/* offset to data */
    (void) getlong(file);		/* random junk */
    if ( offset>= file_end )
  goto fail;
    for ( i=1; i<num_records; ++i ) {
	next_offset = getlong(file);
	(void) getlong(file);
	if ( feof(file) || next_offset<offset || next_offset>file_end )
  goto fail;
	sf = PalmTestRecord(file,offset,next_offset,name);
	if ( sf!=NULL ) {
	    fclose(file);
return( sf );
	}
	offset=next_offset;
    }
    sf = PalmTestRecord(file,offset,file_end,name);
    if ( sf!=NULL ) {
	fclose(file);
return( sf );
    }

  fail:
    fclose(file);
return( NULL );			/* failed */
}

static FILE *MakeSingleRecordPdb(char *filename) {
    FILE *file;
    char *fn = galloc(strlen(filename)+8), *pt1, *pt2;
    long now;
    int i;

    strcpy(fn,filename);
    pt1 = strrchr(fn,'/');
    if ( pt1==NULL ) pt1 = fn; else ++pt1;
    pt2 = strrchr(fn,'.');
    if ( pt2==NULL || pt2<pt1 )
	pt2 = fn+strlen(fn);
    strcpy(pt2,".pdb");
    file = fopen(fn,"wb");
    if ( file==NULL ) {
#if defined(FONTFORGE_CONFIG_GDRAW)
	GWidgetErrorR(_STR_CouldNotOpenFile,_STR_CouldNotOpenFileName,fn);
#elif defined(FONTFORGE_CONFIG_GTK)
	gwwv_post_error(_("Could not open file"),_("Could not open file %.200s"), fn);
#endif
	free(fn);
return( NULL );
    }

    *pt2 = '\0';
    for ( i=0; i<31 && *pt1; ++i, ++pt1 )
	putc(*pt1,file);
    while ( i<32 ) {
	putc('\0',file);
	++i;
    }
    putshort(file,0);				/* attributes */
    putshort(file,0);				/* version */
    now = mactime();
    putlong(file,now);
    putlong(file,now);
    putlong(file,now);
    putlong(file,0);				/* modification number */
    putlong(file,0);				/* app info */
    putlong(file,0);				/* sort info */
    putlong(file,CHR('F','o','n','t'));		/* type */
    putlong(file,CHR('F','t','F','g'));		/* creator */
    putlong(file,0);				/* uniqueIDseed */

    /* Record list */
    putlong(file,0);				/* next record id */
    putshort(file,1);				/* numRecords */

    putlong(file,ftell(file)+8);		/* offset to data */
    putlong(file,0);
return(file);
}
    
static BDFFont *getbdfsize(SplineFont *sf, int32 size) {
    BDFFont *bdf;

    for ( bdf=sf->bitmaps; bdf!=NULL && (bdf->pixelsize!=(size&0xffff) || BDFDepth(bdf)!=(size>>16)); bdf=bdf->next );
return( bdf );
}

struct FontTag {
  int16 fontType;
  int16 firstChar;
  int16 lastChar;
  int16 maxWidth;
  int16 kernMax;
  int16 nDescent;
  int16 fRectWidth; 
  int16 fRectHeight;
  int16 owTLoc;
  int16 ascent;
  int16 descent;
  int16 leading;
  int16 rowWords;
};

static int ValidMetrics(BDFFont *test,BDFFont *base,int den) {
    /* All glyphs must fit within 0,advance-width */
    /* All advance widths must be den*base->advance_width */
    int i;
    int warned=false, wwarned = false;

    if ( test==NULL )
return( true );

    if ( test==base && base->charcnt>=256 )
#if defined(FONTFORGE_CONFIG_GDRAW)
	GWidgetErrorR(_STR_BadMetrics,_STR_OnlyFirst256);
#elif defined(FONTFORGE_CONFIG_GTK)
	gwwv_post_error(_("Bad Metrics"),_("Only the first 256 glyphs in the encoding will be used"));
#endif

    for ( i=0; i<base->charcnt && i<256; ++i ) if ( test->chars[i]!=NULL || base->chars[i]!=NULL ) {
	if ( base->chars[i]==NULL || test->chars[i]==NULL ) {
#if defined(FONTFORGE_CONFIG_GDRAW)
	    GWidgetErrorR(_STR_BadMetrics,_STR_NoCorrespondingGlyph,
		    test->pixelsize,base->pixelsize, i);
#elif defined(FONTFORGE_CONFIG_GTK)
	    gwwv_post_error(_("Bad Metrics"),_("One of the fonts %d,%d is missing glyph %d"),
		    temp->pixelsize,base->pixelsize,i);
#endif
return( false );
	}
	if ( !warned &&
		(test->chars[i]->xmin<0 ||
		 test->chars[i]->xmax>test->chars[i]->width ||
		 test->chars[i]->ymax>=test->ascent ||
		 test->chars[i]->ymin<-test->descent)) {
#if defined(FONTFORGE_CONFIG_GDRAW)
	    GWidgetErrorR(_STR_BadMetrics,_STR_GlyphKernsBadly,
		    test->pixelsize,test->chars[i]->sc->name);
#elif defined(FONTFORGE_CONFIG_GTK)
	    gwwv_post_error(_("Bad Metrics"),_("In font %d the glyph %.30s either starts before 0, or extends after the advance width or is above the ascent or below the descent"),
		    temp->pixelsize,test->chars[i]->sc->name);
#endif
	    warned = true;
	}
	if ( !wwarned && test->chars[i]->width!=den*base->chars[i]->width ) {
#if defined(FONTFORGE_CONFIG_GDRAW)
	    GWidgetErrorR(_STR_BadMetrics,_STR_AdvanceWidthBad,
		    test->pixelsize,test->chars[i]->sc->name);
#elif defined(FONTFORGE_CONFIG_GTK)
	    gwwv_post_error(_("Bad Metrics"),_("In font %d the advance width of glyph %.30s does not scale the base advance width properly, it shall be forced to the proper value"),
		    temp->pixelsize,test->chars[i]->sc->name);
#endif
	    wwarned = true;
	}
	if ( base->chars[i]->width>127 ) {
#if defined(FONTFORGE_CONFIG_GDRAW)
	    GWidgetErrorR(_STR_BadMetrics,_STR_AdvanceWidthTooBig,
		    test->pixelsize,test->chars[i]->sc->name);
#elif defined(FONTFORGE_CONFIG_GTK)
	    gwwv_post_error(_("Bad Metrics"),_("Advance width of glyph %.30s must be less than 127"),
		    temp->pixelsize,test->chars[i]->sc->name);
#endif
return( false );
	}
    }
return( true );
}

static void PalmAddChar(uint16 *image,int rw,int rbits,
	BDFFont *bdf,BDFChar *bc, int width) {
    int i,j;

    for ( i=0; i<bdf->pixelsize; ++i ) {
	int y = bdf->ascent-1-i;
	if ( y<=bc->ymax && y>=bc->ymin ) {
	    int bi = bc->ymax-y;
	    int ipos = i*rw;
	    int bipos = bi*bc->bytes_per_line;
	    for ( j=bc->xmin<=0?0:bc->xmin; j<width && j<=bc->xmax; ++j )
		if ( bc->bitmap[bipos+((j-bc->xmin)>>3)]&(0x80>>((j-bc->xmin)&7)) )
		    image[ipos+((rbits+j)>>4)] |= (0x8000>>((rbits+j)&0xf));
	}
    }
}

static uint16 *BDF2Image(struct FontTag *fn, BDFFont *bdf, int **offsets, int **widths, int16 *rowWords, BDFFont *base) {
    int rbits, rw;
    int hasnotdef = false;
    int i,j;
    uint16 *image;
    int den;

    if ( bdf==NULL )
return( NULL );

    den = bdf->pixelsize/fn->fRectHeight;

    if ( bdf->chars[0]!=NULL && strcmp(bdf->chars[0]->sc->name,".notdef")==0 )
	hasnotdef = true;
    rbits = 0;
    for ( i=fn->firstChar; i<=fn->lastChar; ++i ) if ( bdf->chars[i]!=NULL )
	rbits += base->chars[i]->width;
    if ( hasnotdef )
	rbits += base->chars[0]->width;
    else
	rbits += (fn->fRectHeight/2)+1;
    rw = den * ((rbits+15)/16);

    if ( rowWords!=NULL ) {
	*rowWords = rw;
	*offsets = galloc((fn->lastChar-fn->firstChar+3)*sizeof(int));
	*widths = galloc((fn->lastChar-fn->firstChar+3)*sizeof(int));
    }
    image = gcalloc(bdf->pixelsize*rw,sizeof(uint16));
    rbits = 0;
    for ( i=fn->firstChar; i<=fn->lastChar; ++i ) {
	if ( offsets!=NULL )
	    (*offsets)[i-fn->firstChar] = rbits;
	if ( bdf->chars[i]!=NULL ) {
	    if ( widths!=NULL )
		(*widths)[i-fn->firstChar] = den*base->chars[i]->width;
	    PalmAddChar(image,rw,rbits,bdf,bdf->chars[i],den*base->chars[i]->width);
	    rbits += den*base->chars[i]->width;
	} else if ( widths!=NULL )
	    (*widths)[i-fn->firstChar] = -1;
    }
    if ( offsets!=NULL )
	(*offsets)[i-fn->firstChar] = rbits;
    if ( hasnotdef ) {
	PalmAddChar(image,rw,rbits,bdf,bdf->chars[0],den*base->chars[0]->width);
	if ( widths!=NULL )
	    (*widths)[i-fn->firstChar] = den*base->chars[0]->width;
	rbits += bdf->chars[0]->width;
    } else {
	int wid, down, height;
	wid = (fn->fRectHeight/2)*(bdf->pixelsize/fn->fRectHeight)-1;
	if ( widths!=NULL )
	    (*widths)[i-fn->firstChar] = wid+1;
	height = 2*bdf->ascent/3;
	if ( height<3 ) height = bdf->ascent;
	down = bdf->ascent-height;
	for ( j=down; j<down+height; ++j ) {
	    image[j*rw+(rbits>>4)] |= (0x8000>>(rbits&0xf));
	    image[j*rw+((rbits+wid-1)>>4)] |= (0x8000>>((rbits+wid-1)&0xf));
	}
	for ( j=rbits; j<=rbits+wid; ++j ) {
	    image[down*rw+(j>>4)] |= (0x8000>>(j&0xf));
	    image[(down+height-1)*rw+(j>>4)] |= (0x8000>>(j&0xf));
	}
	rbits += wid+1;
    }
    if ( offsets!=NULL )
	(*offsets)[i+1-fn->firstChar] = rbits;
return( image );    
}

int WritePalmBitmaps(char *filename,SplineFont *sf, int32 *sizes) {
    BDFFont *base=NULL, *temp;
    BDFFont *densities[4];	/* Be prepared for up to quad density */
    				/* Ignore 1.5 density. No docs on how odd metrics get rounded */
    int i, j, k, den, dencnt;
    struct FontTag fn;
    uint16 *images[4];
    int *offsets, *widths;
    int owbase, owpos, font_start, density_starts;
    FILE *file;

    if ( sizes==NULL || sizes[0]==0 )
return(false);
    for ( i=0; sizes[i]!=0; ++i ) {
	temp = getbdfsize(sf,sizes[i]);
	if ( temp==NULL || BDFDepth(temp)!=1 )
return( false );
	if ( base==NULL || base->pixelsize>temp->pixelsize )
	    base = temp;
    }
    memset(densities,0,sizeof(densities));
    for ( i=0; sizes[i]!=0; ++i ) {
	temp = getbdfsize(sf,sizes[i]);
	den = temp->pixelsize/base->pixelsize;
	if ( temp->pixelsize!=base->pixelsize*den || den>4 ) {
#if defined(FONTFORGE_CONFIG_GDRAW)
	    GWidgetErrorR(_STR_UnexpectedDensity,_STR_UnexpectedDensityLong,
		    temp->pixelsize,base->pixelsize);
#elif defined(FONTFORGE_CONFIG_GTK)
	    gwwv_post_error(_("Unexpected density"),_("One of the bitmap fonts, %d, specified is not an integral scale up of the smallest font, %d (or is too large a factor)"),
		    temp->pixelsize,base->pixelsize);
#endif
return( false );
	}
	densities[den-1] = temp;
    }

    dencnt = 0;
    for ( i=0; i<4; ++i ) {
	if ( !ValidMetrics(densities[i],base,i+1))
return( false );
	if ( densities[i]) ++dencnt;
    }

    memset(&fn,0,sizeof(fn));
    fn.fontType = dencnt>1 ? 0x9200 : 0x9000;
    fn.firstChar = -1;
    fn.fRectHeight = base->pixelsize;
    fn.ascent = base->ascent;
    fn.descent = base->descent;
    for ( i=0; i<base->charcnt && i<256; ++i ) {
	if ( base->chars[i]!=NULL && (i!=0 || strcmp(sf->chars[i]->name,".notdef")!=0 )) {
	    if ( fn.firstChar==-1 ) fn.firstChar = i;
	    fn.lastChar = i;
	    if ( base->chars[i]->width > fn.maxWidth )
		fn.maxWidth = fn.fRectWidth = base->chars[i]->width;
	}
	if ( base->chars[i]!=NULL ) {
	    if ( base->chars[i]->width > fn.maxWidth )
		fn.maxWidth = fn.fRectWidth = base->chars[i]->width;
	}
    }
	    
    file = MakeSingleRecordPdb(filename);
    if ( file==NULL )
return( false );

    images[0] = BDF2Image(&fn,base,&offsets,&widths,&fn.rowWords,base);
    for ( i=1; i<4; ++i )
	images[i] = BDF2Image(&fn,densities[i],NULL,NULL,NULL,base);

    font_start = ftell(file);

    putshort(file,fn.fontType);
    putshort(file,fn.firstChar);
    putshort(file,fn.lastChar);
    putshort(file,fn.maxWidth);
    putshort(file,fn.kernMax);
    putshort(file,fn.nDescent);
    putshort(file,fn.fRectWidth);
    putshort(file,fn.fRectHeight);
    owbase = ftell(file);
    putshort(file,fn.owTLoc);
    putshort(file,fn.ascent);
    putshort(file,fn.descent);
    putshort(file,fn.leading);
    putshort(file,fn.rowWords);

    if ( dencnt==1 ) {
	for ( i=0; i<fn.fRectHeight*fn.rowWords; ++i )
	    putshort(file,images[0][i]);
	free(images[0]);
    } else {
	putshort(file,1);		/* Extended version field */
	putshort(file,dencnt);
	density_starts = ftell(file);
	for ( i=0; i<4; ++i ) if ( densities[i]!=NULL ) {
	    putshort(file,(i+1)*72);
	    putlong(file,0);
	}
    }
    for ( i=fn.firstChar; i<=fn.lastChar+2; ++i )
	putshort(file,offsets[i-fn.firstChar]);
    free(offsets);
    owpos = ftell(file);
    fseek(file,owbase,SEEK_SET);
    putshort(file,(owpos-owbase)/2);
    fseek(file,owpos,SEEK_SET);
    for ( i=fn.firstChar; i<=fn.lastChar; ++i ) {
	if ( base->chars[i]==NULL ) {
	    putc(-1,file);
	    putc(-1,file);
	} else {
	    putc(0,file);
	    putc(base->chars[i]->width,file);
	}
    }
    putc(0,file);		/* offset/width for notdef */
    putc(widths[i-fn.firstChar],file);
    free(widths);

    if ( dencnt>1 ) {
	for ( i=j=0; i<4; ++i ) if ( densities[i]!=NULL ) {
	    int here = ftell(file);
	    fseek(file,density_starts+j*6+2,SEEK_SET);
	    putlong(file,here-font_start);
	    fseek(file,here,SEEK_SET);
	    for ( k=0; k<(i+1)*(i+1)*fn.fRectHeight*fn.rowWords; ++k )
		putshort(file,images[i][k]);
	    ++j;
	    free(images[i]);
	}
    }
    fclose(file);
return( true );
}
