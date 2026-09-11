{ config, lib, pkgs, ... }:
let cfg = config.hardware.thinkpad-ec-telemetry;
in {
 options.hardware.thinkpad-ec-telemetry = {
  enable = lib.mkEnableOption "experimental X230 EC sensors";
  experimental = lib.mkOption {
   type = lib.types.bool;
   default = false;
   description = "Explicitly bypass the ordinary sensor DMI guard for compatibility testing. Does not bypass the cell-voltage guard.";
  };
  cellVoltages = lib.mkOption {
   type = lib.types.bool;
   default = false;
   description = "Enable experimental fixed-address battery reads through the G2HT35WW debug interface, subject to strict machine and battery guards.";
  };
 };
 config = lib.mkIf cfg.enable {
  boot.extraModulePackages = [ (pkgs.callPackage ./reader.nix {
   kernel = config.boot.kernelPackages.kernel;
  }) ];
  boot.kernelModules = [ "x230_ec_hwmon" ];
  boot.extraModprobeConfig = ''
   options x230_ec_hwmon experimental=${if cfg.experimental then "1" else "0"} cell_voltages=${if cfg.cellVoltages then "1" else "0"}
  '';
 };
}
