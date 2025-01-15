/*
 * Copyright 2024 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AETHER_TELE_ENV_CPU_ARCHITECTURE_H_
#define AETHER_TELE_ENV_CPU_ARCHITECTURE_H_

#if defined GEKKO
#  define AE_CPU_TYPE "IBM PowerPC 750 (NGC)"
#endif

#if defined mc68000 || defined m68k || defined __MC68K__ || defined m68000
#  define AE_CPU_TYPE "MC68000"
#endif

#if defined __PPC__ || defined __POWERPC__ || defined powerpc || \
    defined _POWER || defined __ppc__ || defined __powerpc__
#  if !defined AE_CPU_TYPE
#    if defined __powerpc64__
#      define AE_CPU_TYPE "PowerPC64"
#    else
#      define AE_CPU_TYPE "PowerPC"
#    endif
#  endif
#endif

#if defined _CRAYT3E || defined _CRAYMPP
/* target processor is a DEC Alpha 21164 used in a Cray T3E*/
#  define AE_CPU_TYPE "Cray T3E (Alpha 21164)"
#endif

#if defined CRAY || defined _CRAY && !defined _CRAYT3E
#  error Non-AXP Cray systems not supported
#endif

#if defined _SH3
#  define AE_CPU_TYPE "Hitachi SH-3"
#endif

#if defined __sh4__ || defined __SH4__
#  define AE_CPU_TYPE "Hitachi SH-4"
#endif

#if defined __sparc__ || defined __sparc
#  if defined __arch64__ || defined __sparcv9 || defined __sparc_v9__
#    define AE_CPU_TYPE "Sparc/64"
#  else
#    define AE_CPU_TYPE "Sparc/32"
#  endif
#endif

#if defined ARM || defined __arm__ || __arm64__ || defined _ARM
#  define AE_CPU_TYPE "ARM"
#endif

#if defined mips || defined __mips__ || defined __MIPS__ || defined _MIPS
#  if defined _R5900
#    define AE_CPU_TYPE "MIPS R5900 (PS2)"
#  else
#    define AE_CPU_TYPE "MIPS"
#  endif
#endif

#if defined __ia64 || defined _M_IA64 || defined __ia64__
#  define AE_CPU_TYPE "IA64"
#endif

#if defined __X86__ || defined __i386__ || defined i386 || defined _M_IX86 || \
    defined __386__ || defined __x86_64__ || defined _M_X64
#  if defined __x86_64__ || defined _M_X64
#    define AE_CPU_TYPE "AMD x86-64"
#  else
#    define AE_CPU_TYPE "Intel 386+"
#  endif
#endif

#if defined __alpha || defined alpha || defined _M_ALPHA || defined __alpha__
#  define AE_CPU_TYPE "AXP"
#endif

#if defined __hppa || defined hppa
#  define AE_CPU_TYPE "PA-RISC"
#endif

#if defined ESP_PLATFORM
#  include <sdkconfig.h>
#  define AE_CPU_TYPE CONFIG_IDF_TARGET
#endif

#if !defined AE_CPU_TYPE
#  warning Unable to determine CPU type
#  define AE_CPU_TYPE "Unknown" /* this is here for Doxygen's benefit */
#endif

#endif  // AETHER_TELE_ENV_CPU_ARCHITECTURE_H_ */
