/*
    spectra.c:

    Copyright (C) 1995 Barry Vercoe

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

#include "csdl.h"         /*                                    SPECTRA.C       */
#include <math.h>
#include "cwindow.h"
#include "spectra.h"
#include "pitch.h"
#include "uggab.h"

#define FZERO   (FL(0.0))
#define LOGTWO  (0.69314718056)

void DOWNset(CSOUND *p, DOWNDAT *downdp, long npts)
{
    long nbytes = npts * sizeof(MYFLT);

    if (downdp->auxch.auxp == NULL || downdp->auxch.size != nbytes)
      p->AuxAlloc(p, nbytes, &downdp->auxch);
    downdp->npts = npts;
}

void SPECset(CSOUND *p, SPECDAT *specdp, long npts)
{
    long nbytes = npts * sizeof(MYFLT);

    if (specdp->auxch.auxp == NULL || nbytes != specdp->auxch.size)
      p->AuxAlloc(p, nbytes, &specdp->auxch);
    specdp->npts = npts;
}

static const char *outstring[] = {"mag", "db", "mag sqrd", "root mag"};

int spectset(CSOUND *csound, SPECTRUM *p)
                           /* spectrum - calcs disc Fourier transform of */
                           /* oct-downsampled data outputs coefs (mag, */
                           /* db or mag2) of log freq within each octave */
{
    int     n, nocts, nfreqs, ncoefs, hanning;
    MYFLT   Q, *fltp;
    OCTDAT  *octp;
    DOWNDAT *dwnp = &p->downsig;
    SPECDAT *specp = p->wsig;

    /* for mac roundoff */
    p->timcount = (int)(csound->ekr * *p->iprd + FL(0.001));
    nocts = (int)*p->iocts;
    nfreqs = (int)*p->ifrqs;
    ncoefs = nocts * nfreqs;
    Q = *p->iq;
    hanning = (*p->ihann) ? 1 : 0;
    p->dbout = (int)*p->idbout;
    if ((p->disprd = (int)(csound->ekr * *p->idisprd)) < 0)  p->disprd = 0;

    if (p->timcount <= 0)
      return csound->InitError(csound, Str("illegal iprd"));
    if (nocts <= 0 || nocts > MAXOCTS)
      return csound->InitError(csound, Str("illegal iocts"));
    if (nfreqs <= 0 || nfreqs > MAXFRQS)
      return csound->InitError(csound, Str("illegal ifrqs"));
    if (Q <= FZERO)
      return csound->InitError(csound, Str("illegal Q value"));
    if (p->dbout < 0 || p->dbout > 3)
      return csound->InitError(csound, Str("unknown dbout code"));

    if (nocts != dwnp->nocts ||
        nfreqs != p->nfreqs  || /* if anything has changed */
        Q != p->curq         ||
        (p->disprd && !p->octwindow.windid) ||
        hanning != p->hanning) {                /*     make new tables */
      double      basfrq, curfrq, frqmlt, Qfactor;
      double      theta, a, windamp, onedws, pidws;
      MYFLT       *sinp, *cosp;
      int         k, sumk, windsiz, halfsiz, *wsizp, *woffp;
      long        auxsiz, bufsiz = 0;
      long        majr, minr, totsamps, totsize;
      double      hicps,locps,oct;  /*   must alloc anew */

      p->nfreqs = nfreqs;
      p->curq = Q;
      p->hanning = hanning;
      p->ncoefs = ncoefs;
      csound->Message(csound,
                      Str("spectrum: %s window, %s out, making tables ...\n"),
                      (hanning) ? "hanning":"hamming", outstring[p->dbout]);

      if (p->h.optext->t.intype == 'k') {
        dwnp->srate = csound->ekr;            /* define the srate           */
        p->nsmps = 1;
      }
      else {
        dwnp->srate = csound->esr;
        p->nsmps = csound->ksmps;
      }
      hicps = dwnp->srate * 0.375;            /* top freq is 3/4 pi/2 ...   */
      oct = log(hicps / ONEPT) / LOGTWO;      /* octcps()  (see aops.c)     */
      if (p->h.optext->t.intype != 'k') {     /* for sr sampling:           */
        oct = ((int)(oct*12.0 + 0.5)) / 12.0; /*     semitone round to A440 */
        hicps = pow(2.0, oct) * ONEPT;        /*     cpsoct()               */
      }
      dwnp->looct = (MYFLT)(oct - nocts);     /* true oct val of lowest frq */
      locps = hicps / (1L << nocts);
      csound->Message(csound, Str("\thigh cps %7.1f\n\t low cps %7.1f\n"),
                              hicps, locps);

      basfrq = hicps/2.0;                          /* oct below retuned top */
      frqmlt = pow((double)2.0,(double)1.0/nfreqs);  /* nfreq interval mult */
      Qfactor = Q * dwnp->srate;
      curfrq = basfrq;
      for (sumk=0,wsizp=p->winlen,woffp=p->offset,n=nfreqs; n--; ) {
        *wsizp++ = k = (int)(Qfactor/curfrq) | 01;  /* calc odd wind sizes */
        *woffp++ = (*(p->winlen) - k) / 2;          /* & symmetric offsets */
        sumk += k;                                  /*    and find total   */
        curfrq *= frqmlt;
      }
      windsiz = *(p->winlen);
      csound->Message(csound,
                      Str("\tQ %4.1f uses a %d sample window each octdown\n"),
                      Q, windsiz);
      auxsiz = (windsiz + 2*sumk) * sizeof(MYFLT);   /* calc lcl space rqd */

      csound->AuxAlloc(csound, (long)auxsiz, &p->auxch1); /* & alloc auxspace */

      fltp = (MYFLT *) p->auxch1.auxp;
      p->linbufp = fltp;      fltp += windsiz; /* linbuf must take nsamps */
      p->sinp = sinp = fltp;  fltp += sumk;
      p->cosp = cosp = fltp;                         /* cos gets rem sumk  */
      wsizp = p->winlen;
      curfrq = basfrq * TWOPI / dwnp->srate;
      for (n = nfreqs; n--; ) {                      /* now fill tables */
        windsiz = *wsizp++;                          /*  (odd win size) */
        halfsiz = windsiz >> 1;
        onedws = 1.0 / (windsiz-1);
        pidws = PI / (windsiz-1);
        for (k = -halfsiz; k<=halfsiz; k++) {        /*   with sines    */
          a = cos(k * pidws);
          windamp = a * a;                           /*   times hanning */
          if (!hanning)
            windamp = 0.08 + 0.92 * windamp;         /*   or hamming    */
          windamp *= onedws;                         /*   scaled        */
          theta = k * curfrq;
          *sinp++ = (MYFLT)(windamp * sin(theta));
          *cosp++ = (MYFLT)(windamp * cos(theta));
        }
        curfrq *= frqmlt;                        /*   step by log freq  */
      }
      if (*p->idsines != FZERO) {      /* if reqd, dsply windowed sines now! */
        csound->dispset(csound, &p->sinwindow, p->sinp, (long) sumk,
                                Str("spectrum windowed sines:"), 0, "spectrum");
        csound->display(csound, &p->sinwindow);
      }

      dwnp->hifrq = (MYFLT)hicps;
      dwnp->lofrq = (MYFLT)locps;
      dwnp->nsamps = windsiz = *(p->winlen);
      dwnp->nocts = nocts;
      minr = windsiz >> 1;                  /* sep odd windsiz into maj, min */
      majr = windsiz - minr;                /*      & calc totsamps reqd     */
      totsamps = (majr*nocts) + (minr<<nocts) - minr;
      DOWNset(csound,
              dwnp, totsamps);              /* auxalloc in DOWNDAT struct */
      fltp = (MYFLT *) dwnp->auxch.auxp;    /*  & distrib to octdata */
      for (n=nocts,octp=dwnp->octdata+(nocts-1); n--; octp--) {
        bufsiz = majr + minr;
        octp->begp = fltp;  fltp += bufsiz; /*        (lo oct first) */
        octp->endp = fltp;  minr *= 2;
      }
      csound->Message(csound, Str("\t%d oct analysis window "
                                  "delay = %ld samples (%d msecs)\n"),
                              nocts, bufsiz, (int)(bufsiz*1000/dwnp->srate));
      if (p->disprd) {                      /* if display requested, */
        totsize = totsamps * sizeof(MYFLT); /*  alloc an equiv local */
        csound->AuxAlloc(csound,
                         (long)totsize, &p->auxch2);/*  linear output window */
        csound->dispset(csound, &p->octwindow, (MYFLT *)p->auxch2.auxp,
                        (long)totsamps, Str("octdown buffers:"), 0, "spectrum");
      }
      SPECset(csound,
              specp, (long)ncoefs);          /* prep the spec dspace */
      specp->downsrcp = dwnp;                /*  & record its source */
    }
    for (octp=dwnp->octdata; nocts--; octp++) { /* reset all oct params, &    */
      octp->curp = octp->begp;
      for (fltp=octp->feedback,n=6; n--; )
        *fltp++ = FZERO;
      octp->scount = 0;
    }
    specp->nfreqs = p->nfreqs;               /* save the spec descriptors */
    specp->dbout = p->dbout;
    specp->ktimstamp = 0;                    /* init specdata to not new  */
    specp->ktimprd = p->timcount;
    p->scountdown = p->timcount;             /* prime the spect countdown */
    p->dcountdown = p->disprd;               /*   & the display countdown */
    return OK;
}

static void linocts(DOWNDAT *dwnp, MYFLT *bufp)
     /* linearize octdown dat to 1 buf */
{    /* presumes correct buffer alloc'd in set */
    MYFLT  *curp, *endp;
    int    wrap;
    OCTDAT *octp;
    int    nocts;
    MYFLT  *begp;

    nocts = dwnp->nocts;
    octp = dwnp->octdata + nocts;
    while (nocts--) {
      octp--;                            /* for each octave (low to high) */
      begp = octp->begp;
      curp = octp->curp;
      endp = octp->endp;
      wrap = curp - begp;
      while (curp < endp)                     /*   copy circbuf to linbuf */
        *bufp++ = *curp++;
      for (curp=begp; wrap--; )
        *bufp++ = *curp++;
    }
}

static const MYFLT bicoefs[] = {
   -FL(0.2674054), FL(0.7491305), FL(0.7160484), FL(0.0496285), FL(0.7160484),
    FL(0.0505247), FL(0.3514850), FL(0.5257536), FL(0.3505025), FL(0.5257536),
    FL(0.3661840), FL(0.0837990), FL(0.3867783), FL(0.6764264), FL(0.3867783)
};

int spectrum(CSOUND *csound, SPECTRUM *p)
{
    MYFLT   a, b, *dftp, *sigp = p->signal, SIG, yt1, yt2;
    int     nocts, nsmps = p->nsmps, winlen;
    DOWNDAT *downp = &p->downsig;
    OCTDAT  *octp;
    SPECDAT *specp;
    double  c;

    do {
      SIG = *sigp++;                        /* for each source sample:     */
      octp = downp->octdata;                /*   align onto top octave     */
      nocts = downp->nocts;
      do {                                  /*   then for each oct:        */
        const MYFLT *coefp;
        MYFLT       *ytp, *curp;
        int         nfilt;
        curp = octp->curp;
        *curp++ = SIG;                      /*  write samp to cur buf  */
        if (curp >= octp->endp)
          curp = octp->begp;                /*    & modulo the pointer */
        octp->curp = curp;
        if (!(--nocts))  break;             /*  if lastoct, break      */
        coefp = bicoefs;  ytp = octp->feedback;
        for (nfilt = 3; nfilt--; ) {        /*  apply triple biquad:   */
          yt2 = *ytp++; yt1 = *ytp--;             /* get prev feedback */
          SIG -= (*coefp++ * yt1);                /* apply recurs filt */
          SIG -= (*coefp++ * yt2);
          *ytp++ = yt1; *ytp++ = SIG;             /* stor nxt feedback */
          SIG *= *coefp++;
          SIG += (*coefp++ * yt1);                /* apply forwrd filt */
          SIG += (*coefp++ * yt2);
        }
      } while (!(++octp->scount & 01) && octp++); /* send alt samps to nxtoct */
    } while (--nsmps);

    if (p->disprd)                         /* if displays requested,   */
      if (!(--p->dcountdown)) {            /*   on countdown           */
        linocts(downp, (MYFLT *)p->auxch2.auxp); /*   linearize the oct bufs */
        csound->display(csound, &p->octwindow);    /*      & display           */
        p->dcountdown = p->disprd;
      }

    if ((--p->scountdown)) return OK;/* if not yet time for new spec, return */
    p->scountdown = p->timcount;     /* else reset counter & proceed:        */
    downp = &p->downsig;
    specp = p->wsig;
    nocts = downp->nocts;
    octp = downp->octdata + nocts;
    dftp = (MYFLT *) specp->auxch.auxp;
    winlen = *(p->winlen);
    while (nocts--) {
      MYFLT  *bufp, *sinp, *cosp;
      int    len, *lenp, *offp, nfreqs;
      MYFLT    *begp, *curp, *endp, *linbufp;
      int      len2;
      octp--;                                /* for each oct (low to high)   */
      begp = octp->begp;
      curp = octp->curp;
      endp = octp->endp;
      if ((len = endp - curp) >= winlen)     /*   if no wrap               */
        linbufp = curp;                      /*     use samples in circbuf */
      else {
        len2 = winlen - len;
        linbufp = bufp = p->linbufp;         /*   else cp crcbuf to linbuf */
        while (len--)
          *bufp++ = *curp++;
        curp = begp;
        while (len2--)
          *bufp++ = *curp++;
      }
      cosp = p->cosp;                        /*   get start windowed sines */
      sinp = p->sinp;
      lenp = p->winlen;
      offp = p->offset;
      for (nfreqs=p->nfreqs; nfreqs--; ) {   /*   now for ea. frq this oct */
        a = FL(0.0);
        b = FL(0.0);
        bufp = linbufp + *offp++;
        for (len = *lenp++; len--; bufp++) { /* apply windowed sine seg */
          a += *bufp * *cosp++;
          b += *bufp * *sinp++;
        }
        c = a*a + b*b;                       /* get magnitude squared   */
        switch (p->dbout) {
        case 1:
          if (c < .001) c = .001;            /* and convert to db       */
          c = 10.0 * log10(c);
          break;
        case 3:
          c = sqrt(c);                       /*    or root mag          */
        case 0 :
          c = sqrt(c);                       /*    or mag               */
        case 2:
          break;                             /*    or leave mag sqrd    */
        }
        *dftp++ = (MYFLT)c;                  /* store in out spectrum   */
      }
    }
    specp->ktimstamp = csound->kcounter;     /* time-stamp the output   */
    return OK;
}

#if 0
int nocdfset(CSOUND *csound, NOCTDFT *p)
    /* noctdft - calcs disc Fourier transform of oct-downsampled data */
    /* outputs coefs (mag, db or mag2) of log freq within each octave */
{
    int     nfreqs, hanning, nocts, ncoefs;
    MYFLT   Q, *fltp;
    DOWNDAT *downp = p->dsig;
    SPECDAT *specp = p->wsig;

    p->timcount = csound->ekr * *p->iprd;
    nfreqs = *p->ifrqs;
    Q = *p->iq;
    hanning = (*p->ihann) ? 1 : 0;
    if ((p->dbout = *p->idbout) && p->dbout != 1 && p->dbout != 2) {
      return csound->InitError(csound, Str("noctdft: unknown dbout code of %d"),
                                       p->dbout);
    }
    nocts = downp->nocts;
    ncoefs = nocts * nfreqs;
    if (nfreqs != p->nfreqs || Q != p->curq         /* if anything changed */
        || p->timcount <= 0 || Q <= 0.
        || hanning != p->hanning
        || ncoefs != p->ncoefs) {                   /*     make new tables */
      double      basfrq, curfrq, frqmlt, Qfactor;
      double      theta, a, windamp, onedws, pidws;
      MYFLT       *sinp, *cosp;
      int         n, k, sumk, windsiz, *wsizp, nsamps;
      long        auxsiz;

      csound->Message(csound,
                      Str("noctdft: %s window, %s out, making tables ...\n"),
                      (hanning) ? "hanning":"hamming", outstring[p->dbout]);
      if (p->timcount <= 0)
        return csound->InitError(csound, Str("illegal iprd"));
      if (nfreqs <= 0 || nfreqs > MAXFRQS)
        return csound->InitError(csound, Str("illegal ifrqs"));
      if (Q <= FZERO)
        return csound->InitError(csound, Str("illegal Q value"));
      nsamps = downp->nsamps;
      p->nfreqs = nfreqs;
      p->curq = Q;
      p->hanning = hanning;
      p->ncoefs = ncoefs;
      basfrq = downp->hifrq/2.0 * TWOPI/downp->srate; /* oct below retuned top */
      frqmlt = pow(2.0,1.0/(double)nfreqs);    /* nfreq interval mult */
      Qfactor = TWOPI * Q;      /* Was incorrect value for 2pi?? */
      curfrq = basfrq;
      for (sumk=0,wsizp=p->winlen,n=nfreqs; n--; ) {
        *wsizp++ = k = Qfactor/curfrq + 0.5;         /* calc window sizes  */
        sumk += k;                                   /*   and find total   */
        curfrq *= frqmlt;
      }
      if ((windsiz = *(p->winlen)) > nsamps) {        /* chk longest windsiz */
        return csound->InitError(csound, Str("Q %4.1f needs %d samples, "
                                             "octdown has just %d"),
                                         Q, windsiz, nsamps);
      }
      else csound->Message(csound, Str("noctdft: Q %4.1f uses %d of "
                                       "%d samps per octdown\n"),
                                   Q, windsiz, nsamps);
      auxsiz = (nsamps + 2*sumk) * sizeof(MYFLT);    /* calc local space reqd */
      csound->AuxAlloc(csound, (long)auxsiz, &p->auxch); /* & alloc auxspace  */
      fltp = (MYFLT *) p->auxch.auxp;
      p->linbufp = fltp;          fltp += nsamps; /* linbuf must handle nsamps */
      p->sinp = sinp = fltp;      fltp += sumk;
      p->cosp = cosp = fltp;                         /* cos gets rem sumk  */
      wsizp = p->winlen;
      for (curfrq=basfrq,n=nfreqs; n--; ) {           /* now fill tables */
        windsiz = *wsizp++;
        onedws = 1.0 / windsiz;
        pidws = PI / windsiz;
        for (k=0; k<windsiz; k++) {                 /*   with sines    */
          a = sin(k * pidws);
          windamp = a * a;                        /*   times hanning */
          if (!hanning)
            windamp = 0.08 + 0.92 * windamp;    /*   or hamming    */
          windamp *= onedws;                      /*   scaled        */
          theta = k * curfrq;
          *sinp++ = windamp * sin(theta);
          *cosp++ = windamp * cos(theta);
        }
        curfrq *= frqmlt;                           /*   step by log freq  */
      }
      if (*p->idsines != FL(0.0)) {
        /* if reqd, display windowed sines immediately */
        csound->dispset(csound, &p->dwindow, p->sinp, (long) sumk,
                                Str("octdft windowed sines:"), 0, "octdft");
        csound->display(csound, &p->dwindow);
      }
      SPECset(csound,
              specp, (long)ncoefs);                /* prep the spec dspace */
      specp->downsrcp = downp;                     /*  & record its source */
    }
    specp->nfreqs = p->nfreqs;                 /* save the spec descriptors */
    specp->dbout = p->dbout;
    specp->ktimstamp = 0;                      /* init specdata to not new  */
    specp->ktimprd = p->timcount;
    p->countdown = p->timcount;                /*     & prime the countdown */
    return OK;
}

int noctdft(CSOUND *csound, NOCTDFT *p)
{
    DOWNDAT *downp;
    SPECDAT *specp;
    OCTDAT  *octp;
    MYFLT   *dftp;
    int     nocts, wrap;
    MYFLT   a, b;
    double  c;

    if ((--p->countdown))  return;    /* if not yet time for new spec, return */
    if (p->auxch.auxp==NULL) { /* RWD fix */
      return csound->PerfError(csound, Str("noctdft: not initialised"));
    }
    p->countdown = p->timcount;            /* else reset counter & proceed:   */
    downp = p->dsig;
    specp = p->wsig;
    nocts = downp->nocts;
    octp = downp->octdata + nocts;
    dftp = (MYFLT *) specp->auxch.auxp;
    while (nocts--) {
      MYFLT  *bufp, *sinp, *cosp;
      int    len, *lenp, nfreqs;
      MYFLT   *begp, *curp, *endp;
      octp--;                              /* for each octave (low to high)   */
      begp = octp->begp;
      curp = octp->curp;
      endp = octp->endp;
      wrap = curp - begp;
      bufp = p->linbufp;
      while (curp < endp)                    /*   copy circbuf to linbuf   */
        *bufp++ = *curp++;
      for (curp=begp,len=wrap; len--; )
        *bufp++ = *curp++;
      cosp = p->cosp;                        /*   get start windowed sines */
      sinp = p->sinp;
      lenp = p->winlen;
      for (nfreqs=p->nfreqs; nfreqs--; ) {   /*   now for each freq this oct: */
        a = 0.0;
        b = 0.0;
        bufp = p->linbufp;
        for (len = *lenp++; len--; bufp++) {    /*  apply windowed sine seg */
          a += *bufp * *cosp++;
          b += *bufp * *sinp++;
        }
        c = a*a + b*b;                          /*  get magnitude squared   */
        if (!(p->dbout))                        /*    & optionally convert  */
          c = sqrt(c);                          /*    to  mag or db         */
        else if (p->dbout == 1) {
          if (c < .001) c = .001;
          c = 10. * log10(c);
        }
        *dftp++ = c;                            /* store in out spectrum   */
      }
    }
    specp->ktimstamp = csound->kcounter;        /* time-stamp the output   */
    return OK;
}
#endif

int spdspset(CSOUND *csound, SPECDISP *p)
{
    char  strmsg[256];
    /* RWD is this enough? */
    if (p->wsig->auxch.auxp==NULL) {
      return csound->InitError(csound, Str("specdisp: not initialised"));
    }
    if ((p->timcount = (int)(csound->ekr * *p->iprd)) <= 0) {
      return csound->InitError(csound, Str("illegal iperiod"));
    }
    if (!(p->dwindow.windid)) {
      SPECDAT *specp = p->wsig;
      DOWNDAT *downp = specp->downsrcp;
      if (downp->lofrq > 5.) {
        sprintf(strmsg,
                Str("instr %d %s, dft (%s), %ld octaves (%d - %d Hz):"),
                (int) p->h.insdshead->p1, p->h.optext->t.inlist->arg[0],
                outstring[specp->dbout],
                downp->nocts, (int)downp->lofrq, (int)downp->hifrq);
      }
      else {                            /* more detail if low frequency  */
        sprintf(strmsg,
                Str("instr %d %s, dft (%s), %ld octaves (%3.1f - %3.1f Hz):"),
                (int) p->h.insdshead->p1, p->h.optext->t.inlist->arg[0],
                outstring[specp->dbout],
                downp->nocts, downp->lofrq, downp->hifrq);
      }
      csound->dispset(csound, &p->dwindow, (MYFLT*) specp->auxch.auxp,
                      (long)specp->npts, strmsg, (int)*p->iwtflg,
                      "specdisp");
    }
    p->countdown = p->timcount;         /* prime the countdown */
    return OK;
}

int specdisp(CSOUND *csound, SPECDISP *p)
{
    /* RWD is this enough? */
    if (p->wsig->auxch.auxp==NULL) {
      return csound->PerfError(csound, Str("specdisp: not initialised"));
    }
    if (!(--p->countdown)) {            /* on countdown     */
      csound->display(csound, &p->dwindow);     /*    display spect */
      p->countdown = p->timcount;       /*    & reset count */
    }
    return OK;
}

int sptrkset(CSOUND *csound, SPECPTRK *p)
{
    SPECDAT *inspecp = p->wsig;
    long    npts, nptls, nn, lobin;
    int     *dstp, ptlmax, inc;
    MYFLT   nfreqs, rolloff, *oct0p, *flop, *fhip, *fundp, *fendp, *fp;
    MYFLT   weight, weightsum, dbthresh, ampthresh;

    if ((npts = inspecp->npts) != p->winpts) {  /* if size has changed */
      SPECset(csound,
              &p->wfund, (long)npts);           /*   realloc for wfund */
      p->wfund.downsrcp = inspecp->downsrcp;
      p->fundp = (MYFLT *) p->wfund.auxch.auxp;
      p->winpts = npts;
        }
    if ((p->ftimcnt = (int)(csound->ekr**p->ifprd)) > 0) {/* if displaying wfund */
      SPECDISP *fdp = &p->fdisplay;
      fdp->h = p->h;
      fdp->wsig = &p->wfund;                    /*  pass the param pntrs */
      fdp->iprd = p->ifprd;
      fdp->iwtflg = p->iwtflg;
/*       fdp->altname = "specptrk"; */
/*       fdp->altarg = "X-corr"; */
      p->wfund.dbout = inspecp->dbout;
      spdspset(csound,fdp);                     /*  & call specdisp init */
    }
    else p->ftimcnt = 0;
    if ((nptls = (long)*p->inptls) <= 0 || nptls > MAXPTL) {
      return csound->InitError(csound, Str("illegal no of partials"));
    }
    p->nptls = nptls;        /* number, whether all or odd */
    if (*p->iodd == FZERO) {
      ptlmax = nptls;
      inc = 1;
    } else {
      ptlmax = nptls * 2 - 1;
      inc = 2;
    }
    dstp = p->pdist;
    nfreqs = (MYFLT)inspecp->nfreqs;
    for (nn = 1; nn <= ptlmax; nn += inc)
      *dstp++ = (int) ((log((double) nn) / LOGTWO) * nfreqs + 0.5);
    if ((rolloff = *p->irolloff) == 0. || rolloff == 1. || nptls == 1) {
      p->rolloff = 0;
      weightsum = (MYFLT)nptls;
    } else {
      MYFLT *fltp = p->pmult;
      MYFLT octdrop = (FL(1.0) - rolloff) / nfreqs;
      weightsum = FZERO;
      for (dstp = p->pdist, nn = nptls; nn--; ) {
        weight = FL(1.0) - octdrop * *dstp++;       /* rolloff * octdistance */
        weightsum += weight;
        *fltp++ = weight;
      }
      if (*--fltp < FZERO) {
        return csound->InitError(csound, Str("per oct rolloff too steep"));
      }
      p->rolloff = 1;
    }
    lobin = (long)(inspecp->downsrcp->looct * nfreqs);
    oct0p = p->fundp - lobin;                   /* virtual loc of oct 0 */

    flop = oct0p + (int)(*p->ilo * nfreqs);
    fhip = oct0p + (int)(*p->ihi * nfreqs);
    fundp = p->fundp;
    fendp = fundp + inspecp->npts;
    if (flop < fundp) flop = fundp;
    if (fhip > fendp) fhip = fendp;
    if (flop >= fhip) {         /* chk hi-lo range valid */
      return csound->InitError(csound, Str("illegal lo-hi values"));
    }
    for (fp = fundp; fp < flop; )
      *fp++ = FZERO;   /* clear unused lo and hi range */
    for (fp = fhip; fp < fendp; )
      *fp++ = FZERO;

    csound->Message(csound, Str("specptrk: %d freqs, %d%s ptls at "),
                            (int)nfreqs, (int)nptls, inc==2 ? Str(" odd") : "");
    for (nn = 0; nn < nptls; nn++)
      csound->Message(csound, "\t%d", p->pdist[nn]);
    if (p->rolloff) {
      csound->Message(csound, Str("\n\t\trolloff vals:"));
      for (nn = 0; nn < nptls; nn++)
        csound->Message(csound, "\t%4.2f", p->pmult[nn]);
    }

    dbthresh = *p->idbthresh;                     /* thresholds: */
    ampthresh = (MYFLT)exp((double)dbthresh * LOG10D20);
    switch(inspecp->dbout) {
    case 0: p->threshon = ampthresh;              /* mag */
      p->threshoff = ampthresh / FL(2.0);
                break;
    case 1: p->threshon = dbthresh;               /* db  */
      p->threshoff = dbthresh - FL(6.0);
      break;
    case 2: p->threshon = ampthresh * ampthresh;  /* mag sqrd */
      p->threshoff = p->threshon / FL(4.0);
      break;
    case 3: p->threshon = (MYFLT)sqrt(ampthresh);        /* root mag */
      p->threshoff = p->threshon / FL(1.414);
      break;
    }
    p->threshon *= weightsum;
    p->threshoff *= weightsum;
    csound->Message(csound, Str("\n\tdbthresh %4.1f: X-corr %s "
                                "threshon %4.1f, threshoff %4.1f\n"),
                            dbthresh, outstring[inspecp->dbout],
                            p->threshon, p->threshoff);
    p->oct0p = oct0p;                 /* virtual loc of oct 0 */
    p->confact = *p->iconf;
    p->flop = flop;
    p->fhip = fhip;
    p->kinterp = (*p->interp == FZERO) ? 0 : 1;
    p->playing = 0;
    p->kvalsav = *p->istrt;
    p->kval = p->kinc = FZERO;
    p->kavl = p->kanc = FZERO;
    p->jmpcount =  0;
    return OK;
}

#define STARTING 1
#define PLAYING  2

int specptrk(CSOUND *csound, SPECPTRK *p)
{
    SPECDAT *inspecp = p->wsig;

    if (inspecp->ktimstamp == csound->kcounter) {   /* if inspectrum is new: */
      MYFLT *inp = (MYFLT *) inspecp->auxch.auxp;
      MYFLT *endp = inp + inspecp->npts;
      MYFLT *inp2, sum, *fp;
      int   nn, *pdist, confirms;
      MYFLT kval, kvar, fmax, *fmaxp, absdiff, realbin;
      MYFLT *flop, *fhip, *ilop, *ihip, a, b, c, denom, delta;
      long  lobin, hibin;

      if (inp==NULL) {             /* RWD fix */
        return csound->PerfError(csound, Str("specptrk: not initialised"));
      }
      if ((kvar = *p->kvar) < FZERO)
        kvar = -kvar;
      kval = p->playing == PLAYING ? p->kval : p->kvalsav;
      lobin = (long)((kval-kvar) * inspecp->nfreqs); /* set lims of frq interest */
      hibin = (long)((kval+kvar) * inspecp->nfreqs);
      if ((flop = p->oct0p + lobin) < p->flop)  /*       as fundp bin pntrs */
        flop = p->flop;
      if ((fhip = p->oct0p + hibin) > p->fhip)  /*       within hard limits */
        fhip = p->fhip;
      ilop = inp + (flop - p->fundp);           /* similar for input bins   */
      ihip = inp + (fhip - p->fundp);
      if (p->ftimcnt) {                         /* if displaying,  */
        for (fp = p->flop; fp < flop; )         /*   clr to limits */
          *fp++ = FZERO;
        for (fp = p->fhip; fp > fhip; )
          *--fp = FZERO;
      }
      inp = ilop;
      fp = flop;
      if (p->rolloff) {
        MYFLT *pmult;
        do {
          sum = *inp;
          pdist = p->pdist + 1;
          pmult = p->pmult + 1;
          for (nn = p->nptls; --nn; ) {
            if ((inp2 = inp + *pdist++) >= endp)
              break;
            sum += *inp2 * *pmult++;
          }
          *fp++ = sum;
        } while (++inp < ihip);
      }
      else {
        do {
          sum = *inp;
          pdist = p->pdist + 1;
          for (nn = p->nptls; --nn; ) {
            if ((inp2 = inp + *pdist++) >= endp)
              break;
            sum += *inp2;
          }
          *fp++ = sum;
        } while (++inp < ihip);
      }
      fp = flop;                               /* now srch fbins for peak */
      for (fmaxp = fp, fmax = *fp; ++fp<fhip; )
        if (*fp > fmax) {
          fmax = *fp;
          fmaxp = fp;
        }
      if (!p->playing) {
        if (fmax > p->threshon)         /* not playing & threshon? */
          p->playing = STARTING;      /*   prepare to turn on    */
        else goto output;
      }
      else {
        if (fmax < p->threshoff) {      /* playing & threshoff ? */
          if (p->playing == PLAYING)
            p->kvalsav = p->kval;   /*   save val & turn off */
          p->kval = FZERO;
          p->kavl = FZERO;
          p->kinc = FZERO;
          p->kanc = FZERO;
          p->playing = 0;
          goto output;
        }
      }
      a = fmaxp>flop ? *(fmaxp-1) : FZERO;     /* calc a refined bin no */
      b = fmax;
      c = fmaxp<fhip-1 ? *(fmaxp+1) : FZERO;
      if (b < FL(2.0) * (a + c))
        denom = b * FL(2.0) - a - c;
      else denom = a + b + c;
      if (denom != FZERO)
        delta = FL(0.5) * (c - a) / denom;
      else delta = FZERO;
      realbin = (fmaxp - p->oct0p) + delta;    /* get modified bin number  */
      kval = realbin / inspecp->nfreqs;        /*     & cvt to true decoct */

      if (p->playing == STARTING) {            /* STARTING mode:           */
        if ((absdiff = kval - p->kvalsav) < FZERO)
          absdiff = -absdiff;
        confirms = (int)(absdiff * p->confact); /* get interval dependency  */
        if (p->jmpcount < confirms) {
          p->jmpcount += 1;                /* if not enough confirms,  */
          goto output;                     /*    must wait some more   */
        } else {
          p->playing = PLAYING;            /* else switch on playing   */
          p->jmpcount = 0;
          p->kval = kval;                  /*    but suppress interp   */
          p->kinc = FZERO;
        }
      } else {                                 /* PLAYING mode:            */
        if ((absdiff = kval - p->kval) < FZERO)
          absdiff = -absdiff;
        confirms = (int)(absdiff * p->confact); /* get interval dependency  */
        if (p->jmpcount < confirms) {
          p->jmpcount += 1;                /* if not enough confirms,  */
          p->kinc = FZERO;                 /*    must wait some more   */
        } else {
          p->jmpcount = 0;                 /* else OK to jump interval */
          if (p->kinterp)                  /*    with optional interp  */
            p->kinc = (kval - p->kval) / inspecp->ktimprd;
          else p->kval = kval;
        }
      }
      fmax += delta * (c - a) / FL(4.0);           /* get modified amp */
      if (p->kinterp)                         /*   & new kanc if interp */
        p->kanc = (fmax - p->kavl) / inspecp->ktimprd;
      else p->kavl = fmax;
    }
 output:
    *p->koct = p->kval;                   /* output true decoct & amp */
    *p->kamp = p->kavl;
    if (p->kinterp) {                     /* interp if reqd  */
      p->kval += p->kinc;
      p->kavl += p->kanc;
    }
    if (p->ftimcnt)
      specdisp(csound,&p->fdisplay);
    return OK;
}

int spsumset(CSOUND *csound, SPECSUM *p)
{
    p->kinterp = (*p->interp == FZERO) ? 0 : 1;
    p->kval = FZERO;
    p->kinc = FZERO;
    return OK;
}

int specsum(CSOUND *csound, SPECSUM *p)
                               /* sum all vals of a spectrum and put as ksig */
                               /*         optionally interpolate the output  */
{
    SPECDAT *specp = p->wsig;
    if (specp->auxch.auxp==NULL) { /* RWD fix */
      return csound->PerfError(csound, Str("specsum: not initialised"));
    }
    if (specp->ktimstamp == csound->kcounter) { /* if spectrum is new   */
      MYFLT *valp = (MYFLT *) specp->auxch.auxp;
      MYFLT sum = FL(0.0);
      long npts = specp->npts;                  /*   sum all the values */
      do {
        sum += *valp++;
      } while (--npts);
      if (p->kinterp)                           /*   new kinc if interp */
        p->kinc = (sum - p->kval) / specp->ktimprd;
      else p->kval = sum;
    }
    *p->ksum = p->kval;       /* output current kval */
    if (p->kinterp)           /*   & interp if reqd  */
      p->kval += p->kinc;
    return OK;
}

int spadmset(CSOUND *csound, SPECADDM *p)
{
    SPECDAT *inspec1p = p->wsig1;
    SPECDAT *inspec2p = p->wsig2;
    int   npts;

    if ((npts = inspec1p->npts) != inspec2p->npts)
      /* inspecs must agree in size */
      return csound->InitError(csound, Str("inputs have different sizes"));
    if (inspec1p->ktimprd != inspec2p->ktimprd)
      /*                time period */
      return csound->InitError(csound, Str("inputs have diff. time periods"));
    if (inspec1p->nfreqs != inspec2p->nfreqs)
      /*                frq resoltn */
      return csound->InitError(csound,
                               Str("inputs have different freq resolution"));
    if (inspec1p->dbout != inspec2p->dbout)
      /*                and db type */
      return csound->InitError(csound, Str("inputs have different amptypes"));
    if (npts != p->waddm->npts) {                 /* if out does not match ins */
      SPECset(csound,
              p->waddm, (long)npts);              /*       reinit the out spec */
      p->waddm->downsrcp = inspec1p->downsrcp;
    }
    p->waddm->ktimprd = inspec1p->ktimprd;        /* pass the other specinfo */
    p->waddm->nfreqs = inspec1p->nfreqs;
    p->waddm->dbout = inspec1p->dbout;
    p->waddm->ktimstamp = 0;                      /* mark the outspec not new */
    return OK;
}

int specaddm(CSOUND *csound, SPECADDM *p)
{
    if ((p->wsig1->auxch.auxp==NULL) || /* RWD fix */
        (p->wsig2->auxch.auxp==NULL) ||
        (p->waddm->auxch.auxp==NULL)) {
      return csound->PerfError(csound, Str("specaddm: not initialised"));
    }
    if (p->wsig1->ktimstamp == csound->kcounter) {  /* if inspec1 is new:     */
      MYFLT *in1p = (MYFLT *) p->wsig1->auxch.auxp;
      MYFLT *in2p = (MYFLT *) p->wsig2->auxch.auxp;
      MYFLT *outp = (MYFLT *) p->waddm->auxch.auxp;
      MYFLT mul2 = p->mul2;
      int   npts = p->wsig1->npts;

      do {
        *outp++ = *in1p++ + *in2p++ * mul2;         /* out = in1 + in2 * mul2 */
      } while (--npts);
      p->waddm->ktimstamp = csound->kcounter; /* mark the output spec as new */
    }
    return OK;
}

int spdifset(CSOUND *csound, SPECDIFF *p)
{
    SPECDAT *inspecp = p->wsig;
    MYFLT *lclp;
    MYFLT *outp;
    int   npts;

    if ((npts = inspecp->npts) != p->specsave.npts) { /* if inspec not matched  */
      SPECset(csound,
              &p->specsave, (long)npts);              /*   reinit the save spec */
      SPECset(csound,
              p->wdiff, (long)npts);                  /*   & the out diff spec  */
      p->wdiff->downsrcp = inspecp->downsrcp;
    }
    p->wdiff->ktimprd = inspecp->ktimprd;            /* pass the other specinfo */
    p->wdiff->nfreqs = inspecp->nfreqs;
    p->wdiff->dbout = inspecp->dbout;
    lclp = (MYFLT *) p->specsave.auxch.auxp;
    outp = (MYFLT *) p->wdiff->auxch.auxp;
    if (lclp==NULL || outp==NULL) { /* RWD  */
      return csound->InitError(csound,
                               Str("specdiff: local buffers not initialised"));
    }
    do {
      *lclp++ = FL(0.0);                    /* clr local & out spec bufs */
      *outp++ = FL(0.0);
    } while (--npts);
    p->wdiff->ktimstamp = 0;             /* mark the out spec not new */
    return OK;
}

int specdiff(CSOUND *csound, SPECDIFF *p)
{
    SPECDAT *inspecp = p->wsig;

    if ((inspecp->auxch.auxp==NULL) /* RWD fix */
        ||
        (p->specsave.auxch.auxp==NULL)
        ||
        (p->wdiff->auxch.auxp==NULL)) {
      return csound->PerfError(csound, Str("specdiff: not initialised"));
    }
    if (inspecp->ktimstamp == csound->kcounter) {   /* if inspectrum is new: */
      MYFLT *newp = (MYFLT *) inspecp->auxch.auxp;
      MYFLT *prvp = (MYFLT *) p->specsave.auxch.auxp;
      MYFLT *difp = (MYFLT *) p->wdiff->auxch.auxp;
      MYFLT newval, prvval, diff, possum = FL(0.0);
      int   npts = inspecp->npts;

      do {
        newval = *newp++;                     /* compare new & old coefs */
        prvval = *prvp;
        if ((diff = newval-prvval) > FL(0.0)) {  /* if new coef > prv coef  */
          *difp++ = diff;
          possum += diff;                     /*   enter & accum diff    */
        }
        else *difp++ = FL(0.0);               /* else enter zero         */
        *prvp++ = newval;                     /* sav newval for nxt time */
      } while (--npts);
      p->wdiff->ktimstamp = csound->kcounter; /* mark the output spec as new */
    }
    return OK;
}

int spsclset(CSOUND *csound, SPECSCAL *p)
{
    SPECDAT *inspecp = p->wsig;
    SPECDAT *outspecp = p->wscaled;
    FUNC    *ftp;
    long    npts;

    if ((npts = inspecp->npts) != outspecp->npts) {  /* if size has changed,   */
      SPECset(csound,
              outspecp, (long)npts);                 /*    realloc             */
      outspecp->downsrcp = inspecp->downsrcp;
      csound->AuxAlloc(csound, (long)npts * 2 * sizeof(MYFLT), &p->auxch);
    }
    outspecp->ktimprd = inspecp->ktimprd;      /* pass the source spec info     */
    outspecp->nfreqs = inspecp->nfreqs;
    outspecp->dbout = inspecp->dbout;
    p->fscale = (MYFLT *) p->auxch.auxp;       /* setup scale & thresh fn areas */
    if (p->fscale==NULL) {  /* RWD fix */
      return csound->InitError(csound,
                               Str("specscal: local buffer not initialised"));
    }
    p->fthresh = p->fscale + npts;
    if ((ftp=csound->FTFind(csound, p->ifscale)) == NULL) {
      /* if fscale given,        */
      return csound->InitError(csound, Str("missing fscale table"));
    }
    else {
      long nn = npts;
      long phs = 0;
      long inc = (long)PHMASK / npts;
      long lobits = ftp->lobits;
      MYFLT *ftable = ftp->ftable;
      MYFLT *flp = p->fscale;
      do {
        *flp++ = *(ftable + (phs >> lobits));    /*  sample into scale area */
        phs += inc;
      } while (--nn);
    }
    if ((p->thresh = (int)*p->ifthresh)
        && (ftp=csound->FTFind(csound, p->ifthresh)) != NULL) {
      /* if fthresh given,       */
      long nn = npts;
      long phs = 0;
      long inc = (long)PHMASK / npts;
      long lobits = ftp->lobits;
      MYFLT *ftable = ftp->ftable;
      MYFLT *flp = p->fthresh;
      do {
        *flp++ = *(ftable + (phs >> lobits));    /*  sample into thresh area */
        phs += inc;
      } while (--nn);
    }
    else p->thresh = 0;
    outspecp->ktimstamp = 0;                     /* mark the out spec not new */
    return OK;
}

int specscal(CSOUND *csound, SPECSCAL *p)
{
    SPECDAT *inspecp = p->wsig;
    if ((inspecp->auxch.auxp==NULL) /* RWD fix */
        ||
        (p->wscaled->auxch.auxp==NULL)
        ||
        (p->fscale==NULL)) {
      return csound->PerfError(csound, Str("specscal: not intiialised"));
    }
    if (inspecp->ktimstamp == csound->kcounter) {   /* if inspectrum is new: */
      SPECDAT *outspecp = p->wscaled;
      MYFLT *inp = (MYFLT *) inspecp->auxch.auxp;
      MYFLT *outp = (MYFLT *) outspecp->auxch.auxp;
      MYFLT *sclp = p->fscale;
      long npts = inspecp->npts;

      if (p->thresh) {                              /* if thresh requested,  */
        MYFLT *threshp = p->fthresh;
        MYFLT val;
        do {
          if ((val = *inp++ - *threshp++) > FL(0.0)) /* for vals above thresh */
            *outp++ = val * *sclp;                   /*     scale & write out */
          else *outp++ = FL(0.0);                    /*   else output is 0.   */
          sclp++;
        } while (--npts);
      }
      else {
        do {
          *outp++ = *inp++ * *sclp++;             /* no thresh: rescale only */
        } while (--npts);
      }
      outspecp->ktimstamp = csound->kcounter;     /* mark the outspec as new */
    }
    return OK;
}

int sphstset(CSOUND *csound, SPECHIST *p)
{
    SPECDAT *inspecp = p->wsig;
    MYFLT *lclp;
    MYFLT *outp;
    int   npts;

    if ((npts = inspecp->npts) != p->accumer.npts) { /* if inspec not matched   */
      SPECset(csound,
              &p->accumer, (long)npts);              /*   reinit the accum spec */
      SPECset(csound,
              p->wacout, (long)npts);                /*    & the output spec    */
      p->wacout->downsrcp = inspecp->downsrcp;
    }
    p->wacout->ktimprd = inspecp->ktimprd;           /* pass the other specinfo */
    p->wacout->nfreqs = inspecp->nfreqs;
    p->wacout->dbout = inspecp->dbout;
    lclp = (MYFLT *) p->accumer.auxch.auxp;
    outp = (MYFLT *) p->wacout->auxch.auxp;
    if (lclp==NULL || outp==NULL) { /* RWD fix */
      return csound->InitError(csound,
                               Str("spechist: local buffers not intiialised"));
    }
    do {
      *lclp++ = FL(0.0);                    /* clr local & out spec bufs */
      *outp++ = FL(0.0);
    } while (--npts);
    p->wacout->ktimstamp = 0;             /* mark the out spec not new */
    return OK;
}

int spechist(CSOUND *csound, SPECHIST *p)
{
    SPECDAT *inspecp = p->wsig;
    if ((inspecp->auxch.auxp==NULL) /* RWD fix */
        ||
        (p->accumer.auxch.auxp==NULL)
        ||
        (p->wacout->auxch.auxp==NULL)) {
      return csound->PerfError(csound, Str("spechist: not initialised"));
    }
    if (inspecp->ktimstamp == csound->kcounter) {   /* if inspectrum is new: */
      MYFLT *newp = (MYFLT *) inspecp->auxch.auxp;
      MYFLT *acup = (MYFLT *) p->accumer.auxch.auxp;
      MYFLT *outp = (MYFLT *) p->wacout->auxch.auxp;
      MYFLT newval;
      int   npts = inspecp->npts;

      do {
        newval = *acup + *newp++;           /* add new to old coefs */
        *acup++ = newval;                   /* sav in accumulator   */
        *outp++ = newval;                   /* & copy to output     */
      } while (--npts);
      p->wacout->ktimstamp = csound->kcounter; /* mark the output spec as new */
    }
    return OK;
}

int spfilset(CSOUND *csound, SPECFILT *p)
{
    SPECDAT *inspecp = p->wsig;
    SPECDAT *outspecp = p->wfil;
    FUNC    *ftp;
    long    npts;

    if ((npts = inspecp->npts) != outspecp->npts) {  /* if inspec not matched */
      SPECset(csound,
              outspecp, (long)npts);                 /*   reinit the out spec */
      csound->AuxAlloc(csound,
                       (long)npts*2* sizeof(MYFLT),
                       &p->auxch);                   /*   & local auxspace  */
      p->coefs = (MYFLT *) p->auxch.auxp;            /*   reassign filt tbls  */
      p->states = p->coefs + npts;
    }
    if (p->coefs==NULL || p->states==NULL) { /* RWD fix */
      return csound->InitError(csound,
                               Str("specfilt: local buffers not initialised"));
    }
    outspecp->ktimprd = inspecp->ktimprd;          /* pass other spect info */
    outspecp->nfreqs = inspecp->nfreqs;
    outspecp->dbout = inspecp->dbout;
    outspecp->downsrcp = inspecp->downsrcp;
    if ((ftp=csound->FTFind(csound, p->ifhtim)) == NULL) {
      /* if fhtim table given,    */
      return csound->InitError(csound, Str("missing htim ftable"));
    }
    {
      long nn = npts;
      long phs = 0;
      long inc = (long)PHMASK / npts;
      long lobits = ftp->lobits;
      MYFLT *ftable = ftp->ftable;
      MYFLT *flp = p->coefs;
      do {
        *flp++ = *(ftable + (phs >> lobits));    /*  sample into coefs area */
        phs += inc;
      } while (--nn);
    }
    {
      long  nn = npts;
      MYFLT *flp = p->coefs;
      double halftim, reittim = inspecp->ktimprd * csound->onedkr;
      do {
        if ((halftim = *flp) > 0.)
          *flp++ = (MYFLT)pow(0.5, reittim/halftim);
        else {
          return csound->InitError(csound,
                                   Str("htim ftable must be all-positive"));
        }
      } while (--nn);
    }
    csound->Message(csound, Str("coef range: %6.3f - %6.3f\n"),
                            *p->coefs, *(p->coefs+npts-1));
    {
      MYFLT *flp = (MYFLT *) p->states;
      do {
        *flp++ = FL(0.0);               /* clr the persist buf state mem */
      } while (--npts);
    }
    outspecp->ktimstamp = 0;            /* mark the output spec as not new */
    return OK;
}

int specfilt(CSOUND *csound, SPECFILT *p)
{
    if (p->wsig->ktimstamp == csound->kcounter) {   /* if input spec is new,  */
      SPECDAT *inspecp = p->wsig;
      SPECDAT *outspecp = p->wfil;
      MYFLT *newp = (MYFLT *) inspecp->auxch.auxp;
      MYFLT *outp = (MYFLT *) outspecp->auxch.auxp;
      MYFLT curval, *coefp = p->coefs;
      MYFLT *persp = p->states;
      int   npts = inspecp->npts;

      if (newp==NULL || outp==NULL || coefp==NULL || persp==NULL) { /* RWD */
        return csound->PerfError(csound, Str("specfilt: not initialised"));
      }
      do {                                         /* for npts of inspec:     */
        *outp++ = curval = *persp;                 /*   output current point  */
        *persp++ = *coefp++ * curval + *newp++;    /*   decay & addin newval  */
      } while (--npts);
      outspecp->ktimstamp = csound->kcounter;      /* mark output spec as new */
    }
    return OK;
}

#define S       sizeof

static OENTRY localops[] = {
{ "spectrum", S(SPECTRUM),7, "w", "siiiqoooo",
                                   (SUBR)spectset,(SUBR)spectrum,(SUBR)spectrum},
{ "specaddm", S(SPECADDM),5, "w",  "wwp",  (SUBR)spadmset,NULL,  (SUBR)specaddm},
{ "specdiff", S(SPECDIFF),5, "w",  "w",    (SUBR)spdifset,NULL,  (SUBR)specdiff},
{ "specscal", S(SPECSCAL),5, "w",  "wii",  (SUBR)spsclset,NULL,  (SUBR)specscal},
{ "spechist", S(SPECHIST),5, "w",  "w",    (SUBR)sphstset,NULL,  (SUBR)spechist},
{ "specfilt", S(SPECFILT),5, "w",  "wi",   (SUBR)spfilset,NULL,  (SUBR)specfilt},
{ "specptrk", S(SPECPTRK),5, "kk", "wkiiiiiioqooo",
                                             (SUBR)sptrkset,NULL,(SUBR)specptrk},
{ "specsum",  S(SPECSUM), 5, "k",  "wo",   (SUBR)spsumset,NULL,  (SUBR)specsum },
{ "specdisp", S(SPECDISP),5, "",   "wio",  (SUBR)spdspset,NULL,  (SUBR)specdisp},
{ "pitch", S(PITCH),     5,    "kk", "aiiiiqooooojo",
                                             (SUBR)pitchset, NULL, (SUBR)pitch },
{ "maca", S(SUM),        5,  "a", "y",    (SUBR)macset,      NULL, (SUBR)maca    },
{ "mac", S(SUM),         5,  "a", "Z",    (SUBR)macset,      NULL, (SUBR)mac     },
{ "clockon", S(CLOCK),   3,  "",  "i",    (SUBR)clockset, (SUBR)clockon, NULL    },
{ "clockoff", S(CLOCK),  3,  "",  "i",    (SUBR)clockset, (SUBR)clockoff, NULL   },
{ "readclock", S(CLKRD), 1,  "i", "i",    (SUBR)clockread, NULL, NULL            },
{ "pitchamdf",S(PITCHAMDF),5,"kk","aiioppoo",
                                       (SUBR)pitchamdfset, NULL, (SUBR)pitchamdf },
{ "hsboscil",S(HSBOSC),  5,  "a", "kkkiiioo",(SUBR)hsboscset,NULL,(SUBR)hsboscil },
{ "phasorbnk", S(PHSORBNK),7,"s", "xkio",
                                (SUBR)phsbnkset, (SUBR)kphsorbnk, (SUBR)phsorbnk },
{ "adsynt",S(HSBOSC),    5,  "a", "kkiiiio", (SUBR)adsyntset, NULL, (SUBR)adsynt },
{ "mpulse", S(IMPULSE),  5,  "a", "kko",  (SUBR)impulse_set, NULL, (SUBR)impulse },
{ "lpf18", S(LPF18),     5,  "a", "akkk",  (SUBR)lpf18set, NULL, (SUBR)lpf18db   },
{ "waveset", S(BARRI),   5,  "a", "ako",   (SUBR)wavesetset,  NULL, (SUBR)waveset},
{ "pinkish", S(PINKISH),  5, "a", "xoooo", (SUBR)pinkset, NULL, (SUBR)pinkish },
{ "noise",  S(VARI),   5,    "a", "xk",   (SUBR)varicolset, NULL, (SUBR)varicol },
{ "transeg", S(TRANSEG), 7,  "s", "iiim", (SUBR)trnset,(SUBR)ktrnseg,(SUBR)trnseg},
{ "clip", S(CLIP),       5,  "a", "aiiv", (SUBR)clip_set, NULL, (SUBR)clip  },
{ "cpuprc", S(CPU_PERC), 1,     "",     "ii",   (SUBR)cpuperc, NULL, NULL   },
{ "maxalloc", S(CPU_PERC), 1,   "",     "ii",   (SUBR)maxalloc, NULL, NULL  },
{ "active", 0xffff                                                          },
{ "active.i", S(INSTCNT),1,     "i",    "i",    (SUBR)instcount, NULL, NULL },
{ "active.k", S(INSTCNT),2,     "k",    "k",    NULL, (SUBR)instcount, NULL },
{ "p.i", S(PFUN),        1,     "i",    "i",     (SUBR)pfun, NULL, NULL     },
{ "p.k", S(PFUN),        2,     "k",    "k",     NULL, (SUBR)pfun, NULL     },
{ "mute", S(MUTE), 1,          "",      "So",   (SUBR)mute_inst             },
#ifdef BETA
{ "oscilv",  0xfffe                                                         },
{ "oscilv.kk", S(XOSC),  5,     "a",   "kkio", (SUBR)Foscset, NULL, (SUBR)Fosckk },
{ "oscilv.ka", S(XOSC),  5,     "a",   "kaio", (SUBR)Foscset, NULL, (SUBR)Foscka },
{ "oscilv.ak", S(XOSC),  5,     "a",   "akio", (SUBR)Foscset, NULL, (SUBR)Foscak },
{ "oscilv.aa", S(XOSC),  5,     "a",   "aaio", (SUBR)Foscset, NULL, (SUBR)Foscaa },
#endif
};

LINKAGE

