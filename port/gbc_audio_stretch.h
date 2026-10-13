/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Optional frontend helper. See docs/audio_stretch.md for buffer/lifetime contracts. */
#define GBC_STRETCH_CAP 2048U
#define GBC_STRETCH_HOP 320U
#define GBC_STRETCH_SEARCH 320U
struct gbc_audio_stretch {
    int16_t source[GBC_STRETCH_CAP][2], tail[GBC_STRETCH_HOP][2];
    uint64_t written, base, cursor_q16, last_start;
    uint64_t input_frames, output_frames;
    uint32_t searches;
    unsigned primed;
};
static inline void gbc_audio_stretch_reset(struct gbc_audio_stretch *s)
{ memset(s, 0, sizeof(*s)); }
static inline int gbc_audio_stretch_feed(struct gbc_audio_stretch *s,
    const int16_t *pcm, unsigned frames)
{
    if (frames > GBC_STRETCH_CAP || s->written-s->base+frames > GBC_STRETCH_CAP) return -1;
    for (unsigned i=0; i<frames; i++) {
        unsigned at=(unsigned)(s->written+i)&(GBC_STRETCH_CAP-1);
        s->source[at][0]=pcm[2*i];s->source[at][1]=pcm[2*i+1];
    }
    s->written+=frames;s->input_frames+=frames;return 0;
}
static inline void gbc_audio_stretch_copy(struct gbc_audio_stretch *s,
    int16_t *out, uint64_t at, unsigned frames)
{
    unsigned start=(unsigned)at&(GBC_STRETCH_CAP-1), first=GBC_STRETCH_CAP-start;
    if(first>frames)first=frames;
    memcpy(out,s->source[start],first*4);
    if(first<frames)memcpy(out+first*2,s->source,(frames-first)*4);
}
static inline void gbc_audio_stretch_advance(struct gbc_audio_stretch *s, uint64_t best, uint32_t q16)
{
    s->last_start=best;s->primed=1;s->cursor_q16+=(uint64_t)q16*GBC_STRETCH_HOP;
    uint64_t next=s->cursor_q16>>16;
    s->base=next>GBC_STRETCH_SEARCH?next-GBC_STRETCH_SEARCH:0;
    s->output_frames+=GBC_STRETCH_HOP;
}
static inline uint64_t gbc_audio_stretch_error(struct gbc_audio_stretch *s, uint64_t at)
{
    uint64_t error=0;
    for(unsigned i=0;i<GBC_STRETCH_HOP;i+=17) for(unsigned ch=0;ch<2;ch++) {
        int32_t d=(int32_t)s->tail[i][ch]-s->source[(unsigned)(at+i)&(GBC_STRETCH_CAP-1)][ch];
        /* Divide before squaring: bounded integer metric, including S16 extremes. */
        d/=8;error+=(uint32_t)(d*d);
    }
    s->searches++;return error;
}
/* q16 source time / output time, [0.5,1]. Stereo always shares one splice.
 * Caller supplies a fixed 10 ms output workspace and retains packet boundaries. */
static inline unsigned gbc_audio_stretch_render(struct gbc_audio_stretch *s,
    int16_t *out, uint32_t q16)
{
    uint64_t center=s->cursor_q16>>16, best, low, high;
    if(q16<32768)q16=32768;
    if(q16>65536)q16=65536;
    if(!s->primed) {
        if(s->written<2*GBC_STRETCH_HOP+GBC_STRETCH_SEARCH)return 0;
        best=0;
    } else {
        low=center>GBC_STRETCH_SEARCH?center-GBC_STRETCH_SEARCH:0;
        if(low<s->base)low=s->base;
        high=center+GBC_STRETCH_SEARCH;
        if(s->written<high+2*GBC_STRETCH_HOP)return 0;
        best=center;
        /* A constant-rate continuation after a splice retains its phase
         * offset. No search is needed when the whole overlap is identical. */
        if(q16==65536 && s->last_start+GBC_STRETCH_HOP>=low && s->last_start+GBC_STRETCH_HOP<=high) {
            best=s->last_start+GBC_STRETCH_HOP;
            /* The new first half is exactly the saved second half. */
            memcpy(out,s->tail,sizeof(s->tail));
            gbc_audio_stretch_copy(s,&s->tail[0][0],best+GBC_STRETCH_HOP,GBC_STRETCH_HOP);
            gbc_audio_stretch_advance(s,best,q16);return GBC_STRETCH_HOP;
        }
        uint64_t error=gbc_audio_stretch_error(s,best);
        if(error) {
            for(uint64_t at=low;at<=high;at+=32) {
                uint64_t e=gbc_audio_stretch_error(s,at);if(e<error){error=e;best=at;}
            }
            uint64_t coarse=best;
            for(int delta=-24;delta<=24;delta+=8) {
                uint64_t at=coarse+delta;
                if(at<low || at>high)continue;
                uint64_t e=gbc_audio_stretch_error(s,at);if(e<error){error=e;best=at;}
            }
            coarse=best;
            for(int delta=-6;delta<=6;delta+=2) {
                uint64_t at=coarse+delta;
                if(at<low || at>high)continue;
                uint64_t e=gbc_audio_stretch_error(s,at);if(e<error){error=e;best=at;}
            }
            coarse=best;
            for(int delta=-1;delta<=1;delta++) {
                uint64_t at=coarse+delta;
                if(at<low || at>high)continue;
                uint64_t e=gbc_audio_stretch_error(s,at);if(e<error){error=e;best=at;}
            }
        }
    }
    for(unsigned i=0;i<GBC_STRETCH_HOP;i++)for(unsigned ch=0;ch<2;ch++) {
        int32_t sample=s->source[(unsigned)(best+i)&(GBC_STRETCH_CAP-1)][ch];
        if(s->primed)sample=((int32_t)s->tail[i][ch]*(int32_t)(GBC_STRETCH_HOP-i)+sample*(int32_t)i)/(int32_t)GBC_STRETCH_HOP;
        out[2*i+ch]=(int16_t)sample;
        s->tail[i][ch]=s->source[(unsigned)(best+GBC_STRETCH_HOP+i)&(GBC_STRETCH_CAP-1)][ch];
    }
    gbc_audio_stretch_advance(s,best,q16);return GBC_STRETCH_HOP;
}

#ifdef __cplusplus
}
#endif
