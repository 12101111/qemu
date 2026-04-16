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
#include "hw/boards.h"
#include "hw/i386/wa2x.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/reset.h"
#include <stdint.h>

static const MemMapEntry wa2x_memmap[] = {
    [WA2X_ROM] = {0x80000000, 0x400000},
    [WA2X_RAM] = {0x80400000, 0x0},
    [WA2X_SYSCON_BUFFER] = {0x50010000, 0x10000},
    [WA2X_SYSCON_MMIO] = {0x50000000, 0x10000},
    [WA2X_MODULE] = {0x58000000, 0x4000000},
    [WA2X_MROM] = {0x10000, 0x10000},
};

#define PAGE_TABLE_ADDRESS 0x11000ULL
#define GDT_SELECTOR_CODE 8
#define GDT_SELECTOR_DATA 16

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_PWT (1ULL << 3) // write-through
#define PTE_PCD (1ULL << 4) // cache disable
#define PTE_PS (1ULL << 7)  // huge page (2M)
#define PTE_XD (1ULL << 63) // no execute
#define MB (1024 * 1024)

#define PDPT_PADDR (PAGE_TABLE_ADDRESS + 0x1000)
#define PD0_PADDR (PAGE_TABLE_ADDRESS + 0x2000)
#define PD1_PADDR (PAGE_TABLE_ADDRESS + 0x3000)
#define PD2_PADDR (PAGE_TABLE_ADDRESS + 0x4000)
#define PT0_PADDR (PAGE_TABLE_ADDRESS + 0x5000)
#define PT1_PADDR (PAGE_TABLE_ADDRESS + 0x6000)

#define SYSCON_ADDRESS wa2x_memmap[WA2X_SYSCON_MMIO].base
#define SYSCON_END                                                             \
  (wa2x_memmap[WA2X_SYSCON_MMIO].base + wa2x_memmap[WA2X_SYSCON_MMIO].size)
#define BUFFER_ADDRESS wa2x_memmap[WA2X_SYSCON_BUFFER].base
#define BUFFER_END                                                             \
  (wa2x_memmap[WA2X_SYSCON_BUFFER].base + wa2x_memmap[WA2X_SYSCON_BUFFER].size)
#define MODULE_ADDRESS wa2x_memmap[WA2X_MODULE].base
#define MODULE_END                                                             \
  (wa2x_memmap[WA2X_MODULE].base + wa2x_memmap[WA2X_MODULE].size)
#define RAM_ADDRESS wa2x_memmap[WA2X_RAM].base
#define DEFAULT_RAM_SIZE 0x100000
#define LIME_BEGIN 0x81000000
#define WASM_AOT_END 0x90000000

static uint64_t map_descriptor(uint32_t base, uint32_t limit, uint32_t type,
                               int present, int dpl, int s, int db, int l,
                               int g) {
  uint32_t lower = ((base & 0xFFFF) << 16) | (limit & 0xFFFF);
  uint32_t upper = ((base >> 24) << 24) | (g << 23) | (db << 22) | (l << 21) |
                   (((limit >> 16) & 0xF) << 16) | (present << 15) |
                   (dpl << 13) | (s << 12) | (type << 8) |
                   ((base >> 16) & 0xFF);
  return lower | ((uint64_t)upper << 32);
}

static void wa2x_x86_bootrom_setup(MemoryRegion *boot_rom) {
  uint64_t *mem = memory_region_get_ram_ptr(boot_rom);

  /* GDT */
  mem[0] = 0;                                                 /* null */
  mem[1] = map_descriptor(0, 0xFFFFF, 0xB, 1, 0, 1, 0, 1, 1); /* code */
  mem[2] = map_descriptor(0, 0xFFFFF, 0x3, 1, 0, 1, 1, 0, 1); /* data */

  /* Page tables */
  uint64_t pml4_idx = 0x1000 / 8;
  uint64_t pdpt_idx = 0x2000 / 8;
  uint64_t pd0_idx = 0x3000 / 8;
  uint64_t pd1_idx = 0x4000 / 8;
  uint64_t pd2_idx = 0x5000 / 8;
  uint64_t pt0_idx = 0x6000 / 8;
  uint64_t pt1_idx = 0x7000 / 8;

  // 1. PML4
  // 0-512G
  mem[pml4_idx + 0] = PDPT_PADDR | PTE_PRESENT | PTE_WRITABLE;

  // 2. PDPT
  // 0-1G
  mem[pdpt_idx + 0] = PD0_PADDR | PTE_PRESENT | PTE_WRITABLE;
  // 1-2G
  mem[pdpt_idx + 1] = PD1_PADDR | PTE_PRESENT | PTE_WRITABLE;
  // 2-3G
  mem[pdpt_idx + 2] = PD2_PADDR | PTE_PRESENT | PTE_WRITABLE;

  // 3. PD0 (0-1GB)
  // 3a) index 0: PT0, 0-2MB 4KB page
  mem[pd0_idx + 0] = PT0_PADDR | PTE_PRESENT | PTE_WRITABLE;

  // 4. PD1 (1-2GB)
  const uint64_t PD1_BASE = 0x40000000;
  const uint64_t PD1_BASE_INDEX = PD1_BASE / (2 * MB);
  // 4a) index 0x200: PT1, 0x50000000-0x50020000 4KB page
  const uint64_t PT1_INDEX = (SYSCON_ADDRESS - PD1_BASE) / (2 * MB);
  mem[pd1_idx + PT1_INDEX] = PT1_PADDR | PTE_PRESENT | PTE_WRITABLE;

  // 4b) index 0x280-0x2BF: 0x58000000-0x5C000000 2MB huge page RWX
  const uint64_t MODULE_ENTRY_BEGIN = (MODULE_ADDRESS - PD1_BASE) / (2 * MB);
  const uint64_t MODULE_ENTRY_END = (MODULE_END - PD1_BASE) / (2 * MB);
  for (uint64_t idx = MODULE_ENTRY_BEGIN; idx < MODULE_ENTRY_END; idx++) {
    uint64_t phys = ((uint64_t)(idx + PD1_BASE_INDEX)) << 21;
    mem[pd1_idx + idx] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_PS;
  }

  // 5. PD2 (2-3GB)
  const uint64_t PD2_BASE = 0x80000000;
  const uint64_t PD2_BASE_INDEX = PD2_BASE / (2 * MB);
  // 5a) index 0x400-0x401: 0x80000000-0x80400000 2MB huge page RX
  for (uint64_t idx = 0; idx <= 1; idx++) {
    uint64_t phys = ((uint64_t)(idx + PD2_BASE_INDEX)) << 21;
    mem[pd2_idx + idx] = phys | PTE_PRESENT | PTE_PS;
  }

  // 5b) index 0x402～0x402: 0x80400000-0x80600000 2MB huge page RW
  const uint64_t RAM_ENTRY_BEGIN = (RAM_ADDRESS - PD2_BASE) / (2 * MB);
  const uint64_t RAM_ENTRY_END = RAM_ENTRY_BEGIN + 1;
  for (uint64_t idx = RAM_ENTRY_BEGIN; idx < RAM_ENTRY_END; idx++) {
    uint64_t phys = ((uint64_t)(idx + PD2_BASE_INDEX)) << 21;
    mem[pd2_idx + idx] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_PS;
  }

  // 5c) index 0x408～0x480: 0x81000000-0x90000000 2MB huge page RW
  const uint64_t LIME_ENTRY_BEGIN = (LIME_BEGIN - PD2_BASE) / (2 * MB);
  const uint64_t AOT_ENTRY_END = (WASM_AOT_END - PD2_BASE) / (2 * MB);
  for (uint64_t idx = LIME_ENTRY_BEGIN; idx < AOT_ENTRY_END; idx++) {
    uint64_t phys = ((uint64_t)(idx + PD2_BASE_INDEX)) << 21;
    mem[pd2_idx + idx] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_PS;
  }

  // 6. PT0 (0-2MB)
  // index 0x10-0x1F: 0x10000-0x20000 4K page
  for (uint64_t pte_idx = 0x10; pte_idx <= 0x1F; pte_idx++) {
    uint64_t phys = pte_idx << 12;
    mem[pt0_idx + pte_idx] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_XD;
  }

  // 7. PT1 (0x50000000-0x50200000)
  const uint64_t PT1_BASE = 0x50000000;
  const uint64_t PT1_BASE_INDEX = PT1_BASE / 4096;
  // 7a) index 0: 0x50000000-0x50001000
  const uint64_t SYSCON_ENTRY_BEGIN = (SYSCON_ADDRESS - PT1_BASE) / 4096;
  const uint64_t SYSCON_ENTRY_END = (SYSCON_END - PT1_BASE) / 4096;
  for (uint64_t pte_idx = SYSCON_ENTRY_BEGIN; pte_idx < SYSCON_ENTRY_END;
       pte_idx++) {
    uint64_t phys = (pte_idx + PT1_BASE_INDEX) << 12;
    mem[pt1_idx + pte_idx] =
        phys | PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD | PTE_XD;
  }
  mem[pt1_idx + 0] = (uint64_t)SYSCON_ADDRESS | PTE_PRESENT | PTE_WRITABLE |
                     PTE_PWT | PTE_PCD | PTE_XD;

  // 7b) index 1: 0x50001000-0x50002000
  const uint64_t BUFFER_ENTRY_BEGIN = (BUFFER_ADDRESS - PT1_BASE) / 4096;
  const uint64_t BUFFER_ENTRY_END = (BUFFER_END - PT1_BASE) / 4096;
  for (uint64_t pte_idx = BUFFER_ENTRY_BEGIN; pte_idx < BUFFER_ENTRY_END;
       pte_idx++) {
    uint64_t phys = (pte_idx + PT1_BASE_INDEX) << 12;
    mem[pt1_idx + pte_idx] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_XD;
  }
}

static void wa2x_x86_cpu_setup(X86CPU *cpu) {
  CPUX86State *env = &cpu->env;

  cpu_load_efer(env, MSR_EFER_SCE | MSR_EFER_LME | MSR_EFER_LMA | MSR_EFER_NXE |
                         MSR_EFER_FFXSR);
  cpu_x86_update_cr4(env, CR4_PAE_MASK | CR4_FSGSBASE_MASK | CR4_OSFXSR_MASK |
                              CR4_OSXSAVE_MASK);
  cpu_x86_update_cr3(env, PAGE_TABLE_ADDRESS);
  cpu_x86_update_cr0(env, CR0_PE_MASK | CR0_PG_MASK | CR0_NE_MASK);

  cpu_x86_load_seg_cache(env, R_CS, GDT_SELECTOR_CODE, 0, 0xFFFFFFFF,
                         DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                             DESC_R_MASK | DESC_A_MASK | DESC_L_MASK |
                             DESC_G_MASK);
  cpu_x86_load_seg_cache(env, R_DS, GDT_SELECTOR_DATA, 0, 0xFFFFFFFF,
                         DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK |
                             DESC_B_MASK | DESC_G_MASK);
  cpu_x86_load_seg_cache(env, R_ES, GDT_SELECTOR_DATA, 0, 0xFFFFFFFF,
                         DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK |
                             DESC_B_MASK | DESC_G_MASK);
  cpu_x86_load_seg_cache(env, R_FS, GDT_SELECTOR_DATA, 0, 0xFFFFFFFF,
                         DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK |
                             DESC_B_MASK | DESC_G_MASK);
  cpu_x86_load_seg_cache(env, R_GS, GDT_SELECTOR_DATA, 0, 0xFFFFFFFF,
                         DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK |
                             DESC_B_MASK | DESC_G_MASK);
  cpu_x86_load_seg_cache(env, R_SS, GDT_SELECTOR_DATA, 0, 0xFFFFFFFF,
                         DESC_P_MASK | DESC_S_MASK | DESC_W_MASK | DESC_A_MASK |
                             DESC_B_MASK | DESC_G_MASK);

  env->gdt.base = wa2x_memmap[WA2X_MROM].base;
  env->gdt.limit = 23;
  env->eip = wa2x_memmap[WA2X_ROM].base;
  env->eflags = 0x2;
  env->xcr0 = XSTATE_FP_MASK | XSTATE_SSE_MASK | XSTATE_YMM_MASK;
  cpu_sync_avx_hflag(env);
  CPU(cpu)->vcpu_dirty = true;
}

static void wa2x_machine_state_init(MachineState *machine) {
  MachineClass *mc = MACHINE_GET_CLASS(machine);
  Wa2xMachineState *s = X86_WA2X_MACHINE(machine);
  MemoryRegion *system_memory = get_system_memory();

  /* No default firmware */
  if (!machine->firmware) {
    error_report("No firmware");
    exit(EXIT_FAILURE);
  }

  s->memmap = wa2x_memmap;
  /* Initialize SoC */
  object_initialize_child(OBJECT(machine), "cpu", &s->cpu, machine->cpu_type);
  if (object_property_find(OBJECT(&s->cpu), "apic-id")) {
    object_property_set_uint(OBJECT(&s->cpu), "apic-id", 0, &error_abort);
  }
  object_initialize_child(OBJECT(machine), "syscon", &s->syscon,
                          TYPE_WA2X_SYSCON);
  if (!qdev_realize(DEVICE(&s->cpu), NULL, &error_fatal)) {
    error_report("CPU failed to init");
    exit(EXIT_FAILURE);
  }

  /* ROM */
  memory_region_init_rom(&s->rom_mem, NULL, "x86.wa2x.rom",
                         wa2x_memmap[WA2X_ROM].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_ROM].base,
                              &s->rom_mem);

  /* load firmware to ROM
   * In our x86_64 memory layout, the boot addr is fixed to WA2X_ROM
   */
  uint64_t elf_entry, elf_low, elf_high;
  if (load_elf(machine->firmware, NULL, NULL, NULL, &elf_entry, &elf_low,
               &elf_high, NULL, 0, EM_X86_64, 1, 0) < 0) {
    error_report("failed to load firmware");
    exit(EXIT_FAILURE);
  }
  if (elf_entry != wa2x_memmap[WA2X_ROM].base) {
    error_report("the firmware should start from %lx",
                 wa2x_memmap[WA2X_ROM].base);
    exit(EXIT_FAILURE);
  }

  qdev_prop_set_string(DEVICE(&(s->syscon)), "runner", machine->firmware);
  if (s->opt)
    qdev_prop_set_string(DEVICE(&(s->syscon)), "opt", s->opt);
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
  memory_region_init_ram(&s->syscon.buffer, NULL, "x86.wa2x.buffer",
                         wa2x_memmap[WA2X_SYSCON_BUFFER].size, &error_fatal);
  memory_region_add_subregion(
      system_memory, wa2x_memmap[WA2X_SYSCON_BUFFER].base, &s->syscon.buffer);

  /* register module memory */
  memory_region_init_ram(&s->syscon.module, NULL, "x86.wa2x.module",
                         wa2x_memmap[WA2X_MODULE].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MODULE].base,
                              &s->syscon.module);

  /* boot rom */
  memory_region_init_ram(&s->boot_rom, NULL, "x86.wa2x.bootrom",
                         wa2x_memmap[WA2X_MROM].size, &error_fatal);
  memory_region_add_subregion(system_memory, wa2x_memmap[WA2X_MROM].base,
                              &s->boot_rom);
  wa2x_x86_bootrom_setup(&s->boot_rom);
}

static void wa2x_machine_reset(MachineState *machine, ResetType type) {
  CPUState *cs;
  X86CPU *cpu;

  qemu_devices_reset(type);

  CPU_FOREACH(cs) {
    cpu = X86_CPU(cs);
    x86_cpu_after_reset(cpu);
    wa2x_x86_cpu_setup(cpu);
  }
}

static char *wa2x_machine_opt_get(Object *obj, Error **errp) {
  Wa2xMachineState *s = X86_WA2X_MACHINE(obj);
  if (s->opt) {
    return g_strdup(s->opt);
  } else {
    return g_strdup("");
  }
}

static void wa2x_machine_opt_set(Object *obj, const char *val, Error **errp) {
  Wa2xMachineState *s = X86_WA2X_MACHINE(obj);
  if (s->opt)
    g_free(s->opt);
  s->opt = g_strdup(val);
}

static void wa2x_machine_class_init(ObjectClass *klass, const void *data) {
  MachineClass *mc = MACHINE_CLASS(klass);
  mc->desc = "Wa2x test runner";
  mc->init = wa2x_machine_state_init;
  mc->default_cpu_type = X86_CPU_TYPE_NAME("Nehalem");
  mc->max_cpus = 1;
  mc->default_ram_id = "x86.wa2x.ram";
  mc->default_ram_size = 252 * MiB;
  object_class_property_add_str(klass, "opt", wa2x_machine_opt_get,
                                wa2x_machine_opt_set);
  mc->reset = wa2x_machine_reset;
}

static const TypeInfo wa2x_machine_type_info = {
    .name = TYPE_X86_WA2X_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = wa2x_machine_class_init,
    .instance_size = sizeof(Wa2xMachineState),
};

static void wa2x_machine_type_info_register(void) {
  type_register_static(&wa2x_machine_type_info);
}
type_init(wa2x_machine_type_info_register)
