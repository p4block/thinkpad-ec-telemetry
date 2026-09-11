{kernel, lib}:
kernel.stdenv.mkDerivation {
 pname = "x230-ec-hwmon";
 version = "0.1";
 src = lib.cleanSource ./.;
 nativeBuildInputs = kernel.nativeBuildInputs;
 hardeningDisable = [ "all" ];
 dontConfigure = true;
 buildPhase = ''
   make -C ${kernel.dev}/lib/modules/${kernel.modDirVersion}/build M=$PWD modules
 '';
 installPhase = ''
   mkdir -p $out/lib/modules/${kernel.modDirVersion}/extra
   cp x230_ec_hwmon.ko $out/lib/modules/${kernel.modDirVersion}/extra/
 '';
 meta.license = lib.licenses.wtfpl;
}
