"""Google Books adapter through production HTTP routes; loopback fixtures only."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error

with socket.socket() as listener:
    listener.bind(("127.0.0.1", 0))
    port = listener.getsockname()[1]
with tempfile.TemporaryDirectory(prefix="liberomnia-books-test-") as directory:
    env = dict(os.environ, LIBEROMNIA_PORT=str(port), LIBEROMNIA_DATA_DIR=directory)
    with subprocess.Popen([str(Path(sys.argv[1]).resolve())], env=env, cwd=directory,
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as server:
        try:
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            def request(path, body=None):
                data = None if body is None else json.dumps(body).encode()
                req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data,
                                             headers={"Content-Type": "application/json"})
                with opener.open(req, timeout=2) as response:
                    assert response.headers["Cache-Control"] == "no-store"
                    return json.load(response)
            deadline = time.monotonic()+5
            while True:
                try:
                    catalog = request("/api/sources")
                    break
                except urllib.error.URLError:
                    if time.monotonic() > deadline: raise
                    time.sleep(.02)
            assert catalog["searchAvailable"]
            assert sum(s["implementationStatus"] == "available" for s in catalog["sources"]) == 1
            def search(**values):
                created = request("/api/search", {"text": "Fixture", "sourceIds": ["google-books"], **values})
                path = "/api/search/"+created["session"]["id"]+"/results"
                deadline = time.monotonic()+3
                while True:
                    result = request(path)
                    if result["session"]["finished"]: return result
                    if time.monotonic() > deadline: raise AssertionError("Session deadline exceeded")
                    time.sleep(.01)
            result = search(field="title", publication={"from": 1801, "to": 1900}, language="la", availability="pdf")
            assert len(result["results"]) == 1, result
            record = result["results"][0]["record"]
            assert record["sourceRecordId"] == "fixture-1880" and record["publicationYear"] == 1880
            assert record["publishedDate"] == "1880-05" and record["originalPublicationYear"] is None
            assert record["workId"] is None and record["editionId"] is None
            assert record["textualEvidence"][0]["kind"] == "source_excerpt"
            assert record["textualEvidence"][0]["location"] is None
            progress = result["session"]["sources"][0]
            assert progress["localFiltersApplied"] and progress["fallbackUsed"] and progress["coverageIncomplete"]
            for text, state, code in [("invalid","failed","invalid_provider_response"),
                                       ("timeout","timed_out","provider_timed_out"),
                                       ("quota","rate_limited","provider_rate_limited")]:
                progress = search(text=text)["session"]["sources"][0]
                assert (progress["state"],progress["error"]["code"]) == (state,code), progress
            catalog = request("/api/sources")
            assert not catalog["searchAvailable"]
            print("Google Books HTTP catalog, fallback, historical filters, provenance and typed errors: passed")
        finally:
            server.terminate()
            try: server.wait(timeout=5)
            except subprocess.TimeoutExpired: server.kill()
