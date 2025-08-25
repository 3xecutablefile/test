# BlackhatOS

BlackhatOS is an experiment in cooperative virtualization.
It aims to resurrect the spirit of coLinux by running a Linux kernel side-by-side with a host OS without a full hypervisor.

## Current Components
- **Cooperative syscall** `sys_coop_yield` so the guest voluntarily yields CPU time
- **Ring buffer & misc device** `/dev/coop_console` for host–guest messaging
- **User tools and launchers** including `coop_console_test` for exercising the console device
- **Driver stubs** for macOS and Windows to bridge console I/O

## Repository Layout
```
CMakeLists.txt   - Top-level build script
config/          - Default runtime configuration
kernel/          - Linux kernel module and cooperative primitives
user/            - User-space utilities and scripts
mac/ , win/      - Host driver stubs for macOS and Windows
```

## Building
```bash
cmake -S . -B build
cmake --build build
```

## Console Demo
1. Load the kernel module:
   ```bash
   sudo insmod kernel/cooperative.ko
   ```
2. Run the userland test:
   ```bash
   ./build/user/coop_console_test
   ```
   The program spawns writer/reader threads and forwards stdin through `/dev/coop_console`.

## Status
BlackhatOS is a prototype. Block and network devices are placeholders and shared-memory mapping is still stubbed out. Contributions are welcome.

