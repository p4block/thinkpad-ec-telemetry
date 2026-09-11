{ config, lib, pkgs, ... }:
let
 cfg = config.hardware.thinkpad-ec-telemetry.accelerometer;
 kernel = config.boot.kernelPackages.kernel;
 module = kernel.stdenv.mkDerivation {
  pname = "thinkpad-ec-accel";
  version = "0.1";
  src = lib.fileset.toSource { root = ./.; fileset = ./x230_ec_accel.c; };
  nativeBuildInputs = kernel.nativeBuildInputs;
  hardeningDisable = [ "all" ];
  dontConfigure = true;
  buildPhase = ''
   echo 'obj-m += x230_ec_accel.o' > Makefile
   make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M=$PWD modules
  '';
  installPhase = ''
   mkdir -p $out/lib/modules/${kernel.modDirVersion}/extra
   cp x230_ec_accel.ko $out/lib/modules/${kernel.modDirVersion}/extra/
  '';
 };
 rotate = pkgs.callPackage ./tools/thinkpad-rotate {};
in {
 options.hardware.thinkpad-ec-telemetry.accelerometer = {
  enable = lib.mkEnableOption "tested G2HT35WW X230 raw two-axis accelerometer";
  swayRotation = lib.mkEnableOption "opt-in Sway rotation user service (start manually)";
 };
 config = lib.mkIf cfg.enable {
  boot.extraModulePackages = [ module ];
  boot.kernelModules = [ "x230_ec_accel" ];
  services.udev.packages = [ (pkgs.runCommand "thinkpad-ec-accel-rules" {} ''
   mkdir -p $out/lib/udev/rules.d
   cp ${./udev}/*.rules $out/lib/udev/rules.d/
  '') ];
  environment.systemPackages = lib.mkIf cfg.swayRotation [ rotate ];
  systemd.user.services.thinkpad-rotate = lib.mkIf cfg.swayRotation {
   description = "ThinkPad base tilt to Sway display rotation";
   # No WantedBy: sensor polling is explicitly opt-in each session.
   partOf = [ "graphical-session.target" ];
   serviceConfig = {
    ExecStart = "${rotate}/bin/thinkpad-rotate";
    Restart = "no";
    TimeoutStopSec = 15;
    NoNewPrivileges = true;
    ProtectSystem = "strict";
    ProtectHome = "read-only";
    ReadWritePaths = [ "%t" ];
    RestrictAddressFamilies = [ "AF_UNIX" ];
   };
  };
 };
}
