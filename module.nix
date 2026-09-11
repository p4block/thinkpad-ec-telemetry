# Compatibility import for the original investigation laptop only.
# New configurations should import nixos-module.nix and set explicit options.
{ config, pkgs, ... }:
{
 boot.extraModulePackages = [ (pkgs.callPackage ./reader.nix {
   kernel = config.boot.kernelPackages.kernel;
 }) ];
 boot.extraModprobeConfig = "options x230_ec_hwmon cell_voltages=1";
 boot.kernelModules = [ "x230_ec_hwmon" ];
}
