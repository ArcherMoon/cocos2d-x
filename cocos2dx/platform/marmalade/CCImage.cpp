/****************************************************************************
 Copyright (c) 2011      cocos2d-x.org   http://cocos2d-x.org
 Copyright (c) 2011      Максим Аксенов
 Copyright (c) 2011      Giovanni Zito, Francis Styck

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
#include <vector>
#include <string>
#include <sstream> 
#include <algorithm>
#include "CCImage.h"
#include "CCCommon.h"
#include "CCStdC.h"
#include "CCFileUtils.h"
#include "s3eFile.h"
#include "IwUtil.h"
#include "png.h"
#include "ft2build.h"
#include "tiffio.h"
#include FT_FREETYPE_H 
#define FONT_KERNING 2
#define RSHIFT6(num) ((num)>>6)

#include <strings.h>

extern "C"
{
#include <jpeglib.h>
}

//#include <string>

typedef struct 
{
    unsigned char* data;
    int size;
    int offset;
} tImageSource;

struct TextLine {
	std::string sLineStr;
	int iLineWidth;
};

NS_CC_BEGIN;

class CC_DLL ImageHelper
{
public:
	ImageHelper();
	~ImageHelper();
	// dummy funcs to help libjpeg
	static void JPEGInitSource(j_decompress_ptr cinfo)
	{
	}

	static boolean JPEGFillInputBuffer(j_decompress_ptr cinfo)
	{
		return 0;
	}

	static void JPEGSkipInputData(j_decompress_ptr cinfo, long num_bytes)
	{
		cinfo->src->next_input_byte += num_bytes;
		cinfo->src->bytes_in_buffer -= num_bytes;
	}

	static void JPEGTermSource(j_decompress_ptr cinfo)
	{
	}
};


class BitmapDC
{
public:
	BitmapDC();
	~BitmapDC();

	void reset();
	bool getBitmap(const char *text, int nWidth, int nHeight, Image::ETextAlign eAlignMask, const char * pFontName, uint fontSize);

public:
	unsigned char*		_data;
	int					_maxLineWidth;
	int					_maxLineHeight;

private:
	void buildLine(std::stringstream& ss, FT_Face face, int iCurXCursor, char cLastChar);

	bool divideString(FT_Face face, const char* sText, int iMaxWidth, int iMaxHeight);

	/**
	 * compute the start pos of every line
	 * return value>0 represents the start x pos of the line, while -1 means fail
	 */
	int computeLineStart(FT_Face face, Image::ETextAlign eAlignMask, char cText, int iLineIndex);

	bool startsWith(const std::string& str, const std::string& what);
	bool endsWith(const std::string& str, const std::string& what);
	std::string fileNameExtension(const std::string& pathName);
	std::string basename(const std::string& pathName);
	int openFont(const std::string& fontName, uint fontSize);

private:
	FT_Library		_library;
	FT_Face			_face ;
	std::string		_fontName ;
	uint			_fontSize ;

	int				_libError;
	int				_interval;
	std::vector<TextLine> _vLines;
};

bool BitmapDC::startsWith(const std::string& str, const std::string& what)
{
	bool result = false ;
	if( what.size() <= str.size() ) {
		result = (str.substr( 0, what.size() ) == what) ;
	}
	return result ;
}


bool BitmapDC::endsWith(const std::string& str, const std::string& what)
{
	bool result = false ;
	if( what.size() <= str.size() ) {
		result = (str.substr( str.size() - what.size() ) == what) ;
	}
	return result ;
}

std::string BitmapDC::fileNameExtension(const std::string& pathName)
{
	std::string ext ;
	std::string::size_type pos = pathName.find_last_of(".") ;
	if( pos != std::string::npos && pos != pathName.size()-1 ) {
		ext = pathName.substr(pos+1) ;
	}

	return ext ;
}

std::string BitmapDC::basename(const std::string& pathName)
{
	int pos = pathName.rfind("/"); 
	std::string bn = pathName.substr(pos + 1, pathName.length() - pos + 1);
	return bn ;
}

BitmapDC::BitmapDC() :
	_face(NULL)
	,_fontName()
	,_fontSize(0)
	,_interval(FONT_KERNING)
	,_data(NULL)
{
	_libError = FT_Init_FreeType( &_library );
	reset();
}

BitmapDC::~BitmapDC()
{
	//  free face
	if( _face ) {
		FT_Done_Face(_face);
		_face = NULL;
	}

	FT_Done_FreeType(_library);
	//data will be deleted by Image
	//	if (_data) {
	//		delete [] _data;
	//	}
}

void BitmapDC::reset()
{
	_maxLineWidth = 0;
	_maxLineHeight = 0;
	_vLines.clear();
}

void BitmapDC::buildLine(std::stringstream& ss, FT_Face face, int iCurXCursor, char cLastChar )
{
	TextLine oTempLine;
	ss << '\0';
	oTempLine.sLineStr = ss.str();
	//get last glyph
	FT_Load_Glyph(face, FT_Get_Char_Index(face, cLastChar), FT_LOAD_DEFAULT);

	oTempLine.iLineWidth =
		iCurXCursor - 
		RSHIFT6( face->glyph->metrics.horiAdvance +
		face->glyph->metrics.horiBearingX
		- face->glyph->metrics.width)/*-iInterval*/;	//TODO interval

	_maxLineWidth = MAX(_maxLineWidth, oTempLine.iLineWidth);
	ss.clear();
	ss.str("");
	_vLines.push_back(oTempLine);
}

bool BitmapDC::divideString( FT_Face face, const char* sText, int iMaxWidth, int iMaxHeight )
{
	const char* pText = sText;
	int iError = 0;
	int iCurXCursor;
	iError = FT_Load_Glyph(face, FT_Get_Char_Index(face, *pText), FT_LOAD_DEFAULT);
	if (iError) {
		return false;
	}
	iCurXCursor = -RSHIFT6(face->glyph->metrics.horiBearingX);
	//init stringstream
	std::stringstream ss;

	int cLastCh = 0;

	while (*pText != '\0') {
		if (*pText == '\n') {
			buildLine(ss, face, iCurXCursor, cLastCh);

			pText++;
			iError = FT_Load_Glyph(face, FT_Get_Char_Index(face, *pText), FT_LOAD_DEFAULT);
			if (iError) {
				return false;
			}
			iCurXCursor = -RSHIFT6(face->glyph->metrics.horiBearingX);
			continue;
		}

		iError = FT_Load_Glyph(face, FT_Get_Char_Index(face, *pText), FT_LOAD_DEFAULT);

		if (iError) {
			return false;
			//break;
		}
		//check its width
		//divide it when exceeding
		if ((iMaxWidth > 0 && iCurXCursor + RSHIFT6(face->glyph->metrics.width) > iMaxWidth)) {
			buildLine(ss, face , iCurXCursor, cLastCh);

			iCurXCursor = -RSHIFT6(face->glyph->metrics.horiBearingX);

		}

		cLastCh = *pText;
		ss << *pText;
		iCurXCursor += RSHIFT6(face->glyph->metrics.horiAdvance) + _interval;
		pText++;

/*
		if (cLastCh == ' ' || cLastCh == ',' || cLastCh == '.' || cLastCh == '!' || cLastCh == '?')
		{
			char *pText_temp = (char *)pText;
			int	iCurXCursor_temp = 0;
			while((strlen(pText_temp) > 0) && (*pText_temp!=' ') && (*pText_temp !=',') && (*pText_temp != '.') && (*pText_temp != '!') && (*pText_temp != '?') && (*pText_temp != '/0') && (*pText_temp != '/n'))
			{
				iError = FT_Load_Glyph(face, FT_Get_Char_Index(face, *pText_temp), FT_LOAD_DEFAULT);

				if (iError) {
					return false;
					//break;
				}
				iCurXCursor_temp += SHIFT6(face->glyph->metrics.horiAdvance) + iInterval;
				if (iCurXCursor + iCurXCursor_temp > iMaxWidth && iMaxWidth > 0)
				{
					buildLine(ss, face , iCurXCursor, cLastCh);

					iCurXCursor = -SHIFT6(face->glyph->metrics.horiBearingX);
				}
				pText_temp++;
			}
		}
*/
	}

	if (iError) {
		return false;
	}

	buildLine(ss,face, iCurXCursor, cLastCh);

	return true;
}

int BitmapDC::computeLineStart( FT_Face face, Image::ETextAlign eAlignMask, char cText, int iLineIndex )
{
	int iRet;
	int iError = FT_Load_Glyph(face, FT_Get_Char_Index(face, cText), FT_LOAD_DEFAULT);
	if (iError) {
		return -1;
	}

	if (eAlignMask == Image::kAlignCenter) {
		iRet = (_maxLineWidth - _vLines[iLineIndex].iLineWidth) / 2 - RSHIFT6(face->glyph->metrics.horiBearingX );

	} else if (eAlignMask == Image::kAlignRight) {
		iRet = (_maxLineWidth - _vLines[iLineIndex].iLineWidth) - RSHIFT6(face->glyph->metrics.horiBearingX );
	} else {
		// left or other situation
		iRet = -RSHIFT6(face->glyph->metrics.horiBearingX );
	}
	return iRet;
}

int BitmapDC::openFont(const std::string& fontName, uint fontSize)
{
	FT_Face aFace ;

	int iError = 0 ;
	if( _fontName != basename(fontName) || _fontSize != fontSize ) {
		iError = FT_New_Face( _library, fontName.c_str(), 0, &aFace );
		if( !iError ) {
			if(_face) {
				FT_Done_Face(_face);
			}

			_face = aFace ;
			_fontName = basename(fontName) ;
			_fontSize = fontSize ;
		}
	}

	return iError ;
}

bool BitmapDC::getBitmap( const char *text, int nWidth, int nHeight, Image::ETextAlign eAlignMask, const char * pFontName, uint fontSize )
{
	FT_Error iError;

	unsigned char cTemp ;
	int iY, iX, iTemp ;
	uint32 offset, rowOffset ;

	//data will be deleted by Image
	//		if (_data) {
	//			delete _data;
	//		}

	int iCurXCursor, iCurYCursor;
	bool bRet = false;
	if (_libError) {
		return false;
	}

	do {
		std::string fName = pFontName ;
		
		std::string ext = fileNameExtension(fName) ;
		if( ext.empty() || (ext != "ttf" && ext != "TTF") ) {
			fName += ".ttf" ;
		}

		if( !_face || (_fontName != basename(fName) || _fontSize != fontSize) ) {

			iError = openFont( fName, fontSize );
			if (iError) {		// try loading from "fonts" folder
				if( !startsWith(fName,"fonts/") ) {
				 	fName = std::string("fonts/") + fName ;
				}

				iError = openFont( fName, fontSize );
				if (iError) { //no valid font found, try to use default
					
					fName = "fonts/Marker Felt.ttf" ;
					//CCLog("No valid font, use default %s", fName.c_str());
					iError = openFont( fName, fontSize );
				}
			}
			CC_BREAK_IF(iError);

			//select utf8 charmap
			iError = FT_Select_Charmap(_face,FT_ENCODING_UNICODE);
			CC_BREAK_IF(iError);

			iError = FT_Set_Pixel_Sizes(_face, fontSize,fontSize);
			CC_BREAK_IF(iError);
		}

		iError = divideString(_face, text, nWidth, nHeight) ? 0 : 1 ;

		//compute the final line width
		_maxLineWidth = MAX(_maxLineWidth, nWidth);

		FT_Pos ascenderPixels = RSHIFT6(_face->size->metrics.ascender) ;
		FT_Pos descenderPixels = RSHIFT6(_face->size->metrics.descender) ;

		_maxLineHeight = ascenderPixels - descenderPixels;
		_maxLineHeight *= _vLines.size();

		//compute the final line height
		_maxLineHeight = MAX(_maxLineHeight, nHeight);

		uint bitmapSize = _maxLineWidth * _maxLineHeight*4 ;

		_data = new unsigned char[bitmapSize];
		memset(_data,0, bitmapSize);

		const char* pText = text;
		iCurYCursor = ascenderPixels;

		for (size_t i = 0; i < _vLines.size(); i++) {
			pText = _vLines[i].sLineStr.c_str();
			//initialize the origin cursor
			iCurXCursor = computeLineStart(_face, eAlignMask, *pText, i);

			while (*pText != 0) {
				int iError = FT_Load_Glyph(_face, FT_Get_Char_Index(_face, *pText), FT_LOAD_RENDER);
				if (iError) {
					break;
				}

				//  convert glyph to bitmap with 256 gray
				//  and get the bitmap
				FT_Bitmap & bitmap = _face->glyph->bitmap;

				FT_Pos horiBearingYPixels = RSHIFT6(_face->glyph->metrics.horiBearingY) ;
				FT_Pos horiBearingXPixels = RSHIFT6(_face->glyph->metrics.horiBearingX) ;
				FT_Pos horiAdvancePixels = RSHIFT6(_face->glyph->metrics.horiAdvance) ;

				for (int i = 0; i < bitmap.rows; ++i) {

					iY = iCurYCursor + i - horiBearingYPixels;
					if (iY < 0 || iY>=_maxLineHeight) {
						//exceed the height truncate
						continue;
					}

					rowOffset = iY * _maxLineWidth ;

					// if it has gray>0 we set show it as 1, otherwise 0 
					for (int j = 0; j < bitmap.width; ++j) {
						cTemp = bitmap.buffer[i	* bitmap.width + j];
						if( cTemp )
						{
							iX = iCurXCursor + j + horiBearingXPixels;

							offset = (rowOffset + iX) * 4 ;

							IwAssert( GAME, ((offset + 3) < bitmapSize) ) ;

							iTemp = cTemp << 24 | cTemp << 16 | cTemp << 8 | cTemp;
 							*(int*) &_data[ offset ] = iTemp ;	// ARGB
						}
					}
				}

				//step to next glyph
				iCurXCursor += horiAdvancePixels + _interval;
				pText++;
			}
			iCurYCursor += ascenderPixels - descenderPixels ;
		}

		//clear all lines
		_vLines.clear();

		//success;
		if (iError) {
			bRet = false;
		} else
			bRet = true;
	}while(0);

	return bRet;
}








static BitmapDC& sharedBitmapDC()
{
	static BitmapDC s_BmpDC;
	return s_BmpDC;
}
 
//////////////////////////////////////////////////////////////////////////
// Implement Image
//////////////////////////////////////////////////////////////////////////

Image::Image()
: _width(0)
, _height(0)
, _bitsPerComponent(0)
, _data(0)
, _hasAlpha(false)
, _preMulti(false)
{
	
}

Image::~Image()
{
    CC_SAFE_DELETE_ARRAY(_data);
}
bool Image::initWithImageFile(const char * strPath, EImageFormat eImgFmt/* = eFmtPng*/)
{
	IW_CALLSTACK("UIImage::initWithImageFile");
    bool bRet = false;
    unsigned long nSize = 0;
    unsigned char* pBuffer = FileUtils::sharedFileUtils()->getFileData(strPath, "rb", &nSize);
    if (pBuffer != NULL && nSize > 0)
    {
        bRet = initWithImageData(pBuffer, nSize, eImgFmt);
    }
    CC_SAFE_DELETE_ARRAY(pBuffer);
    return bRet;
}

bool Image::initWithImageFileThreadSafe( const char *fullpath, EImageFormat imageType /*= kFmtPng*/ )
{
	CC_UNUSED_PARAM(imageType);
    bool bRet = false;
    unsigned long nSize = 0;
    unsigned char *pBuffer = FileUtils::sharedFileUtils()->getFileData(fullpath, "rb", &nSize);
    if (pBuffer != NULL && nSize > 0)
    {
        bRet = initWithImageData(pBuffer, nSize, imageType);
    }
    CC_SAFE_DELETE_ARRAY(pBuffer);
    return bRet;
}

bool Image::initWithImageData(void * pData, 
								int nDataLen, 
								EImageFormat eFmt,
								int nWidth,
								int nHeight,
								int nBitsPerComponent)
{
    bool bRet = false;
    do 
    {
    	CC_BREAK_IF(! pData || nDataLen <= 0);
		
        if (kFmtPng == eFmt)
        {
            bRet = _initWithPngData(pData, nDataLen);
            break;
        }
        else if (kFmtJpg == eFmt)
        {
            bRet = _initWithJpgData(pData, nDataLen);
            break;
        }
        else if (kFmtTiff == eFmt)
        {
            bRet = _initWithTiffData(pData, nDataLen);
            break;
        }
        else if (kFmtWebp == eFmt)
        {
            bRet = _initWithWebpData(pData, nDataLen);
            break;
        }
        else if (kFmtRawData == eFmt)
        {
            bRet = initWithRawData(pData, nDataLen, nWidth, nHeight, nBitsPerComponent, false);
            break;
        }
        else
        {
            // if it is a png file buffer.
            if (nDataLen > 8)
            {
                unsigned char* pHead = (unsigned char*)pData;
                if (   pHead[0] == 0x89
                    && pHead[1] == 0x50
                    && pHead[2] == 0x4E
                    && pHead[3] == 0x47
                    && pHead[4] == 0x0D
                    && pHead[5] == 0x0A
                    && pHead[6] == 0x1A
                    && pHead[7] == 0x0A)
                {
                    bRet = _initWithPngData(pData, nDataLen);
                    break;
                }
            }

            // if it is a tiff file buffer.
            if (nDataLen > 2)
            {
                unsigned char* pHead = (unsigned char*)pData;
                if (  (pHead[0] == 0x49 && pHead[1] == 0x49)
                    || (pHead[0] == 0x4d && pHead[1] == 0x4d)
                    )
                {
                    bRet = _initWithTiffData(pData, nDataLen);
                    break;
                }
            }

            // if it is a jpeg file buffer.
            if (nDataLen > 2)
            {
                unsigned char* pHead = (unsigned char*)pData;
                if (   pHead[0] == 0xff
                    && pHead[1] == 0xd8)
                {
                    bRet = _initWithJpgData(pData, nDataLen);
                    break;
                }
            }
        }
	} while (0);
    return bRet;
}

bool Image::_initWithJpgData(void * data, int nSize)
{	
	IW_CALLSTACK("CCImage::_initWithJpgData");

    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = {0};
    unsigned long location = 0;
    unsigned int i = 0;

    bool bRet = false;
    do 
    {
        /* here we set up the standard libjpeg error handler */
        cinfo.err = jpeg_std_error( &jerr );

        /* setup decompression process and source, then read JPEG header */
        jpeg_create_decompress( &cinfo );

	    jpeg_source_mgr srcmgr;

		srcmgr.bytes_in_buffer = nSize;
		srcmgr.next_input_byte = (JOCTET*) data;
		srcmgr.init_source = ImageHelper::JPEGInitSource;
		srcmgr.fill_input_buffer = ImageHelper::JPEGFillInputBuffer;
		srcmgr.skip_input_data = ImageHelper::JPEGSkipInputData;
		srcmgr.resync_to_restart = jpeg_resync_to_restart;
		srcmgr.term_source = ImageHelper::JPEGTermSource;
		cinfo.src = &srcmgr;
//      jpeg_mem_src( &cinfo, (unsigned char *) data, nSize );

        /* reading the image header which contains image information */
        jpeg_read_header( &cinfo, true );

        // we only support RGB or grayscale
        if (cinfo.jpeg_color_space != JCS_RGB)
		{
            if (cinfo.jpeg_color_space == JCS_GRAYSCALE || cinfo.jpeg_color_space == JCS_YCbCr)
            {
                cinfo.out_color_space = JCS_RGB;
		    }
        }
        else
        {
            break;
        }

        /* Start decompression jpeg here */
        jpeg_start_decompress( &cinfo );

        /* init image info */
        _width  = (short)(cinfo.image_width);
        _height = (short)(cinfo.image_height);
		_hasAlpha = false;
        _preMulti = false;
        _bitsPerComponent = 8;
        row_pointer[0] = new unsigned char[cinfo.output_width*cinfo.output_components];
        CC_BREAK_IF(! row_pointer[0]);

        _data = new unsigned char[cinfo.output_width*cinfo.output_height*cinfo.output_components];
        CC_BREAK_IF(! _data);

        /* now actually read the jpeg into the raw buffer */
        /* read one scan line at a time */
        while( cinfo.output_scanline < cinfo.image_height )
		{
            jpeg_read_scanlines( &cinfo, row_pointer, 1 );
            for( i=0; i<cinfo.image_width*cinfo.output_components;i++) 
            {
                _data[location++] = row_pointer[0][i];
			}
		}

        jpeg_finish_decompress( &cinfo );
		jpeg_destroy_decompress(&cinfo);
        /* wrap up decompression, destroy objects, free pointers and close open files */        
		bRet = true;
    } while (0);

    CC_SAFE_DELETE_ARRAY(row_pointer[0]);
	return bRet;
}

void userReadData(png_structp pngPtr, png_bytep data, png_size_t length) {
	png_voidp png_pointer = png_get_io_ptr(pngPtr);
	s3eFileRead((char*)data, length, 1, (s3eFile*)png_pointer);
}

#define PNGSIGSIZE 8
bool Image::_initWithPngData(void * pData, int nDatalen)
{
	IW_CALLSTACK("CCImage::_initWithPngData");
	
    bool bRet = false;
	
	s3eFile* pFile = s3eFileOpenFromMemory(pData, nDatalen);
	
	IwAssert(GAME, pFile);
	
	png_byte pngsig[PNGSIGSIZE];
	
	bool is_png = false;
	
	s3eFileRead((char*)pngsig, PNGSIGSIZE, 1, pFile);
	
	is_png = png_sig_cmp(pngsig, 0, PNGSIGSIZE) == 0 ? true : false;
	
	if (!is_png)
		return false;
	
	png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	
	if (!pngPtr)
		return false;
	
	png_infop infoPtr = png_create_info_struct(pngPtr);
	
	if (!infoPtr)
		return false;
	
	png_bytep* rowPtrs = NULL;
	_data = NULL;
	
	if (setjmp(png_jmpbuf(pngPtr))) {
		png_destroy_read_struct(&pngPtr, &infoPtr,(png_infopp)0);
		if (rowPtrs != NULL) delete [] rowPtrs;
		if (_data != NULL) delete [] _data;
		
		CCLog("ERROR: An error occured while reading the PNG file");
		
		return false;
	}
	
	png_set_read_fn(pngPtr, pFile, userReadData);
	png_set_sig_bytes(pngPtr, PNGSIGSIZE);
	png_read_info(pngPtr, infoPtr);
	
	
	png_uint_32 bitdepth   = png_get_bit_depth(pngPtr, infoPtr);
	png_uint_32 channels   = png_get_channels(pngPtr, infoPtr);
	png_uint_32 color_type = png_get_color_type(pngPtr, infoPtr);
	
	// Convert palette color to true color
	if (color_type ==PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(pngPtr);
	
	// Convert low bit colors to 8 bit colors
	if (png_get_bit_depth(pngPtr, infoPtr) < 8)
	{
		if (color_type==PNG_COLOR_TYPE_GRAY || color_type==PNG_COLOR_TYPE_GRAY_ALPHA)
			png_set_gray_1_2_4_to_8(pngPtr);
		else
			png_set_packing(pngPtr);
	}

	if (png_get_valid(pngPtr, infoPtr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(pngPtr);
	
	// Convert high bit colors to 8 bit colors
	if (bitdepth == 16)
		png_set_strip_16(pngPtr);
	
	// Convert gray color to true color
	if (color_type==PNG_COLOR_TYPE_GRAY || color_type==PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(pngPtr);
	
	// Update the changes
	png_read_update_info(pngPtr, infoPtr);
	
	// init image info
	_preMulti	= true;
	
	unsigned int bytesPerComponent = png_get_channels(pngPtr, infoPtr);
	
	_hasAlpha = (bytesPerComponent == 4 ? true : false);
	
	_height = (unsigned int)png_get_image_height(pngPtr, infoPtr);
	_width = (unsigned int) png_get_image_width(pngPtr, infoPtr);
	
	_bitsPerComponent = (unsigned int)png_get_bit_depth(pngPtr, infoPtr);
	
	_data = new unsigned char[_height * _width * bytesPerComponent];
	
	unsigned int bytesPerRow = _width * bytesPerComponent;
	
	{
		unsigned char *ptr = _data;
		rowPtrs = new png_bytep[_height];
				
		for (int i = 0; i < _height; i++) {
			
			int q = (i) * bytesPerRow;
			
			rowPtrs[i] = (png_bytep)_data + q;
		}
		
		png_read_image(pngPtr, rowPtrs);
		
		delete[] (png_bytep)rowPtrs;
		png_destroy_read_struct(&pngPtr, &infoPtr,(png_infopp)0);
		
		s3eFileClose(pFile);
		pFile = 0;
	}
	
	// premultiplay if alpha
	if(_hasAlpha)
		for(unsigned int i = 0; i < _height*bytesPerRow; i += bytesPerComponent){
			*(_data + i + 0)	=  (*(_data + i + 0) * *(_data + i + 3) + 1) >> 8;
			*(_data + i + 1)	=  (*(_data + i + 1) * *(_data + i + 3) + 1) >> 8;					
			*(_data + i + 2)	=  (*(_data + i + 2) * *(_data + i + 3) + 1) >> 8;
			*(_data + i + 3)	=   *(_data + i + 3);
	}
	

	
	bRet = true;
    return bRet;
}

bool Image::initWithString(
							 const char *    pText, 
							 int             nWidth/* = 0*/, 
							 int             nHeight/* = 0*/,
							 ETextAlign      eAlignMask/* = kAlignCenter*/,
							 const char *    pFontName/* = nil*/,
							 int             nSize/* = 0*/)
{
	bool bRet = false;
	do
	{
		CC_BREAK_IF(! pText);

		BitmapDC &dc = sharedBitmapDC();

        std::string fullFontName = pFontName;
    	std::string lowerCasePath = fullFontName;
    	std::transform(lowerCasePath.begin(), lowerCasePath.end(), lowerCasePath.begin(), ::tolower);
        
    	if ( lowerCasePath.find(".ttf") != std::string::npos ) {
    		fullFontName = FileUtils::sharedFileUtils()->fullPathForFilename(pFontName);
    	}

		CC_BREAK_IF(! dc.getBitmap(pText, nWidth, nHeight, eAlignMask, fullFontName.c_str(), nSize));

		// assign the dc._data to _data in order to save time
		_data = dc._data;
		CC_BREAK_IF(! _data);

		_width = (short)dc._maxLineWidth;
		_height = (short)dc._maxLineHeight;
		_hasAlpha = true;
		_preMulti = true;
		_bitsPerComponent = 8;

		bRet = true;

		dc.reset();
	}while (0);

	//do nothing
	return bRet; 
}

static tmsize_t _tiffReadProc(thandle_t fd, void* buf, tmsize_t size)
{
    tImageSource* isource = (tImageSource*)fd;
    uint8* ma;
    uint64 mb;
    unsigned long n;
    unsigned long o;
    tmsize_t p;
    ma=(uint8*)buf;
    mb=size;
    p=0;
    while (mb>0)
    {
        n=0x80000000UL;
        if ((uint64)n>mb)
            n=(unsigned long)mb;


        if((int)(isource->offset + n) <= isource->size)
        {
            memcpy(ma, isource->data+isource->offset, n);
            isource->offset += n;
            o = n;
        }
        else
        {
            return 0;
        }

        ma+=o;
        mb-=o;
        p+=o;
        if (o!=n)
        {
            break;
        }
    }
    return p;
}

static tmsize_t _tiffWriteProc(thandle_t fd, void* buf, tmsize_t size)
{
    CC_UNUSED_PARAM(fd);
    CC_UNUSED_PARAM(buf);
    CC_UNUSED_PARAM(size);
    return 0;
}


static uint64 _tiffSeekProc(thandle_t fd, uint64 off, int whence)
{
    tImageSource* isource = (tImageSource*)fd;
    uint64 ret = -1;
    do 
    {
        if (whence == SEEK_SET)
        {
            CC_BREAK_IF(off > isource->size-1);
            ret = isource->offset = (uint32)off;
        }
        else if (whence == SEEK_CUR)
        {
            CC_BREAK_IF(isource->offset + off > isource->size-1);
            ret = isource->offset += (uint32)off;
        }
        else if (whence == SEEK_END)
        {
            CC_BREAK_IF(off > isource->size-1);
            ret = isource->offset = (uint32)(isource->size-1 - off);
        }
        else
        {
            CC_BREAK_IF(off > isource->size-1);
            ret = isource->offset = (uint32)off;
        }
    } while (0);

    return ret;
}

static uint64 _tiffSizeProc(thandle_t fd)
{
    tImageSource* pImageSrc = (tImageSource*)fd;
    return pImageSrc->size;
}

static int _tiffCloseProc(thandle_t fd)
{
    CC_UNUSED_PARAM(fd);
    return 0;
}

static int _tiffMapProc(thandle_t fd, void** pbase, toff_t* psize)
{
    CC_UNUSED_PARAM(fd);
    CC_UNUSED_PARAM(pbase);
    CC_UNUSED_PARAM(psize);
    return 0;
}

static void _tiffUnmapProc(thandle_t fd, void* base, toff_t size)
{
    CC_UNUSED_PARAM(fd);
    CC_UNUSED_PARAM(base);
    CC_UNUSED_PARAM(size);
}

bool Image::_initWithTiffData(void* pData, int nDataLen)
{
    bool bRet = false;
    do 
    {
        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = (unsigned char*)pData;
        imageSource.size    = nDataLen;
        imageSource.offset  = 0;

        TIFF* tif = TIFFClientOpen("file.tif", "r", (thandle_t)&imageSource, 
            _tiffReadProc, _tiffWriteProc,
            _tiffSeekProc, _tiffCloseProc, _tiffSizeProc,
            _tiffMapProc,
            _tiffUnmapProc);

        CC_BREAK_IF(NULL == tif);

        uint32 w = 0, h = 0;
        uint16 bitsPerSample = 0, samplePerPixel = 0, planarConfig = 0;
        size_t npixels = 0;
        
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplePerPixel);
        TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &planarConfig);

        npixels = w * h;
        
        _hasAlpha = true;
        _width = w;
        _height = h;
        _bitsPerComponent = 8;

        _data = new unsigned char[npixels * sizeof (uint32)];

        uint32* raster = (uint32*) _TIFFmalloc(npixels * sizeof (uint32));
        if (raster != NULL) 
        {
           if (TIFFReadRGBAImageOriented(tif, w, h, raster, ORIENTATION_TOPLEFT, 0))
           {
                unsigned char* src = (unsigned char*)raster;
                unsigned int* tmp = (unsigned int*)_data;

                /* the raster data is pre-multiplied by the alpha component 
                   after invoking TIFFReadRGBAImageOriented
                for(int j = 0; j < _width * _height * 4; j += 4)
                {
                    *tmp++ = CC_RGB_PREMULTIPLY_ALPHA( src[j], src[j + 1], 
                        src[j + 2], src[j + 3] );
                }
                */
                _preMulti = true;

               memcpy(_data, raster, npixels*sizeof (uint32));
           }

          _TIFFfree(raster);
        }
        

        TIFFClose(tif);

        bRet = true;
    } while (0);
    return bRet;
}

bool Image::initWithRawData(void * pData, int nDatalen, int nWidth, int nHeight, int nBitsPerComponent, bool bPreMulti)
{
    bool bRet = false;
    do 
    {
        CC_BREAK_IF(0 == nWidth || 0 == nHeight);

        _bitsPerComponent = nBitsPerComponent;
        _height   = (short)nHeight;
        _width    = (short)nWidth;
        _hasAlpha = true;

        // only RGBA8888 supported
        int nBytesPerComponent = 4;
        int nSize = nHeight * nWidth * nBytesPerComponent;
        _data = new unsigned char[nSize];
        CC_BREAK_IF(! _data);
        memcpy(_data, pData, nSize);

        bRet = true;
    } while (0);
    return bRet;
}

bool Image::saveToFile(const char *pszFilePath, bool bIsToRGB)
{
	// todo
	return false;
}

bool Image::_saveImageToPNG(const char * pszFilePath, bool bIsToRGB)
{
	// todo
	return false;
}

bool Image::_saveImageToJPG(const char * pszFilePath)
{
	// todo
	return false;
}

NS_CC_END;
