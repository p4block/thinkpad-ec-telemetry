{ config, lib, pkgs, ... }:
let cfg = config.hardware.thinkpadEc;
in {
  options.hardware.thinkpadEc = {
    enable = lib.mkEnableOption "firmware-checked ThinkPad EC drivers";
    telemetry = lib.mkOption { type = lib.types.bool; default = true; description = "Expose supported EC hwmon sensors."; };
    accelerometer = lib.mkEnableOption "on-demand EC accelerometer input";
    probeOnly = lib.mkEnableOption "accelerometer status probe without registering input";
    cellVoltages = lib.mkEnableOption "experimental X230 G2HT35WW battery group reads";
  };
  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [ (pkgs.callPackage ./package.nix { kernel = config.boot.kernelPackages.kernel; }) ];
    boot.kernelModules = lib.optional cfg.telemetry "thinkpad_ec_hwmon" ++ lib.optional cfg.accelerometer "thinkpad_ec_accel";
    boot.extraModprobeConfig = ''
      options thinkpad_ec_hwmon cell_voltages=${if cfg.cellVoltages then "1" else "0"}
      options thinkpad_ec_accel probe_only=${if cfg.probeOnly then "1" else "0"}
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
