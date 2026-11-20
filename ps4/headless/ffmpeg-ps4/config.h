/* FFmpeg build configuration for the PS4: the SDK's Linux x86-64 configuration (plain C, no
 * assembler: HAVE_X86ASM is 0 there) with the Linux-only system features turned off.
 * FFmpeg's own config.h only knows Windows, macOS, Linux and Android. */
#ifndef WWE_PS4_FFMPEG_CONFIG_H
#define WWE_PS4_FFMPEG_CONFIG_H
#include "config_linux_x86_64.h"

#undef HAVE_LINUX_PERF_EVENT_H
#define HAVE_LINUX_PERF_EVENT_H 0
#undef HAVE_LINUX_DMA_BUF_H
#define HAVE_LINUX_DMA_BUF_H 0
#undef HAVE_SCHED_GETAFFINITY
#define HAVE_SCHED_GETAFFINITY 0
#undef HAVE_SYS_RESOURCE_H
#define HAVE_SYS_RESOURCE_H 0
#undef HAVE_PTHREAD_CANCEL
#define HAVE_PTHREAD_CANCEL 0
#undef HAVE_MKSTEMP
#define HAVE_MKSTEMP 0
#undef HAVE_GETAUXVAL
#define HAVE_GETAUXVAL 0
#undef HAVE_ELF_AUX_INFO
#define HAVE_ELF_AUX_INFO 0
/* The Jaguar CPU has no AVX2/FMA3, and inline assembler is not worth the risk here: portable C only. */
#undef HAVE_INLINE_ASM
#define HAVE_INLINE_ASM 0
#undef HAVE_AESNI_INLINE
#define HAVE_AESNI_INLINE 0
#undef HAVE_AMD3DNOW_INLINE
#define HAVE_AMD3DNOW_INLINE 0
#undef HAVE_AMD3DNOWEXT_INLINE
#define HAVE_AMD3DNOWEXT_INLINE 0
#undef HAVE_AVX_INLINE
#define HAVE_AVX_INLINE 0
#undef HAVE_AVX2_INLINE
#define HAVE_AVX2_INLINE 0
#undef HAVE_AVX512_INLINE
#define HAVE_AVX512_INLINE 0
#undef HAVE_FMA3_INLINE
#define HAVE_FMA3_INLINE 0
#undef HAVE_FMA4_INLINE
#define HAVE_FMA4_INLINE 0
#undef HAVE_MMX_INLINE
#define HAVE_MMX_INLINE 0
#undef HAVE_MMXEXT_INLINE
#define HAVE_MMXEXT_INLINE 0
#undef HAVE_SSE_INLINE
#define HAVE_SSE_INLINE 0
#undef HAVE_SSE2_INLINE
#define HAVE_SSE2_INLINE 0
#undef HAVE_SSE3_INLINE
#define HAVE_SSE3_INLINE 0
#undef HAVE_SSE4_INLINE
#define HAVE_SSE4_INLINE 0
#undef HAVE_SSE42_INLINE
#define HAVE_SSE42_INLINE 0
#undef HAVE_SSSE3_INLINE
#define HAVE_SSSE3_INLINE 0
#undef HAVE_XOP_INLINE
#define HAVE_XOP_INLINE 0
#endif
