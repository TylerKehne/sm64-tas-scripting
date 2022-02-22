# sm64-tas-scripting
This project provides a framework for automating the SM64 TAS workflow. Very much a WIP at this time.

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