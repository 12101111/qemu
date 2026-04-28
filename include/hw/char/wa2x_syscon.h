/*
 * Wa2x syscon
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

#ifndef HW_WA2X_SYSCON_H
#define HW_WA2X_SYSCON_H

#include "qemu/osdep.h"

#include "hw/core/sysbus.h"

#define TYPE_WA2X_SYSCON "wa2x-syscon"
#define WA2X_SYSCON(obj) OBJECT_CHECK(Wa2xSysconState, (obj), TYPE_WA2X_SYSCON)

#define SYSCON_ADDRESS 0xA0000000
#define SYSCON_SIZE 0x10000
#define BUFFER_ADDRESS 0xA0010000
#define BUFFER_SIZE 0x10000
#define MODULE_ADDRESS 0xA8000000
#define MODULE_SIZE 0x4000000
#define WASM_AOT_BEGIN 0x60000000
#define WASM_AOT_SIZE 0x4000000
#define ROM_ADDRESS 0x80000000
#define ROM_SIZE 0x400000
#define RAM_ADDRESS 0x80400000
#define WASM_MEMORY_BEGIN 0x81000000
#define WASM_MEMORY_SIZE 0xB000000

enum {
    WA2X_SYSCON_MMIO,
    WA2X_SYSCON_BUFFER,
    WA2X_MODULE,
    WA2X_AOT,
    WA2X_ROM,
    WA2X_RAM,
    WA2X_LIME,
    WA2X_MROM,
};

typedef struct {
  /* <private> */
  SysBusDevice parent_obj;

  /* <public> */
  MemoryRegion mmio;
  MemoryRegion buffer;
  MemoryRegion module;
  MemoryRegion aot;
  MemoryRegion lime;
} Wa2xSysconState;

extern bool wa2x_runner_new(Wa2xSysconState *syscon);

extern void wa2x_runner_drop(void);

extern void wa2x_runner_read(uint64_t addr, size_t len, uint8_t *bytes);

extern void wa2x_runner_write(uint64_t addr, size_t len, const uint8_t *bytes);

void wa2x_syscon_exit_code(Wa2xSysconState *syscon, uint16_t code);
void wa2x_syscon_write_buffer(Wa2xSysconState *syscon, size_t len,
                              const uint8_t *bytes);
void wa2x_syscon_read_buffer(Wa2xSysconState *syscon, size_t len,
                             uint8_t *bytes);
void wa2x_syscon_write_module(Wa2xSysconState *syscon, size_t len,
                              const uint8_t *bytes);

#endif /* HW_WA2X_SYSCON_H */
