/* SPDX-License-Identifier: MIT OR GPL-2.0-only */

#ifndef USEEPLUS_PROTOCOL_H
#define USEEPLUS_PROTOCOL_H

#ifdef __cplusplus

#if defined(__APPLE__)
#include "useeplus_protocol_macos_cpp.h"
#else
#include "useeplus_protocol_linux_cpp.h"
#endif /* (__APPLE__) */

#else

#if defined(__APPLE__)
#include "useeplus_protocol_macos_c.h"
#else
#include "useeplus_protocol_linux_c.h"
#endif /* (__APPLE__) */

#endif /* __cplusplus */

#include "useeplus_protocol_utils.h"

#endif /* USEEPLUS_PROTOCOL_H */
