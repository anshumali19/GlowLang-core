"""Load generator: python3 load.py <port> <seconds> <processes>

Each process sends GET /notes/<n> over a new connection per request (GlowWeb
closes every connection, so the others are measured the same way) for the
given time, then all report: requests per second, and the median and 99th
percentile time per request. Only status 200 answers are counted."""
import socket, sys, time
from multiprocessing import Pool

def worker(args):
    port, seconds, seed = args
    times, failed, n = [], 0, seed
    end = time.perf_counter() + seconds
    while time.perf_counter() < end:
        n += 1
        t0 = time.perf_counter()
        try:
            s = socket.create_connection(("127.0.0.1", port))
            s.sendall(b"GET /notes/%d HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" % n)
            data = b""
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                data += chunk
            s.close()
            if data.startswith(b"HTTP/1.1 200") or data.startswith(b"HTTP/1.0 200"):
                times.append(time.perf_counter() - t0)
            else:
                failed += 1
        except OSError:
            failed += 1
    return times, failed

if __name__ == "__main__":
    port, seconds, procs = int(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3])
    with Pool(procs) as pool:
        results = pool.map(worker, [(port, seconds, i * 1_000_000) for i in range(procs)])
    times = sorted(t for r in results for t in r[0])
    failed = sum(r[1] for r in results)
    if not times:
        print("no successful requests"); sys.exit(1)
    p = lambda q: times[min(len(times) - 1, int(q * len(times)))] * 1000
    print(f"{len(times) / seconds:.0f} req/s  median {p(0.5):.2f} ms  p99 {p(0.99):.2f} ms  failed {failed}")
