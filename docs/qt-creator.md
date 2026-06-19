# Qt Creator Setup

## Qt Creator (for development)

Qt Creator provides a great development experience for Qt. To ensure proper integration and setup of environment variables, qtcreator must be launched from a Nix development shell: 
```bash
# enter nix development shell
nix develop

# launch qt creator
# on macos, this is typically located at "/Applications/Qt\ Creator.app/Contents/MacOS/Qt\ Creator"
path/to/qtcreator_exec

### Installation

#### Install from the repository

If your package manager provides `qtcreator`, this is the easiest way to start. You will need to install some dependencies with it.  
Note that you should install and run it from a Toolbox, otherwise you may face `glx` errors:

```bash
sudo dnf install cmake ninja clangd qtcreator gcc
```

If you cannot run it from inside a toolbox, try to install `chromium` in order to have proper dependencies installed.

### Configuration

To import the project into Qt Creator, click on `File -> Open File or Project` and select the `CMakeLists.txt` file. A configuration popup will appear. Make sure you have a **Debug** build configuration pointing to the `build` directory and then click on `Configure project`.

Enable CMake debug logging, add `--log-level=DEBUG` in `Projects` -> `Imported Kits` -> `Build` -> `Additional CMake options`.

Ensure that `clangd` is enabled for your project. Go to `Projects` on the left, then click on `Manage Kits` at the top. Select the `C++` tab and open the last tab, `Clangd`. Check `Use clangd` and, if needed, configure it to use the `clangd` installed on your system.

That’s it. The configuration defined in `CMakeLists.txt` should allow the project to build correctly.

If you encounter any configuration issues, close Qt Creator, remove the `CMakeLists.txt.user` file, and restart Qt Creator to reconfigure the project.
