Hybrid Networking for WHP Guest (Design)

Goals
- Stealth mode: guest shares host IP; no new MAC/DHCP, outbound TCP/UDP via host sockets (slirp‑like). Works on enterprise Wi‑Fi with client isolation.
- Attack mode: guest has its own L2 presence; ARP, raw packet crafting, sniffing.

CLI
- `colinux-daemon.exe --kernel <bzImage> [--initrd <initrd>] [--cmdline "..."] --net stealth|passthrough`

Architecture
- Frontend (guest): virtio‑net (preferred) or a minimal para‑net device backed by I/O exits (WHP emulator callbacks).
- Backend (host):
  - Stealth: user‑mode slirp or socket proxy. DNS + TCP/UDP mapping to host WinSock. No L2; NAT semantics.
  - Passthrough: attach a TAP/USB NIC; forward frames at L2. Options:
    - USB/Ethernet dongle bound exclusively to the daemon (WinUSB/libusb) → virtio‑net bridge.
    - Wintun/TAP vNIC bridged to a physical NIC (limited on Wi‑Fi), or ICS/NAT + ARP proxy (trade‑offs).

WHP considerations
- Use `WHvEmulator*` interfaces to intercept I/O ports/MMIO for NIC device model.
- Map guest memory with `WHvMapGpaRange`; advanced DMA uses bounce buffers.

MVP Phases
1) Stealth MVP: TCP connect/send/recv proxy over a simple para‑net queue (no raw UDP/ICMP initially). Good enough for curl, apt, SSH via host.
2) Virtio‑net device model + slirp: general outbound TCP/UDP, basic inbound port forward.
3) Passthrough MVP: TAP/Wintun device and basic L2 bridging to a USB NIC.
4) Hardening: timeouts, rate limits, DNS cache, observability.

Enterprise notes
- Stealth works where inbound is blocked; outbound on 443 usually allowed.
- Passthrough with Wi‑Fi often cannot be bridged at L2; prefer a USB/Ethernet NIC for clean ARP presence.

