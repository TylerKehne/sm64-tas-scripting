# sm64-tas-scripting
This project provides a framework for automating the SM64 TAS workflow. Very much a WIP at this time.

# Building instructions
This project is built using CMake. 

**Windows**
- Install vcpkg
- `vcpkg install nlohmann-json`

```powershell
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE="<path to vcpkg>\scripts\buildsystems\vcpkg.cmake" ..
cmake --build .
```

**MacOS and Linux**
- Install `nlohmann-json` using your favourite package manager.
```bash
mkdir build
cd build
cmake ..
cmake --build .
```
If you're using Visual Studio, open the root directory as a local folder. This enables VS's CMake integration. More detailed instructions can be found [here](https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170#building-cmake-projects)

# Configuration system
_Written by [@jgcodes2020](https://github.com/jgcodes2020)_

The configuration system uses a JSON file, and enables easy configuration of mostly non-changing parameters. When built with a build type other than `Debug`, the built executable always takes a config file as its first argument. When built for `Debug`, it will default to reading a file named `config.json` placed in the same directory as itself.

The current schema is as follows:
```json
{
	"libsm64": "Full path to libsm64",
	"m64_file": "Full path to .m64 file being used"
}
```