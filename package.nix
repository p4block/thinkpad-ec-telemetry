{ lib, kernel }:
kernel.stdenv.mkDerivation {
  pname = "thinkpad-ec";
  version = "0.2.0";
  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [ ./Makefile ./thinkpad_ec_hwmon.c ./thinkpad_ec_accel.c ./cell-voltage.h ./profiles.h ./ec-profile.h ];
  };
  nativeBuildInputs = kernel.moduleBuildDependencies;
  hardeningDisable = [ "pic" ];
  dontPatchELF = true;
  makeFlags = [ "-C" "${kernel.dev}/lib/modules/${kernel.modDirVersion}/build" "M=$(PWD)" ];
  buildFlags = [ "modules" ];
  installPhase = ''
    install -Dm644 thinkpad_ec_hwmon.ko $out/lib/modules/${kernel.modDirVersion}/extra/thinkpad_ec_hwmon.ko
    install -Dm644 thinkpad_ec_accel.ko $out/lib/modules/${kernel.modDirVersion}/extra/thinkpad_ec_accel.ko
  '';
  meta.license = lib.licenses.wtfpl;
}
