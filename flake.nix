{
  description = "Firmware-checked ThinkPad EC telemetry and accelerometer drivers";
  inputs.nixpkgs.url = "https://channels.nixos.org/nixos-unstable/nixexprs.tar.zst";
  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      driver = pkgs.callPackage ./package.nix { kernel = pkgs.linuxPackages_latest.kernel; };
      machine = nixpkgs.lib.nixosSystem {
        inherit system;
        modules = [ self.nixosModules.default { system.stateVersion = "26.05"; hardware.thinkpadEc = { enable = true; accelerometer = true; }; } ];
      };
    in {
      packages.${system}.default = driver;
      nixosModules.default = import ./module.nix;
      checks.${system} = {
        build = driver;
        tests = pkgs.runCommand "thinkpad-ec-tests" { nativeBuildInputs = [ pkgs.python3 pkgs.stdenv.cc ]; } ''
          cp -r ${./tests} tests
          cp ${./thinkpad_ec_hwmon.c} thinkpad_ec_hwmon.c
          cp ${./thinkpad_ec_accel.c} thinkpad_ec_accel.c
          python3 -m unittest discover -s tests
          touch $out
        '';
        module = assert builtins.elem "thinkpad_ec_accel" machine.config.boot.kernelModules;
          pkgs.writeText "thinkpad-ec-options" machine.config.boot.extraModprobeConfig;
      };
    };
}
