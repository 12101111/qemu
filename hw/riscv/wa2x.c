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

#include "qemu/osdep.h"

#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/wa2x.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"

static const MemMapEntry wa2x_memmap[] = {
    [WA2X_ROM] = {0x80000000, 0x400000},
    [WA2X_RAM] = {0x80400000, 0x0},
    [WA2X_SYSCON_BUFFER] = {0x50010000, 0x10000},
    [WA2X_SYSCON_MMIO] = {0x50000000, 0x10000},
    [WA2X_MODULE] = {0x58000000, 0x4000000},
    [WA2X_MROM] = {0x1000, 0x1000},
};

static void wa2x_machine_state_init(MachineState *machine) {
  MachineClass *mc = MACHINE_GET_CLASS(machine);
  Wa2xMachineState *s = RISCV_WA2X_MACHINE(machine);
  MemoryRegion *system_memory = get_system_memory();
  SysBusDevice *sysbus;
  hwaddr firmware_load_addr = wa2x_memmap[WA2X_ROM].base;

  /* No default firmware */
  if (!machine->firmware) {
    error_report("No firmware");
    exit(EXIT_FAILURE);
  }

  s->memmap = wa2x_memmap;
  /* Initialize SoC */
  object_initialize_child(OBJECT(machine), "soc", &s->soc,
                          TYPE_RISCV_HART_ARRAY);
  object_initialize_child(OBJECT(machine), "syscon", &s->syscon,
                          TYPE_WA2X_SYSCON);
  object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                          &error_abort);
  object_property_set_int(OBJECT(&s->soc), "hartid-base", 0, &error_abort);
  object_property_set_int(OBJECT(&s->soc), "num-harts", 1, &error_abort);
  sysbus = SYS_BUS_DEVICE(&s->soc);
  sysbus_realize(sysbus, &error_fatal);

  /* ROM */
  memory_region_init_rom(&s->rom_mem, NULL, "riscv.wa2x.rom",
                         wa2x_memmap[WA2X_ROM].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_ROM].base,
                              &s->rom_mem);

  /* load firmware to ROM
   * In our RISC-V memory layout, the boot addr is fixed to WA2X_ROM
   */
  riscv_load_firmware(machine->firmware, &firmware_load_addr, NULL);

  if (!sysbus_realize(SYS_BUS_DEVICE(&s->syscon), &error_fatal)) {
    return;
  }
  sysbus_mmio_map(SYS_BUS_DEVICE(&s->syscon), 0,
                  wa2x_memmap[WA2X_SYSCON_MMIO].base);

  /* register main RAM
   */
  if (machine->ram_size < mc->default_ram_size) {
    char *sz = size_to_str(mc->default_ram_size);
    error_report("Invalid RAM size, should be large than %s", sz);
    g_free(sz);
    exit(EXIT_FAILURE);
  }
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_RAM].base,
                              machine->ram);

  /* register syscon buffer */
  memory_region_init_ram(&s->syscon.buffer, NULL, "riscv.wa2x.buffer",
                         wa2x_memmap[WA2X_SYSCON_BUFFER].size, &error_fatal);
  memory_region_add_subregion(
      system_memory, wa2x_memmap[WA2X_SYSCON_BUFFER].base, &s->syscon.buffer);

  /* register module memory */
  memory_region_init_ram(&s->syscon.module, NULL, "riscv.wa2x.module",
                         wa2x_memmap[WA2X_MODULE].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MODULE].base,
                              &s->syscon.module);

  /* boot rom */
  memory_region_init_rom(&s->mask_rom, NULL, "riscv.wa2x.mrom",
                         wa2x_memmap[WA2X_MROM].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MROM].base,
                              &s->mask_rom);

  /* ROM reset vector */
  riscv_setup_rom_reset_vec(
      machine, &s->soc, firmware_load_addr, wa2x_memmap[WA2X_MROM].base,
      wa2x_memmap[WA2X_MROM].size, wa2x_memmap[WA2X_ROM].base, 0);
}

static void wa2x_machine_class_init(ObjectClass *klass, const void *data) {
  MachineClass *mc = MACHINE_CLASS(klass);
  mc->desc = "Wa2x test runner";
  mc->init = wa2x_machine_state_init;
  mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
  mc->max_cpus = 1;
  mc->default_ram_id = "riscv.wa2x.ram";
  mc->default_ram_size = 252 * MiB;
}

static const TypeInfo wa2x_machine_type_info = {
    .name = TYPE_RISCV_WA2X_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = wa2x_machine_class_init,
    .instance_size = sizeof(Wa2xMachineState),
};

static void wa2x_machine_type_info_register(void) {
  type_register_static(&wa2x_machine_type_info);
}
type_init(wa2x_machine_type_info_register)
