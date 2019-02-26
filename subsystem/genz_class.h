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

#ifndef GENZ_CLASS_DOT_H
#define GENZ_CLASS_DOT_H

/**
  * Gen-Z 1.0 Appendix C, Component Class Encodings.
  * NB == Non-Bootable
  * NC == Non-Coherent
  */

enum genz_component_class_encodings {
	GENZ_CCE_RESERVED_SHALL_NOT_BE_USED = 0x0,
	GENZ_CCE_MEMORY_P2P_CORE,
	GENZ_CCE_MEMORY_EXPLICIT_OPCLASS,
	GENZ_CCE_INTEGRATED_SWITCH,
	GENZ_CCE_ENC_EXP_SWITCH,
	GENZ_CCE_FABRIC_SWITCH,
	GENZ_CCE_PROCESSOR,
	GENZ_CCE_PROCESSOR_NB,
	GENZ_CCE_ACCELERATOR_NB_NC = 0x8,
	GENZ_CCE_ACCELERATOR_NB,
	GENZ_CCE_ACCELERATOR_NC,
	GENZ_CCE_ACCELERATOR,
	GENZ_CCE_IO_NB_NC,
	GENZ_CCE_IO_NB,
	GENZ_CCE_IO_NC,
	GENZ_CCE_IO,
	GENZ_CCE_BLOCK_STORAGE = 0x10,
	GENZ_CCE_BLOCK_STORAGE_NB,
	GENZ_CCE_TRANSPARENT_ROUTER,
	GENZ_CCE_MULTI_CLASS,
	GENZ_CCE_DISCRETE_BRIDGE,
	GENZ_CCE_INTEGRATED_BRIDGE = 0x15,
	GENZ_CCE_TOO_BIG,
};

int genz_classes_init(void);

void genz_classes_destroy(void);

struct class *genz_class_getter(unsigned);

#endif
