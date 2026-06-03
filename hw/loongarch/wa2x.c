/**
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
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "hw/loongarch/wa2x.h"
#include "elf.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/reset.h"

static const MemMapEntry wa2x_memmap[] = {
    [WA2X_SYSCON_MMIO] = {SYSCON_ADDRESS, SYSCON_SIZE},
    [WA2X_SYSCON_BUFFER] = {BUFFER_ADDRESS, BUFFER_SIZE},
    [WA2X_MODULE] = {MODULE_ADDRESS, MODULE_SIZE},
    [WA2X_AOT] = {WASM_AOT_BEGIN, WASM_AOT_SIZE},
    [WA2X_ROM] = {ROM_ADDRESS, ROM_SIZE},
    [WA2X_RAM] = {RAM_ADDRESS, 0x0},
    [WA2X_LIME] = {WASM_MEMORY_BEGIN, WASM_MEMORY_SIZE},
    [WA2X_MROM] = {0x1000, 0x1000},
};

static void wa2x_machine_state_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    Wa2xMachineState *s = LOONGARCH_WA2X_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    uint64_t elf_entry, elf_low, elf_high;

    /* No default firmware */
    if (!machine->firmware) {
        error_report("No firmware");
        exit(EXIT_FAILURE);
    }

    s->memmap = wa2x_memmap;
    /* Initialize CPU */
    object_initialize_child(OBJECT(machine), "cpu", &s->cpu, machine->cpu_type);
    object_initialize_child(OBJECT(machine), "syscon", &s->syscon,
                            TYPE_WA2X_SYSCON);
    if (!qdev_realize(DEVICE(&s->cpu), NULL, &error_fatal)) {
        error_report("CPU failed to init");
        exit(EXIT_FAILURE);
    }

    /* ROM */
    memory_region_init_rom(&s->rom_mem, NULL, "loongarch.wa2x.rom",
                           wa2x_memmap[WA2X_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_ROM].base,
                                &s->rom_mem);

    /* load firmware to ROM
     * In our LoongArch64 memory layout, the boot addr is fixed to WA2X_ROM
     */
    if (load_elf(machine->firmware, NULL, NULL, NULL, &elf_entry, &elf_low,
                 &elf_high, NULL, 0, EM_LOONGARCH, 1, 0) < 0) {
        error_report("failed to load firmware");
        exit(EXIT_FAILURE);
    }
    if (elf_entry != wa2x_memmap[WA2X_ROM].base) {
        error_report("the firmware should start from %lx",
                     wa2x_memmap[WA2X_ROM].base);
        exit(EXIT_FAILURE);
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->syscon), &error_fatal)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->syscon), 0,
                    wa2x_memmap[WA2X_SYSCON_MMIO].base);

    /* register main RAM */
    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be large than %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }
    memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_RAM].base,
                                machine->ram);

    /* register syscon buffer */
    memory_region_init_ram(&s->syscon.buffer, NULL, "loongarch.wa2x.buffer",
                           wa2x_memmap[WA2X_SYSCON_BUFFER].size, &error_fatal);
    memory_region_add_subregion(
        system_memory, wa2x_memmap[WA2X_SYSCON_BUFFER].base, &s->syscon.buffer);

    /* register module memory */
    memory_region_init_ram(&s->syscon.module, NULL, "loongarch.wa2x.module",
                           wa2x_memmap[WA2X_MODULE].size, &error_fatal);
    memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MODULE].base,
                                &s->syscon.module);

    /* register aot memory */
    memory_region_init_ram(&s->syscon.aot, NULL, "loongarch.wa2x.aot",
                           wa2x_memmap[WA2X_AOT].size, &error_fatal);
    memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_AOT].base,
                                &s->syscon.aot);

    /* register lime memory */
    memory_region_init_ram(&s->syscon.lime, NULL, "loongarch.wa2x.lime",
                           wa2x_memmap[WA2X_LIME].size, &error_fatal);
    memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_LIME].base,
                                &s->syscon.lime);

    /* boot rom / mask rom */
    memory_region_init_rom(&s->mask_rom, NULL, "loongarch.wa2x.mrom",
                           wa2x_memmap[WA2X_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MROM].base,
                                &s->mask_rom);
}

static void wa2x_machine_reset(MachineState *machine, ResetType type)
{
    CPUState *cs;
    LoongArchCPU *cpu;

    qemu_devices_reset(type);

    CPU_FOREACH(cs) {
        cpu = LOONGARCH_CPU(cs);
        cpu->env.pc = wa2x_memmap[WA2X_ROM].base;
    }
}

static void wa2x_machine_class_init(ObjectClass *klass, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    mc->desc = "Wa2x test runner";
    mc->init = wa2x_machine_state_init;
    mc->default_cpu_type = LOONGARCH_CPU_TYPE_NAME("la464");
    mc->max_cpus = 1;
    mc->default_ram_id = "loongarch.wa2x.ram";
    mc->default_ram_size = 8 * MiB;
    mc->reset = wa2x_machine_reset;
}

static const TypeInfo wa2x_machine_type_info = {
    .name = TYPE_LOONGARCH_WA2X_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = wa2x_machine_class_init,
    .instance_size = sizeof(Wa2xMachineState),
};

static void wa2x_machine_type_info_register(void)
{
    type_register_static(&wa2x_machine_type_info);
}
type_init(wa2x_machine_type_info_register)
