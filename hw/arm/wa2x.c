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

#include "cpu.h"
#include "elf.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/wa2x.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/target-info.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include <stdint.h>

static const MemMapEntry wa2x_memmap[] = {
    [WA2X_SYSCON_MMIO] = {SYSCON_ADDRESS, SYSCON_SIZE},
    [WA2X_SYSCON_BUFFER] = {BUFFER_ADDRESS, BUFFER_SIZE},
    [WA2X_MODULE] = {MODULE_ADDRESS, MODULE_SIZE},
    [WA2X_AOT] = {WASM_AOT_BEGIN, WASM_AOT_SIZE},
    [WA2X_ROM] = {ROM_ADDRESS, ROM_SIZE},
    [WA2X_RAM] = {RAM_ADDRESS, 0x0},
    [WA2X_LIME] = {WASM_MEMORY_BEGIN, WASM_MEMORY_SIZE},
};

static struct arm_boot_info bootinfo;

static void wa2x_machine_state_init(MachineState *machine) {
  MachineClass *mc = MACHINE_GET_CLASS(machine);
  Wa2xMachineState *s = ARM_WA2X_MACHINE(machine);
  MemoryRegion *system_memory = get_system_memory();

  /* No default firmware */
  if (!machine->firmware) {
    error_report("No firmware");
    exit(EXIT_FAILURE);
  }

  s->memmap = wa2x_memmap;
  /* Initialize SoC */
  object_initialize_child(OBJECT(machine), "cpu", &s->cpu, machine->cpu_type);
  object_initialize_child(OBJECT(machine), "syscon", &s->syscon,
                          TYPE_WA2X_SYSCON);
  if (object_property_find(OBJECT(&s->cpu), "has_el3")) {
    object_property_set_bool(OBJECT(&s->cpu), "has_el3", false, &error_abort);
  }
  if (object_property_find(OBJECT(&s->cpu), "has_el2")) {
    object_property_set_bool(OBJECT(&s->cpu), "has_el2", false, &error_abort);
  }
  if (!qdev_realize(DEVICE(&s->cpu), NULL, &error_fatal)) {
    error_report("CPU failed to init");
    exit(EXIT_FAILURE);
  }

  /* ROM */
  memory_region_init_rom(&s->rom_mem, NULL, "arm.wa2x.rom",
                         wa2x_memmap[WA2X_ROM].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_ROM].base,
                              &s->rom_mem);
  /* alias ROM to 0 */
  memory_region_init_alias(&s->rom_alias, NULL, "arm.wa2x.brom", &s->rom_mem, 0,
                           wa2x_memmap[WA2X_ROM].size);
  memory_region_add_subregion(system_memory, 0x0, &s->rom_alias);

  /* load firmware to ROM
   * In our AArch64 memory layout, the boot addr is fixed to WA2X_ROM
   */
  uint64_t elf_entry, elf_low, elf_high;
  int elf_machine;
  bool aarch64 =
      object_property_find(OBJECT(&s->cpu), "aarch64") &&
      object_property_get_bool(OBJECT(&s->cpu), "aarch64", &error_fatal);
  if (aarch64) {
    elf_machine = EM_AARCH64;
  } else {
    elf_machine = EM_ARM;
  }
  if (load_elf(machine->firmware, NULL, NULL, NULL, &elf_entry, &elf_low,
               &elf_high, NULL, 0, elf_machine, 1, 0) < 0) {
    error_report("failed to load firmware");
    exit(EXIT_FAILURE);
  }

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
  memory_region_init_ram(&s->syscon.buffer, NULL, "arm.wa2x.buffer",
                         wa2x_memmap[WA2X_SYSCON_BUFFER].size, &error_fatal);
  memory_region_add_subregion(
      system_memory, wa2x_memmap[WA2X_SYSCON_BUFFER].base, &s->syscon.buffer);

  /* register module memory */
  memory_region_init_ram(&s->syscon.module, NULL, "arm.wa2x.module",
                         wa2x_memmap[WA2X_MODULE].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MODULE].base,
                              &s->syscon.module);

  /* register aot memory */
  memory_region_init_ram(&s->syscon.aot, NULL, "arm.wa2x.aot",
                         wa2x_memmap[WA2X_AOT].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_AOT].base,
                              &s->syscon.aot);

  /* register lime memory */
  memory_region_init_ram(&s->syscon.lime, NULL, "arm.wa2x.lime",
                         wa2x_memmap[WA2X_LIME].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_LIME].base,
                              &s->syscon.lime);

  /* ROM reset vector */
  bootinfo.ram_size = machine->ram_size;
  bootinfo.entry = elf_entry;
  bootinfo.is_linux = false;
  bootinfo.firmware_loaded = true;
  s->cpu.env.boot_info = &bootinfo;
  arm_load_kernel(&s->cpu, machine, &bootinfo);
}

static void wa2x_machine_instance_init(Object *obj) {}

static const char *wa2x_get_default_cpu_type(const MachineState *ms) {
  if (target_aarch64()) {
    return ARM_CPU_TYPE_NAME("cortex-a53");
  }
  return ARM_CPU_TYPE_NAME("cortex-a15");
}

static void wa2x_machine_class_init(ObjectClass *klass, const void *data) {
  MachineClass *mc = MACHINE_CLASS(klass);
  mc->desc = "Wa2x test runner";
  mc->init = wa2x_machine_state_init;
  mc->get_default_cpu_type = wa2x_get_default_cpu_type;
  mc->max_cpus = 1;
  mc->default_ram_id = "arm.wa2x.ram";
  mc->default_ram_size = 8 * MiB;
}

static const TypeInfo wa2x_machine_type_info = {
    .name = TYPE_ARM_WA2X_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = wa2x_machine_class_init,
    .instance_init = wa2x_machine_instance_init,
    .instance_size = sizeof(Wa2xMachineState),
    .interfaces = arm_machine_interfaces,
};

static void wa2x_machine_type_info_register(void) {
  type_register_static(&wa2x_machine_type_info);
}
type_init(wa2x_machine_type_info_register)
