# ToneLoader

> **Disclaimer:** This repository, including its code and documentation, was
> generated entirely by AI.

ToneLoader is a Linux LV2 audio plugin that runs a fixed mono guitar-processing
chain:

```text
input -> pedal NAM -> amp NAM -> cabinet IR -> output
```

It provides separate bypass, input, output or wet, and quality controls for the
three stages. Models are loaded off the audio thread, and the selected models
are saved in LV2 state. ToneLoader currently requires a 48 kHz session.

The bundle includes the CC0-licensed Crate Vintage Club 20 model as its default
amp. Other models and impulse responses are supplied by the user.

## Model library

ToneLoader initially looks in `~/Music/ToneLoader`. The library location can
be changed from the gear menu using the directory chooser and is saved in
`~/.config/ToneLoader/settings.toml`.

Organize the library by stage and pack:

```text
ToneLoader/
├── pedal/
│   ├── Pedal pack/*.nam
│   └── Another pack.zip
├── NAM/
│   ├── Amp pack/*.nam
│   └── Another pack.zip
└── IR/
    ├── Cabinet pack/*.wav
    └── Another pack.zip
```

Pack directories are scanned one level deep. ZIP packs must contain supported
model files at the archive root rather than in nested directories.

## Build and install

The supported build runs in Docker so build dependencies are not installed on
the host. A clean configure requires network access to fetch pinned source
dependencies and the checksum-verified factory model.

```sh
make build       # release bundle in build/lv2/ToneLoader.lv2
make test        # build and run tests
make validate    # inspect the bundle with lv2info
make install     # copy the bundle to ~/.lv2
```

Push any Git tag to build and validate the project, then publish the LV2 bundle
as a Linux x86-64 tarball on the corresponding GitHub Release.

At runtime the plugin uses the system's libsndfile, Cairo, and X11 shared
libraries. libzip is included in the LV2 bundle.

## Inspirations

ToneLoader is inspired by these projects:

- [neural-amp-modeler-lv2](https://github.com/mikeoliphant/neural-amp-modeler-lv2)
- [neural-amp-modeler-ui](https://github.com/brummer10/neural-amp-modeler-ui)
- [ImpulseLoader.lv2](https://github.com/brummer10/ImpulseLoader.lv2)

NAM processing is provided by NeuralAudio and NeuralAmpModelerCore. Cabinet IR
convolution uses FFTConvolver directly; ToneLoader does not include code from
ImpulseLoader.lv2. The UI uses libxputty. All fetched repositories are pinned
to commits in `CMakeLists.txt`.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for revisions, attribution,
and license terms.

## License

ToneLoader is available under the [MIT License](LICENSE). The factory model and
third-party components remain under their respective licenses.
