{
  description = "F-Dpi: get around censorship and DPI blocking (Throne based sing-box proxy + zapret nfqws DPI bypass)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system} = rec {
        f-dpi = pkgs.callPackage ./nix/package.nix { src = self; };
        default = f-dpi;
      };

      # programs.f-dpi.enable = true;  (setuid core through security.wrappers)
      nixosModules.default = import ./nix/module.nix self;

      checks.${system}.module = import ./nix/test.nix { inherit pkgs self; };
    };
}
