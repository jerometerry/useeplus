/* SPDX-License-Identifier: MIT OR GPL-2.0-only */

#ifndef USEEPLUS_PROTOCOL_H
#define USEEPLUS_PROTOCOL_H

#ifdef __KERNEL__
#include "useeplus_protoco_linux_kernel.h"
#else

#ifdef __cplusplus
extern "C" {
#if defined(__APPLE__)
#include "useeplus_protoco_macos_cpp.h"
#else
#include "useeplus_protoco_linux_cpp.h"
#endif
}
#else
#if defined(__APPLE__)
#include "useeplus_protoco_macos_c.h"
#else
#include "useeplus_protoco_linux_c.h"
#endif
#endif

#endif /* USEEPLUS_PROTOCOL_H */
