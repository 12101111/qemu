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

#include "hw/sysbus.h"

#define TYPE_WA2X_SYSCON "wa2x-syscon"
#define WA2X_SYSCON(obj) OBJECT_CHECK(Wa2xSysconState, (obj), TYPE_WA2X_SYSCON)

typedef struct Wa2xSysconConf {
  char *runner;
  char *opt;
} Wa2xSysconConf;

typedef void *runner_t;

typedef struct {
  /* <private> */
  SysBusDevice parent_obj;

  /* <public> */
  MemoryRegion mmio;
  MemoryRegion buffer;
  MemoryRegion module;
  MemoryRegion aot;
  runner_t runner;
  Wa2xSysconConf conf;
} Wa2xSysconState;

extern runner_t wa2x_runner_new(Wa2xSysconState* syscon, const char* runner, const char* args);

extern void wa2x_runner_drop(runner_t);

extern void wa2x_runner_read(runner_t, uint64_t addr, size_t len,
                             uint8_t *bytes);

extern void wa2x_runner_write(runner_t, uint64_t addr, size_t len,
                              const uint8_t *bytes);

void wa2x_syscon_exit_code(Wa2xSysconState* syscon, uint16_t code);
void wa2x_syscon_write_buffer(Wa2xSysconState* syscon, size_t len, const uint8_t *bytes);
void wa2x_syscon_read_buffer(Wa2xSysconState* syscon, size_t len, uint8_t *bytes);
void wa2x_syscon_write_module(Wa2xSysconState* syscon, size_t len, const uint8_t *bytes);

#endif /* HW_WA2X_SYSCON_H */
