{
  pkgs,
  lib,
  config,
  inputs,
  ...
}:

{
  packages = [
    pkgs.git
    pkgs.lazygit
    pkgs.just
    inputs.nixpkgs-esp-dev.packages.${pkgs.stdenv.hostPlatform.system}.esp-idf-full
  ];

  languages.python.enable = true;

  dotenv.enable = true;
}
