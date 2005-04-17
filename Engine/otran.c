/*
    otran.c:

    Copyright (C) 1991, 1997, 2003 Barry Vercoe, John ffitch, Istvan Varga

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

#include "cs.h"                 /*                              OTRAN.C */
#include "oload.h"
#include <math.h>
#include <string.h>
#include "pstream.h"
#include "namedins.h"           /* IV - Oct 31 2002 */
#include <ctype.h>

static struct namepool {
    NAME                *names;
    NAME                *nxtslot;
    NAME                *namlim;
    struct namepool     *next;
} gbl, lcl;
static  int     gblsize = GNAMES, lclsize = LNAMES;
static  ARGLST  *nullist;
static  ARGOFFS *nulloffs;
static  int     lclkcnt, lcldcnt, lclwcnt, lclfixed;
static  int     lclpcnt, lclnxtpcnt;
static  int     lclnxtkcnt, lclnxtdcnt, lclnxtwcnt, lclnxtacnt;
static  int     gblnxtkcnt = 0, gblnxtacnt = 0, gblfixed, gblacount;
static  int     *nxtargoffp, *argofflim, lclpmax;
static  char    *strargspace, *strargptr;
static  long    poolcount, strargsize = 0, argoffsize;
static  int     nconsts;
static  int     displop1, displop2, displop3;

extern  OPARMS  O;

static  int     gexist(char *), gbloffndx(char *), lcloffndx(char *);
static  int     constndx(char *);
static  void    insprep(INSTRTXT *), lgbuild(char *);
#define txtcpy(a,b) memcpy(a,b,sizeof(TEXT));
static  void    gblnamset(char *);
static  int     plgndx(char *);
static  NAME    *lclnamset(char *);
void    putop(TEXT*);


extern  void    rdorchfile(ENVIRON*);
extern  int     getopnum(char *);

extern  void    (*spinrecv)(void*), (*spoutran)(void*);
extern  void    spoutsf(void*);

#define KTYPE   1
#define DTYPE   2
#define WTYPE   3
#define ATYPE   4
#define Dfloats (sizeof(DOWNDAT)/sizeof(MYFLT))
#define Wfloats (sizeof(SPECDAT)/sizeof(MYFLT))
#define PTYPE   5
#define Pfloats (sizeof(PVSDAT) / sizeof(MYFLT))

void csoundDefaultSpinRecv(void *csound)
{
    csound = csound;
}

void csoundDefaultSpouTran(void *csound)
{
    csound = csound;
}

void tranRESET(ENVIRON *csound)
{
    spinrecv            = csoundDefaultSpinRecv;
    spoutran            = spoutsf;
    gblsize             = GNAMES;
    lclsize             = LNAMES;
    nullist             = NULL;
    nulloffs            = NULL;
    nxtargoffp          = argofflim = NULL;
    strargspace         = strargptr = NULL;
    gblnxtkcnt          = 0;
    gblnxtacnt          = 0;
    strargsize          = 0L;
    csound->tran_sr     = FL(-1.0);
    csound->tran_kr     = FL(-1.0);
    csound->tran_ksmps  = FL(-1.0);
    csound->tran_nchnls = DFLT_NCHNLS;
    csound->tran_0dbfs  = DFLT_DBFS;
    csound->nlabels     = NLABELS;
    /* IV - Oct 12 2002: free all instrument names */
    while (csound->opcodeInfo != NULL) {
      OPCODINFO *inm = csound->opcodeInfo->prv;
      /* note: out_ndx_list should not be mfree'd */
      if (csound->opcodeInfo->in_ndx_list != NULL)
        mfree(csound, csound->opcodeInfo->in_ndx_list);
      mfree(csound, csound->opcodeInfo);
      csound->opcodeInfo = inm;
    }
    named_instr_free(csound);        /* IV - Oct 31 2002 */
    opcode_list_free(csound);
}


/* IV - Oct 12 2002: new function to parse arguments of opcode definitions */

static int parse_opcode_args(ENVIRON *csound, OENTRY *opc)
{
    OPCODINFO   *inm = (OPCODINFO*) opc->useropinfo;
    char    *types, *otypes;
    int     i, i_incnt, a_incnt, k_incnt, i_outcnt, a_outcnt, k_outcnt, err;
    short   *a_inlist, *k_inlist, *i_inlist, *a_outlist, *k_outlist, *i_outlist;

    /* count the number of arguments, and check types */
    i = i_incnt = a_incnt = k_incnt = i_outcnt = a_outcnt = k_outcnt = err = 0;
    types = inm->intypes; otypes = opc->intypes;
    if (!strcmp(types, "0")) types++;   /* no input args */
    while (*types) {
      switch (*types) {
      case 'a':
        a_incnt++; *otypes++ = *types;
        break;
      case 'K':
        i_incnt++;              /* also updated at i-time */
      case 'k':
        k_incnt++; *otypes++ = 'k';
        break;
      case 'i':
      case 'o':
      case 'p':
      case 'j':
        i_incnt++; *otypes++ = *types;
        break;
      default:
        sprintf(errmsg, "invalid input type for opcode %s", inm->name);
        synterr(errmsg); err++;
      }
      i++; types++;
      if (i > OPCODENUMOUTS) {
        sprintf(errmsg, "too many input args for opcode %s", inm->name);
        synterr(errmsg); err++; break;
      }
    }
    *otypes++ = 'o'; *otypes = '\0';    /* optional arg for local ksmps */
    inm->inchns = strlen(opc->intypes) - 1; /* total number of input chnls */
    inm->perf_incnt = a_incnt + k_incnt;
    /* same for outputs */
    i = 0;
    types = inm->outtypes; otypes = opc->outypes;
    if (!strcmp(types, "0")) types++;   /* no output args */
    while (*types) {
      switch (*types) {
      case 'a':
        a_outcnt++; *otypes++ = *types;
        break;
      case 'K':
        i_outcnt++;             /* also updated at i-time */
      case 'k':
        k_outcnt++; *otypes++ = 'k';
        break;
      case 'i':
        i_outcnt++; *otypes++ = *types;
        break;
      default:
        sprintf(errmsg, "invalid output type for opcode %s", inm->name);
        synterr(errmsg); err++;
      }
      i++; types++;
      if (i >= OPCODENUMOUTS) {
        sprintf(errmsg, "too many output args for opcode %s", inm->name);
        synterr(errmsg); err++; break;
      }
    }
    *otypes = '\0';
    inm->outchns = strlen(opc->outypes);    /* total number of output chnls */
    inm->perf_outcnt = a_outcnt + k_outcnt;
    /* now build index lists for the various types of arguments */
    i = i_incnt + inm->perf_incnt + i_outcnt + inm->perf_outcnt;
    i_inlist = inm->in_ndx_list = (short*) mmalloc(csound,
                                                   sizeof(short) * (i + 6));
    a_inlist = i_inlist + i_incnt + 1;
    k_inlist = a_inlist + a_incnt + 1;
    i = 0; types = inm->intypes;
    while (*types) {
      switch (*types++) {
        case 'a': *a_inlist++ = i; break;
        case 'k': *k_inlist++ = i; break;
        case 'K': *k_inlist++ = i;      /* also updated at i-time */
        case 'i':
        case 'o':
        case 'p':
        case 'j': *i_inlist++ = i;
      }
      i++;
    }
    *i_inlist = *a_inlist = *k_inlist = -1;     /* put delimiters */
    i_outlist = inm->out_ndx_list = k_inlist + 1;
    a_outlist = i_outlist + i_outcnt + 1;
    k_outlist = a_outlist + a_outcnt + 1;
    i = 0; types = inm->outtypes;
    while (*types) {
      switch (*types++) {
        case 'a': *a_outlist++ = i; break;
        case 'k': *k_outlist++ = i; break;
        case 'K': *k_outlist++ = i;     /* also updated at i-time */
        case 'i': *i_outlist++ = i;
      }
      i++;
    }
    *i_outlist = *a_outlist = *k_outlist = -1;  /* put delimiters */
    return err;
}

static int pnum(char *s)        /* check a char string for pnum format  */
                                /*   and return the pnum ( >= 0 )       */
{                               /* else return -1                       */
    int n;

    if (*s == 'p' || *s == 'P')
      if (sscanf(++s, "%d", &n))
        return(n);
    return(-1);
}

void otran(ENVIRON *csound)
{
    OPARMS      *O = csound->oparms;
    TEXT        *tp, *getoptxt(int *);
    int         init = 1;
    INSTRTXT    *ip = NULL;
    INSTRTXT    *prvinstxt = &(csound->instxtanchor);
    OPTXT       *bp, *prvbp = NULL;
    ARGLST      *alp;
    char        *s;
    long        pmax = -1, nn;
    long        n, opdstot=0, count, sumcount, instxtcount, optxtcount;

    gbl.names  = (NAME *)mmalloc(csound, (long)(GNAMES*sizeof(NAME)));
    gbl.namlim = gbl.names + GNAMES;
    gbl.nxtslot = gbl.names;
    gbl.next = NULL;
    lcl.names = (NAME *)mmalloc(csound, (long)(LNAMES*sizeof(NAME)));
    lcl.namlim = lcl.names + LNAMES;
    lcl.next = NULL;
    csound->instrtxtp = (INSTRTXT **) mcalloc(csound, (1 + csound->maxinsno)
                                                      * sizeof(INSTRTXT*));
    csound->opcodeInfo = NULL;          /* IV - Oct 20 2002 */
    opcode_list_create(csound);         /* IV - Oct 31 2002 */

    gblnamset("sr");            /* enter global reserved words */
    gblnamset("kr");
    gblnamset("ksmps");
    gblnamset("nchnls");
    gblnamset("0dbfs");         /* no commandline override for that! */
    gblnamset("$sr");           /* incl command-line overrides */
    gblnamset("$kr");
    gblnamset("$ksmps");

    displop1 = getopnum("print");       /* opnums that need "signal name" */
    displop2 = getopnum("display");
    displop3 = getopnum("dispfft");
/*  csound->displop4 = getopnum("specdisp"); */

    rdorchfile(csound);                 /* go read orch file    */
    if (pool == NULL) {
      pool = (MYFLT *)mmalloc(csound, (long)NCONSTS * sizeof(MYFLT));
      *pool = (MYFLT)SSTRCOD;
      poolcount = 1;
      nconsts = NCONSTS;
    }
    while ((tp = getoptxt(&init)) != NULL) {    /*   then for each opcode: */
        unsigned int threads=0;
        int opnum = tp->opnum;
        switch (opnum) {
        case INSTR:
        case OPCODE:            /* IV - Sep 8 2002 */
            ip = (INSTRTXT *) mcalloc(csound, (long)sizeof(INSTRTXT));
            prvinstxt = prvinstxt->nxtinstxt = ip;
            txtcpy((char *)&ip->t,(char *)tp);
            prvbp = (OPTXT *) ip;               /* begin an optxt chain */
            alp = ip->t.inlist;
/* <---- IV - Oct 16 2002: rewritten this code ---- */
            if (opnum == INSTR) {
              int err = 0, cnt, i;
              if (!alp->count) {  /* IV - Sep 8 2002: check for missing name */
                synterr(Str("missing instrument number or name"));
                continue;
              }
              /* IV - Oct 16 2002: allow both numbers and names for instr */
              for (cnt = 0; cnt < alp->count; cnt++) {
                char *c = alp->arg[cnt];
                if (strlen(c) <= 0) {
                  synterr(Str("missing instrument number or name"));
                  err++; continue;
                }
                if (isdigit(*c)) {      /* numbered instrument */
                  if (!sscanf(c, "%ld", &n) || n < 0) {
                    synterr(Str("illegal instr number"));
                    err++; continue;
                  }
                  if (n > csound->maxinsno) {
                    int old_maxinsno = csound->maxinsno;
                    /* expand */
                    while (n>csound->maxinsno) csound->maxinsno += MAXINSNO;
/*                  csound->Message(csound, Str("Extending instr number "
                                                "from %d to %d\n"),
                                            old_maxinsno, csound->maxinsno); */
                    csound->instrtxtp = (INSTRTXT**)
                      mrealloc(csound, csound->instrtxtp, (1 + csound->maxinsno)
                                                          * sizeof(INSTRTXT*));
                    /* Array expected to be nulled so.... */
                    for (i = old_maxinsno + 1; i <= csound->maxinsno; i++)
                      csound->instrtxtp[i] = NULL;
                  }
/*                else if (n<0) { */
/*                  synterr(Str("illegal instr number")); */
/*                  continue; */
/*                } */
                  if (csound->instrtxtp[n] != NULL) {
                    sprintf(errmsg,Str("instr %ld redefined"),n);
                    synterr(errmsg);
                    err++; continue;
                  }
                  csound->instrtxtp[n] = ip;
                }
                else {                  /* named instrument */
                  long  insno_priority = -1L;
                  if (*c == '+') {
                    insno_priority--; c++;
                  }
                  /* IV - Oct 31 2002: some error checking */
                  if (!check_instr_name(c)) {
                    synterr(Str("invalid name for instrument"));
                    err++; continue;
                  }
                  /* IV - Oct 31 2002: store the name */
                  if (!named_instr_alloc(csound, c, ip, insno_priority)) {
                    sprintf(errmsg,"instr %s redefined", c);
                    synterr(errmsg);
                    err++; continue;
                  }
                  ip->insname = c;  /* IV - Nov 10 2002: also in INSTRTXT */
                  n = -2;
                }
              }
              if (err) continue;
              if (n) putop(&ip->t);     /* print, except i0 */
            }
            else {      /* opcode definition with string name */
              OENTRY *opc, *newopc = NULL;              /* IV - Oct 31 2002 */
              long  opcListNumItems = oplstend - opcodlst, newopnum;
              OPCODINFO *inm;
              char  *name = alp->arg[0];

              /* some error checking */
              if (!alp->count || (strlen(name) <= 0)) {
                  synterr(Str("No opcode name"));
                  continue;
                }
              /* IV - Oct 31 2002 */
              if (!check_instr_name(name)) {
                synterr(Str("invalid name for opcode"));
                continue;
              }
              if (ip->t.inlist->count != 3) {
                sprintf(errmsg, Str("opcode declaration error "
                                    "(usage: opcode name, outtypes, intypes) "
                                    "-- opcode %s"), name);
                synterr(errmsg);
                continue;
              }

              /* IV - Oct 31 2002: check if opcode is already defined */
              newopnum = find_opcode(csound, name);
              if (newopnum) {
                /* IV - Oct 31 2002: redefine old opcode if possible */
                if (newopnum < SETEND || !strcmp(name, "subinstr")) {
                  sprintf(errmsg, Str("cannot redefine %s"), name);
                  synterr(errmsg); continue;
                }
                csound->Message(csound,
                                Str("WARNING: redefined opcode: %s\n"), name);
              }
              newopnum = opcListNumItems;
              /* IV - Oct 31 2002: reduced number of calls to mrealloc() */
              if (!(newopnum & 0xFFL) || !csound->opcodeInfo)
                opcodlst = (OENTRY *)
                  mrealloc(csound, opcodlst,
                           sizeof(OENTRY) * ((newopnum | 0xFFL) + 1L));
              oplstend = newopc = opcodlst + newopnum;
              oplstend++;
              /* IV - Oct 31 2002 */
              /* store the name in a linked list (note: must use mcalloc) */
              inm = (OPCODINFO *) mcalloc(csound, sizeof(OPCODINFO));
              inm->name = name;
              inm->intypes = alp->arg[2];
              inm->outtypes = alp->arg[1];
              inm->ip = ip;
              inm->prv = csound->opcodeInfo;
              csound->opcodeInfo = inm;
              /* IV - Oct 31 2002: */
              /* create a fake opcode so we can call it as such */
              opc = opcodlst + find_opcode(csound, ".userOpcode");
              memcpy(newopc, opc, sizeof(OENTRY));
              newopc->opname = name;
              newopc->useropinfo = (void*) inm; /* ptr to opcode parameters */
              opcode_list_add_entry(csound, newopnum, 1);
              ip->insname = name; ip->opcode_info = inm; /* IV - Nov 10 2002 */
              /* check in/out types and copy to the opcode's */
              newopc->outypes = mmalloc(csound, strlen(alp->arg[1]) + 1);
                /* IV - Sep 8 2002: opcodes have an optional arg for ksmps */
              newopc->intypes = mmalloc(csound, strlen(alp->arg[2]) + 2);
              if (parse_opcode_args(csound, newopc)) continue;
              n = -2;
/* ---- IV - Oct 16 2002: end of new code ----> */
              putop(&ip->t);
            }
            lcl.nxtslot = lcl.names;    /* clear lcl namlist */
            if (lcl.next) {
                struct namepool *lll = lcl.next;
                lcl.next = NULL;
                while (lll) {
                    struct namepool *n = lll->next;
                    mfree(csound, lll->names);
                    mfree(csound, lll);
                    lll = n;
                }
            }
            lclnxtkcnt = lclnxtdcnt = 0;        /*   for rebuilding  */
            lclnxtwcnt = lclnxtacnt = 0;
            lclnxtpcnt = 0;
            opdstot = 0;
            threads = 0;
            pmax = 3L;                  /* set minimum pflds */
            break;
        case ENDIN:
        case ENDOP:             /* IV - Sep 8 2002 */
            bp = (OPTXT *) mcalloc(csound, (long)sizeof(OPTXT));
            txtcpy((char *)&bp->t, (char *)tp);
            prvbp->nxtop = bp;
            bp->nxtop = NULL;   /* terminate the optxt chain */
            if (O->odebug) {
              putop(&bp->t);
              csound->Message(csound, "pmax %ld, kcnt %d, dcnt %d, "
                                      "wcnt %d, acnt %d pcnt %d\n",
                                      pmax, lclnxtkcnt, lclnxtdcnt,
                                      lclnxtwcnt, lclnxtacnt, lclnxtpcnt);
            }
            ip->pmax = (int)pmax;
            ip->pextrab = ((n = pmax-3L) > 0 ? (int) n * sizeof(MYFLT) : 0);
            ip->pextrab = ((int) ip->pextrab + 7) & (~7);
            ip->mdepends = threads >> 4;
            ip->lclkcnt = lclnxtkcnt;
            ip->lcldcnt = lclnxtdcnt;
            ip->lclwcnt = lclnxtwcnt;
            ip->lclacnt = lclnxtacnt;
            ip->lclfixed = lclnxtkcnt + lclnxtdcnt * Dfloats
                                      + lclnxtwcnt * Wfloats;
            ip->lclpcnt  = lclnxtpcnt;
            ip->lclfixed = lclnxtkcnt + lclnxtdcnt * Dfloats
                                      + lclnxtwcnt * Wfloats
                                      + lclnxtpcnt * Pfloats;
            ip->opdstot = opdstot;              /* store total opds reqd */
            ip->muted = 1;      /* Allow to play */
            n = -1;             /* No longer in an instrument */
            break;
        default:
            bp = (OPTXT *) mcalloc(csound, (long)sizeof(OPTXT));
            txtcpy((char *)&bp->t,(char *)tp);
            prvbp = prvbp->nxtop = bp;  /* link into optxt chain */
            threads |= opcodlst[opnum].thread;
            opdstot += opcodlst[opnum].dsblksiz;        /* sum opds's */
            if (O->odebug) putop(&bp->t);
            if (opnum == displop1)                      /* display op arg ? */
              for (alp=bp->t.inlist, nn=alp->count; nn>0; ) {
                s = alp->arg[--nn];
                strargsize += (long)strlen(s) +  1L;/* sum the chars */
              }
            if (opnum == displop2 || opnum == displop3 ||
                opnum == csound->displop4) {
              alp=bp->t.inlist;
              s = alp->arg[0];
              strargsize += (long)strlen(s) + 1L;
            }
            for (alp=bp->t.inlist, nn=alp->count; nn>0; ) {
              s = alp->arg[--nn];
              if (*s == '"') {                        /* "string" arg ? */
                strargsize += (long)strlen(s) - 1L; /* sum real chars */
                continue;
              }
              if ((n = pnum(s)) >= 0)
                { if (n > pmax)  pmax = n; }
              else lgbuild(s);
            }
            for (alp=bp->t.outlist, nn=alp->count; nn>0; ) {
              s = alp->arg[--nn];
              if ((n = pnum(s)) >= 0) {
                if (n > pmax)  pmax = n;
              }
              else lgbuild(s);
              if (!nn && bp->t.opcod[1] == '.'        /* rsvd glbal = n ? */
                  && strcmp(bp->t.opcod,"=.r")==0) {  /*  (assume const)  */
                MYFLT constval = pool[constndx(bp->t.inlist->arg[0])];
                if (strcmp(s,"sr") == 0)
                  csound->tran_sr = constval;         /* modify otran defaults*/
                else if (strcmp(s,"kr") == 0)
                  csound->tran_kr = constval;
                else if (strcmp(s,"ksmps") == 0)
                  csound->tran_ksmps = constval;
                else if (strcmp(s,"nchnls") == 0)
                  csound->tran_nchnls = (int)constval;
                /* we have set this as reserved in rdorch.c */
                else if (strcmp(s,"0dbfs") == 0)
                  csound->tran_0dbfs = constval;
              }
            }
            n = 0;              /* Mark as unfinished */
            break;
        }
    }
    if (n != -1)
      synterr(Str("Missing endin"));
    /* now add the instruments with names, assigning them fake instr numbers */
    named_instr_assign_numbers(csound);         /* IV - Oct 31 2002 */
    if (csound->opcodeInfo) {
      int num = csound->maxinsno;       /* store after any other instruments */
      OPCODINFO *inm = csound->opcodeInfo;
      /* IV - Oct 31 2002: now add user defined opcodes */
      while (inm) {
        /* we may need to expand the instrument array */
        if (++num > csound->maxopcno) {
          int i;
          i = (csound->maxopcno > 0 ? csound->maxopcno : csound->maxinsno);
          csound->maxopcno = i + MAXINSNO;
          csound->instrtxtp = (INSTRTXT**)
            mrealloc(csound, csound->instrtxtp, (1 + csound->maxopcno)
                                                * sizeof(INSTRTXT*));
          /* Array expected to be nulled so.... */
          while (++i <= csound->maxopcno) csound->instrtxtp[i] = NULL;
        }
        inm->instno = num;
        csound->instrtxtp[num] = inm->ip;
        inm = inm->prv;
      }
    }
    /* Deal with defaults and consistency */
    if (csound->tran_ksmps == FL(-1.0)) {
      if (csound->tran_sr == FL(-1.0)) csound->tran_sr = DFLT_SR;
      if (csound->tran_kr == FL(-1.0)) csound->tran_kr = DFLT_KR;
      csound->tran_ksmps = (MYFLT) ((int) (csound->tran_sr / csound->tran_kr
                                           + FL(0.5)));
    }
    else if (csound->tran_kr == FL(-1.0)) {
      if (csound->tran_sr == FL(-1.0)) csound->tran_sr = DFLT_SR;
      csound->tran_kr = csound->tran_sr / csound->tran_ksmps;
    }
    else if (csound->tran_sr == FL(-1.0)) {
      csound->tran_sr = csound->tran_kr * csound->tran_ksmps;
    }
                                /* That deals with missing values */
                                /* However we do need ksmps to be integer */
    /* IV - Feb 18 2003 */
    {
#ifndef USE_DOUBLE
      double    max_err = 5.0e-7;
#else
      double    max_err = 1.0e-12;
#endif
      char      sbuf[256];
      sprintf(sbuf, "sr = %.7g, kr = %.7g, ksmps = %.7g",
                    csound->tran_sr, csound->tran_kr, csound->tran_ksmps);
      if (csound->tran_ksmps < FL(0.75) ||
          fabs((double) csound->tran_ksmps
               / (double) ((int) (csound->tran_ksmps + FL(0.5))) - 1.0)
          > max_err) {
        strcat(sbuf, "\n"); strcat(sbuf, Str("error: invalid ksmps value"));
      }
      if (csound->tran_sr <= FL(0.0)) {
        strcat(sbuf, "\n"); strcat(sbuf, Str("error: invalid sample rate"));
      }
      if (csound->tran_kr <= FL(0.0)) {
        strcat(sbuf, "\n"); strcat(sbuf, Str("error: invalid control rate"));
      }
      if (fabs((double) csound->tran_sr
               / ((double) csound->tran_kr * (double) csound->tran_ksmps)
               - 1.0) > max_err) {
        strcat(sbuf, "\n");
        strcat(sbuf, Str("error: inconsistent sr, kr, ksmps"));
      }
      if (strchr(sbuf, '\n') != NULL)
        synterr(Str(sbuf));
    }

    ip = csound->instxtanchor.nxtinstxt;
    bp = (OPTXT *) ip;
    while (bp != (OPTXT *) NULL && (bp = bp->nxtop) != NULL) {
      /* chk instr 0 for illegal perfs */
      int thread, opnum = bp->t.opnum;
      if (opnum == ENDIN) break;
      if (opnum == LABEL || opnum == STRSET) continue;
      if ((thread = opcodlst[opnum].thread) & 06 ||
          (!thread && bp->t.pftype != 'b'))
        synterr(Str("perf-pass statements illegal in header blk"));
    }
    if (csound->synterrcnt) {
      csound->Message(csound, Str("%d syntax errors in orchestra.  "
                                  "compilation invalid\n"), csound->synterrcnt);
      longjmp(csound->exitjmp, 1);
    }
    if (O->odebug) {
      long n; MYFLT *p;
      csound->Message(csound, "poolcount = %ld, strargsize = %ld\n",
                              poolcount, strargsize);
      csound->Message(csound, "pool:");
      for (n = poolcount, p = pool; n--; p++)
        csound->Message(csound, "\t%g", *p);
      csound->Message(csound, "\n");
    }
    gblfixed = gblnxtkcnt;
    gblacount = gblnxtacnt;

    if (strargsize) {
      strargspace = mcalloc(csound, (long)strargsize);
      strargptr = strargspace;
    }
    ip = &(csound->instxtanchor);
    for (sumcount = 0; (ip = ip->nxtinstxt) != NULL; ) {/* for each instxt */
      OPTXT *optxt = (OPTXT *) ip;
      int optxtcount = 0;
      while ((optxt = optxt->nxtop) != NULL) {/* for each op in instr  */
        TEXT *ttp = &optxt->t;
        optxtcount += 1;
        if (ttp->opnum == ENDIN                 /*    (until ENDIN)      */
            || ttp->opnum == ENDOP) break;  /* (IV - Oct 26 2002: or ENDOP) */
        if ((count = ttp->inlist->count)!=0)
          sumcount += count +1;           /* count the non-nullist */
        if ((count = ttp->outlist->count)!=0)       /* slots in all arglists */
          sumcount += count +1;
      }
      ip->optxtcount = optxtcount;              /* optxts in this instxt */
    }
    argoffsize = (sumcount + 1) * sizeof(int);  /* alloc all plus 1 null */
    csound->argoffspace = (int*) mmalloc(csound, argoffsize); /*as argoff ints*/
    nxtargoffp = csound->argoffspace;
    nulloffs = (ARGOFFS *) csound->argoffspace; /* setup the null argoff */
    *nxtargoffp++ = 0;
    argofflim = nxtargoffp + sumcount;
    ip = &(csound->instxtanchor);
    while ((ip = ip->nxtinstxt) != NULL)        /* add all other entries */
        insprep(ip);                            /*   as combined offsets */
    if (O->odebug) {
      int *p = csound->argoffspace;
      csound->Message(csound, "argoff array:\n");
      do {
        csound->Message(csound, "\t%d", *p++);
      } while (p < argofflim);
      csound->Message(csound, "\n");
    }
    if (nxtargoffp != argofflim)
      csoundDie(csound, Str("inconsistent argoff sumcount"));
    if (strargsize && strargptr != strargspace + strargsize)
      csoundDie(csound, Str("inconsistent strarg sizecount"));

    ip = &(csound->instxtanchor);               /* set the OPARMS values */
    instxtcount = optxtcount = 0;
    while ((ip = ip->nxtinstxt) != NULL) {
      instxtcount += 1;
      optxtcount += ip->optxtcount;
    }
    O->instxtcount = instxtcount;
    O->optxtsize = instxtcount * sizeof(INSTRTXT) + optxtcount * sizeof(OPTXT);
    O->poolcount = poolcount;
    O->gblfixed = gblnxtkcnt;
    O->gblacount = gblnxtacnt;
    O->argoffsize = argoffsize;
    O->argoffspace = (char *) csound->argoffspace;
    O->strargsize = strargsize;
    O->strargspace = strargspace;
}

static void insprep(INSTRTXT *tp) /* prep an instr template for efficient */
                                  /* allocs repl arg refs by offset ndx to */
                                  /* lcl/gbl space */
{
    ENVIRON     *csound = &cenviron;
    OPARMS      *O = csound->oparms;
    OPTXT       *optxt;
    OENTRY      *ep;
    int         n, opnum, inreqd;
    char        **argp;
    char        **labels, **lblsp;
    LBLARG      *larg, *largp;
    ARGLST      *outlist, *inlist;
    ARGOFFS     *outoffs, *inoffs;
    int         indx, *ndxp;

    labels = (char **)mmalloc(csound, (csound->nlabels) * sizeof(char *));
    lblsp = labels;
    larg = (LBLARG *)mmalloc(csound, (csound->ngotos) * sizeof(LBLARG));
    largp = larg;
    lclkcnt = tp->lclkcnt;
    lcldcnt = tp->lcldcnt;
    lclwcnt = tp->lclwcnt;
    lclfixed = tp->lclfixed;
    lclpcnt  = tp->lclpcnt;
    lcl.nxtslot = lcl.names;                    /* clear lcl namlist */
    if (lcl.next) {
      struct namepool *lll = lcl.next;
      lcl.next = NULL;
      while (lll) {
        struct namepool *n = lll->next;
        mfree(csound, lll->names);
        mfree(csound, lll);
        lll = n;
      }
    }
    lclnxtkcnt = lclnxtdcnt = 0;                /*   for rebuilding  */
    lclnxtwcnt = lclnxtacnt = 0;
    lclnxtpcnt = 0;
    lclpmax = tp->pmax;                         /* set pmax for plgndx */
    ndxp = nxtargoffp;
    optxt = (OPTXT *)tp;
    while ((optxt = optxt->nxtop) != NULL) {    /* for each op in instr */
      TEXT *ttp = &optxt->t;
      int whichstr = 0;
      if ((opnum = ttp->opnum) == ENDIN         /*  (until ENDIN)  */
          || opnum == ENDOP)            /* (IV - Oct 31 2002: or ENDOP) */
        break;
      if (opnum == LABEL) {
        if (lblsp - labels >= csound->nlabels) {
          int oldn = lblsp - labels;
          csound->nlabels += NLABELS;
          if (lblsp - labels >= csound->nlabels)
            csound->nlabels = lblsp - labels + 2;
          if(csound->oparms->msglevel)
            csound->Message(csound,
                            Str("LABELS list is full...extending to %d\n"),
                            csound->nlabels);
          labels =
            (char**)mrealloc(csound, labels, csound->nlabels*sizeof(char*));
          lblsp = &labels[oldn];
        }
        *lblsp++ = ttp->opcod;
        continue;
      }
      ep = &opcodlst[opnum];
      if (O->odebug) csound->Message(csound, "%s argndxs:", ep->opname);
      if ((outlist = ttp->outlist) == nullist || !outlist->count)
        ttp->outoffs = nulloffs;
      else {
        ttp->outoffs = outoffs = (ARGOFFS *) ndxp;
        outoffs->count = n = outlist->count;
        argp = outlist->arg;            /* get outarg indices */
        ndxp = outoffs->indx;
        while (n--) {
          *ndxp++ = indx = plgndx(*argp++);
          if (O->odebug) csound->Message(csound, "\t%d",indx);
        }
      }
      if ((inlist = ttp->inlist) == nullist || !inlist->count)
        ttp->inoffs = nulloffs;
      else {
        ttp->inoffs = inoffs = (ARGOFFS *) ndxp;
        inoffs->count = inlist->count;
        if (opnum == displop1) {                /* display op arg ? */
          optxt->t.strargs[0] = strargptr;
          for (n=0; n < inlist->count; n++ ) {
            char *s = inlist->arg[n];
            do {
              *strargptr++ = *s;       /*   copy all args  */
            } while (*s++);
          }
        }
        else if (opnum == displop2 || opnum == displop3 ||
                 opnum == csound->displop4) {
          char *s = inlist->arg[0];
          optxt->t.strargs[0] = strargptr;
          do {
            *strargptr++ = *s;         /*   or just the 1st */
          } while (*s++);
        }
        inreqd = strlen(ep->intypes);
        argp = inlist->arg;                     /* get inarg indices */
        ndxp = inoffs->indx;
        for (n=0; n < inlist->count; n++, argp++, ndxp++) {
          if (n < inreqd && ep->intypes[n] == 'l') {
            if (largp - larg >= csound->ngotos) {
              int oldn = csound->ngotos;
              csound->ngotos += NGOTOS;
              if(csound->oparms->msglevel)
                csound->Message(csound,
                                Str("GOTOS list is full..extending to %d\n"),
                                csound->ngotos);
              if (largp - larg >= csound->ngotos)
                csound->ngotos = largp - larg + 1;
              larg = (LBLARG *)
                mrealloc(csound, larg, csound->ngotos * sizeof(LBLARG));
              largp = &larg[oldn];
            }
            if (O->odebug)
              csound->Message(csound, "\t***lbl");  /* if arg is label,  */
            largp->lbltxt = *argp;
            largp->ndxp = ndxp;                     /*  defer till later */
            largp++;
          }
          else {
            char *s = *argp;
            if (*s == '"') {            /* string argument:  */
              optxt->t.strargs[whichstr++] = strargptr;/*  save strargs ptr */
              s++;
              do {
                char c = *s++;
#if defined(SYMANTEC) || defined(mac_classic)
                if (c == '/')   /*  on Mac subst ':' */
                  c = DIRSEP;
#endif
                *strargptr++ = c;       /*  copy w/o quotes  */
              } while (*s != '"');
              *strargptr++ = '\0';      /*  terminate string */
              indx = 1;                 /*  cod=1st pool val */
            }
            else indx = plgndx(s);      /* else normal index */
            if (O->odebug) csound->Message(csound, "\t%d",indx);
            *ndxp = indx;
          }
        }
      }
      if (O->odebug) csound->Message(csound, "\n");
    }
 nxt:
    while (--largp >= larg) {                   /* resolve the lbl refs */
      char *s = largp->lbltxt;
      char **lp;
      for (lp = labels; lp < lblsp; lp++)
        if (strcmp(s, *lp) == 0) {
          *largp->ndxp = lp - labels + LABELOFS;
          goto nxt;
        }
      csoundDie(csound, Str("target label '%s' not found"), s);
    }
    nxtargoffp = ndxp;
    mfree(csound, labels);
    mfree(csound, larg);
}

static void lgbuild(char *s)    /* build pool of floating const values  */
                                /* build lcl/gbl list of ds names, offsets */
{                               /*   (no need to save the returned values) */
    char c;

    if (((c = *s) >= '0' && c <= '9') ||
        c == '.' || c == '-' || c == '+')
        constndx(s);
    else if (!(lgexist(s))) {
      if (c == 'g' || (c == '#' && *(s+1) == 'g'))
        gblnamset(s);
      else lclnamset(s);
    }
}

static int plgndx(char *s)      /* get storage ndx of const, pnum, lcl or gbl */
                                /* argument const/gbl indexes are positiv+1, */
                                /* pnum/lcl negativ-1 called only after      */
                                /* poolcount & lclpmax are finalised */
{
    char        c;
    int         n, indx;

    c = *s;
    if (
        /* must trap 0dbfs as name starts with a digit! */
        strcmp(s,"0dbfs") && ((c >= '0' && c <= '9') ||
                              c == '.' || c == '-' || c == '+')
        ) {
      indx = constndx(s) + 1;
    }
    else if ((n = pnum(s)) >= 0)
      indx = - n;
    else if (c == 'g' || (c == '#' && *(s+1) == 'g') || gexist(s))
      indx = (int) (poolcount + 1 + gbloffndx(s));
    else indx = - (lclpmax + 1 + lcloffndx(s));
/*     csound->Message(csound, " [%s -> %d (%x)]\n", s, indx, indx); */
    return(indx);
}

static int constndx(char *s)        /* get storage ndx of float const value */
{                                   /* builds value pool on 1st occurrence  */
    ENVIRON *csound = &cenviron;    /* final poolcount used in plgndx above */
    MYFLT   newval;                 /* pool may be moved w. ndx still valid */
    long    n;
    MYFLT   *fp;
    char    *str = s;

#ifdef USE_DOUBLE
    if (sscanf(s,"%lf",&newval) != 1) goto flerror;
#else
    if (sscanf(s,"%f",&newval) != 1) goto flerror;
#endif
    /* It is tempting to assume that if this loop is removed then we
     * would not share constants.  However this breaks something else
     * as this function is used to retrieve constants as well....
     * I (JPff) have not understood this yet.
     */
    for (fp=pool,n=poolcount; n--; fp++) {      /* now search constpool */
      if (newval == *fp)                        /* if val is there      */
        return(fp - pool);                      /*    return w. index   */
    }
    if (++poolcount > nconsts) {
      /* csoundDie(csound, "flconst pool is full"); */
      int indx = fp-pool;
      nconsts += NCONSTS;
      if(csound->oparms->msglevel)
        csound->Message(csound,Str("extending Floating pool to %d\n"), nconsts);
      pool = (MYFLT*)mrealloc(csound, pool, nconsts*sizeof(MYFLT));
      fp = pool + indx;
    }
    *fp = newval;                               /* else enter newval    */
/*      csound->Message(csound, "Constant %d: %f\n", fp-pool, newval); */
    return(fp - pool);                          /*   and return new ndx */

 flerror:
    sprintf(errmsg,Str("numeric syntax '%s'"),str);
    synterr(errmsg);
    return(0);
}

static void gblnamset(char *s) /* builds namelist & type counts for gbl names */
{
    ENVIRON     *csound = &cenviron;
    NAME        *np = NULL;
    struct namepool *ggg;

    for (ggg=&gbl; ggg!=NULL; ggg=ggg->next) {
      for (np=ggg->names; np<ggg->nxtslot; np++)/* search gbl namelist: */
        if (strcmp(s,np->namep) == 0)   /* if name is there     */
          return;                               /*    return            */

      if (ggg->nxtslot+1 >= ggg->namlim) {      /* chk for full table   */
/*      csoundDie(csound, "gbl namelist is full"); */
        if (ggg->next == NULL) {
          if(csound->oparms->msglevel)
            csound->Message(csound,Str("Extending Global pool to %d\n"),
                            gblsize+=GNAMES);
          ggg->next = (struct namepool*)mmalloc(csound,sizeof(struct namepool));
          ggg = ggg->next;
          ggg->names = (NAME *)mmalloc(csound, (long)(GNAMES*sizeof(NAME)));
          ggg->namlim = ggg->names + GNAMES;
          ggg->nxtslot = ggg->names;
          ggg->next = NULL;
          np = ggg->names;
          break;
        }
        else continue;
      }
      else break;
    }
    ++(ggg->nxtslot);
    np->namep = s;                              /* else record newname  */
    if (*s == '#')      s++;
    if (*s == 'g')      s++;
    if (*s == 'a') {                            /*   and its type-count */
      np->type = ATYPE;
      np->count = gblnxtacnt++;
    }
    else {
      np->type = KTYPE;
      np->count = gblnxtkcnt++;
    }
}

static NAME *lclnamset(char *s)
                        /* builds namelist & type counts for lcl names  */
                        /*   called by otran for each instr for lcl cnts */
{                       /*   lists then redone by insprep via lcloffndx  */
    ENVIRON     *csound = &cenviron;
    NAME        *np=NULL;
    struct namepool     *lll;

    for (lll=&lcl; lll!=NULL; lll=lll->next) {
      for (np=lll->names; np<lll->nxtslot; np++)/* search lcl namelist: */
        if (strcmp(s,np->namep) == 0)   /* if name is there     */
          return(np);                   /*    return ptr        */
      if (lll->nxtslot+1 >= lll->namlim) {      /* chk for full table   */
        /*          csoundDie(csound, "lcl namelist is full"); */
        if (lll->next == NULL) {
          if(csound->oparms->msglevel)
            csound->Message(csound,Str("Extending Local pool to %d\n"),
                            lclsize+=LNAMES);
          lll->next = (struct namepool*)mmalloc(csound,
                                                sizeof(struct namepool));
          lll = lll->next;
          lll->names = (NAME *)mmalloc(csound, (long)(LNAMES*sizeof(NAME)));
          lll->namlim = lll->names + LNAMES;
          lll->nxtslot = lll->names;
          lll->next = NULL;
          np = lll->names;
          break;
        }
        else continue;
      }
      else break;
    }
    ++(lll->nxtslot);
    np->namep = s;                              /* else record newname  */
    if (*s == '#')      s++;
    switch (*s) {                               /*   and its type-count */
    case 'd': np->type = DTYPE; np->count = lclnxtdcnt++; break;
    case 'w': np->type = WTYPE; np->count = lclnxtwcnt++; break;
    case 'a': np->type = ATYPE; np->count = lclnxtacnt++; break;
    case 'f': np->type = PTYPE; np->count = lclnxtpcnt++; break;
    default:  np->type = KTYPE; np->count = lclnxtkcnt++; break;
    }
    return(np);
}

static int gbloffndx(char *s)   /* get named offset index into gbl dspace */
                                /* called only after otran and gblfixed valid */
{
    ENVIRON     *csound = &cenviron;
    NAME        *np;
    int indx;
    struct namepool *ggg = &gbl;

    while (1) {
      for (np=ggg->names; np<ggg->nxtslot; np++)  /* search gbl namelist: */
        if (strcmp(s,np->namep) == 0) { /* if name is there     */
          if (np->type == ATYPE)
            indx = gblfixed + np->count;
          else indx = np->count;        /*    return w. index   */
          return(indx);
        }
      if (ggg->nxtslot+1 < ggg->namlim)
        csoundDie(csound, Str("unexpected global name")); /* else complain */
      ggg = ggg->next;
      if (ggg == NULL)
        csoundDie(csound, Str("no pool for unexpected global name"));
    }
}

static int lcloffndx(char *s)   /* get named offset index into instr lcl */
                                /* dspace called by insprep aftr lclcnts,*/
                                /* lclfixed valid */
{
    ENVIRON     *csound = &cenviron;
    NAME        *np = lclnamset(s);             /* rebuild the table    */
    int indx = 0;
    int Pfloatsize = Pfloats;
    switch (np->type) {                         /* use cnts to calc ndx */
    case KTYPE:  indx = np->count;  break;
    case DTYPE:  indx = lclkcnt + np->count * Dfloats;  break;
    case WTYPE:  indx = lclkcnt + lcldcnt * Dfloats
                                + np->count * Wfloats;  break;
    case ATYPE:  indx = lclfixed + np->count;  break;
                /*RWD ???? */
    case PTYPE: indx = lclkcnt + np->count * Pfloatsize; break;
    default:     csoundDie(csound, Str("unknown nametype"));  break;
    }
    return(indx);                       /*   and rtn this offset */
}

static int gexist(char *s)      /* tests whether variable name exists   */
{                               /*      in gbl namelist                 */
    NAME        *np;
    struct namepool     *ggg = &gbl;

    while (ggg) {               /* search gbl namelist:                 */
      for (np = ggg->names; np < ggg->nxtslot; np++)
        if (strcmp(s,np->namep) == 0)   /* if name is there     */
          return(1);                    /*      return 1        */
      ggg = ggg->next;
    }
    return(0);                  /* else return 0                        */
}

int lgexist(char *s)            /* tests whether variable name exists   */
                                /*      in gbl or lcl namelist          */
{
    NAME        *np;
    struct namepool     *gl;

    for (gl = &gbl; gl!=NULL; gl=gl->next) {
      for (np = gl->names; np < gl->nxtslot; np++) /* search gbl namelist: */
        if (strcmp(s,np->namep) == 0)   /* if name is there     */
          return(1);                    /*      return 1        */
    }
    for (gl = &lcl; gl!=NULL; gl=gl->next) {
      for (np = gl->names; np < gl->nxtslot; np++) /* search lcl namelist: */
        if (strcmp(s,np->namep) == 0)   /* if name is there     */
          return(1);                    /*      return 1        */
    }
    return(0);                          /* cannot find, return 0 */
}

void putop(TEXT *tp)
{
    ENVIRON *csound = &cenviron;
    int n, nn;

    if ((n = tp->outlist->count)!=0) {
      nn = 0;
      while (n--) csound->Message(csound,"%s\t", tp->outlist->arg[nn++]);
    }
    else csound->Message(csound,"\t");
    csound->Message(csound,"%s\t", tp->opcod);
    if ((n = tp->inlist->count)!=0) {
      nn = 0;
      while (n--) csound->Message(csound,"%s\t",tp->inlist->arg[nn++]);
    }
    csound->Message(csound,"\n");
}

