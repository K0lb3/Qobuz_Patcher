{
  pkgs ? import <nixpkgs> { },
}:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # C/C++ Build tools for Miniaudio / node-gyp
    stdenv.cc
    gnumake
    cmake

    # Node.js environment (matches your Node 24 requirement/context)
    nodejs

    # Audio backends for compile time linking
    libpulseaudio    # For PulseAudio / PipeWire pulse compatibility
    alsa-lib         # For ALSA (asoundlib.h / libasound)
    sndio            # For Sndio
    jack2            # For JACK (jack/jack.h / libjack)

    # Common utilities if your patching/extraction scripts need them (curl, unzip, jq, etc.)
    p7zip
  ];

  shellHook = ''
    echo "🎵 Qobuz Patcher Development Environment Loaded!"
    echo "-> C compiler: $(cc --version | head -n 1)"
    echo "-> Node version: $(node -v)"
    echo "-> NPM version: $(npm -v)"
  '';
}
