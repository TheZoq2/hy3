{
  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
  };

  outputs = { self, hyprland, ... }: let
    inherit (hyprland.inputs) nixpkgs;
    hyprlandSystems = fn: nixpkgs.lib.genAttrs (builtins.attrNames hyprland.packages) (system: fn system nixpkgs.legacyPackages.${system});
  in {
    packages = hyprlandSystems (system: pkgs: rec {
      hy3 = pkgs.gcc12Stdenv.mkDerivation {
        pname = "hy3";
        version = "0.1";
        src = ./.;

        nativeBuildInputs = with pkgs; [ cmake pkg-config ];

        buildInputs = [
          hyprland.packages.${system}.hyprland.dev
        ] ++ hyprland.packages.${system}.hyprland.buildInputs;

        # no noticeable impact on performance and greatly assists debugging
        cmakeBuildType = "Debug";
        dontStrip = true;

        meta = with pkgs.lib; {
          homepage = "https://github.com/outfoxxed/hy3";
          description = "Hyprland plugin for an i3 / sway like manual tiling layout";
          license = licenses.gpl3;
          platforms = platforms.linux;
        };
      };

      default = hy3;
    });

    devShells = hyprlandSystems (system: pkgs: {
      default = pkgs.mkShell.override { stdenv = pkgs.gcc12Stdenv; } {
        name = "hy3";

        nativeBuildInputs = with pkgs; [
          clang-tools_15
          bear
        ];

        inputsFrom = [ self.packages.${system}.hy3 ];
      };
    });
  };
}
