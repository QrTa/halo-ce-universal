#!/usr/bin/env python3
"""Install, start, follow and fetch the PS5 title (port/ps5/README.md) from the PC.

The console (port/ps5/PS5.md, "Decisions") runs an FTP server (ftpsrv, port
2121), a kernel log server (klogsrv, port 3232) and, once it is loaded after
each boot, PS5_Vulkan's resident ps5vkctl payload (port 9111), which starts
and closes titles: one command a connection, answered with one line. The
title is installed in /data/homebrew/<TITLE_ID>/, where ShadowMountPlus
lists it. The protocols are PS5_Vulkan's (its tools/ps5_console.py and
docs/DEPLOYMENT.md), written again here.

Usage: python3 tools/ps5_deploy.py [options] ACTION...   (in the order given)
       python3 tools/ps5_deploy.py --run [--hold] [--timeout S]

Actions:
  fetch          the title's log (halo.log in /data/halo, or in the title's
                 folder; both when both are there) and the *.txt files of the
                 title's folder, into build/ps5/logs/<UTC time>/
  upload         the built title folder (build/ps5/title/<ID>), eboot.bin and
                 sce_sys/param.json last, each file's size checked after it;
                 refused while the title runs, unless --force. --hold puts
                 hold.txt in the title's folder (the host then waits 120 s
                 before it ends, so it can be killed); without it a hold.txt
                 there is removed
  status, launch, kill, restart
                 one ps5vkctl command each; a launch of the title while it
                 runs is refused, and kill and restart name only this title
  follow         the kernel log: the backlog the server replays is skipped,
                 each line it doubles is shown once, and the host's lines
                 ("halo: ") and any fatal signal are printed until the host's
                 last line, a fatal signal, or --timeout (180 s); the whole
                 capture is saved
  load-ps5vkctl  PS5_Vulkan's build/ps5vkctl/ps5vkctl.elf sent to the user's
                 payload manager (itsPLK's ps5-payload-manager, port 8084):
                 an upload, then a load; then ping. Only when ps5vkctl does
                 not answer, and the manager is up

--run is fetch (the previous run's files first), kill if the title runs,
upload, launch with follow started before it, then fetch. Its exit status
is 0 only if the host's last line was seen.
"""

import argparse
import datetime
import ftplib
import http.client
import io
import socket
import sys
import time
import urllib.parse
from pathlib import Path
from typing import Callable, List, Optional

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_HOST = "192.168.2.157"
DEFAULT_TITLE = "PPSA99500"
DATA_FOLDER = "/data/halo"
DEFAULT_PS5VKCTL = Path.home() / "projects" / "ps5-deps" / "PS5_Vulkan" / "build" / "ps5vkctl" / "ps5vkctl.elf"
# the files that go last, in this order, so the title never shows half uploaded
LAST = ("eboot.bin", "sce_sys/param.json")
# the SELF files of a title (eboot.bin, fake-signed with ps5-native-tool's
# magic, and libc.prx, a PS5 SELF): the server decrypts them when they are
# read, so what it says of their size is not the signed file's
# (PS5_FINDINGS.md)
SELF_MAGICS = (bytes.fromhex("4f153d1d"), bytes.fromhex("5414f5ee"))
# klogsrv replays its buffer on connect: what arrives until it is quiet this
# long (and at most this long in all) is that backlog
BACKLOG_QUIET_SECONDS = 0.75
BACKLOG_LIMIT_SECONDS = 5.0
# the host's last lines (port/ps5/host/host_main.c): the end, or the hold
# before it
HOST_LAST_LINES = ("halo: the host ends", "halo: holding for ")
FATAL_SIGNAL = "# A user thread receives a fatal signal"


class DeployError(Exception):
    pass


def say(text: str) -> None:
    print(text, flush=True)


# ---------- ps5vkctl

def ps5vkctl(args: argparse.Namespace, command: str, timeout: float = 60.0) -> str:
    """one command, one connection, one line back"""
    try:
        with socket.create_connection((args.host, args.ctl_port), timeout=timeout) as connection:
            with connection.makefile("rwb", buffering=0) as stream:
                stream.write(f"{command}\n".encode("ascii"))
                reply = stream.readline()
    except OSError as error:
        raise DeployError(f"ps5vkctl at {args.host}:{args.ctl_port} did not answer ({error}); load it once the "
                          "payload manager is up: tools/ps5_deploy.py load-ps5vkctl") from error
    return reply.decode("ascii", "replace").strip()


def title_running(args: argparse.Namespace) -> bool:
    reply = ps5vkctl(args, "status")
    return reply.startswith("ok running") and f"title={args.title}" in reply.split()


def action_status(args: argparse.Namespace) -> int:
    say(f"ps5vkctl status: {ps5vkctl(args, 'status')}")
    return 0


def action_launch(args: argparse.Namespace) -> int:
    if title_running(args):
        raise DeployError(f"{args.title} is running already: kill it first (or --run, which does)")
    reply = ps5vkctl(args, f"launch {args.title}")
    say(f"ps5vkctl launch {args.title}: {reply}")
    return 0 if reply.startswith("ok launched") else 1


def action_kill(args: argparse.Namespace) -> int:
    reply = ps5vkctl(args, f"kill {args.title}")
    say(f"ps5vkctl kill {args.title}: {reply}")
    return 0 if reply.startswith("ok") else 1


def action_restart(args: argparse.Namespace) -> int:
    reply = ps5vkctl(args, f"restart {args.title}")
    say(f"ps5vkctl restart {args.title}: {reply}")
    return 0 if reply.startswith("ok") else 1


def action_load_ps5vkctl(args: argparse.Namespace) -> int:
    """the payload manager's two requests (ps5-payload-manager 0.5.2's
    http_server.c): POST /manage:upload?filename=<name> with the ELF as the
    body, which stores it in its payload folder, then GET /loadpayload:<name>,
    which hands it to the console's ELF loader (port 9021, local only)"""
    payload = Path(args.ps5vkctl)
    if not payload.is_file():
        raise DeployError(f"no {payload}: build it with PS5_PAYLOAD_SDK=<a payload SDK> bash "
                          "<PS5_Vulkan>/tools/build-ps5vkctl.sh")
    name = payload.name
    requests = [("POST", f"/manage:upload?filename={urllib.parse.quote(name)}", payload.read_bytes()),
                ("GET", f"/loadpayload:{urllib.parse.quote(name)}", None)]
    for method, path, body in requests:
        say(f"payload manager {args.host}:{args.manager_port}: {method} {path}"
            + (f" ({len(body)} bytes)" if body else ""))
        try:
            connection = http.client.HTTPConnection(args.host, args.manager_port, timeout=60)
            headers = {"Content-Type": "application/octet-stream"} if body else {}
            connection.request(method, path, body=body, headers=headers)
            response = connection.getresponse()
            text = response.read().decode("utf-8", "replace").strip()
            connection.close()
        except OSError as error:
            raise DeployError(f"the payload manager at {args.host}:{args.manager_port} did not answer "
                              f"({error})") from error
        say(f"  {response.status} {response.reason}: {text}")
        if response.status != 200:
            return 1
    for _ in range(20):
        time.sleep(0.5)
        try:
            say(f"ps5vkctl ping: {ps5vkctl(args, 'ping', timeout=5)}")
            return 0
        except DeployError:
            continue
    raise DeployError(f"ps5vkctl did not answer on {args.host}:{args.ctl_port} after the load")


# ---------- FTP

def ftp_open(args: argparse.Namespace) -> ftplib.FTP:
    try:
        ftp = ftplib.FTP()
        ftp.connect(args.host, args.ftp_port, timeout=30)
        ftp.login("anonymous", "anonymous@")
    except (OSError, ftplib.Error) as error:
        raise DeployError(f"the console's FTP server at {args.host}:{args.ftp_port} did not answer ({error})") \
            from error
    return ftp


def ftp_close(ftp: ftplib.FTP) -> None:
    try:
        ftp.quit()
    except (OSError, EOFError, ftplib.Error):
        pass


def ftp_delete(ftp: ftplib.FTP, name: str) -> bool:
    """DELE, for a server that answers it with 226 (ftpsrv), which ftplib's
    delete() refuses; False if there was no such file"""
    try:
        ftp.sendcmd(f"DELE {name}")
        return True
    except ftplib.error_perm as error:
        if str(error).startswith("550"):
            return False
        raise


def ftp_files(ftp: ftplib.FTP, folder: str) -> Optional[List[str]]:
    """the names of the files in a folder (None if there is no folder);
    ftpsrv ignores MLSD's argument, so the listing changes folder first"""
    try:
        ftp.cwd(folder)
    except ftplib.error_perm:
        return None
    return sorted(name for name, facts in ftp.mlsd() if facts.get("type") == "file")


def title_folder(args: argparse.Namespace) -> str:
    return f"/data/homebrew/{args.title}"


def new_log_folder(args: argparse.Namespace) -> Path:
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
    folder = Path(args.logs) / stamp
    suffix = 1
    while folder.exists():
        suffix += 1
        folder = Path(args.logs) / f"{stamp}-{suffix}"
    folder.mkdir(parents=True)
    return folder


def action_fetch(args: argparse.Namespace, folder: Optional[Path] = None) -> int:
    folder = folder or new_log_folder(args)
    ftp = ftp_open(args)
    fetched = []
    try:
        wanted = []
        title_files = ftp_files(ftp, title_folder(args)) or []
        if "halo.log" in title_files:
            wanted.append((title_folder(args), "halo.log", "app0-halo.log"))
        # (a run's reports; not the title's own NOTICE.txt and hold.txt)
        wanted += [(title_folder(args), name, name) for name in title_files
                   if name.endswith(".txt") and name not in ("NOTICE.txt", "hold.txt")]
        if "halo.log" in (ftp_files(ftp, DATA_FOLDER) or []):
            wanted.append((DATA_FOLDER, "halo.log", "halo.log"))
        for remote_folder, name, local in wanted:
            ftp.cwd(remote_folder)
            with (folder / local).open("wb") as handle:
                ftp.retrbinary(f"RETR {name}", handle.write)
            fetched.append(f"{remote_folder}/{name}")
    finally:
        ftp_close(ftp)
    say(f"fetched {', '.join(fetched) if fetched else 'nothing (no log on the console)'} into {folder}")
    return 0


def action_upload(args: argparse.Namespace) -> int:
    source = Path(args.title_dir)
    if not (source / "eboot.bin").is_file() or not (source / "sce_sys" / "param.json").is_file():
        raise DeployError(f"{source} is not a built title: ninja ps5 first")
    if not args.force and title_running(args):
        raise DeployError(f"{args.title} is running: kill it first, or --force")
    files = sorted(path.relative_to(source).as_posix() for path in source.rglob("*") if path.is_file())
    files = [name for name in files if name not in LAST] + [name for name in LAST if name in files]
    unsigned = ROOT / "build" / "ps5" / "host" / "eboot.title.elf"
    started = time.monotonic()
    total = 0
    ftp = ftp_open(args)
    try:
        for folder in sorted({str(Path(name).parent) for name in files if "/" in name}):
            path = title_folder(args)
            for part in [""] + folder.split("/"):
                path = f"{path}/{part}" if part else path
                try:
                    ftp.mkd(path)
                except ftplib.error_perm as error:
                    if not str(error).startswith("550"):
                        raise
        for name in files:
            local = source / name
            remote_folder, _, remote_name = f"{title_folder(args)}/{name}".rpartition("/")
            ftp.cwd(remote_folder)
            size = local.stat().st_size
            say(f"{local.relative_to(ROOT) if local.is_relative_to(ROOT) else local} -> {remote_folder}/{remote_name} "
                f"({size / 1048576.0:.1f} MB)")
            # (straight over the old file, as tools/switch_deploy.py does, and
            # for its reasons; a transfer cut short shows in the size)
            with local.open("rb") as handle:
                ftp.storbinary(f"STOR {remote_name}", handle)
            total += size
            actual = ftp.size(remote_name)
            with local.open("rb") as handle:
                is_self = handle.read(4) in SELF_MAGICS
            if actual == size:
                continue
            if is_self and name == "eboot.bin" and unsigned.is_file() and actual == unsigned.stat().st_size:
                say(f"  (the server gives the decrypted size, {actual} bytes: the unsigned ELF's)")
            elif is_self:
                say(f"  (a SELF: the server gives {actual} bytes, its decrypted size, for {size})")
            else:
                raise DeployError(f"{remote_name} is {actual} bytes on the console, {size} expected: "
                                  "the transfer was cut short")
        ftp.cwd(title_folder(args))
        if args.hold:
            ftp.storbinary("STOR hold.txt", io.BytesIO(b"the host waits 120 s before it ends\n"))
            say(f"{title_folder(args)}/hold.txt: put (the host will wait 120 s before it ends)")
        elif ftp_delete(ftp, "hold.txt"):
            say(f"{title_folder(args)}/hold.txt: removed")
    finally:
        ftp_close(ftp)
    elapsed = time.monotonic() - started
    say(f"uploaded {len(files)} files, {total / 1048576.0:.1f} MB, in {elapsed:.1f} s")
    return 0


# ---------- the kernel log

def klog_lines(connection: socket.socket, args: argparse.Namespace, until: float,
               idle: Optional[float] = None):
    """the kernel log's lines as they come, until the monotonic time until;
    with idle, also as soon as the server has said nothing for that long (the
    way the replayed backlog is told from the run). A connection the server
    closes ends the lines."""
    rest = b""
    connection.settimeout(idle if idle is not None else 1.0)
    while time.monotonic() < until:
        try:
            data = connection.recv(65536)
        except socket.timeout:
            if idle is not None:
                return
            continue
        if not data:
            raise DeployError(f"klogsrv at {args.host}:{args.klog_port} closed the connection")
        rest += data
        cut = rest.find(b"\n")
        while cut >= 0:
            yield rest[:cut].decode("utf-8", "replace").rstrip("\r")
            rest = rest[cut + 1:]
            cut = rest.find(b"\n")


def action_follow(args: argparse.Namespace, folder: Optional[Path] = None,
                  on_ready: Optional[Callable[[], None]] = None) -> int:
    """0 when the host's last line arrives, 1 on a fatal signal, 2 at the
    timeout"""
    folder = folder or new_log_folder(args)
    capture = folder / "klog.txt"
    try:
        connection = socket.create_connection((args.host, args.klog_port), timeout=10)
    except OSError as error:
        raise DeployError(f"klogsrv at {args.host}:{args.klog_port} did not answer ({error})") from error
    status = 2
    with connection, capture.open("w", encoding="utf-8", newline="\n") as log_file:
        # the server replays its buffer to a new connection: that is what
        # arrives before it first falls quiet (phase 0 B measured the replay in
        # well under a second)
        backlog = 0
        for line in klog_lines(connection, args, time.monotonic() + BACKLOG_LIMIT_SECONDS, idle=BACKLOG_QUIET_SECONDS):
            log_file.write(line + "\n")
            backlog += 1
        log_file.write("---- the backlog ends; the run follows ----\n")
        say(f"klog {args.host}:{args.klog_port}: {backlog} backlog lines skipped; following for {args.timeout:.0f} s")
        if on_ready:
            on_ready()

        # the server has been seen to send each line twice (phase 0 B): a line
        # is shown when it first comes, and one later copy of it is let pass
        copies_owed: dict = {}
        seen_host = False
        in_signal = False
        signal_lines = 0
        for line in klog_lines(connection, args, time.monotonic() + args.timeout):
            log_file.write(line + "\n")
            at = line.find("halo: ")
            if at >= 0:
                text = line[at:]
            elif line.startswith("#") and (in_signal or line.startswith(FATAL_SIGNAL)):
                text = line
            else:
                continue
            if copies_owed.get(text, 0) > 0:
                copies_owed[text] -= 1
                continue
            copies_owed[text] = copies_owed.get(text, 0) + 1
            if text.startswith(FATAL_SIGNAL):
                in_signal = True
                signal_lines = 0
            if in_signal:
                say(f"  {text}")
                signal_lines += 1
                # (the block ends with its list of libraries; a signal after
                # the host's lines ends the follow, one before them is another
                # process's)
                if text.startswith("# dynamic libraries") or signal_lines > 40:
                    in_signal = False
                    if seen_host:
                        status = 1
                        break
                continue
            seen_host = True
            say(text)
            if text.startswith(HOST_LAST_LINES):
                status = 0
                break
            log_file.flush()
    if status == 2:
        say(f"follow: no last line from the host within {args.timeout:.0f} s")
    say(f"klog capture: {capture}")
    return status


# ---------- the whole loop

def action_run(args: argparse.Namespace) -> int:
    started = time.monotonic()
    say("-- the previous run's files")
    action_fetch(args)
    if title_running(args):
        say(f"-- {args.title} is running: kill")
        if action_kill(args) != 0:
            raise DeployError(f"{args.title} could not be killed")
        for _ in range(20):
            if not title_running(args):
                break
            time.sleep(0.5)
    say("-- upload")
    action_upload(args)
    say("-- launch and follow")
    folder = new_log_folder(args)
    launched = []

    def launch() -> None:
        launched.append(time.monotonic())
        if action_launch(args) != 0:
            raise DeployError(f"{args.title} was not launched")

    status = action_follow(args, folder, on_ready=launch)
    say("-- this run's files")
    action_fetch(args, folder)
    say(f"--run: {time.monotonic() - started:.1f} s in all, {time.monotonic() - launched[0]:.1f} s from the launch"
        if launched else f"--run: {time.monotonic() - started:.1f} s")
    return 0 if status == 0 else 1


ACTIONS = {
    "fetch": action_fetch,
    "upload": action_upload,
    "status": action_status,
    "launch": action_launch,
    "kill": action_kill,
    "restart": action_restart,
    "follow": action_follow,
    "load-ps5vkctl": action_load_ps5vkctl,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("actions", nargs="*", metavar="ACTION",
                        help="fetch, upload, status, launch, kill, restart, follow, load-ps5vkctl")
    parser.add_argument("--run", action="store_true", help="fetch, kill if running, upload, launch and follow, fetch")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--ftp-port", type=int, default=2121)
    parser.add_argument("--ctl-port", type=int, default=9111)
    parser.add_argument("--klog-port", type=int, default=3232)
    parser.add_argument("--manager-port", type=int, default=8084)
    parser.add_argument("--title", default=DEFAULT_TITLE)
    parser.add_argument("--title-dir", default=str(ROOT / "build" / "ps5" / "title" / DEFAULT_TITLE))
    parser.add_argument("--logs", default=str(ROOT / "build" / "ps5" / "logs"))
    parser.add_argument("--timeout", type=float, default=180.0, help="follow's, in seconds (default 180)")
    parser.add_argument("--hold", action="store_true", help="upload puts hold.txt in the title's folder")
    parser.add_argument("--force", action="store_true", help="upload while the title runs")
    parser.add_argument("--ps5vkctl", default=str(DEFAULT_PS5VKCTL), help="the payload load-ps5vkctl sends")
    args = parser.parse_args()
    if not args.run and not args.actions:
        parser.error("name an action, or --run")
    unknown = [action for action in args.actions if action not in ACTIONS]
    if unknown:
        parser.error(f"unknown action {', '.join(unknown)}: {', '.join(ACTIONS)}")
    try:
        if args.run:
            return action_run(args)
        status = 0
        for action in args.actions:
            status = ACTIONS[action](args)
            if status != 0:
                break
        return status
    except DeployError as error:
        print(f"ps5_deploy: {error}", file=sys.stderr)
        return 1
    except ftplib.Error as error:
        print(f"ps5_deploy: the FTP server at {args.host}:{args.ftp_port}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
