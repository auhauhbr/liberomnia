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
    configured_missing = len(sys.argv) > 2 and sys.argv[2] == "missing-key"
    executable = str(Path(sys.argv[1]).resolve())
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="liberomnia-test-") as directory:
        environment = dict(os.environ, LIBEROMNIA_ADDRESS="127.0.0.1",
                           LIBEROMNIA_PORT=str(port), GOOGLE_BOOKS_ENABLED="true" if configured_missing else "false", GOOGLE_BOOKS_API_KEY="", LIBEROMNIA_DATA_DIR=directory,
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

                def request(path, origin=None, method="GET", body=None, extra_headers=None):
                    headers = {"Origin": origin} if origin else {}
                    headers.update(extra_headers or {})
                    return opener.open(urllib.request.Request(
                        f"http://127.0.0.1:{port}{path}", headers=headers, method=method, data=body), timeout=2)

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
                    books = next(s for s in catalog["sources"] if s["id"] == "google-books")
                    assert books["operational"]["implemented"]
                    assert not books["operational"]["configured"]
                    assert not books["operational"]["searchAllowed"]
                    assert books["operational"]["state"] == ("configuration_required" if configured_missing else "disabled")
                    assert books["operational"]["lastSuccessfulAt"] is None
                    assert len(catalog["sources"]) == 5
                    assert all(s["implementationStatus"] == ("temporarily_unavailable" if s["id"] == "google-books" else "planned") for s in catalog["sources"])

                for origin in (None, "https://untrusted.example", "http://localhost:5173.evil.example"):
                    with request("/api/health", origin) as response:
                        assert response.headers.get("Access-Control-Allow-Origin") is None

                def expect_error(path, status, code, method="POST", body=b'{"text":"books"}', headers=None):
                    try:
                        request(path, "http://localhost:5173", method, body,
                                headers or {"Content-Type": "application/json"}).close()
                        raise AssertionError("Expected an error response")
                    except urllib.error.HTTPError as error:
                        assert error.code == status, (error.code, status, error.read())
                        payload = json.load(error)
                        assert payload["apiVersion"] == "1"
                        assert payload["error"]["code"] == code, payload
                        assert "session" not in payload
                        assert error.headers["Access-Control-Allow-Origin"] == "http://localhost:5173"

                expect_error("/api/search", 503, "no_available_providers")
                expect_error("/api/search", 503, "no_available_providers", body=b'{"text":"books","sourceIds":["gallica"]}')
                expect_error("/api/search", 503, "no_available_providers", body=b'{"text":"books","sourceIds":["annas-archive"]}')
                expect_error("/api/search", 400, "invalid_json", body=b'{broken')
                expect_error("/api/search", 400, "invalid_json", body=b'{"text":"a","text":"b"}')
                expect_error("/api/search", 422, "invalid_request", body=b'{"text":" "}')
                expect_error("/api/search", 422, "invalid_request", body=b'{"text":"a","publication":{"from":1900,"to":1801}}')
                expect_error("/api/search", 422, "unknown_source", body=b'{"text":"a","sourceIds":["missing"]}')
                expect_error("/api/search", 422, "invalid_request", body=json.dumps({"text": "a", "sourceIds": ["source-" + str(i) for i in range(17)]}).encode())
                expect_error("/api/search", 415, "unsupported_media_type", headers={"Content-Type": "text/plain"})
                for suffix, method in (("", "GET"), ("/results", "GET"), ("/cancel", "POST")):
                    expect_error("/api/search/" + "a" * 32 + suffix, 404, "session_not_found", method=method, body=None)
                with request("/api/search", "http://localhost:5173", "OPTIONS", extra_headers={
                    "Access-Control-Request-Method": "POST", "Access-Control-Request-Headers": "content-type"
                }) as response:
                    assert response.status == 204
                    assert "POST" in response.headers["Access-Control-Allow-Methods"]
                    assert response.headers["Access-Control-Allow-Headers"] == "Content-Type"
                try:
                    request("/api/search", "https://untrusted.example", "OPTIONS", extra_headers={
                        "Access-Control-Request-Method": "POST"
                    }).close()
                    raise AssertionError("Untrusted preflight succeeded")
                except urllib.error.HTTPError as error:
                    assert error.code == 403
                    assert error.headers.get("Access-Control-Allow-Origin") is None
                for method, headers in (("DELETE", "content-type"), ("POST", "x-arbitrary")):
                    expect_error("/api/search", 403, "invalid_preflight", method="OPTIONS", body=None,
                                 headers={"Access-Control-Request-Method": method, "Access-Control-Request-Headers": headers})
                try:
                    request("/api/search", method="POST", body=b"x" * 16385,
                            extra_headers={"Content-Type": "application/json"}).close()
                    raise AssertionError("Oversized body was accepted")
                except urllib.error.HTTPError as error:
                    assert error.code == 413

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
