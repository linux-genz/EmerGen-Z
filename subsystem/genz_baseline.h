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

// Only the beginning

#ifndef GENZ_BASELINE_DOT_H
#define GENZ_BASELINE_DOT_H

#include <linux/device.h>
#include <linux/list.h>

#define GENZ_DEBUG

#define DRV_NAME	"Gen-Z"
#define DRV_VERSION	"0.1"

#define GZNAMFMTSIZ	64

#define __unused __attribute__ ((unused))

struct genz_device {
	char namefmt[GZNAMFMTSIZ];
	struct list_head lister;
	uint64_t flags;
	struct device dev;
	void *private_data;
};
#define to_genz_dev(pDeV) container_of(pDeV, struct genz_device, dev)

struct genz_device_ops {
	int (*init)(struct genz_device *genz_dev);
	void (*uninit)(struct genz_device *genz_dev);
};

// Minimum proscribed data structures are listed in
// Gen-Z 1.0 "8.13.1 Grouping: Baseline Structures" and 
// Gen-Z 1.0 "8.13.2 Grouping: Routing/Fabric Structures"
// Definitions below ending in "_structure" are merely pertinent fields.
// Those ending in "_format" are the packed binary layout.

// Gen-Z 1.0 "8.14 Core Structure"

enum genz_core_structure_optional_substructures {
	GENZ_CORE_STRUCTURE_ALLOC_COMP_DEST_TABLE =	1 << 0,
	GENZ_CORE_STRUCTURE_ALLOC_XYZZY_TABLE =		1 << 1,
	GENZ_CORE_STRUCTURE_ALLOC_ALL =			(1 << 2) - 1
};

struct genz_core_structure {
	unsigned CCE;
	char Base_C_Class_str[32];
	uint32_t MaxInterface, MaxData, MaxCTL;
	int32_t	CID0, SID0,	// 0 if unassigned, -1 if unused
		PMCID,		// If I am the primary manager
		PFMCID, PFMSID,	// If someone else is the fabric manager
	SFMCID, SFMSID;
	struct genz_component_destination_table_structure *comp_dest_table;
};

// Gen-Z 1.0 "8.15 Opcode Set Structure"
struct genz_opcode_set_structure {
	int HiMom;
};

// Gen-Z 1.0 "8.16 Interface Structure"
struct genz_interface_structure {
	uint32_t Version, InterfaceID,
		 HVS, HVE,
		 I_Status,
		 PeerIntefaceID,
		 PeerBaseC_Class,
		 PeerCID, PeerSID,
		 PeerState;
};

//-------------------------------------------------------------------------
// genz_bus.c

struct device *genz_find_bus_by_instance(int);

//-------------------------------------------------------------------------
// genz_class.c

int genz_classes_init(void);
void genz_classes_destroy(void);

// EXPORTed

extern struct class *genz_class_getter(unsigned);

extern int verbose;
#define __PRE		"genz:"

#define PR_ERR(a...)	{ pr_err("%s(): ", __FUNCTION__); pr_cont(a); }

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
