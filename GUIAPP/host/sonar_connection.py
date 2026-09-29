"""Find the board and use the selected wired interface without changing its IP."""
from dataclasses import dataclass
import ipaddress
import json
import os
import secrets
import socket
import struct
import subprocess
import time

DISCOVERY_PORT = 5002
FALLBACK_BOARD = "169.254.59.46"


@dataclass(frozen=True)
class Interface:
    address: str
    prefix: int
    index: int = 0

    @property
    def network(self):
        return ipaddress.IPv4Interface(f"{self.address}/{self.prefix}").network


def choose_interface(rows):
    """Prefer a usable link-local address for this direct-cable setup."""
    if isinstance(rows, dict):
        rows = [rows]
    candidates = []
    for row in rows or []:
        ip = ipaddress.IPv4Address(row["IPAddress"])
        if ip.is_loopback or ip.is_multicast or ip.is_unspecified:
            continue
        candidates.append(Interface(str(ip), int(row["PrefixLength"]), int(row["InterfaceIndex"])))
    if not candidates:
        raise OSError("Selected Ethernet adapter has no ready IPv4 address. Check the cable and link.")
    candidates.sort(key=lambda item: (not ipaddress.IPv4Address(item.address).is_link_local, item.address))
    return candidates[0]


def local_interface(name="Ethernet", local_ip=None):
    if local_ip:
        value = ipaddress.IPv4Interface(local_ip)
        return Interface(str(value.ip), value.network.prefixlen)
    if os.name != "nt":
        raise OSError("Specify --local-ip ADDRESS/PREFIX for the wired interface on this operating system.")
    # Only read the named adapter. Quoting prevents adapter names becoming code.
    quoted = name.replace("'", "''")
    command = ("$ErrorActionPreference='Stop'; "
               "[Console]::OutputEncoding=[Text.Encoding]::UTF8; "
               f"Get-NetIPAddress -InterfaceAlias '{quoted}' -AddressFamily IPv4 -AddressState Preferred | "
               "Select-Object IPAddress,PrefixLength,InterfaceIndex | ConvertTo-Json -Compress")
    try:
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", command],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired as error:
        raise OSError("Timed out reading the Ethernet adapter; retrying will refresh its address.") from error
    if result.returncode:
        raise OSError(f"Cannot read adapter {name!r}: {result.stderr.strip()}. "
                      "Use --interface with its Windows name, or --local-ip ADDRESS/PREFIX.")
    return choose_interface(json.loads(result.stdout.lstrip("\ufeff").strip() or "[]"))


def bind_interface(sock, interface):
    sock.bind((interface.address, 0))
    if os.name == "nt" and interface.index:
        # Winsock IP_UNICAST_IF takes a 4-byte interface index in network order.
        # This selects Ethernet for this socket; it does not edit system routes.
        sock.setsockopt(socket.IPPROTO_IP, getattr(socket, "IP_UNICAST_IF", 31),
                        struct.pack("!I", interface.index))


def discover(interface, timeout=2.0):
    nonce = secrets.token_bytes(8)
    request, expected = b"SONARWHO" + nonce, b"SONARIP2" + nonce
    found = set()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        bind_interface(sock, interface)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.sendto(request, (str(interface.network.broadcast_address), DISCOVERY_PORT))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            sock.settimeout(max(0.001, deadline - time.monotonic()))
            try:
                data, peer = sock.recvfrom(64)
            except (socket.timeout, ConnectionResetError):
                # Windows can report ICMP port-unreachable for older firmware
                # without discovery; the legacy TCP fallback still applies.
                break
            if (data == expected and peer[1] == DISCOVERY_PORT and
                    ipaddress.IPv4Address(peer[0]) in interface.network and peer[0] != interface.address):
                found.add(peer[0])
    if len(found) > 1:
        raise ValueError("Multiple sonar boards replied; select one with --board IP.")
    return next(iter(found), None)


def connect_board(args, log=print):
    """Refresh the laptop address on every reconnect, including DHCP/APIPA changes."""
    interface = local_interface(args.interface, args.local_ip)
    board = args.board
    if board is None:
        board = discover(interface)
        if board is None:
            # Older firmware does not implement discovery. Keep it diagnosable.
            board = FALLBACK_BOARD
            log("No discovery reply; trying the legacy board address. Verify the new firmware banner.")
    if ipaddress.IPv4Address(board) not in interface.network or board == interface.address:
        raise OSError(f"Board {board} is not a distinct address on Ethernet {interface.address}/{interface.prefix}. "
                      "This direct-cable firmware uses 169.254.0.0/16; unrelated subnets need matching "
                      "board configuration. --board selects an address, it does not change the board's IP.")
    log(f"Connecting to {board}:{args.port} through {args.interface} ({interface.address})...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        bind_interface(sock, interface)
        sock.settimeout(10)
        sock.connect((board, args.port))
        return sock, board, interface.address
    except BaseException:
        sock.close()
        raise


def permission_help(error):
    if getattr(error, "winerror", None) == 10013 or isinstance(error, PermissionError):
        return ("Windows blocked this socket (10013 / permission denied). Check the VPN's local-network "
                "access or kill-switch policy and the Python outbound firewall rule. Changing the board IP "
                "cannot remove a Windows permission block. No network/security settings were changed.")
    return None
