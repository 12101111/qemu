/*
 * Wa2x test runner
 *
 * Copyright (c) 2026 Han Puyu <w12101111@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_WA2X_H
#define HW_WA2X_H

#include "hw/arm/armv7m.h"
#include "hw/boards.h"
#include "hw/char/wa2x_syscon.h"

#define TYPE_ARM_WA2XM_MACHINE MACHINE_TYPE_NAME("wa2xm")
#define ARM_WA2XM_MACHINE(obj) \
    OBJECT_CHECK(Wa2xmMachineState, (obj), TYPE_ARM_WA2XM_MACHINE)

typedef struct Wa2xmMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    ARMv7MState armv7m;
    Wa2xSysconState syscon;
    MemoryRegion rom_mem;
    MemoryRegion rom_alias;
    const MemMapEntry *memmap;
} Wa2xmMachineState;

enum {
    WA2X_ROM,
    WA2X_RAM,
    WA2X_SYSCON_BUFFER,
    WA2X_SYSCON_MMIO,
    WA2X_MODULE,
};

#endif
