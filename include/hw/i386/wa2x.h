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

#include "hw/boards.h"
#include "cpu.h"
#include "system/memory.h"
#include "target/i386/cpu-qom.h"
#include "qom/object.h"
#include "hw/char/wa2x_syscon.h"

#define TYPE_X86_WA2X_MACHINE MACHINE_TYPE_NAME("wa2x")
#define X86_WA2X_MACHINE(obj) \
    OBJECT_CHECK(Wa2xMachineState, (obj), TYPE_X86_WA2X_MACHINE)

typedef struct Wa2xMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    X86CPU cpu;
    Wa2xSysconState syscon;
    MemoryRegion rom_mem;
    MemoryRegion boot_rom;
    const MemMapEntry *memmap;
    char* opt;
} Wa2xMachineState;

enum {
    WA2X_ROM,
    WA2X_RAM,
    WA2X_SYSCON_BUFFER,
    WA2X_SYSCON_MMIO,
    WA2X_MODULE,
    WA2X_MROM,
};

#endif
