/* SPDX-License-Identifier: WTFPL */
#ifndef THINKPAD_EC_PROFILES_H
#define THINKPAD_EC_PROFILES_H

#define TP_MAX_TEMPS 16
#define TP_HWMON 1
#define TP_ACCEL 2
#define TP_CELLS 4

/* These profiles describe the existing paged EC and two-axis mailbox protocols.
 * A different protocol needs implementation/evidence, not invented parameters.
 */
struct tp_temp_channel {
 unsigned char slot;
 const char *label;
};
struct tp_ec_profile {
 const char *name;
 const char *hwmon_name;
 char *ec_lock; /* ACPICA mutex API takes a mutable path pointer. */
 unsigned char temp_page, temp_bytes, temp_count;
 struct tp_temp_channel temps[TP_MAX_TEMPS];
 bool hide_unavailable;
 bool x230_electrical; /* Verified CC/CD scaling and battery page layout. */
 bool g2ht35ww_cells;  /* Fixed debug RAM layout, never granted by an override. */
 unsigned short mailbox_port;
 bool check_command_errors;
};

static const struct tp_ec_profile tp_profiles[] = {
 {
  .name = "x230", .hwmon_name = "x230_ec",
  .ec_lock = "\\_SB.PCI0.LPCB.EC.ECLK",
  .temp_page = 0x60, .temp_bytes = 13, .temp_count = 12,
  .temps = {
   {0, "CPU (EC)"}, {2, "CPU VRM area"}, {3, "WLAN area"},
   {4, "Battery 0 source 2"}, {5, "Battery 1 source 2"},
   {6, "Battery 0 source 1"}, {7, "Battery 1 source 1"},
   {8, "Memory top"}, {9, "WWAN area"}, {10, "Memory bottom"},
   {11, "PCH"}, {12, "VTT regulator area"}
  },
  .x230_electrical = true, .g2ht35ww_cells = true,
  .mailbox_port = 0x1610,
 },
 {
  .name = "t480", .hwmon_name = "t480_ec",
  .ec_lock = "\\_SB.PCI0.LPCB.EC.ECLK",
  .temp_page = 0x60, .temp_bytes = 14, .temp_count = 14,
  .temps = {
   {0, "CPU (EC)"}, {1, "EC source 0"}, {2, "CPU VRM area"},
   {3, "EC slot 3"}, {4, "Battery 0 source 2"}, {5, "Battery 1 source 2"},
   {6, "Battery 0 source 1"}, {7, "Battery 1 source 1"},
   {8, "Fan area"}, {9, "DIMM area"}, {10, "WWAN area"},
   {11, "EC slot 11"}, {12, "dGPU VRM area"}, {13, "WLAN area"}
  },
  .hide_unavailable = true, .mailbox_port = 0x1610,
  .check_command_errors = true,
 }
};

/* Model aliases and exact EC IDs are separate from the reusable protocol/map.
 * Grant features independently: temperature evidence does not prove motion.
 */
struct tp_ec_match {
 const char *model;
 const char *firmware;
 const char *profile;
 unsigned int features;
};
static const struct tp_ec_match tp_matches[] = {
 {"ThinkPad X230", "G2HT35WW", "x230", TP_HWMON | TP_ACCEL | TP_CELLS},
 {"ThinkPad T480", "N24HT37W", "t480", TP_HWMON | TP_ACCEL},
 {"T480", "N24HT37W", "t480", TP_HWMON | TP_ACCEL},
};

static inline bool tp_model_matches(const char *model, const char *name,
                                    const char *version)
{
 return (name && !strcmp(model, name)) ||
        (version && !strcmp(model, version));
}
static inline const struct tp_ec_profile *tp_profile_named(const char *name)
{
 unsigned int i;
 if (!name || !*name)
  return NULL;
 for (i = 0; i < sizeof(tp_profiles) / sizeof(tp_profiles[0]); i++)
  if (!strcmp(name, tp_profiles[i].name))
   return &tp_profiles[i];
 return NULL;
}
/* Bootstrap only the known EC-ID transport, before reading F0..F7.
 * Revisions for a model must share this identity transport; otherwise extend it.
 */
static inline const struct tp_ec_profile *tp_identity_profile(const char *name,
 const char *version, const char *requested)
{
 unsigned int i;
 if (requested && *requested)
  return tp_profile_named(requested);
 for (i = 0; i < sizeof(tp_matches) / sizeof(tp_matches[0]); i++)
  if (tp_model_matches(tp_matches[i].model, name, version))
   return tp_profile_named(tp_matches[i].profile);
 return NULL;
}
static inline const struct tp_ec_profile *tp_select_profile(const char *name,
 const char *version, const char *firmware, unsigned int feature,
 const char *requested, bool allow_unsupported, bool *verified)
{
 const struct tp_ec_profile *forced = tp_profile_named(requested);
 unsigned int i;
 *verified = false;
 if (requested && *requested && !forced)
  return NULL;
 for (i = 0; i < sizeof(tp_matches) / sizeof(tp_matches[0]); i++) {
  const struct tp_ec_match *match = &tp_matches[i];
  const struct tp_ec_profile *candidate = tp_profile_named(match->profile);
  if (tp_model_matches(match->model, name, version) &&
      !strcmp(match->firmware, firmware) && (match->features & feature) == feature &&
      (!forced || forced == candidate)) {
   *verified = true;
   return candidate;
  }
 }
 return allow_unsupported ? forced : NULL;
}
#endif
