# nix flake check: the module installs a setuid core that runs, and the package's files are where the core expects them.
{ pkgs, self }:
pkgs.testers.runNixOSTest {
  name = "f-dpi-module";
  nodes.machine = {
    imports = [ self.nixosModules.default ];
    programs.f-dpi.enable = true;
  };
  testScript = ''
    machine.wait_for_unit("multi-user.target")
    machine.succeed("test -u /run/wrappers/bin/ThroneCore")
    machine.succeed("/run/wrappers/bin/ThroneCore --version | grep sing-box")
    machine.succeed("test -x ${self.packages.x86_64-linux.default}/opt/F-Dpi/zapret/nfqws")
    machine.succeed("test -f ${self.packages.x86_64-linux.default}/opt/F-Dpi/zapret/files/quic_initial_www_google_com.bin")
  '';
}
