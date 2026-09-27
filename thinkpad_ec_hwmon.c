// SPDX-License-Identifier: WTFPL
/* Firmware-restricted ThinkPad EC telemetry; see docs/protocol.md. */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include "ec-profile.h"
static DEFINE_MUTEX(cache_lock);
static bool known_ec_layout; /* Exact debug authorization, separate from a map. */
static unsigned long present;
#include "cell-voltage.h"
static struct platform_device *pdev;
static unsigned long sampled;
static bool attempted;
static int sample_error;
static u8 temps[TP_MAX_TEMPS];
static unsigned long voltage_sampled;
static bool voltage_attempted;
static int voltage_error;
static long monitor_mv;
static unsigned long battery_sampled;
static bool battery_attempted;
static int battery_error;
static s16 battery_ma[2];
static const char * const raw_labels[TP_MAX_TEMPS] = {
 "EC slot 0", "EC slot 1", "EC slot 2", "EC slot 3",
 "EC slot 4", "EC slot 5", "EC slot 6", "EC slot 7",
 "EC slot 8", "EC slot 9", "EC slot 10", "EC slot 11",
 "EC slot 12", "EC slot 13", "EC slot 14", "EC slot 15"
};
static int temp_count(void)
{
 return verified ? ec_profile->temp_count : ec_profile->temp_bytes;
}
static int temp_slot(int channel)
{
 return verified ? ec_profile->temps[channel].slot : channel;
}
static bool electrical_supported(void)
{
 return verified && !allow_unsupported && ec_profile->x230_electrical;
}

/* Caller serializes Linux readers; ECLK serializes battery AML page use. */
static int snapshot(void)
{
 u8 saved, verify, next[TP_MAX_TEMPS];
 int count = ec_profile->temp_bytes;
 int ret, restore, i;
 if (ACPI_FAILURE(acpi_acquire_mutex(NULL, EC_LOCK, 2000)))
  return -EBUSY;
 ret = ec_read(0x81, &saved);
 if (ret)
  goto unlock;
 ret = ec_write(0x81, ec_profile->temp_page);
 if (!ret)
  ret = ec_read(0x81, &verify);
 if (!ret && verify != ec_profile->temp_page)
  ret = -EIO;
 for (i = 0; !ret && i < count; i++)
  ret = ec_read(0xa0 + i, &next[i]);
 /* Restore even after a failed selection or data read. */
 restore = ec_write(0x81, saved);
 if (!restore)
  restore = ec_read(0x81, &verify);
 if (!restore && verify != saved)
  restore = -EIO;
 if (restore) {
  pr_err_ratelimited("thinkpad_ec_hwmon: EC page restoration failed: %d\n", restore);
  ret = restore;
 }
 if (!ret)
  memcpy(temps, next, count);
unlock:
 acpi_release_mutex(NULL, EC_LOCK);
 return ret;
}

static umode_t visible(const void *data, enum hwmon_sensor_types type,
                      u32 attr, int channel)
{
 if (type == hwmon_temp) {
  if (channel >= temp_count())
   return 0;
  if (verified && ec_profile->hide_unavailable && !(present & BIT(channel)))
   return 0;
 } else if (!electrical_supported()) {
  return 0;
 }
 if (type == hwmon_curr)
  return attr == hwmon_curr_input || attr == hwmon_curr_label ? 0444 : 0;
 if (type == hwmon_power)
  return attr == hwmon_power_input || attr == hwmon_power_label ? 0444 : 0;
 if (type == hwmon_in && channel && (!cell_voltages || !known_ec_layout))
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
  u8 raw = temps[temp_slot(channel)];
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
 *str = verified ? ec_profile->temps[channel].label : raw_labels[channel];
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
                         TEMP, TEMP, TEMP, TEMP, TEMP, TEMP, TEMP, TEMP, TEMP, TEMP), NULL
};
static const struct hwmon_chip_info chip = { .ops = &ops, .info = channels };
static int __init hwmon_init(void)
{
 struct device *hwmon;
 int ret, i;
 ret = select_ec_profile(TP_HWMON);
 if (ret)
  return ret;
 known_ec_layout = !allow_unsupported && tp_feature_verified(TP_CELLS) && ec_profile->g2ht35ww_cells &&
                   !strcmp(ec_firmware, "G2HT35WW");
 ret = snapshot();
 if (ret)
  return ret;
 sampled = jiffies;
 attempted = true;
 for (i = 0; i < temp_count(); i++)
  if (temps[temp_slot(i)] != 0x80)
   present |= BIT(i);
 pdev = platform_device_register_simple("thinkpad_ec_hwmon", -1, NULL, 0);
 if (IS_ERR(pdev))
  return PTR_ERR(pdev);
 hwmon = devm_hwmon_device_register_with_info(&pdev->dev, verified ? ec_profile->hwmon_name : "thinkpad_ec_test",
                                              NULL, &chip, NULL);
 if (IS_ERR(hwmon)) {
  ret = PTR_ERR(hwmon);
  platform_device_unregister(pdev);
  return ret;
 }
 return 0;
}
static void __exit hwmon_exit(void)
{
 platform_device_unregister(pdev);
}
module_init(hwmon_init);
module_exit(hwmon_exit);
/* GPL-compatible module tag; source is licensed under WTFPL v2. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ThinkPad X230 G2HT35WW and T480 N24HT37W cached EC telemetry");
