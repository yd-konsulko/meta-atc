# meta-atc

A Yocto layer that builds an atc-managed device image. The image carries atcd
and its boot-confirm guard. With the `rauc` distro feature, the layer also
builds an A/B image and a signed RAUC bundle that updates it.

## Requirements

- poky `scarthgap`
- `meta-rauc`. `LAYERDEPENDS` names it, so a build without it fails at parse
  time
- `meta-tegra`, only for an NVIDIA Jetson machine

`conf/layers.pin` names the tested commit of each layer.
`scripts/checkout-layers.sh` checks them out. Add `--for tegra234` to include
meta-tegra.

## Add the layer

```sh
bitbake-layers add-layer /path/to/meta-atc
```

The layer needs systemd and its own configuration. Set this in `local.conf`, or
in a distro conf. It changes the init system of every image in the build
directory:

```
INIT_MANAGER = "systemd"
require conf/distro/include/atc.inc
```

`atc.inc` sets the `atc` distro feature, which turns on the layer's changes to
recipes of other layers (GRUB, the kernel, RAUC, the meta-tegra BSP), and the
Jetson flash layout. Without it, adding the layer changes no other recipe, and
the atc images are skipped.

## Set the machine

Set `MACHINE` in `local.conf`:

```
MACHINE = "qemux86-64"
```

| Machine | Slot backend |
|---|---|
| `qemux86-64` | GRUB |
| `qemuarm64` | none: `atc-image` only |
| a meta-tegra `tegra234` machine, for example `jetson-orin-nano-devkit-nvme` | NVIDIA boot control (`nvbootctrl`) |

The layer selects the slot backend from the machine. You do not set it.

To set `MACHINE` from the environment instead, add it to the passthrough list.
Otherwise bitbake drops it and builds `local.conf`'s machine without a warning:

```sh
export BB_ENV_PASSTHROUGH_ADDITIONS="${BB_ENV_PASSTHROUGH_ADDITIONS:-} MACHINE"
MACHINE=qemux86-64 bitbake atc-image
```

## atcd

The layer does not compile atcd. `recipes-core/atc/atcd.inc` is the recipe.
Each release is a file `atcd_<version>.bb` that requires it and pins the
sha256 of the binary of each target. `do_fetch` downloads that binary from
`https://atc.dev/agency/blobs/sha256/` and checks its sha256, like any other
source. The download is cached in `DL_DIR`, so a build after the first one
needs no network. With several releases, bitbake builds the highest one; set
`PREFERRED_VERSION_atcd` to take another.

### atcd version

To add the current release of a track (default `stable`):

```sh
scripts/atcd-add-release.sh [track]
```

It fetches the binary of each target with `scripts/install-atcd.sh`, which
checks the release signature and the sha256, and writes
`recipes-core/atc/atcd_<version>.bb`:

```
require atcd.inc

ATCD_SHA256:aarch64 = "<digest of linux-aarch64-musl>"
ATCD_SHA256:x86-64 = "<digest of linux-x86_64-musl>"
```

Remove an old release file when nothing needs it anymore.

## Build

| Target | Output |
|---|---|
| `atc-image` | a bootable image: systemd, sshd, sudo, a deploy user, atcd |
| `atc-image-ab` | the same system on an A/B slot layout, for the factory flash |
| `atc-bundle` | a signed RAUC bundle that updates one slot of `atc-image-ab` |

```sh
bitbake atc-image
```

An A/B build needs the `rauc` distro feature, an OS version, and signing keys.
The keys are not in this layer. For a test build,
`scripts/rauc-test-keys.sh <dir>` makes a test CA and a signing pair in
`<dir>`. A product signs with its own keys. Supply them:

```
DISTRO_FEATURES:append = " rauc"
ATC_OS_VERSION = "2026.11.09-1"
RAUC_KEYRING_FILE = "/abs/path/ca.cert.pem"
RAUC_KEY_FILE = "/abs/path/signing.key.pem"
RAUC_CERT_FILE = "/abs/path/signing.cert.pem"
```

```sh
bitbake atc-image-ab atc-bundle
```

Write `atc-image-ab` to a device once. Update it with `atc-bundle` after that. A
whole-disk image written to a running device erases its store and its identity.

## Settings

| Variable | Default | Effect |
|---|---|---|
| `ATC_DEPLOY_USER` | `atc-deploy` | the account that atc logs in as |
| `ATC_DEPLOY_SSH_KEY` | empty | an SSH public key to install for that account |
| `ATC_OS_VERSION` | empty | the version that the bundle and `/etc/atc-os-version` carry |
| `ATC_SLOT_KIB` | `1048576` | the size limit of one rootfs slot, in KiB |
| `ATC_TEGRA_REDUNDANT` | `1` | `0` selects meta-tegra's single-rootfs layout. The A/B targets are then skipped |

Set them in `local.conf`, or in `conf/auto.conf` when a script writes
`local.conf`.

## Repository layout

The repository root is the layer root. `scripts/` holds build drivers and
repository checks. `tests/` holds their tests. Neither is layer content.

## Test

```sh
tests/run.sh
```

This runs every check that needs no Yocto build. CI runs the same command.

## Contributing

Send patches as pull requests to
<https://github.com/Christian-Prather/meta-atc>. Run `tests/run.sh` first.

Maintainer: Christian Prather <christian@agencytool.company>

## License

MIT. See [LICENSE](LICENSE). The atcd binary that the layer downloads is
under the atc license, MIT or Apache-2.0.
