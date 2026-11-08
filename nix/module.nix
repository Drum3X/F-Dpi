# NixOS: programs.f-dpi.enable = true;
# The core is setuid root through a security wrapper (/run/wrappers/bin/ThroneCore, which the package finds on PATH).
# The wrapper execs the store copy, so the core's own path stays next to F-Dpi and zapret/ (parent and nfqws checks).
self:
{ config, lib, pkgs, ... }:
let
  cfg = config.programs.f-dpi;
in
{
  options.programs.f-dpi = {
    enable = lib.mkEnableOption "F-Dpi (sing-box proxy and zapret DPI bypass)";
    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      description = "The F-Dpi package.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];
    security.wrappers.ThroneCore = {
      source = "${cfg.package}/opt/F-Dpi/ThroneCore";
      owner = "root";
      group = "root";
      setuid = true;
    };
    # nfqws reads an NFQUEUE that an nftables rule fills; both load on demand, listed so a locked-down kernel has them.
    boot.kernelModules = [ "nfnetlink_queue" "nft_queue" ];
  };
}
