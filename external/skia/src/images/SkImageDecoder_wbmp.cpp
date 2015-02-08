/**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/

#include "SkImageDecoder.h"
#include "SkColor.h"
#include "SkColorPriv.h"
#include "SkMath.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkUtils.h"
//add for sample
#include "SkScaledBitmapSampler.h"
#include "SkCanvas.h"


class SkWBMPImageDecoder : public SkImageDecoder {
public:
    virtual Format getFormat() const {
        return kWBMP_Format;
    }
    
protected:
    virtual bool onDecode(SkStream* stream, SkBitmap* bm, Mode);
};

static bool read_byte(SkStream* stream, uint8_t* data)
{
    return stream->read(data, 1) == 1;
}

static bool read_mbf(SkStream* stream, int* value)
{
    int n = 0;
    uint8_t data;
    do {
        if (!read_byte(stream, &data)) {
            return false;
        }
        n = (n << 7) | (data & 0x7F);
    } while (data & 0x80);
    
    *value = n;
    return true;
}

struct wbmp_head {
    int fWidth;
    int fHeight;
    
    bool init(SkStream* stream)
    {
        uint8_t data;
        
        if (!read_byte(stream, &data) || data != 0) { // unknown type
            return false;
        }
        if (!read_byte(stream, &data) || (data & 0x9F)) { // skip fixed header
            return false;
        }
        if (!read_mbf(stream, &fWidth) || (unsigned)fWidth > 0xFFFF) {
            return false;
        }
        if (!read_mbf(stream, &fHeight) || (unsigned)fHeight > 0xFFFF) {
            return false;
        }
        return fWidth != 0 && fHeight != 0;
    }
};
    
static void expand_bits_to_bytes(uint8_t dst[], const uint8_t src[], int bits)
{
    int bytes = bits >> 3;
    
    for (int i = 0; i < bytes; i++) {
        unsigned mask = *src++;
        dst[0] = (mask >> 7) & 1;
        dst[1] = (mask >> 6) & 1;
        dst[2] = (mask >> 5) & 1;
        dst[3] = (mask >> 4) & 1;
        dst[4] = (mask >> 3) & 1;
        dst[5] = (mask >> 2) & 1;
        dst[6] = (mask >> 1) & 1;
        dst[7] = (mask >> 0) & 1;
        dst += 8;
    }
    
    bits &= 7;
    if (bits > 0) {
        unsigned mask = *src;
        do {
            *dst++ = (mask >> 7) & 1;;
            mask <<= 1;
        } while (--bits != 0);    
    }
}

#define SkAlign8(x)     (((x) + 7) & ~7)

bool SkWBMPImageDecoder::onDecode(SkStream* stream, SkBitmap* decodedBitmap,
                                  Mode mode)
{
    wbmp_head   head;
    
    if (!head.init(stream)) {
        return false;
    }
        
    int width = head.fWidth;
    int height = head.fHeight;
    
    //draw to scale and change config
    SkBitmap::Config config = this->getPrefConfig(k32Bit_SrcDepth, false);
    if (SkBitmap::kARGB_4444_Config != config &&
            SkBitmap::kRGB_565_Config != config) {
        config = SkBitmap::kARGB_8888_Config;
    }

    SkScaledBitmapSampler sampler(width, height, getSampleSize());

    decodedBitmap->setConfig(config, sampler.scaledWidth(), sampler.scaledHeight());
    decodedBitmap->setIsOpaque(true);

    if (SkImageDecoder::kDecodeBounds_Mode == mode) {
        return true;
    }

    // assign these directly, in case we return kDimensions_Result
    SkBitmap tmpBitmap;
    tmpBitmap.setConfig(SkBitmap::kIndex8_Config, width, height);
    tmpBitmap.setIsOpaque(true);
    
    const SkPMColor colors[] = { SK_ColorBLACK, SK_ColorWHITE };
    SkColorTable* ct = SkNEW_ARGS(SkColorTable, (colors, 2));
    SkAutoUnref   aur(ct);

    if (!tmpBitmap.allocPixels(ct)) {
        return false;
    }

    //AutoLock and unlock
    {
        SkAutoLockPixels alp(tmpBitmap);

        uint8_t* dst = tmpBitmap.getAddr8(0, 0);
        // store the 1-bit valuess at the end of our pixels, so we won't stomp
        // on them before we're read them. Just trying to avoid a temp allocation
        size_t srcRB = SkAlign8(width) >> 3;
        size_t srcSize = height * srcRB;
        uint8_t* src = dst + tmpBitmap.getSize() - srcSize;
        if (stream->read(src, srcSize) != srcSize) {
            return false;
        }

        for (int y = 0; y < height; y++)
        {
            expand_bits_to_bytes(dst, src, width);
            dst += tmpBitmap.rowBytes();
            src += srcRB;
        }

        if (!this->allocPixelRef(decodedBitmap, NULL)) {
            return false;
        }
        SkAutoLockPixels alp2(*decodedBitmap);
        SkCanvas canvas(*decodedBitmap);
        SkRect rect;
        rect.iset(0, 0, decodedBitmap->width(), decodedBitmap->height());
        SkPaint paint;
        //we filter bitamp when draw to achieve good image quality
        paint.setFilterBitmap(true);
        canvas.drawBitmapRect(tmpBitmap, NULL, rect, &paint); 
        
    }//AutoLock
    tmpBitmap.reset(); 
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#include "SkTRegistry.h"

static SkImageDecoder* Factory(SkStream* stream) {
    wbmp_head   head;

    if (head.init(stream)) {
        return SkNEW(SkWBMPImageDecoder);
    }
    return NULL;
}

static SkTRegistry<SkImageDecoder*, SkStream*> gReg(Factory);

