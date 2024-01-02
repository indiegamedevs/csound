/*
    vaops.c:

    Copyright (C) 2006 Steven Yi

    This file is part of Csound.

    The Csound Library is free software; you can redistribute it
    and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Csound is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with Csound; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
    02110-1301 USA
*/

#include "csoundCore.h"
#include "interlocks.h"

#define MYFLOOR(x) (x >= FL(0.0) ? (int32)x : (int32)((double)x - 0.99999999))

typedef struct {
        OPDS    h;
        MYFLT   *kout, *kindx, *avar;
} VA_GET;

typedef struct {
        OPDS    h;
        MYFLT   *kval, *kindx, *avar;
} VA_SET;


typedef struct {
        OPDS    h;
        MYFLT   *kout, *avar, *kindx;
} VASIG_GET;

typedef struct {
        OPDS    h;
        MYFLT   *avar, *kval, *kindx;
} VASIG_SET;

static int32_t vaget(CSOUND *csound, VA_GET *p)
{
    int32 ndx = (int32) MYFLOOR((double)*p->kindx);
    uint32_t offset = p->h.insdshead->ksmps_offset;
    uint32_t early  = p->h.insdshead->ksmps_no_end;
    if (UNLIKELY(ndx<(int32)offset || ndx>=(int32)(CS_KSMPS-early)))
      return csound->PerfError(csound, &(p->h),
                               Str("Out of range in vaget (%d)"), ndx);
    *p->kout = p->avar[ndx];
    return OK;
}

static int32_t vaset(CSOUND *csound, VA_SET *p)
{
    int32 ndx = (int32) MYFLOOR((double)*p->kindx);
    uint32_t offset = p->h.insdshead->ksmps_offset;
    uint32_t early  = p->h.insdshead->ksmps_no_end;
    if (UNLIKELY(ndx<(int32)offset || ndx>=(int32)(CS_KSMPS-early)))
      return csound->PerfError(csound, &(p->h),
                               Str("Out of range in vaset (%d)"), ndx);
    p->avar[ndx] = *p->kval;
    return OK;
}


static int32_t vasigget(CSOUND *csound, VASIG_GET *p)
{
    int32 ndx = (int32) MYFLOOR((double)*p->kindx);
    uint32_t offset = p->h.insdshead->ksmps_offset;
    uint32_t early  = p->h.insdshead->ksmps_no_end;
    if (UNLIKELY(ndx<(int32)offset || ndx>=(int32)(CS_KSMPS-early)))
      return csound->PerfError(csound, &(p->h),
                               Str("Out of range in vaget (%d)"), ndx);
    *p->kout = p->avar[ndx];
    return OK;
}

static int32_t vasigset(CSOUND *csound, VASIG_SET *p)
{
    int32 ndx = (int32) MYFLOOR((double)*p->kindx);
    uint32_t offset = p->h.insdshead->ksmps_offset;
    uint32_t early  = p->h.insdshead->ksmps_no_end;
    if (UNLIKELY(ndx<(int32)offset || ndx>=(int32)(CS_KSMPS-early)))
      return csound->PerfError(csound, &(p->h),
                               Str("Out of range in vaset (%d)"), ndx);
    p->avar[ndx] = *p->kval;
    return OK;
}

#define S(x)    sizeof(x)

static OENTRY vaops_localops[] = {
  { "vaget", S(VA_GET),    0, 2,      "k", "ka",  NULL, (SUBR)vaget, NULL, NULL},
  { "vaset", S(VA_SET),    WI, 2,      "",  "kka", NULL, (SUBR)vaset, NULL, NULL},
  { "##array_get", S(VASIG_GET),    0, 2,      "k", "ak",  NULL, (SUBR)vasigget, NULL, NULL},
  { "##array_set", S(VASIG_SET),    0, 2,      "",  "akk", NULL, (SUBR)vasigset, NULL, NULL}
};


LINKAGE_BUILTIN(vaops_localops)

