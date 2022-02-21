# sm64-tas-scripting
This project provides a framework for automating the SM64 TAS workflow. Very much a WIP at this time.

# Configuration system
_Written by [@jgcodes2020](https://github.com/jgcodes2020)_

The configuration system uses a JSON file, and enables easy configuration of mostly non-changing parameters. When built with a build type other than `Debug`, the built executable always takes a config file as its first argument. When built for `Debug`, it will default to reading `<project root>/local/config.json` if the first argument isn't provided.

The current schema is as follows:
```json
{
	"libsm64": "Full path to libsm64",
	"m64_file": "Full path to .m64 file being used"
}
```

The `local` folder is intended to be used for files that do not belong on GitHub, such as the M64 being tested, or your copy of libsm64 (we cannot distribute libsm64 directly for legal reasons).