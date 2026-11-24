# The work of log_bench.glow with Python's logging module (written in
# Python), writing the same kind of line to a file.
import logging, os, tempfile, time

path = os.path.join(tempfile.gettempdir(), "py_log_bench.log")
if os.path.exists(path):
    os.remove(path)
handler = logging.FileHandler(path)
handler.setFormatter(logging.Formatter("%(asctime)s.%(msecs)03dZ %(levelname)s %(message)s", "%Y-%m-%dT%H:%M:%S"))
logger = logging.getLogger("bench")
logger.addHandler(handler)
logger.setLevel(logging.INFO)

start = time.time()
for i in range(100000):
    logger.info("request done status=%s path=%s", 200, "/items")
print(f"{'logging.info to a file':<26}{int(100000 / (time.time() - start)):,} lines/s")
start = time.time()
for i in range(1000000):
    logger.debug("skipped i=%s", i)
print(f"{'logging.debug, level info':<26}{int(1000000 / (time.time() - start)):,} calls/s")
handler.close()
os.remove(path)
