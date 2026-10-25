/* compiler: ee-gcc2.96-no-strict-aliasing */
typedef struct SpeexCallback SpeexCallback;
typedef struct CombFilterMem CombFilterMem;
typedef struct SpeexMode SpeexMode;
typedef struct SpeexBits SpeexBits;
/* Copyright (C) 2002 Jean-Marc Valin 
   File: nb_celp.c

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#define FIXED_POINT
#define NULL 0
typedef short spx_word16_t;
typedef int spx_word32_t;
typedef short spx_coef_t;
typedef short spx_lsp_t;
typedef int spx_sig_t;
typedef int spx_mem_t;
struct SpeexBits;
struct SpeexMode;
struct CombFilterMem;
struct SpeexCallback {
 int callback_id; int (*func)(SpeexBits *,void *,void *); void *data; void *reserved1; int reserved2;
};
typedef void (*lsp_quant_func)(spx_lsp_t *, spx_lsp_t *, int, SpeexBits *);


typedef void (*lsp_unquant_func)(spx_lsp_t *, int, SpeexBits *);



typedef int (*ltp_quant_func)(spx_sig_t *, spx_sig_t *, spx_coef_t *, spx_coef_t *, 
                              spx_coef_t *, spx_sig_t *, const void *, int, int, spx_word16_t, 
                              int, int, SpeexBits*, char *, spx_sig_t *, spx_sig_t *, int, int);


typedef void (*ltp_unquant_func)(spx_sig_t *, int, int, spx_word16_t, const void *, int, int *,
                                 spx_word16_t *, SpeexBits*, char*, int, int, spx_word16_t, int);



typedef void (*innovation_quant_func)(spx_sig_t *, spx_coef_t *, spx_coef_t *, spx_coef_t *, const void *, int, int, 
                                      spx_sig_t *, spx_sig_t *, SpeexBits *, char *, int);


typedef void (*innovation_unquant_func)(spx_sig_t *, const void *, int, SpeexBits*, char *);


typedef struct SpeexSubmode {
   int     lbr_pitch;          
   int     forced_pitch_gain;  
   int     have_subframe_gain; 
   int     double_codebook;    
   
   lsp_quant_func    lsp_quant; 
   lsp_unquant_func  lsp_unquant; 

   
   ltp_quant_func    ltp_quant; 
   ltp_unquant_func  ltp_unquant; 
   const void             *ltp_params; 

   
   innovation_quant_func innovation_quant; 
   innovation_unquant_func innovation_unquant; 
   const void             *innovation_params; 

   
   spx_word16_t      lpc_enh_k1; 
   spx_word16_t      lpc_enh_k2; 
   spx_word16_t      lpc_enh_k3; 
   spx_word16_t      comb_gain;  

   int               bits_per_frame; 
} SpeexSubmode;

#define SPEEX_MAX_CALLBACKS 16
typedef struct DecState {
   const SpeexMode *mode;       
   int    first;          
   int    count_lost;     
   int    frameSize;      
   int    subframeSize;   
   int    nbSubframes;    
   int    windowSize;     
   int    lpcSize;        
   int    bufSize;        
   int    min_pitch;      
   int    max_pitch;      
   int    sampling_rate;

#ifdef EPIC_48K
   int    lbr_48k;
#endif

   spx_word16_t  last_ol_gain;   

   char  *stack;          
   spx_sig_t *inBuf;          
   spx_sig_t *frame;          
   spx_sig_t *excBuf;         
   spx_sig_t *exc;            
   spx_sig_t *innov;          
   spx_lsp_t *qlsp;           
   spx_lsp_t *old_qlsp;       
   spx_lsp_t *interp_qlsp;    
   spx_coef_t *interp_qlpc;    
   spx_mem_t *mem_sp;         
   spx_word32_t *pi_gain;        
   int    last_pitch;     
   spx_word16_t  last_pitch_gain; 
   spx_word16_t  pitch_gain_buf[3];  
   int    pitch_gain_buf_idx; 

   int    encode_submode;
   const SpeexSubmode * const *submodes; 
   int    submodeID;      
   int    lpc_enh_enabled; 
   CombFilterMem *comb_mem;
   SpeexCallback speex_callbacks[SPEEX_MAX_CALLBACKS];

   SpeexCallback user_callback;

   
   float  voc_m1;
   float  voc_m2;
   float  voc_mean;
   int    voc_offset;

   int    dtx_enabled;
} DecState;
#define SUBMODE(x) (st->submodes[st->submodeID]->x)
#define ALIGN(stack,size) ((stack)+=((size)-(int)(stack))&((size)-1))
#define PUSH(stack,size,type) (ALIGN(stack,16),(stack)+=(size)*sizeof(type),(type*)((stack)-(size)*sizeof(type)))
#define GAMMA_SCALING 32768.0f
#define GAIN_SCALING 64
#define GAIN_SCALING_1 0.015625f
#define SIG_SCALING 16384.0f
#define SIG_SHIFT 14
#define LSP_SCALING 8192.0f
#define LSP_MARGIN 16
#define SB_SUBMODE_BITS 3
#define SPEEX_SUBMODE_BITS_PER_FRAME 1
#define PSHR(a,b) (((a)+(1<<((b)-1)))>>(b))
#define MULT16_32_Q15(a,b) ((a)*((b)>>15)+(((a)*((b)&32767))>>15))
#define MULT16_32_Q14(a,b) ((a)*((b)>>14)+(((a)*((b)&16383))>>14))
#define gain_3tap_to_1tap(g) (((g)[1]<0?-(g)[1]:(g)[1])+((g)[0]>0?(g)[0]:-((g)[0]>>1))+((g)[2]>0?(g)[2]:-((g)[2]>>1)))
unsigned func_00567B38(SpeexBits *,int);
int func_00567C48(SpeexBits *);
void func_00567E90(const SpeexMode *,int,int *);
void *func_00567F28(void *,const void *,int);
void func_00567F40(const char *);
void func_00567F48(const char *);
void func_00567F58(int *, int, float);
void func_00568010(SpeexBits *,int);
int func_00568038(SpeexBits *,SpeexCallback *,void *);
void func_0056A290(DecState *,short *,char *);
void func_0056D3B0(short *,short *,int,char *);
void func_0056D5D0(short *,int,short);
void func_0056D698(short *,short *,short *,int,int,int);
void func_0056DD70(short,short *,short *,int);
void func_0056DDE0(int *,int *,int,int);
void func_0056DF58(int *,short *,short *,int *,int,int,int *);
void func_0056E170(int *,short *,int *,int,int,int *);
void func_0056E4B0(int *,int *,short *,int,int,int,short *,short,CombFilterMem *);
float func_0057D488(float);
void *func_005A48D8(void *,int,unsigned);
extern const SpeexMode D_006C9558;
extern const int D_006CAAD0[];
extern const short D_006CAB60[], D_006CAB78[];
static inline float root(float x) { float y; __asm__("sqrt.s %0,%1":"=f"(y):"f"(x)); return y; }
int func_0056A780(void *state, SpeexBits *bits, void *vout)
{
   DecState *st;
   int i, sub;
   int pitch;
   spx_word16_t pitch_gain[3];
   spx_word32_t ol_gain=0;
   int ol_pitch=0;
   spx_word16_t ol_pitch_coef=0;
   int best_pitch=40;
   spx_word16_t best_pitch_gain=0;
   int wideband;
   int m;
   char *stack;
   spx_coef_t *awk1, *awk2, *awk3;
   spx_word16_t pitch_average=0;
#ifdef EPIC_48K
   int pitch_half[2];
   int ol_pitch_id=0;
#endif
   spx_word16_t *out = (spx_word16_t *)vout;

   st=(DecState*)state;
   stack=st->stack;

   if (st->encode_submode)
   {
#ifdef EPIC_48K
   if (!st->lbr_48k) {
#endif

   
   if (!bits && st->dtx_enabled)
   {
      st->submodeID=0;
   } else 
   {
      
      if (!bits)
      {
         func_0056A290(st, out, stack);
         return 0;
      }

      
      do {
         if (func_00567C48(bits)<5)
            return -1;
         wideband = func_00567B38(bits, 1);
         if (wideband) 
         {
            int submode;
            int advance;
            advance = submode = func_00567B38(bits, SB_SUBMODE_BITS);
            func_00567E90(&D_006C9558, SPEEX_SUBMODE_BITS_PER_FRAME, &advance);
            if (advance < 0)
            {
               func_00567F48 ("Invalid wideband mode encountered. Corrupted stream?");
               return -2;
            } 
            advance -= (SB_SUBMODE_BITS+1);
            func_00568010(bits, advance);
            
            if (func_00567C48(bits)<5)
               return -1;
            wideband = func_00567B38(bits, 1);
            if (wideband)
            {
               advance = submode = func_00567B38(bits, SB_SUBMODE_BITS);
               func_00567E90(&D_006C9558, SPEEX_SUBMODE_BITS_PER_FRAME, &advance);
               if (advance < 0)
               {
                  func_00567F48 ("Invalid wideband mode encountered: corrupted stream?");
                  return -2;
               } 
               advance -= (SB_SUBMODE_BITS+1);
               func_00568010(bits, advance);
               wideband = func_00567B38(bits, 1);
               if (wideband)
               {
                  func_00567F48 ("More than two wideband layers found: corrupted stream?");
                  return -2;
               }

            }
         }
         if (func_00567C48(bits)<4)
            return -1;
         
         m = func_00567B38(bits, 4);
         if (m==15) 
         {
            return -1;
         } else if (m==14) 
         {
            int ret = func_00568038(bits, st->speex_callbacks, state);
            if (ret)
               return ret;
         } else if (m==13) 
         {
            int ret = st->user_callback.func(bits, state, st->user_callback.data);
            if (ret)
               return ret;
         } else if (m>8) 
         {
            func_00567F48("Invalid mode encountered: corrupted stream?");
            return -2;
         }
      
      } while (m>8);

      
      st->submodeID = m;

   }
#ifdef EPIC_48K
   }
#endif
   }

   
   func_00567F28(st->inBuf, st->inBuf+st->frameSize, (st->bufSize-st->frameSize)*sizeof(spx_sig_t));
   func_00567F28(st->excBuf, st->excBuf+st->frameSize, (st->bufSize-st->frameSize)*sizeof(spx_sig_t));

   
   if (st->submodes[st->submodeID] == NULL)
   {
      spx_coef_t *lpc;
      lpc = PUSH(stack,11, spx_coef_t);
      func_0056DD70(GAMMA_SCALING*.93f, st->interp_qlpc, lpc, 10);
      
      {
         float innov_gain=0;
         float pgain=GAIN_SCALING_1*st->last_pitch_gain;
         if (pgain>.6f)
            pgain=.6f;
         for (i=0;i<st->frameSize;i++)
            innov_gain += st->innov[i]*st->innov[i];
         innov_gain=root(innov_gain/st->frameSize);
         func_005A48D8(st->exc,0,st->frameSize*sizeof(spx_sig_t));
         func_00567F58(st->exc, st->frameSize, innov_gain);
      }


      st->first=1;

      
      func_0056E170(st->exc, lpc, st->frame, st->frameSize, st->lpcSize, st->mem_sp);

      for (i=0;i<st->frameSize;i++)
      {
         spx_word32_t sig = PSHR(st->frame[i],SIG_SHIFT);
         if (sig>32767)
            sig = 32767;
         if (sig<-32767)
            sig = -32767;
         out[i]=sig;
      }

      st->count_lost=0;
      return 0;
   }

   
   SUBMODE(lsp_unquant)(st->qlsp, st->lpcSize, bits);

   
   if (st->count_lost)
   {
      float lsp_dist=0, fact;
      for (i=0;i<st->lpcSize;i++)
         lsp_dist += __builtin_fabsf(st->old_qlsp[i] - st->qlsp[i]);
      lsp_dist /= LSP_SCALING*LSP_SCALING;
      fact = .6f*func_0057D488(-.2f*lsp_dist);
      for (i=0;i<2*st->lpcSize;i++)
         st->mem_sp[i] = (spx_mem_t)(st->mem_sp[i]*fact);
   }


   
   if (st->first || st->count_lost)
   {
      for (i=0;i<st->lpcSize;i++)
         st->old_qlsp[i] = st->qlsp[i];
   }

#ifdef EPIC_48K
   if (st->lbr_48k) {
      pitch_half[0] = st->min_pitch+func_00567B38(bits, 7);
      pitch_half[1] = pitch_half[0]+func_00567B38(bits, 2)-1;

      ol_pitch_id = func_00567B38(bits, 3);
      ol_pitch_coef=GAIN_SCALING*0.13514f*ol_pitch_id;

      {
         int qe;
         qe = func_00567B38(bits, 4);
         ol_gain = SIG_SCALING*func_0057D488((qe+2)/2.1f),SIG_SHIFT;
      }

   } else {
#endif

   
   if (SUBMODE(lbr_pitch)!=-1)
   {
      ol_pitch = st->min_pitch+func_00567B38(bits, 7);
   } 
   
   if (SUBMODE(forced_pitch_gain))
   {
      int quant;
      quant = func_00567B38(bits, 4);
      ol_pitch_coef=GAIN_SCALING*0.066667f*quant;
   }
   
   
   {
      int qe;
      qe = func_00567B38(bits, 5);
#ifdef FIXED_POINT
      ol_gain = MULT16_32_Q15(28406,D_006CAAD0[qe]);
#else
      ol_gain = SIG_SCALING*func_0057D488(qe/3.5f);
#endif
   }
#ifdef EPIC_48K
   }
#endif

   awk1=PUSH(stack, st->lpcSize+1, spx_coef_t);
   awk2=PUSH(stack, st->lpcSize+1, spx_coef_t);
   awk3=PUSH(stack, st->lpcSize+1, spx_coef_t);

   if (st->submodeID==1)
   {
      int extra;
      extra = func_00567B38(bits, 4);

      if (extra==15)
         st->dtx_enabled=1;
      else
         st->dtx_enabled=0;
   }
   if (st->submodeID>1)
      st->dtx_enabled=0;

   
   for (sub=0;sub<st->nbSubframes;sub++)
   {
      int offset;
      spx_sig_t *sp, *exc;
      spx_word16_t tmp;

#ifdef EPIC_48K
      if (st->lbr_48k)
      {
         if (sub*2 < st->nbSubframes)
            ol_pitch = pitch_half[0];
         else
            ol_pitch = pitch_half[1];
      }
#endif

      
      offset = st->subframeSize*sub;
      
      sp=st->frame+offset;
      
      exc=st->exc+offset;
      

      
      func_0056D698(st->old_qlsp, st->qlsp, st->interp_qlsp, st->lpcSize, sub, st->nbSubframes);

      
      func_0056D5D0(st->interp_qlsp, st->lpcSize, LSP_MARGIN);


      
      func_0056D3B0(st->interp_qlsp, st->interp_qlpc, st->lpcSize, stack);

      
      if (st->lpc_enh_enabled)
      {
         func_0056DD70(SUBMODE(lpc_enh_k1), st->interp_qlpc, awk1, st->lpcSize);
         func_0056DD70(SUBMODE(lpc_enh_k2), st->interp_qlpc, awk2, st->lpcSize);
         func_0056DD70(SUBMODE(lpc_enh_k3), st->interp_qlpc, awk3, st->lpcSize);
      }

      
      {
         spx_word32_t pi_g=st->interp_qlpc[0];
         for (i=1;i<=st->lpcSize;i+=2)
         {
            pi_g += -st->interp_qlpc[i] +  st->interp_qlpc[i+1];
         }
         st->pi_gain[sub] = pi_g;
      }

      
      func_005A48D8(exc,0,st->subframeSize*sizeof(spx_sig_t));

      
      if (SUBMODE(ltp_unquant))
      {
         int pit_min, pit_max;
         
         if (SUBMODE(lbr_pitch) != -1)
         {
            int margin;
            margin = SUBMODE(lbr_pitch);
            if (margin)
            {

               pit_min = ol_pitch-margin+1;
               if (pit_min < st->min_pitch)
		  pit_min = st->min_pitch;
               pit_max = ol_pitch+margin;
               if (pit_max > st->max_pitch)
		  pit_max = st->max_pitch;
            } else {
               pit_min = pit_max = ol_pitch;
            }
         } else {
            pit_min = st->min_pitch;
            pit_max = st->max_pitch;
         }


#ifdef EPIC_48K
         if (st->lbr_48k)
         {
             SUBMODE(ltp_unquant)(exc, pit_min, pit_max, ol_pitch_coef, SUBMODE(ltp_params), 
                                  st->subframeSize, &pitch, &pitch_gain[0], bits, stack, 
                                  st->count_lost, offset, st->last_pitch_gain, ol_pitch_id);
         } else {
#endif

             SUBMODE(ltp_unquant)(exc, pit_min, pit_max, ol_pitch_coef, SUBMODE(ltp_params), 
                                  st->subframeSize, &pitch, &pitch_gain[0], bits, stack, 
                                  st->count_lost, offset, st->last_pitch_gain, 0);

#ifdef EPIC_48K
         }
#endif

         
         
         if (st->count_lost && ol_gain < st->last_ol_gain)
         {
            float fact = (float)ol_gain/(st->last_ol_gain+1);
            for (i=0;i<st->subframeSize;i++)
               exc[i]*=fact;
         }

         tmp = gain_3tap_to_1tap(pitch_gain);

         pitch_average += tmp;
         if (tmp>best_pitch_gain)
         {
            best_pitch = pitch;
	    best_pitch_gain = tmp;
         }
      } else {
         func_00567F40("No pitch prediction, what's wrong");
      }
      
      
      {
         int q_energy;
         spx_word32_t ener;
         spx_sig_t *innov;
         
         innov = st->innov+sub*st->subframeSize;
         func_005A48D8(innov,0,st->subframeSize*sizeof(spx_sig_t));

         
         if (SUBMODE(have_subframe_gain)==3)
         {
            q_energy = func_00567B38(bits, 3);
            ener = MULT16_32_Q14(D_006CAB60[q_energy],ol_gain);
         } else if (SUBMODE(have_subframe_gain)==1)
         {
            q_energy = func_00567B38(bits, 1);
            ener = MULT16_32_Q14(D_006CAB78[q_energy],ol_gain);
         } else {
            ener = ol_gain;
         }
                  
         if (SUBMODE(innovation_unquant))
         {
            
            SUBMODE(innovation_unquant)(innov, SUBMODE(innovation_params), st->subframeSize, bits, stack);
         } else {
            func_00567F40("No fixed codebook");
         }

         
#ifdef FIXED_POINT
         func_0056DDE0(innov, innov, ener, st->subframeSize);
#else
         func_0056DDE0(innov, innov, ener, st->subframeSize);
#endif
         
         if (st->submodeID==1) 
         {
            float g=ol_pitch_coef*GAIN_SCALING_1;

            
            func_005A48D8(exc,0,st->subframeSize*sizeof(spx_sig_t));
            while (st->voc_offset<st->subframeSize)
            {
               if (st->voc_offset>=0)
                  exc[st->voc_offset]=SIG_SCALING*root(1.0f*ol_pitch);
               st->voc_offset+=ol_pitch;
            }
            st->voc_offset -= st->subframeSize;

            g=.5f+2*(g-.6f);
            if (g<0)
               g=0;
            if (g>1)
               g=1;
            for (i=0;i<st->subframeSize;i++)
            {
               float exci=exc[i];
               exc[i]=.8f*g*exc[i]*ol_gain/SIG_SCALING + .6f*g*st->voc_m1*ol_gain/SIG_SCALING + .5f*g*innov[i] - .5f*g*st->voc_m2 + (1-g)*innov[i];
               st->voc_m1 = exci;
               st->voc_m2=innov[i];
               st->voc_mean = .95f*st->voc_mean + .05f*exc[i];
               exc[i]-=st->voc_mean;
            }
         } else {
            for (i=0;i<st->subframeSize;i++)
               exc[i]+=innov[i];
            
         }
         
         if (SUBMODE(double_codebook))
         {
            char *tmp_stack=stack;
            spx_sig_t *innov2 = PUSH(tmp_stack, st->subframeSize, spx_sig_t);
            func_005A48D8(innov2,0,st->subframeSize*sizeof(spx_sig_t));
            SUBMODE(innovation_unquant)(innov2, SUBMODE(innovation_params), st->subframeSize, bits, tmp_stack);
            func_0056DDE0(innov2, innov2, ener*(1/2.2f), st->subframeSize);
            for (i=0;i<st->subframeSize;i++)
               exc[i] += innov2[i];
         }

      }

      for (i=0;i<st->subframeSize;i++)
         sp[i]=exc[i];

      
      if (st->lpc_enh_enabled && SUBMODE(comb_gain)>0)
         func_0056E4B0(exc, sp, st->interp_qlpc, st->lpcSize, st->subframeSize,
                              pitch, pitch_gain, SUBMODE(comb_gain), st->comb_mem);

      if (st->lpc_enh_enabled)
      {
         
         func_0056DF58(sp, awk2, awk1, sp, st->subframeSize, st->lpcSize, 
                     st->mem_sp+st->lpcSize);
         func_0056DF58(sp, awk3, st->interp_qlpc, sp, st->subframeSize, st->lpcSize, 
                     st->mem_sp);
      } else {
         
         func_005A48D8((void *)(st->lpcSize*sizeof(spx_mem_t)+(unsigned)st->mem_sp),0,st->lpcSize*sizeof(spx_mem_t));
         func_0056E170(sp, st->interp_qlpc, sp, st->subframeSize, st->lpcSize, 
                     st->mem_sp);
      }
   }
   
      
   for (i=0;i<st->frameSize;i++)
   {
      spx_word32_t sig = PSHR(st->frame[i],SIG_SHIFT);
      if (sig>32767)
         sig = 32767;
      if (sig<-32767)
         sig = -32767;
     out[i]=sig;
   }

   

   
   for (i=0;i<st->lpcSize;i++)
      st->old_qlsp[i] = st->qlsp[i];

   
   st->first = 0;
   st->count_lost=0;
   st->last_pitch = best_pitch;
#ifdef FIXED_POINT
   st->last_pitch_gain = PSHR(pitch_average,2);
#else
   st->last_pitch_gain = .25f*pitch_average;   
#endif
   st->pitch_gain_buf[st->pitch_gain_buf_idx++] = st->last_pitch_gain;
   if (st->pitch_gain_buf_idx > 2) 
      st->pitch_gain_buf_idx = 0;

   st->last_ol_gain = ol_gain;

   return 0;
}
/* native correction of src/libstream/unit_005676B0/func_0056A780.c (gt4-decomp tools/argcheck.py fix --kind classes --native): classes func_00567F58 */
