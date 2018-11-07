/*
 * (C) Copyright 2018 Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This source code file is part of the EmerGen-Z project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Bridge structures

#ifndef GENZFEE_BRIDGE_DOT_H
#define GENZFEE_BRIDGE_DOT_H

#include <linux/list.h>
#include <linux/mutex.h>

#define GFBRIDGE_DEBUG			// See "Debug assistance" below

#define GFBRIDGE_NAME	"gfbridge"
#define GFBR		"gfbr: "	// pr_xxxx header
#define GFBRSP		"      "	// pr_xxxx header same length indent

#define GFBRIDGE_VERSION	GFBRIDGE_NAME " v0.1.0: gotta start somewhere"

// Just write support for now.
struct bridge_buffers {
	char *wbuf;			// kmalloc(max_msglen)
	struct mutex wbuf_mutex;
};

//-------------------------------------------------------------------------
// Debug support

#ifdef PR_V1		// Avoid "redefine" errors
#undef PR_V1
#undef PR_V2
#undef PR_V3
#undef PR_ENTER
#undef PR_EXIT
#endif

#ifdef GFBRIDGE_DEBUG
#define PR_V1(a...)	{ if (verbose) pr_info(GFBR a); }
#define PR_V2(a...)	{ if (verbose > 1) pr_info(GFBR a); }
#define PR_V3(a...)	{ if (verbose > 2) pr_info(GFBR a); }
#else
#define PR_V1(a...)
#define PR_V2(a...)
#define PR_V3(a...)
#endif

#define _F_		__FUNCTION__
#define PR_ENTER(a...)	{ if (verbose) { \
				pr_info(GFBR "enter %s: ", _F_); pr_cont(a); }}
#define PR_EXIT(a...)	{ if (verbose) { \
				pr_info(GFBR "exit %s: ", _F_); pr_cont(a); }}

#endif
