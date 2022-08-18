# magnifier

## Building Magnifier using Presets and cxx-common

Step 1: get cxx-common.

```sh
scripts/fetch-cxx-common.sh
# This script will set VCPKG_DIR for you
```

Step 2: Set `INSTALL_DIR` and Build

```sh
export INSTALL_DIR=~/magnifier
# you also need to set VCPKG_DIR if not using the fetch script
scripts/build-preset.sh
```

### Example Build Output

```sh
artem@beefy:~/git/magnifier$ export INSTALL_DIR=~/magnifier
artem@beefy:~/git/magnifier$ export VCPKG_ROOT=/opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64/
artem@beefy:~/git/magnifier$ scripts/build-preset.sh debug
Building against VCPKG: [/opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64/]
Installing to: [/home/artem/magnifier]
Checking for clang/clang++ in [/opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64/] [x64-linux-rel]:
Found a clang [/opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64//installed/x64-linux-rel/tools/llvm/clang]:
clang version 13.0.1 (https://github.com/microsoft/vcpkg.git 7e7dad5fe20cdc085731343e0e197a7ae655555b)
Target: x86_64-unknown-linux-gnu
Thread model: posix
InstalledDir: /opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64//installed/x64-linux-rel/tools/llvm
Found a clang [/opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64//installed/x64-linux-rel/tools/llvm/clang++]:
clang version 13.0.1 (https://github.com/microsoft/vcpkg.git 7e7dad5fe20cdc085731343e0e197a7ae655555b)
Target: x86_64-unknown-linux-gnu
Thread model: posix
InstalledDir: /opt/trailofbits/irene/downloads/vcpkg_ubuntu-20.04_llvm-13_amd64//installed/x64-linux-rel/tools/llvm

Configuring [dbg] [x64] against vcpkg [x64-linux-rel]...
Configure success!
Building [dbg] [x64]...
Build success!
Installing [dbg] [x64]...
Install success!
```

## Running Magnifier UI

See the instructions [here](bin/magnifier-ui#magnifierui).
