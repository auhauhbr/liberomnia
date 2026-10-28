"""Exercise the real server using loopback only and the Python standard library."""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="liberomnia-test-") as directory:
        environment = dict(os.environ, LIBEROMNIA_ADDRESS="127.0.0.1",
                           LIBEROMNIA_PORT=str(port), LIBEROMNIA_DATA_DIR=directory,
                           LIBEROMNIA_FRONTEND_ORIGIN="http://localhost:5173")
        invalid = subprocess.run([executable], cwd=directory,
                                 env=dict(environment, LIBEROMNIA_PORT="invalid"),
                                 capture_output=True, text=True, timeout=5)
        assert invalid.returncode != 0
        assert "LIBEROMNIA_PORT" in invalid.stderr
        blocked_path = Path(directory) / "regular-file"
        blocked_path.write_text("not a directory", encoding="utf-8")
        invalid_path = subprocess.run([executable], cwd=directory,
                                      env=dict(environment, LIBEROMNIA_DATA_DIR=str(blocked_path)),
                                      capture_output=True, text=True, timeout=5)
        assert invalid_path.returncode != 0
        assert "startup failed" in invalid_path.stderr
        with tempfile.TemporaryFile(mode="w+") as log:
            process = subprocess.Popen([executable], cwd=directory, env=environment,
                                       stdout=log, stderr=log)
            try:
                opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

                def request(path, origin=None, method="GET"):
                    headers = {"Origin": origin} if origin else {}
                    return opener.open(urllib.request.Request(
                        f"http://127.0.0.1:{port}{path}", headers=headers, method=method), timeout=2)

                deadline = time.monotonic() + 10
                while True:
                    try:
                        with request("/api/health") as response:
                            assert response.status == 200
                            assert json.load(response) == {
                                "status": "ok", "application": "LiberOmnia", "apiVersion": "1"}
                        break
                    except (urllib.error.URLError, TimeoutError):
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Server did not become ready")
                        time.sleep(0.05)

                with request("/api/sources", "http://localhost:5173") as response:
                    assert response.headers["Access-Control-Allow-Origin"] == "http://localhost:5173"
                    assert response.headers["Content-Type"].startswith("application/json")
                    assert response.headers["X-Content-Type-Options"] == "nosniff"
                    assert response.headers["Vary"] == "Origin"
                    catalog = json.load(response)
                    assert catalog["apiVersion"] == "1"
                    assert catalog["searchAvailable"] is False
                    assert len(catalog["sources"]) == 5
                    assert all(s["implementationStatus"] == "planned" for s in catalog["sources"])

                for origin in (None, "https://untrusted.example", "http://localhost:5173.evil.example"):
                    with request("/api/health", origin) as response:
                        assert response.headers.get("Access-Control-Allow-Origin") is None

                for path, method in (("/api/search", "GET"), ("/api/health", "POST")):
                    try:
                        request(path, method=method).close()
                        raise AssertionError("Unsupported route or method unexpectedly succeeded")
                    except urllib.error.HTTPError as error:
                        assert error.code in (403, 404, 405)
                assert not list(Path(directory).glob("*.sqlite3"))
                print("HTTP startup, health, sources, CORS, unsupported routes and invalid configuration: passed")
            except BaseException:
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)


if __name__ == "__main__":
    main()
