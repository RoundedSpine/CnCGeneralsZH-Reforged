/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Copyright (C) Electronic Arts Canada Inc. 1995-2002.  All rights reserved.

#ifndef __REFREAD
#define __REFREAD 1

#include <string.h>
#include <limits.h>
#include "codex.h"
#include "refcodex.h"

/****************************************************************/
/*  Information Functions                                       */
/****************************************************************/

/* check for reasonable header: */
/* 10fb header */

bool GCALL REF_is(const void *compresseddata)
{
    bool ok=false;
    int packtype=ggetm(compresseddata,2);

    if (packtype==0x10fb
     || packtype==0x11fb
     || packtype==0x90fb
     || packtype==0x91fb)
        ok = true;

    return(ok);
}


/****************************************************************/
/*  Decode Functions                                            */
/****************************************************************/

int GCALL REF_size(const void *compresseddata)
{
    int len=0;
    int packtype=ggetm(compresseddata,2);
    int ssize=(packtype&0x8000)?4:3;

    if (packtype&0x100)     /* 11fb */
    {
        len = ggetm((char *)compresseddata+2+ssize,ssize);
    }
    else                    /* 10fb */
    {
        len = ggetm((char *)compresseddata+2,ssize);
    }

    return(len);
}


int GCALL REF_decode(void *dest, int destsize, const void *compresseddata, int sourcesize,
    int *compressedsize)
{
    if (compressedsize) *compressedsize = 0;
    if (!compresseddata || !dest || sourcesize < 2 || destsize < 0)
        return 0;

    const unsigned char *s = (const unsigned char *)compresseddata;
    unsigned char *d = (unsigned char *)dest;
    const unsigned int type = ((unsigned int)s[0] << 8) | s[1];
    if (type != 0x10fb && type != 0x11fb && type != 0x90fb && type != 0x91fb)
        return 0;

    const int sizebytes = (type & 0x8000) ? 4 : 3;
    int input = 2 + ((type & 0x100) ? sizebytes : 0);
    if (sourcesize - input < sizebytes)
        return 0;
    unsigned int declared = 0;
    for (int i = 0; i < sizebytes; ++i)
        declared = (declared << 8) | s[input++];
    if (declared > (unsigned int)INT_MAX || declared > (unsigned int)destsize)
        return 0;
    const int limit = (int)declared;
    int output = 0;

    for (;;)
    {
        if (input == sourcesize)
            return 0; // Every stream must end with an EOF command.
        const unsigned int first = s[input++];
        int literals = 0, match = 0, distance = 0;
        bool eof = false;
        if (!(first & 0x80))
        {
            if (sourcesize - input < 1) return 0;
            const unsigned int second = s[input++];
            literals = first & 3;
            distance = 1 + ((first & 0x60) << 3) + second;
            match = ((first & 0x1c) >> 2) + 3;
        }
        else if (!(first & 0x40))
        {
            if (sourcesize - input < 2) return 0;
            const unsigned int second = s[input++];
            const unsigned int third = s[input++];
            literals = second >> 6;
            distance = 1 + ((second & 0x3f) << 8) + third;
            match = (first & 0x3f) + 4;
        }
        else if (!(first & 0x20))
        {
            if (sourcesize - input < 3) return 0;
            const unsigned int second = s[input++];
            const unsigned int third = s[input++];
            const unsigned int fourth = s[input++];
            literals = first & 3;
            distance = 1 + ((first & 0x10) << 12) + (second << 8) + third;
            match = ((first & 0x0c) << 6) + fourth + 5;
        }
        else if (first < 0xfc)
        {
            literals = ((first & 0x1f) << 2) + 4;
        }
        else
        {
            literals = first & 3;
            eof = true;
        }

        // Validate the entire command before copying. References may overlap the
        // new output, but their initial byte must already have been produced.
        if (literals > sourcesize - input || literals > limit - output)
            return 0;
        if (match > limit - output - literals || (match && distance > output + literals))
            return 0;
        if (literals) memcpy(d + output, s + input, literals);
        input += literals;
        output += literals;
        for (int i = 0; i < match; ++i, ++output)
            d[output] = d[output - distance];
        if (eof)
        {
            if (output != limit) return 0;
            if (compressedsize) *compressedsize = input;
            return output;
        }
    }
}

#endif
