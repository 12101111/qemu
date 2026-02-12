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

#include "qemu/osdep.h"

#include "hw/char/wa2x_syscon.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "system/memory.h"
#include <stdint.h>

static uint64_t wa2x_syscon_read(void *opaque, hwaddr addr, unsigned size) {
  Wa2xSysconState *s = opaque;
  uint64_t value = 0;
  wa2x_runner_read(s->runner, addr, size, (uint8_t *)&value);
  return value;
}

static void wa2x_syscon_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned size) {
  Wa2xSysconState *s = opaque;
  wa2x_runner_write(s->runner, addr, size, (uint8_t *)&data);
}

static const MemoryRegionOps wa2x_syscon_ops = {
    .read = wa2x_syscon_read,
    .write = wa2x_syscon_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.min_access_size = 1, .max_access_size = 8},
    .valid = {.min_access_size = 1, .max_access_size = 8},
};

static void wa2x_syscon_reset(DeviceState *dev) {
  // Wa2xSysconState *s = WA2X_SYSCON(dev);
}

static void wa2x_syscon_realize(DeviceState *dev, Error **errp) {
  Wa2xSysconState *s = WA2X_SYSCON(dev);
  runner_t runner =
      wa2x_runner_new(s, s->conf.runner, s->conf.opt);
  if (!runner) {
    error_setg(errp, "can't create runner");
    return;
  }
  s->runner = runner;
}

static void wa2x_syscon_unrealize(DeviceState *dev) {
  Wa2xSysconState *s = WA2X_SYSCON(dev);
  if (s->runner) {
    wa2x_runner_drop(s->runner);
  }
}

static void wa2x_syscon_instance_init(Object *obj) {
  Wa2xSysconState *s = WA2X_SYSCON(obj);
  memory_region_init_io(&s->mmio, obj, &wa2x_syscon_ops, s, TYPE_WA2X_SYSCON,
                        0x1000);
  sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const Property wa2x_syscon_properties[] = {
    DEFINE_PROP_STRING("runner", Wa2xSysconState, conf.runner),
    DEFINE_PROP_STRING("opt", Wa2xSysconState, conf.opt),
};

static void wa2x_syscon_class_init(ObjectClass *klass, const void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  dc->realize = wa2x_syscon_realize;
  dc->unrealize = wa2x_syscon_unrealize;
  dc->desc = "Wa2x syscon";
  device_class_set_legacy_reset(dc, wa2x_syscon_reset);
  device_class_set_props(dc, wa2x_syscon_properties);
  set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo wa2x_syscon_info = {
    .name = TYPE_WA2X_SYSCON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Wa2xSysconState),
    .class_init = wa2x_syscon_class_init,
    .instance_init = wa2x_syscon_instance_init,
};

static void wa2x_syscon_register_types(void) {
  type_register_static(&wa2x_syscon_info);
}
type_init(wa2x_syscon_register_types);

void wa2x_syscon_exit_code(Wa2xSysconState *syscon, uint16_t code) {
  exit(code);
}

void wa2x_syscon_write_buffer(Wa2xSysconState *syscon, size_t len,
                              const uint8_t *bytes) {
  uint8_t *base = memory_region_get_ram_ptr(&syscon->buffer);
  memcpy(base, bytes, len);
}

void wa2x_syscon_read_buffer(Wa2xSysconState *syscon, size_t len,
                             uint8_t *bytes) {
  const uint8_t *base = memory_region_get_ram_ptr(&syscon->buffer);
  memcpy(bytes, base, len);
}

void wa2x_syscon_write_module(Wa2xSysconState *syscon, size_t len,
                              const uint8_t *bytes) {
  uint8_t *base = memory_region_get_ram_ptr(&syscon->module);
  memcpy(base, bytes, len);
}
