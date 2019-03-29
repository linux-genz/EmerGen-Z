/*
 * (C) Copyright 2018-2019 Hewlett Packard Enterprise Development LP.
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

/* Versioning and debug/trace support. */

#ifndef GENZ_BASELINE_DOT_H
#define GENZ_BASELINE_DOT_H

#include <linux/device.h>

#define GENZ_DEBUG

#define DRV_NAME	"Gen-Z"
#define DRV_VERSION	"0.1"

#define __unused __attribute__ ((unused))

extern int verbose;

#define __PRE		"genz:"

#define PR_ERR(a...)	{ pr_err("%s(): ", __FUNCTION__); pr_cont(a); }

#ifndef PR_V1
#ifdef GENZ_DEBUG
#define PR_V1(a...)     { if (verbose) pr_info(__PRE a); }
#define PR_V2(a...)     { if (verbose > 1) pr_info(__PRE a); }
#define PR_V3(a...)     { if (verbose > 2) pr_info(__PRE a); }
// #undef __PRE
#else
#define PR_V1(a...)
#define PR_V2(a...)
#define PR_V3(a...)
#endif
#endif

#endif
