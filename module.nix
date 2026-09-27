{ config, lib, pkgs, ... }:
let cfg = config.hardware.thinkpadEc;
in {
  options.hardware.thinkpadEc = {
    enable = lib.mkEnableOption "firmware-checked ThinkPad EC drivers";
    telemetry = lib.mkOption { type = lib.types.bool; default = true; description = "Expose supported EC hwmon sensors."; };
    accelerometer = lib.mkEnableOption "on-demand EC accelerometer input";
    probeOnly = lib.mkEnableOption "accelerometer status probe without registering input";
    profile = lib.mkOption {
      type = lib.types.nullOr (lib.types.strMatching "[a-z0-9_-]+");
      default = null;
      description = "Named EC protocol/map profile. Leave null for verified automatic matching.";
    };
    allowUnsupported = lib.mkEnableOption "unverified testing with an explicitly named profile";
    cellVoltages = lib.mkEnableOption "experimental X230 G2HT35WW battery group reads";
  };
  config = lib.mkIf cfg.enable {
    assertions = [
      { assertion = !cfg.allowUnsupported || cfg.profile != null;
        message = "thinkpadEc.allowUnsupported requires a named thinkpadEc.profile."; }
      { assertion = !cfg.allowUnsupported || !cfg.cellVoltages;
        message = "Experimental profile testing cannot enable cell-voltage debug access."; }
    ];
    boot.extraModulePackages = [ (pkgs.callPackage ./package.nix { kernel = config.boot.kernelPackages.kernel; }) ];
    boot.kernelModules = lib.optional cfg.telemetry "thinkpad_ec_hwmon" ++ lib.optional cfg.accelerometer "thinkpad_ec_accel";
    boot.extraModprobeConfig = ''
      options thinkpad_ec_hwmon cell_voltages=${if cfg.cellVoltages then "1" else "0"} allow_unsupported=${if cfg.allowUnsupported then "1" else "0"}${lib.optionalString (cfg.profile != null) " profile=${cfg.profile}"}
      options thinkpad_ec_accel probe_only=${if cfg.probeOnly || cfg.allowUnsupported then "1" else "0"} allow_unsupported=${if cfg.allowUnsupported then "1" else "0"}${lib.optionalString (cfg.profile != null) " profile=${cfg.profile}"}
    '';
    services.udev.packages = lib.mkIf cfg.accelerometer [ (pkgs.writeTextFile {
      name = "thinkpad-ec-input";
      destination = "/lib/udev/rules.d/70-thinkpad-ec-accel.rules";
      text = ''
        SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="ThinkPad EC accelerometer", SYMLINK+="input/thinkpad-ec-accel", TAG+="uaccess", ENV{LIBINPUT_IGNORE_DEVICE}="1"
      '';
    }) ];
  };
}
