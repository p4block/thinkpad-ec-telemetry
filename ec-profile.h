/* SPDX-License-Identifier: WTFPL */
#ifndef THINKPAD_EC_PROFILE_H
#define THINKPAD_EC_PROFILE_H
#include "profiles.h"

static char *profile;
static bool allow_unsupported;
static bool verified;
static char ec_firmware[9];
static char *firmware = ec_firmware;
static char *selected_profile;
static const struct tp_ec_profile *ec_profile;
module_param(profile, charp, 0400);
MODULE_PARM_DESC(profile, "Named protocol/map profile; automatic matching when omitted");
module_param(allow_unsupported, bool, 0400);
MODULE_PARM_DESC(allow_unsupported, "Explicitly test an unverified model/EC with a named profile; no electrical/debug channels");
module_param(verified, bool, 0444);
module_param(firmware, charp, 0444);
module_param(selected_profile, charp, 0444);
#define EC_LOCK (ec_profile->ec_lock)

static int select_ec_profile(unsigned int feature)
{
 const char *name = dmi_get_system_info(DMI_PRODUCT_NAME);
 const char *version = dmi_get_system_info(DMI_PRODUCT_VERSION);
 const struct tp_ec_profile *identity;
 int i, ret = 0;
 if (allow_unsupported && (!profile || !*profile)) {
  pr_err(KBUILD_MODNAME ": allow_unsupported requires a named profile\n");
  return -EINVAL;
 }
 identity = tp_identity_profile(name, version, profile);
 if (!identity) {
  pr_err(KBUILD_MODNAME ": no matching model/profile; see docs/adding-support.md\n");
  return -ENODEV;
 }
 if (ACPI_FAILURE(acpi_acquire_mutex(NULL, identity->ec_lock, 2000)))
  return -EBUSY;
 for (i = 0; i < 8; i++) {
  u8 byte;
  ret = ec_read(0xf0 + i, &byte);
  if (ret)
   break;
  if (byte < 0x21 || byte > 0x7e) {
   ret = -ENODEV;
   break;
  }
  ec_firmware[i] = byte;
 }
 acpi_release_mutex(NULL, identity->ec_lock);
 if (ret)
  return ret;
 ec_profile = tp_select_profile(name, version, ec_firmware, feature,
                                profile, allow_unsupported, &verified);
 if (!ec_profile) {
  pr_err(KBUILD_MODNAME ": unsupported model/EC pair (EC %.8s); see docs/adding-support.md\n", ec_firmware);
  return -ENODEV;
 }
 selected_profile = (char *)ec_profile->name;
 pr_info(KBUILD_MODNAME ": profile=%s firmware=%s verified=%s\n",
         selected_profile, ec_firmware, verified ? "yes" : "NO");
 if (!verified)
  pr_warn(KBUILD_MODNAME ": testing borrowed protocol assumptions; this is not model support\n");
 return 0;
}
static inline bool tp_feature_verified(unsigned int feature)
{
 bool matched;
 return verified && tp_select_profile(
  dmi_get_system_info(DMI_PRODUCT_NAME), dmi_get_system_info(DMI_PRODUCT_VERSION),
  ec_firmware, feature, profile, false, &matched) == ec_profile;
}
#endif
