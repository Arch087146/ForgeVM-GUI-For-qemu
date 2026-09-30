# Forge Virtual Machines

A modern, cross-platform GUI front-end for QEMU virtual machines. Built with Qt6 and C++17.

(This project is new it may have tons of bugs and also tons of broken stuff like how the pre built vm dialog is still broken so if you happen to find one please add a issue)

## Features

### Virtual Machine Management
- **Create VMs** with a modern wizard-style interface
- **Pre-built VM images** - Download ready-to-run Linux VMs from official sources
- **Import existing VMs** from VirtualBox (.vbox/.vdi), UTM, and other formats
- **Convert disk images** between formats (VDI, VMDK, VHD, QCOW2, RAW, OVA)
- **Snapshots** - Create and manage VM snapshots
- **Clone VMs** - Duplicate VMs with a single click

### Operating System Support
- **Linux** - Ubuntu, Fedora, Debian, Arch, openSUSE, Rocky/AlmaLinux, Alpine, NixOS, Gentoo, and more
- **BSD** - FreeBSD, OpenBSD, NetBSD, DragonFly BSD
- **Windows** - Bring your own ISO (Windows 7-11, Server)
- **macOS** - Requires Apple hardware (see [Apple Virtualization](https://developer.apple.com/documentation/virtualization/))
- **Other** - Haiku, ReactOS, FreeDOS, BSD variants, and retro systems

### Advanced Features
- **KVM Acceleration** - Auto-detects and enables KVM when available
- **GPU Passthrough** - VirtIO-GPU, VirtIO-GPU-Native, QXL, Cirrus
- **Networking** - User-mode (NAT), Bridged, MacVTap, Socket
- **UEFI/BIOS** - OVMF UEFI and legacy BIOS boot
- **Audio** - PulseAudio, PipeWire, ALSA, JACK
- **USB Passthrough** - USB device redirection
- **Shared Folders** - VirtIO-FS, 9p, Samba

### Built-in Tools
- **Disk Converter** - Convert between VDI, VMDK, VHD, QCOW2, RAW, OVA
- **VirtualBox/UTM Import** - Import existing VMs with automatic disk conversion
- **ISO Downloader** - Download OS installers directly from official mirrors

## Requirements
- **QEMU 7.0+** with KVM support
- **CMake 3.16+**
- **C++17** compatible compiler
- **OpenSSL** development headers
- **QEMU** with KVM support (for acceleration)

## Building

```bash
# Clone
git clone https://github.com/Arch087146/ForgeVM-GUI-For-qemu.git
cd ForgeVM-GUI-For-qemu

# Build
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Run
./qemu-forgevm
```

### Dependencies (Ubuntu/Debian)
```bash
sudo apt install qemu-system-x86 qemu-utils ovmf \
    libqt6core6 libqt6gui6 libqt6widgets6 libqt6network6 libqt6svg6 \
    qt6-base-dev libssl-dev cmake g++
```

## Configuration

Settings are stored in `~/.config/forgevm/forgevm.conf`:
- Default VM paths
- Network defaults
- Display preferences

## Credits
- **Icons**: OS icons from the [UTM project](https://github.com/utmapp/utm) (Apache-2.0)
- **Pre-built VM images**: Curated from public OS mirrors (see each distro's license)

## License

MIT License - see [LICENSE](LICENSE) for details.

Not affiliated with or endorsed by QEMU, VirtualBox, VMware, UTM, or any OS vendors.
