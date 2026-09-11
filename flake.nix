{
  description = "Additional ThinkPad embedded-controller telemetry";
  inputs.nixpkgs.url = "https://releases.nixos.org/nixpkgs/nixpkgs-26.11pre1066765.9b9402b959a2/nixexprs.tar.xz";
  outputs = { self, nixpkgs }:
    let pkgs = import nixpkgs { system = "x86_64-linux"; };
    in {
      packages.x86_64-linux.default = pkgs.callPackage ./reader.nix {
        kernel = pkgs.linuxPackages_latest.kernel;
      };
      packages.x86_64-linux.thinkpad-rotate = pkgs.callPackage ./tools/thinkpad-rotate {};
      nixosModules.default = import ./nixos-module.nix;
      checks.x86_64-linux.build = self.packages.x86_64-linux.default;
      checks.x86_64-linux.rotation = self.packages.x86_64-linux.thinkpad-rotate;
    };
}
