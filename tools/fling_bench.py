#!/usr/bin/env python3
"""fling_bench.py - time a disk fling to a Webby dongle from a PC, over the same
TCP-3333 protocol a GTi screen uses. Isolates the network path from the screen
and from the dongle's web server.

    python fling_bench.py <host> [--size KB] [--http] [--eject] [--status] [--rounds N]

Default: one raw-TCP fling of 880 KB (a DD ADF), then the ack, then KB/s.
--http   : the same bytes as a multipart POST /upload (the web page's path).
--eject  : CMD_EJECT afterwards, so the dongle is empty again.
--status : CMD_GET_STATUS only (load_id, image size), no transfer.
No dependencies beyond the standard library.
"""
import argparse, socket, struct, sys, time, os, http.client, uuid

TCP_PORT        = 3333
TCP_CMD_ESCAPE  = 0xFFFFFFFF
CMD_GET_STATUS  = 0x02
CMD_EJECT       = 0x03
CMD_SET_NAME    = 0x06
CHUNK           = 8192          # what panel_fleet.h pfSendDisk writes per call
PINGMON         = False         # --pingmon: ping the dongle once a second during the transfer

def connect(host, timeout=8.0):
    s = socket.create_connection((host, TCP_PORT), timeout=timeout)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # pfSendDisk: setNoDelay(true)
    return s

def escape(s, cmd):
    s.sendall(struct.pack(">I", TCP_CMD_ESCAPE) + bytes([cmd]))

def set_name(host, name):
    s = connect(host)
    escape(s, CMD_SET_NAME)
    nb = name.encode()[:128]
    s.sendall(bytes([len(nb)]) + nb)
    r = s.recv(1); s.close()
    return r == b"\x01"

def status(host):
    s = connect(host)
    escape(s, CMD_GET_STATUS)
    r = b""
    t0 = time.time()
    while len(r) < 21 and time.time() - t0 < 3:
        chunk = s.recv(21 - len(r))
        if not chunk: break
        r += chunk
    s.close()
    if len(r) < 21 or r[:2] != b"ST":
        return None
    load_id, dirty, since_ms, writes, img = struct.unpack("<IHIII", r[3:21])
    return dict(proto=r[2], load_id=load_id, dirty=dirty, since_ms=since_ms, writes=writes, image_size=img)

def eject(host):
    s = connect(host)
    escape(s, CMD_EJECT)
    r = s.recv(1); s.close()
    return r == b"\x01"

def fling_tcp(host, size, name, chunk=CHUNK, timeline=False):
    if not set_name(host, name):
        print("  CMD_SET_NAME not acknowledged (older dongle?) - continuing")
    data = os.urandom(size)          # content is irrelevant to TCP; random defeats any compression in the path
    s = connect(host, timeout=20.0)
    t0 = time.perf_counter()
    s.sendall(struct.pack(">I", size))
    sent = 0; stall_max = 0.0; last = time.perf_counter()
    gaps = []                        # (offset_s, gap_s, bytes_sent_so_far) for every send() that took > 250 ms
    secs = {}                        # bytes accepted per wall-clock second
    rtt = {}                         # ping RTT (ms) per wall-clock second, -1 = lost
    stop = [False]
    if PINGMON:
        import subprocess, threading, re
        def pinger():
            while not stop[0]:
                ts = int(time.perf_counter() - t0)
                try:
                    out = subprocess.run(["ping", "-n", "1", "-w", "1500", host], capture_output=True, text=True, timeout=3).stdout
                    m = re.search(r"[=<](\d+)\s*ms", out)
                    rtt[ts] = int(m.group(1)) if m else -1
                except Exception:
                    rtt[ts] = -1
                time.sleep(0.4)
        threading.Thread(target=pinger, daemon=True).start()
    timed_out = False
    s.settimeout(20.0)
    while sent < size:
        try:
            n = s.send(data[sent:sent + chunk])
        except (socket.timeout, TimeoutError):
            timed_out = True; break
        now = time.perf_counter()
        gap = now - last
        if gap > 0.25: gaps.append((now - t0, gap, sent))
        secs[int(now - t0)] = secs.get(int(now - t0), 0) + n
        stall_max = max(stall_max, gap); last = now
        sent += n
    t_send = time.perf_counter() - t0
    stop[0] = True
    if timeline:
        n_s = int(t_send) + 1
        print(f"  KB accepted per second: " + " ".join(f"{secs.get(i, 0)//1024:>4}" for i in range(n_s)))
        if PINGMON:
            print(f"  ping RTT ms per second: " + " ".join(f"{rtt.get(i, 0):>4}" for i in range(n_s)))
        for off, g, b in sorted(gaps, key=lambda x: -x[1])[:6]:
            print(f"  gap {g*1000:6.0f} ms at t={off:6.1f}s after {b//1024} KB")
    if timed_out:
        s.close()
        return dict(ok=False, ack="send() timed out (20 s)", load_id=None, t_send=t_send, t_total=t_send,
                    kbps=sent / 1024 / t_send, longest_send_gap_ms=stall_max * 1000)
    s.settimeout(15.0)
    ack = b""
    try:
        while len(ack) < 5:
            c = s.recv(5 - len(ack))
            if not c: break
            ack += c
    except socket.timeout:
        pass
    t_total = time.perf_counter() - t0
    s.close()
    ok = len(ack) >= 1 and ack[0] == 0x01
    load_id = struct.unpack("<I", ack[1:5])[0] if len(ack) == 5 else None
    return dict(ok=ok, ack=ack.hex(), load_id=load_id, t_send=t_send, t_total=t_total,
                kbps=size / 1024 / t_total, longest_send_gap_ms=stall_max * 1000)

def fling_http(host, size, name):
    data = os.urandom(size)
    boundary = "----bench" + uuid.uuid4().hex
    head = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
            f"Content-Type: application/octet-stream\r\n\r\n").encode()
    tail = f"\r\n--{boundary}--\r\n".encode()
    body = head + data + tail
    c = http.client.HTTPConnection(host, 80, timeout=120)
    t0 = time.perf_counter()
    c.request("POST", "/upload", body=body, headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    r = c.getresponse(); txt = r.read(300).decode(errors="replace")
    t = time.perf_counter() - t0
    c.close()
    return dict(ok=r.status == 200, status=r.status, body=txt, t_total=t, kbps=size / 1024 / t)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--size", type=int, default=880, help="KB to send (default 880 = DD ADF)")
    ap.add_argument("--rounds", type=int, default=1)
    ap.add_argument("--http", action="store_true", help="use POST /upload instead of raw TCP")
    ap.add_argument("--eject", action="store_true", help="CMD_EJECT when done")
    ap.add_argument("--status", action="store_true", help="only CMD_GET_STATUS")
    ap.add_argument("--name", default="BENCH.ADF")
    ap.add_argument("--chunk", type=int, default=CHUNK, help="bytes per send() (default 8192, like the screen)")
    ap.add_argument("--timeline", action="store_true", help="print KB/s per second and the longest gaps")
    ap.add_argument("--poll", action="store_true", help="poll GET /status every 2 s meanwhile, like an open web page")
    ap.add_argument("--pingmon", action="store_true", help="ping the dongle every second during the transfer")
    ap.add_argument("--idle", type=int, default=3, help="seconds to wait between rounds (default 3)")
    ap.add_argument("--diag", action="store_true", help="after each round, print the dongle's GET /diag (rxdiag builds only)")
    a = ap.parse_args()
    size = a.size * 1024
    global PINGMON; PINGMON = a.pingmon
    if a.poll:
        # the SPA (/app) fires several fetches per tick; emulate that with three parallel GETs every second
        import threading
        def one(path):
            try:
                c = http.client.HTTPConnection(a.host, 80, timeout=5); c.request("GET", path); c.getresponse().read(); c.close()
            except Exception as e:
                print(f"  poll {path}: {type(e).__name__}")
        def poller():
            while True:
                ts = [threading.Thread(target=one, args=(p,), daemon=True) for p in ("/status", "/api/fleet", "/api/disk/status")]
                for t in ts: t.start()
                time.sleep(1)
        threading.Thread(target=poller, daemon=True).start()

    st = status(a.host)
    print(f"status before: {st}")
    if a.status:
        return
    for i in range(a.rounds):
        t = time.strftime("%H:%M:%S")
        if a.http:
            r = fling_http(a.host, size, a.name)
            print(f"[{t}] HTTP upload {a.size} KB: ok={r['ok']} http={r['status']} {r['t_total']:.1f}s = {r['kbps']:.0f} KB/s  {r['body'][:80]}")
        else:
            r = fling_tcp(a.host, size, a.name, a.chunk, a.timeline)
            print(f"[{t}] TCP fling {a.size} KB: ok={r['ok']} ack={r['ack']} load_id={r['load_id']} "
                  f"send {r['t_send']:.1f}s, total {r['t_total']:.1f}s = {r['kbps']:.0f} KB/s, longest gap between sends {r['longest_send_gap_ms']:.0f} ms")
        if a.diag:
            try:
                import json
                c = http.client.HTTPConnection(a.host, 80, timeout=5); c.request("GET", "/diag")
                d = json.loads(c.getresponse().read()); c.close()
                ev = d.pop("ev", [])
                print(f"  diag: " + " ".join(f"{k}={v}" for k, v in d.items() if k != "fw"))
                last = [e for e in ev if e[1] in ("rxbeg", "rxgap", "rxend", "rxerr", "rxmax", "elect", "alive")]
                # only this round: everything from the last rxbeg on
                starts = [k for k, e in enumerate(last) if e[1] == "rxbeg"]
                if starts: last = last[starts[-1]:]
                print("  diag ev: " + " ".join(f"{e[1]}@{e[0]}({e[2]},{e[3]})" for e in last))
            except Exception as e:
                print(f"  diag: {type(e).__name__}: {e}")
        if i + 1 < a.rounds:
            time.sleep(a.idle)
    print(f"status after: {status(a.host)}")
    if a.eject:
        print(f"eject: {'ok' if eject(a.host) else 'refused'}")

if __name__ == "__main__":
    main()
