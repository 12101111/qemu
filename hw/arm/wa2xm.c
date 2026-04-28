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

#include "hw/arm/armv7m.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/wa2xm.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"

static const MemMapEntry wa2x_memmap[] = {
    [WA2X_SYSCON_MMIO] = {SYSCON_ADDRESS, SYSCON_SIZE},
    [WA2X_SYSCON_BUFFER] = {BUFFER_ADDRESS, BUFFER_SIZE},
    [WA2X_MODULE] = {MODULE_ADDRESS, MODULE_SIZE},
    [WA2X_AOT] = {WASM_AOT_BEGIN, WASM_AOT_SIZE},
    [WA2X_ROM] = {ROM_ADDRESS, ROM_SIZE},
    [WA2X_RAM] = {RAM_ADDRESS, 0x0},
    [WA2X_LIME] = {WASM_MEMORY_BEGIN, WASM_MEMORY_SIZE},
};

/* Main SYSCLK frequency in Hz (168MHz) */
#define SYSCLK_FRQ 168000000ULL

static void wa2x_machine_state_init(MachineState *machine) {
  MachineClass *mc = MACHINE_GET_CLASS(machine);
  Wa2xmMachineState *s = ARM_WA2XM_MACHINE(machine);
  MemoryRegion *system_memory = get_system_memory();
  Clock *sysclk;

  /* No default firmware */
  if (!machine->firmware) {
    error_report("No firmware");
    exit(EXIT_FAILURE);
  }

  s->memmap = wa2x_memmap;

  /* This clock doesn't need migration because it is fixed-frequency */
  sysclk = clock_new(OBJECT(machine), "SYSCLK");
  clock_set_hz(sysclk, SYSCLK_FRQ);

  /* Initialize SoC */
  object_initialize_child(OBJECT(machine), "armv7m", &s->armv7m, TYPE_ARMV7M);
  qdev_prop_set_string(DEVICE(&s->armv7m), "cpu-type", machine->cpu_type);
  qdev_prop_set_uint32(DEVICE(&s->armv7m), "init-nsvtor",
                       wa2x_memmap[WA2X_ROM].base);
  qdev_connect_clock_in(DEVICE(&s->armv7m), "cpuclk", sysclk);
  object_property_set_link(OBJECT(&s->armv7m), "memory", OBJECT(system_memory),
                           &error_abort);
  object_initialize_child(OBJECT(machine), "syscon", &s->syscon,
                          TYPE_WA2X_SYSCON);

  /* ROM */
  memory_region_init_rom(&s->rom_mem, NULL, "arm.wa2x.rom",
                         wa2x_memmap[WA2X_ROM].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_ROM].base,
                              &s->rom_mem);
  /* alias ROM to 0 */
  memory_region_init_alias(&s->rom_alias, NULL, "arm.wa2x.brom", &s->rom_mem, 0,
                           wa2x_memmap[WA2X_ROM].size);
  memory_region_add_subregion(system_memory, 0x0, &s->rom_alias);

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

  if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), &error_fatal)) {
    error_report("CPU failed to init");
    exit(EXIT_FAILURE);
  }

  armv7m_load_kernel(s->armv7m.cpu, machine->firmware,
                     wa2x_memmap[WA2X_ROM].base, wa2x_memmap[WA2X_ROM].size);
}

static void wa2x_machine_instance_init(Object *obj) {}

static const char *const valid_cpu_types[] = {ARM_CPU_TYPE_NAME("cortex-m0"),
                                              ARM_CPU_TYPE_NAME("cortex-m3"),
                                              ARM_CPU_TYPE_NAME("cortex-m4"),
                                              ARM_CPU_TYPE_NAME("cortex-m7"),
                                              ARM_CPU_TYPE_NAME("cortex-m33"),
                                              ARM_CPU_TYPE_NAME("cortex-m55"),
                                              NULL};

static void wa2x_machine_class_init(ObjectClass *klass, const void *data) {
  MachineClass *mc = MACHINE_CLASS(klass);
  mc->desc = "Wa2x test runner";
  mc->init = wa2x_machine_state_init;
  mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m33");
  mc->valid_cpu_types = valid_cpu_types;
  mc->max_cpus = 1;
  mc->default_ram_id = "arm.wa2x.ram";
  mc->default_ram_size = 8 * MiB;
}

static const TypeInfo wa2x_machine_type_info = {
    .name = TYPE_ARM_WA2XM_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = wa2x_machine_class_init,
    .instance_init = wa2x_machine_instance_init,
    .instance_size = sizeof(Wa2xmMachineState),
    .interfaces = arm_machine_interfaces,
};

static void wa2x_machine_type_info_register(void) {
  type_register_static(&wa2x_machine_type_info);
}
type_init(wa2x_machine_type_info_register)
