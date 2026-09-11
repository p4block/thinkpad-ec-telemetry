// SPDX-License-Identifier: WTFPL
/* Experimental G2HT35WW temperature page, validated on coreboot X230. */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

static bool experimental;
module_param(experimental, bool, 0400);
MODULE_PARM_DESC(experimental, "Explicit opt-in for testing unrecognized EC firmware; never enables unknown debug layouts");
#define EC_LOCK "\\_SB.PCI0.LPCB.EC.ECLK"
static DEFINE_MUTEX(cache_lock);
static bool known_ec_layout;

/* Same build-ID registers used by coreboot's h8_build_id_and_function_spec_version.
 * Read before any page selection or debug authentication. No BIOS-string dependency.
 */
static int check_ec_firmware(void)
{
 char id[9] = { 0 };
 bool valid = true;
 int i, ret = 0;
 if (ACPI_FAILURE(acpi_acquire_mutex(NULL, EC_LOCK, 2000)))
  return -EBUSY;
 for (i = 0; i < 8; i++) {
  u8 byte;
  ret = ec_read(0xf0 + i, &byte);
  if (ret) break;
  id[i] = byte;
  if (byte < 0x21 || byte > 0x7e) valid = false;
 }
 acpi_release_mutex(NULL, EC_LOCK);
 known_ec_layout = !ret && valid && !strcmp(id, "G2HT35WW");
 if (known_ec_layout) {
  pr_info("x230_ec_hwmon: recognized EC firmware %s\n", id);
  return 0;
 }
 if (experimental) {
  pr_warn("x230_ec_hwmon: unverified EC firmware; ordinary sensors only\n");
  return 0;
 }
 /* Model fallback only when no usable ID exists, not a known different build. */
 if ((ret || !valid) &&
     (dmi_match(DMI_PRODUCT_VERSION, "ThinkPad X230") ||
      dmi_match(DMI_PRODUCT_NAME, "ThinkPad X230"))) {
  pr_warn("x230_ec_hwmon: EC ID unavailable; X230 model fallback, ordinary sensors only\n");
  return 0;
 }
 pr_err("x230_ec_hwmon: unsupported EC firmware; use experimental=1 for ordinary sensor testing\n");
 return -ENODEV;
}
#include "cell-voltage.h"
static struct platform_device *pdev;
static unsigned long sampled;
static bool attempted;
static int sample_error;
static u8 temps[13];
static unsigned long voltage_sampled;
static bool voltage_attempted;
static int voltage_error;
static long monitor_mv;
static unsigned long battery_sampled;
static bool battery_attempted;
static int battery_error;
static s16 battery_ma[2];
static const u8 indices[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
static const char * const labels[] = {
 "CPU (EC)", "CPU VRM area", "WLAN area", "Battery 0 source 2",
 "Battery 1 source 2", "Battery 0 source 1", "Battery 1 source 1",
 "Memory top", "WWAN area", "Memory bottom", "PCH", "VTT regulator area"
};

/* Caller serializes Linux readers; ECLK serializes battery AML page use. */
static int snapshot(void)
{
 u8 saved, verify, next[13];
 int ret, restore, i;
 if (ACPI_FAILURE(acpi_acquire_mutex(NULL, EC_LOCK, 2000)))
  return -EBUSY;
 ret = ec_read(0x81, &saved);
 if (ret)
  goto unlock;
 ret = ec_write(0x81, 0x60);
 if (!ret)
  ret = ec_read(0x81, &verify);
 if (!ret && verify != 0x60)
  ret = -EIO;
 for (i = 0; !ret && i < ARRAY_SIZE(next); i++)
  ret = ec_read(0xa0 + i, &next[i]);
 /* Restore even after a failed selection or data read. */
 restore = ec_write(0x81, saved);
 if (!restore)
  restore = ec_read(0x81, &verify);
 if (!restore && verify != saved)
  restore = -EIO;
 if (restore) {
  pr_err_ratelimited("x230_ec_hwmon: EC page restoration failed: %d\n", restore);
  ret = restore;
 }
 if (!ret)
  memcpy(temps, next, sizeof(temps));
unlock:
 acpi_release_mutex(NULL, EC_LOCK);
 return ret;
}

static umode_t visible(const void *data, enum hwmon_sensor_types type,
                      u32 attr, int channel)
{
 if (type == hwmon_curr)
  return attr == hwmon_curr_input || attr == hwmon_curr_label ? 0444 : 0;
 if (type == hwmon_power)
  return attr == hwmon_power_input || attr == hwmon_power_label ? 0444 : 0;
 if (type == hwmon_in && channel && !cell_voltages)
  return 0;
 if (type == hwmon_in)
  return attr == hwmon_in_input || attr == hwmon_in_label ? 0444 : 0;
 return type == hwmon_temp &&
        (attr == hwmon_temp_input || attr == hwmon_temp_label) ? 0444 : 0;
}
/* Dedicated firmware-averaged ADC8 shadow, no diagnostic unlock required. */
static int read_monitor(long *value)
{
 u8 hi, lo, check;
 int i, ret = 0;
 mutex_lock(&cache_lock);
 if (!voltage_attempted || time_after_eq(jiffies, voltage_sampled + 10 * HZ)) {
  for (i = 0; i < 4; i++) {
   ret = ec_read(0xcc, &hi);
   if (!ret) ret = ec_read(0xcd, &lo);
   if (!ret) ret = ec_read(0xcc, &check);
   if (ret || hi == check) break;
  }
  if (!ret && i == 4) ret = -EAGAIN;
  if (!ret) monitor_mv = ((unsigned int)hi << 8) | lo;
  voltage_error = ret;
  voltage_sampled = jiffies;
  voltage_attempted = true;
 }
 ret = voltage_error;
 if (!ret) *value = monitor_mv;
 mutex_unlock(&cache_lock);
 return ret;
}
/* Battery0 dedicated pages: current from SBS0Ah, EC-average separately. */
static int battery_snapshot(void)
{
 u8 saved, verify, lo, hi, avg_lo, avg_hi, cur_lo, cur_hi;
 int ret, restore;
 if (ACPI_FAILURE(acpi_acquire_mutex(NULL, EC_LOCK, 2000))) return -EBUSY;
 ret = ec_read(0x81, &saved);
 if (ret) goto unlock;
 ret = ec_write(0x81, 0x00);
 if (!ret) ret = ec_read(0x81, &verify);
 if (!ret && verify != 0) ret = -EIO;
 /* Voltage getter returns0 for unavailable battery data. */
 if (!ret) ret = ec_read(0xaa, &lo);
 if (!ret) ret = ec_read(0xab, &hi);
 if (!ret && !(lo | hi)) ret = -ENODATA;
 if (!ret) ret = ec_read(0xa8, &avg_lo);
 if (!ret) ret = ec_read(0xa9, &avg_hi);
 if (!ret) ret = ec_write(0x81, 0x01);
 if (!ret) ret = ec_read(0x81, &verify);
 if (!ret && verify != 1) ret = -EIO;
 if (!ret) ret = ec_read(0xa6, &cur_lo);
 if (!ret) ret = ec_read(0xa7, &cur_hi);
 restore = ec_write(0x81, saved);
 if (!restore) restore = ec_read(0x81, &verify);
 if (!restore && verify != saved) restore = -EIO;
 if (restore) ret = restore;
 if (!ret) {
  battery_ma[0] = (s16)(((u16)cur_hi << 8) | cur_lo);
  battery_ma[1] = (s16)(((u16)avg_hi << 8) | avg_lo);
 }
unlock:
 acpi_release_mutex(NULL, EC_LOCK);
 return ret;
}
static int read_battery(int channel, long *value)
{
 int ret;
 mutex_lock(&cache_lock);
 if (!battery_attempted || time_after_eq(jiffies, battery_sampled + 10 * HZ)) {
  battery_error = battery_snapshot();
  battery_sampled = jiffies;
  battery_attempted = true;
 }
 ret = battery_error;
 if (!ret) *value = battery_ma[channel];
 mutex_unlock(&cache_lock);
 return ret;
}
static int read_temp(struct device *dev, enum hwmon_sensor_types type,
                     u32 attr, int channel, long *value)
{
 int ret;
 if (type == hwmon_curr && attr == hwmon_curr_input)
  return read_battery(channel, value);
 if (type == hwmon_power && attr == hwmon_power_input) {
  ret = read_monitor(value);
  /* Empirical estimate at 20 V adapter input: mV / 10 W.
   * hwmon power_input uses microwatts. Includes battery charging.
   */
  if (!ret) *value *= 100000L;
  return ret;
 }
 if (type == hwmon_in && attr == hwmon_in_input)
  return channel ? read_cell(channel - 1, value) : read_monitor(value);
 if (type != hwmon_temp || attr != hwmon_temp_input)
  return -EOPNOTSUPP;
 mutex_lock(&cache_lock);
 if (!attempted || time_after_eq(jiffies, sampled + 10 * HZ)) {
  sample_error = snapshot();
  sampled = jiffies;
  attempted = true;
 }
 ret = sample_error;
 if (!ret) {
  u8 raw = temps[indices[channel]];
  if (raw == 0x80)
   ret = -ENODATA;
  else
   *value = (long)(s8)raw * 1000;
 }
 mutex_unlock(&cache_lock);
 return ret;
}
static int read_label(struct device *dev, enum hwmon_sensor_types type,
                      u32 attr, int channel, const char **str)
{
 if (type == hwmon_curr && attr == hwmon_curr_label) {
  *str = channel ? "Battery0 average (+charge)" : "Battery0 current (+charge)";
  return 0;
 }
 if (type == hwmon_power && attr == hwmon_power_label) {
  *str = "DC input estimate (20V)";
  return 0;
 }
 if (type == hwmon_in && attr == hwmon_in_label) {
  static const char * const names[] = { "Charger IOUT monitor",
   "Battery0 group A (experimental)", "Battery0 group B (experimental)",
   "Battery0 group C (experimental)" };
  *str = names[channel];
  return 0;
 }
 if (type != hwmon_temp || attr != hwmon_temp_label)
  return -EOPNOTSUPP;
 *str = labels[channel];
 return 0;
}
static const struct hwmon_ops ops = {
 .is_visible = visible, .read = read_temp, .read_string = read_label,
};
#define TEMP (HWMON_T_INPUT | HWMON_T_LABEL)
static const struct hwmon_channel_info * const channels[] = {
 HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL,
  HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
  HWMON_I_INPUT | HWMON_I_LABEL),
 HWMON_CHANNEL_INFO(power, HWMON_P_INPUT | HWMON_P_LABEL),
 HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_LABEL, HWMON_C_INPUT | HWMON_C_LABEL),
 HWMON_CHANNEL_INFO(temp, TEMP, TEMP, TEMP, TEMP, TEMP, TEMP,
                         TEMP, TEMP, TEMP, TEMP, TEMP, TEMP), NULL
};
static const struct hwmon_chip_info chip = { .ops = &ops, .info = channels };
static int __init x230_init(void)
{
 struct device *hwmon;
 int ret;
 ret = check_ec_firmware();
 if (ret)
  return ret;
 ret = snapshot();
 if (ret)
  return ret;
 sampled = jiffies;
 attempted = true;
 pdev = platform_device_register_simple("x230_ec_hwmon", -1, NULL, 0);
 if (IS_ERR(pdev))
  return PTR_ERR(pdev);
 hwmon = devm_hwmon_device_register_with_info(&pdev->dev, "x230_ec",
                                              NULL, &chip, NULL);
 if (IS_ERR(hwmon)) {
  ret = PTR_ERR(hwmon);
  platform_device_unregister(pdev);
  return ret;
 }
 return 0;
}
static void __exit x230_exit(void)
{
 platform_device_unregister(pdev);
}
module_init(x230_init);
module_exit(x230_exit);
/* GPL-compatible module tag; source is licensed under WTFPL v2. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental X230 G2HT35WW cached EC temperatures");
