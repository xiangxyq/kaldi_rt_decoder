/*
*****************************************************************************
*
*      GSM AMR-NB speech codec   R98   Version 7.6.0   December 12, 2001
*                                R99   Version 3.3.0                
*                                REL-4 Version 4.1.0                
*
*****************************************************************************
*
*      File             : c2_9pf.c
*      Purpose          : Searches a 9 bit algebraic codebook containing 
*                         2 pulses in a frame of 40 samples.
*
*****************************************************************************
*/

/*
*****************************************************************************
*                         MODULE INCLUDE FILE AND VERSION ID
*****************************************************************************
*/
#include "c2_9pf.h"
const char c2_9pf_id[] = "@(#)$Id $" c2_9pf_h;
 
/*
*****************************************************************************
*                         INCLUDE FILES
*****************************************************************************
*/
#include "typedef.h"
#include "basic_op.h"
#include "count.h"
#include "inv_sqrt.h"
#include "cnst.h"
#include "cor_h.h"
#include "set_sign.h"

/*
*****************************************************************************
*                         LOCAL VARIABLES AND TABLES
*****************************************************************************
*/
#define NB_PULSE  2

#include "c2_9pf.tab"

/*
*****************************************************************************
*                         DECLARATION OF PROTOTYPES
*****************************************************************************
*/
static void search_2i40(
    Word16 subNr,       /* i : subframe number                               */
    Word16 dn[],        /* i : correlation between target and h[]            */
    Word16 rr[][L_CODE],/* i : matrix of autocorrelation                     */
    Word16 codvec[]     /* o : algebraic codebook vector                     */
);
static Word16 build_code(
    Word16 subNr,       /* i : subframe number                               */
    Word16 codvec[],    /* i : algebraic codebook vector                     */
    Word16 dn_sign[],   /* i : sign of dn[]                                  */
    Word16 cod[],       /* o : algebraic (fixed) codebook excitation         */
    Word16 h[],         /* i : impulse response of weighted synthesis filter */
    Word16 y[],         /* o : filtered fixed codebook excitation            */
    Word16 sign[]       /* o : sign of 2 pulses                              */
);


/*
*****************************************************************************
*                         PUBLIC PROGRAM CODE
*****************************************************************************
*/
/*************************************************************************
 *
 *  FUNCTION:  code_2i40_9bits()
 *
 *  PURPOSE:  Searches a 9 bit algebraic codebook containing 2 pulses
 *            in a frame of 40 samples.
 *
 *  DESCRIPTION:
 *    The code length is 40, containing 2 nonzero pulses: i0...i1.
 *    All pulses can have two possible amplitudes: +1 or -1.
 *    Pulse i0 can have 8 possible positions, pulse i1 can have
 *    8 positions. Also coded is which track pair should be used,
 *    i.e. first or second pair. Where each pair contains 2 tracks.
 *
 *     First subframe:
 *     first   i0 :  0, 5, 10, 15, 20, 25, 30, 35.
 *             i1 :  2, 7, 12, 17, 22, 27, 32, 37.
 *     second  i0 :  1, 6, 11, 16, 21, 26, 31, 36.
 *             i1 :  3, 8, 13, 18, 23, 28, 33, 38.
 *
 *     Second subframe:
 *     first   i0 :  0, 5, 10, 15, 20, 25, 30, 35.
 *             i1 :  3, 8, 13, 18, 23, 28, 33, 38.
 *     second  i0 :  2, 7, 12, 17, 22, 27, 32, 37.
 *             i1 :  4, 9, 14, 19, 24, 29, 34, 39.
 *
 *     Third subframe:
 *     first   i0 :  0, 5, 10, 15, 20, 25, 30, 35.
 *             i1 :  2, 7, 12, 17, 22, 27, 32, 37.
 *     second  i0 :  1, 6, 11, 16, 21, 26, 31, 36.
 *             i1 :  4, 9, 14, 19, 24, 29, 34, 39.
 *
 *     Fourth subframe:
 *     first   i0 :  0, 5, 10, 15, 20, 25, 30, 35.
 *             i1 :  3, 8, 13, 18, 23, 28, 33, 38.
 *     second  i0 :  1, 6, 11, 16, 21, 26, 31, 36.
 *             i1 :  4, 9, 14, 19, 24, 29, 34, 39.
 *
 *************************************************************************/

Word16 code_2i40_9bits(
    Word16 subNr,       /* i : subframe number                               */
    Word16 x[],         /* i : target vector                                 */
    Word16 h[],         /* i : impulse response of weighted synthesis filter */
                        /*     h[-L_subfr..-1] must be set to zero.          */
    Word16 T0,          /* i : Pitch lag                                     */
    Word16 pitch_sharp, /* i : Last quantized pitch gain                     */
    Word16 code[],      /* o : Innovative codebook                           */
    Word16 y[],         /* o : filtered fixed codebook excitation            */
    Word16 * sign       /* o : Signs of 2 pulses                             */
)
{
    Word16 codvec[NB_PULSE];
    Word16 dn[L_CODE], dn2[L_CODE], dn_sign[L_CODE];
    Word16 rr[L_CODE][L_CODE];
    Word16 i, index, sharp;
    
    sharp = shl(pitch_sharp, 1);
    test ();
    if (sub(T0, L_CODE) < 0)
       for (i = T0; i < L_CODE; i++) {
          h[i] = add(h[i], mult(h[i - T0], sharp));         move16 ();
       }
    cor_h_x(h, x, dn, 1);
    set_sign(dn, dn_sign, dn2, 8); /* dn2[] not used in this codebook search */
    cor_h(h, dn_sign, rr);
    search_2i40(subNr, dn, rr, codvec);
                                    move16 (); /* function result */
    index = build_code(subNr, codvec, dn_sign, code, h, y, sign);
    
  /*-----------------------------------------------------------------*
   * Compute innovation vector gain.                                 *
   * Include fixed-gain pitch contribution into code[].              *
   *-----------------------------------------------------------------*/
    
    test ();
    if (sub(T0, L_CODE) < 0)
       for (i = T0; i < L_CODE; i++) {
          code[i] = add(code[i], mult(code[i - T0], sharp));  move16 ();
       }
    return index;
}


/*
*****************************************************************************
*                         PRIVATE PROGRAM CODE
*****************************************************************************
*/

/*************************************************************************
 *
 *  FUNCTION  search_2i40()
 *
 *  PURPOSE: Search the best codevector; determine positions of the 2 pulses
 *           in the 40-sample frame.
 *
 *************************************************************************/

#define _1_2    (Word16)(32768L/2)
#define _1_4    (Word16)(32768L/4)
#define _1_8    (Word16)(32768L/8)
#define _1_16   (Word16)(32768L/16)

static void search_2i40(
    Word16 subNr,        /* i : subframe number                    */
    Word16 dn[],         /* i : correlation between target and h[] */
    Word16 rr[][L_CODE], /* i : matrix of autocorrelation          */
    Word16 codvec[]      /* o : algebraic codebook vector          */
)
{
    Word16 i0, i1;
    Word16 ix = 0; /* initialization only needed to keep gcc silent */
    Word16  track1, ipos[NB_PULSE];
    Word16 psk, ps0, ps1, sq, sq1;
    Word16 alpk, alp, alp_16;
    Word32 s, alp0, alp1;
    Word16 i;    

    psk = -1;                     move16 ();
    alpk = 1;                     move16 ();
    for (i = 0; i < NB_PULSE; i++)
    {
       codvec[i] = i;             move16 ();
    }
 
    for (track1 = 0; track1 < 2; track1++) {		
       /* fix starting position */
       
       ipos[0] = startPos[subNr*2+8*track1];     move16 ();
       ipos[1] = startPos[subNr*2+1+8*track1];   move16 ();          
       

          /*----------------------------------------------------------------*
           * i0 loop: try 8 positions.                                      *
           *----------------------------------------------------------------*/
          
			                  move16 (); /* account for ptr. init. (rr[io]) */
          for (i0 = ipos[0]; i0 < L_CODE; i0 += STEP) {
             
             ps0 = dn[i0];                    move16 ();
             alp0 = L_mult(rr[i0][i0], _1_4);
             
          /*----------------------------------------------------------------*
           * i1 loop: 8 positions.                                          *
           *----------------------------------------------------------------*/
             
             sq = -1;                         move16 ();
             alp = 1;                         move16 ();
             ix = ipos[1];                    move16 ();
             
        /*-------------------------------------------------------------------*
        *  These index have low complexity address computation because      *
        *  they are, in fact, pointers with fixed increment.  For example,  *
        *  "rr[i0][i2]" is a pointer initialized to "&rr[i0][ipos[2]]"      *
        *  and incremented by "STEP".                                       *
        *-------------------------------------------------------------------*/
             
             move16 (); /* account for ptr. init. (rr[i1]) */
             move16 (); /* account for ptr. init. (dn[i1]) */
             move16 (); /* account for ptr. init. (rr[io]) */
             for (i1 = ipos[1]; i1 < L_CODE; i1 += STEP) {
                ps1 = add(ps0, dn[i1]);   /* idx increment = STEP */
                
                /* alp1 = alp0 + rr[i0][i1] + 1/2*rr[i1][i1]; */
                   
                alp1 = L_mac(alp0, rr[i1][i1], _1_4); /* idx incr = STEP */
                alp1 = L_mac(alp1, rr[i0][i1], _1_2); /* idx incr = STEP */
                
                sq1 = mult(ps1, ps1);
                
                alp_16 = round(alp1);
                
                s = L_msu(L_mult(alp, sq1), sq, alp_16);

                test ();
                if (s > 0) {
                   sq = sq1;                  move16 ();
                   alp = alp_16;              move16 ();
                   ix = i1;                   move16 ();
                }
             }
             
          /*----------------------------------------------------------------*
           * memorise codevector if this one is better than the last one.   *
           *----------------------------------------------------------------*/
             
             s = L_msu(L_mult(alpk, sq), psk, alp);
             
             test ();
             if (s > 0) {
                psk = sq;                     move16 ();
                alpk = alp;                   move16 ();
                codvec[0] = i0;               move16 ();
                codvec[1] = ix;               move16 ();
             }
          }
    }
    
    return;
}

/*************************************************************************
 *
 *  FUNCTION:  build_code()
 *
 *  PURPOSE: Builds the codeword, the filtered codeword and index of the
 *           codevector, based on the signs and positions of 2 pulses.
 *
 *************************************************************************/

static Word16 build_code(
    Word16 subNr,     /* i : subframe number                               */
    Word16 codvec[],  /* i : position of pulses                            */
    Word16 dn_sign[], /* i : sign of pulses                                */
    Word16 cod[],     /* o : innovative code vector                        */
    Word16 h[],       /* i : impulse response of weighted synthesis filter */
    Word16 y[],       /* o : filtered innovative code                      */
    Word16 sign[]     /* o : sign of 2 pulses                              */
)
{
    Word16 i, j, k, track, first, index, _sign[NB_PULSE], indx, rsign;
    Word16 *p0, *p1, *pt;
    Word32 s;
    static Word16 trackTable[4*5] = {
       0, 1, 0, 1, -1, /* subframe 1; track to code; -1 do not code this position */
       0, -1, 1, 0, 1, /* subframe 2 */
       0, 1, 0, -1, 1, /* subframe 3 */
       0, 1, -1, 0, 1};/* subframe 4 */

    pt = &trackTable[add(subNr, shl(subNr, 2))];
	
    for (i = 0; i < L_CODE; i++) {
        cod[i] = 0;                                 move16 ();
    }
    
    indx = 0; move16 ();
    rsign = 0; move16 ();
    for (k = 0; k < NB_PULSE; k++) {
       i = codvec[k];    /* read pulse position */  move16 ();
       j = dn_sign[i];   /* read sign           */  move16 ();

       index = mult(i, 6554);    /* index = pos/5 */
                                 /* track = pos%5 */
       track = sub(i, extract_l(L_shr(L_mult(index, 5), 1)));
       
       first = pt[track];                           move16 ();

       test ();
       if (first == 0) {
          test ();
          if (k == 0) {
             track = 0;                             move16 ();
          } else {
             track = 1;                             move16 ();           
             index = shl(index, 3);
          }             
       } else {
          test ();
          if (k == 0) {
             track = 0;                             move16 ();            
             index = add(index, 64);  /* table bit is MSB */
          } else {
             track = 1;                             move16 ();           
             index = shl(index, 3);
          }             
       }

       test ();
       if (j > 0) {
          cod[i] = 8191;                            move16 ();
          _sign[k] = 32767;                         move16 ();
          rsign = add(rsign, shl(1, track));
       } else {
          cod[i] = -8192;                           move16 ();
          _sign[k] = (Word16) - 32768L;             move16 ();
        }
       
       indx = add(indx, index);
    }
    *sign = rsign;                                  move16 ();
                                  
    p0 = h - codvec[0];                             move16 ();
    p1 = h - codvec[1];                             move16 ();
    
    for (i = 0; i < L_CODE; i++) {
       s = 0;                                       move32 ();
       s = L_mac(s, *p0++, _sign[0]);
       s = L_mac(s, *p1++, _sign[1]);
       y[i] = round(s);                             move16 ();
    }
    
    return indx;
}
