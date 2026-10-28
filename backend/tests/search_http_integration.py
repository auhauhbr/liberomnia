"""Test the production search routes with explicitly injected test-only adapters."""
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
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with tempfile.TemporaryDirectory(prefix="liberomnia-http-") as directory, tempfile.TemporaryFile(mode="w+") as log:
        environment = dict(os.environ, LIBEROMNIA_ADDRESS="127.0.0.1", LIBEROMNIA_PORT=str(port),
                           LIBEROMNIA_DATA_DIR=directory, LIBEROMNIA_FRONTEND_ORIGIN="http://localhost:5173")
        process = subprocess.Popen([executable], cwd=directory, env=environment, stdout=log, stderr=log)
        try:
            def request(path, method="GET", body=None, expected=200):
                data = None if body is None else json.dumps(body).encode()
                message = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method, data=data,
                    headers={"Content-Type": "application/json", "Origin": "http://localhost:5173"})
                try:
                    response = opener.open(message, timeout=2)
                except urllib.error.HTTPError as error:
                    response = error
                with response:
                    assert response.status == expected, (path, response.status, expected, response.read())
                    if expected == 204:
                        return None
                    assert response.headers["Access-Control-Allow-Origin"] == "http://localhost:5173"
                    payload = json.load(response)
                    assert payload["apiVersion"] == "1"
                    return payload

            def eventually(action):
                deadline = time.monotonic() + 5
                while True:
                    try:
                        value = action()
                        if value:
                            return value
                    except (urllib.error.URLError, TimeoutError):
                        if process.poll() is not None:
                            raise
                    if time.monotonic() >= deadline:
                        raise AssertionError("Server did not reach the expected state")
                    time.sleep(0.005)

            eventually(lambda: request("/api/health"))
            assert request("/api/sources")["searchAvailable"] is True
            created = request("/api/search", "POST", {"text": "test", "field": "title"}, 202)
            session_id = created["session"]["id"]
            path = "/api/search/" + session_id
            assert created["session"]["state"] == "pending"
            eventually(lambda: all(source["state"] == "running" for source in request(path)["session"]["sources"]))
            request("/test/complete/fast/0", "POST", expected=204)
            eventually(lambda: len(request(path + "/results")["results"]) == 1)
            assert request(path)["session"]["finished"] is False
            request("/test/complete/slow/0", "POST", expected=204)
            eventually(lambda: request(path)["session"]["finished"])
            results = request(path + "/results")
            assert results["session"]["state"] == "completed"
            assert [item["record"]["sourceId"] for item in results["results"]] == ["fast", "slow"]
            assert results["results"][0]["original"]["title"] != results["results"][0]["record"]["title"]
            assert "results" not in request(path)

            second = request("/api/search", "POST", {"text": "test", "sourceIds": ["slow"]}, 202)["session"]["id"]
            second_path = "/api/search/" + second
            eventually(lambda: request(second_path)["session"]["sources"][0]["state"] == "running")
            capacity = request("/api/search", "POST", {"text": "test"}, 429)
            assert capacity["error"]["code"] == "session_limit_reached"
            cancelled = request(second_path + "/cancel", "POST")
            assert cancelled["session"]["state"] == "cancelled"
            request("/test/complete/slow/1", "POST", expected=204)
            assert request(second_path + "/results")["results"] == []
            request("/test/advance/1000", "POST", expected=204)
            assert request(path, expected=410)["error"]["code"] == "session_expired"
            assert request(second_path + "/cancel", "POST", expected=410)["error"]["code"] == "session_expired"

            third = request("/api/search", "POST", {"text": "test", "sourceIds": ["slow"]}, 202)["session"]["id"]
            third_path = "/api/search/" + third
            eventually(lambda: request(third_path)["session"]["sources"][0]["state"] == "running")
            request("/test/advance/100", "POST", expected=204)
            timed_out = request(third_path)["session"]
            assert timed_out["state"] == "failed"
            assert timed_out["sources"][0]["state"] == "timed_out"
            assert timed_out["sources"][0]["error"]["code"] == "provider_timed_out"
            request("/test/complete/slow/2", "POST", expected=204)
            assert request(third_path + "/results")["results"] == []
            assert request("/api/search", "POST", {"text": "test", "field": "passage"}, 422)["error"]["code"] == "unsupported_search"
            print("Search HTTP: 202, progress, results, cancel, 410 expiry, timeout, 422 capabilities and 429 capacity passed")
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
