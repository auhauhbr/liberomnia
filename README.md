# LiberOmnia

LiberOmnia is an open source platform being built for federated bibliographic
research: one place to discover books, authors, subjects, and text passages across
digital libraries and archives, while preserving the identity and metadata of
individual editions.

**Current stage: federated search core.** The backend now coordinates asynchronous
providers and bounded, temporary search sessions. There are **no operational external
library adapters**. Production search creation returns HTTP 503 instead of creating
a misleading session. The frontend retains its layout and disabled search form;
its prepared session services are not called by the main interface.

## Planned capabilities

- General, title, author, subject, exact-phrase, and passage searches.
- Language and publication-period filters, subject to each provider's capabilities.
- Federated results with edition provenance, metadata normalization, and links to
  consultation or downloads when available and permitted.
- Google Books, Internet Archive, Open Library, and Gallica adapters.
- Anna's Archive as external search only; no protected-page automation or scraping.

The initial interface includes search modes, an advanced panel, language and
publication filters, historical presets (19th century: **1801–1900**), and source
selection. Planned sources cannot be selected. The form does not send search
requests, even when the API is healthy. Full-text coverage and filter support
must be validated separately for every provider.

## Technology and requirements

| Component | Requirement / tested version |
| --- | --- |
| Platform | Linux |
| C++ | C++20 compiler; tested with GCC 16.2.1 |
| Build | CMake 3.24+; tested with 4.4.5 |
| HTTP | Drogon 1.9.x; tested with stable 1.9.14 and its matching Trantor revision |
| Local persistence | SQLite3 development library; tested with 3.54.0 |
| C++ tests | GoogleTest with CMake config; tested with 1.18.0 |
| HTTP integration tests | Python 3.10+ standard library; tested with 3.14.8 |
| Node.js | 22.12+, 24.x, or 26+ (matching Vitest's engine range); tested with 26.12.1 |
| Frontend | Vue 3.5.44, TypeScript 5.9.3, Vite 8.3.5 |
| Frontend tests | Vitest 5.0.5, Vue Test Utils 2.5.1, jsdom 30.1.3 |

Frontend dependency versions are recorded in `frontend/package-lock.json`.
TypeScript is pinned to 5.9.3 for compatibility with the installed `vue-tsc`
version. No external fonts, image services, authentication, or additional backend
services are required.

## Structure and architecture

```text
.
├── CMakeLists.txt
├── .env.example
├── LICENSE
├── backend/
│   ├── CMakeLists.txt
│   ├── include/liberomnia/
│   │   ├── api.hpp
│   │   ├── config.hpp
│   │   ├── models.hpp
│   │   ├── search_provider.hpp
│   │   ├── provider_registry.hpp
│   │   ├── search_coordinator.hpp
│   │   ├── normalization.hpp
│   │   ├── search_api.hpp
│   │   └── source_catalog.hpp
│   ├── src/
│   └── tests/
└── frontend/
    ├── public/
    ├── src/
    │   ├── components/
    │   ├── composables/
    │   ├── services/
    │   ├── types/
    │   ├── App.vue
    │   ├── main.ts
    │   └── style.css
    └── tests/
```

The backend uses three small boundaries:

- `liberomnia_core`: environment configuration, domain models, provider registry,
  structural normalization, and the session coordinator, with no HTTP dependency. Optional fields distinguish
  missing metadata from invented defaults. Publication years currently use
  positive calendar years from 1 through 9999, with inclusive ranges.
- `liberomnia_http`: Drogon routes, JSON serialization, and exact-origin CORS.
  `liberomnia_server` composes the application and reports startup errors.
- `liberomnia_persistence`: an interface CMake target exposing `SQLite::SQLite3`
  for future repositories. There are no database connections, tables, or migrations.

`SearchRequest` extends `SearchFilters`; `SearchQuery` remains an alias for source
compatibility with the original field access. Search modes are `general`, `title`,
`author`, `subject`, and `passage`. Source record identity, optional work/edition IDs,
edition publication year, and original work year remain distinct. Access metadata
and textual evidence never imply a verified download or confirmed textual occurrence
unless explicitly reported by an adapter. Missing metadata stays absent/null.

`ProviderRegistry` receives descriptors and adapters explicitly and becomes immutable.
An API source cannot be registered as available without an adapter. Planned,
temporarily unavailable, and external-only sources are not dispatched. Production
registers only the five planned descriptors. Test providers and their control routes
exist exclusively under `backend/tests/` and are not linked into the production server.

`SearchCoordinator` uses one owned `std::jthread` for dispatch, normalization, and
maintenance. Provider starts are short asynchronous operations, so multiple upstream
requests can run concurrently without one thread per request. The HTTP handlers never
wait for provider completion. A mutex protects session state and snapshots; provider
code and stop callbacks run outside it. Completion callbacks enqueue at most one result;
weak references and terminal states prevent late/duplicate callbacks from reviving sessions.

The global concurrency limit covers all sessions. The pending queue is bounded and
uses FIFO dispatch. A provider's deadline begins when dispatched, not when queued.
Timeout, cancellation, and expiry request cooperative stop through `std::stop_token`.
A timeout/cancelled operation keeps its execution slot until the adapter acknowledges
completion: an adapter that ignores stop cannot create an unlimited number of abandoned
operations. Such an adapter can exhaust slots; subsequent requests are bounded/rejected.
Adapter `search()` methods, stop callbacks, and destruction must return promptly and
must not perform blocking I/O or wait for their callbacks. C++ cannot forcibly terminate
a misbehaving adapter safely. Exceptions are converted to fixed public error codes;
provider exception messages are not returned to clients.

Adapters return one bounded `ProviderResult`, optionally containing partial records
and a typed `failed`, `blocked`, or `rate_limited` error. Results become visible as each
provider finishes; streaming batches from an individual provider are not implemented.
The provider's `exhaustive` flag defaults to false. `searchCompleted` means that the
request completed successfully, **not** that the entire collection was searched.

Filter support is explicit: `native` filters are sent to the adapter, `local` filters
are removed from the upstream request and applied to returned records, and `unsupported`
filters exclude that provider. Unknown year/language/access values cannot satisfy a
concrete local filter. Local filtering always sets `coverageIncomplete=true`, including
when it yields zero records. Upstream truncation, errors, invalid records, and result
budgets also mark coverage incomplete; `empty` is never substituted for a timeout,
access block, or rate limit.

Normalization collapses basic whitespace in descriptive text, clears blank optional
fields, validates records, retains the original structured record, and deduplicates by
`(sourceId, sourceRecordId)` only. The first occurrence of a duplicate ID in a provider's
response wins. Results are sorted by source ID and original record ID. Sources receive
deterministic shares of the session record budget before dispatch, so completion order
cannot change which source consumes it; unused shares are not reallocated. Work/edition
matching, fuzzy deduplication, raw upstream payload retention, OCR, and pagination are
not implemented.

Sessions are **memory-only and lost on backend restart**. Expiry is measured from
creation with a monotonic clock; cleanup runs periodically and opportunistically during
requests. Completed sessions still consume capacity until expiry. Expired IDs are
retained as bounded tombstones for at most one additional TTL (at most `max_sessions`
IDs), returning 410 while remembered and 404 after eviction. No SQLite tables or
persistent query history are created. Session IDs use 128 bits of Linux `getrandom`
entropy and act as bearer identifiers; the default listener remains loopback. There is
no authentication or ownership isolation in this stage. Query contents are not logged.

The frontend keeps HTTP requests and runtime response validation in `services/`,
loading/error/retry and form state in `composables/`, and presentation in Vue
Composition API components. Requests have an eight-second timeout, are canceled
on unmount/retry, and stale responses cannot overwrite newer state. API text is
rendered through Vue interpolation. There is no raw HTML injection.

## Install dependencies

Install the compiler, CMake, Python, Node.js, and development packages using your
distribution's package manager. For example, on Debian/Ubuntu, an administrator
can install the native build prerequisites with:

```sh
sudo apt-get install build-essential cmake pkg-config curl python3 \
  libjsoncpp-dev libssl-dev zlib1g-dev uuid-dev libsqlite3-dev libgtest-dev
```

This command is a manual prerequisite, not part of the build. Use a Node.js
version satisfying the table above. Install Drogon from your distribution if a
compatible CMake package is available, or build its pinned stable sources into a
local prefix without administrator permissions:

```sh
deps_dir="$(mktemp -d /tmp/liberomnia-deps.XXXXXX)"
deps_prefix="$deps_dir/install"
curl -fL https://github.com/drogonframework/drogon/archive/refs/tags/v1.9.14.tar.gz \
  -o "$deps_dir/drogon.tar.gz"
tar -xzf "$deps_dir/drogon.tar.gz" -C "$deps_dir"
curl -fL https://github.com/an-tao/trantor/archive/78e480f5e138acdc05f76c55a2716718cd846364.tar.gz \
  -o "$deps_dir/trantor.tar.gz"
tar -xzf "$deps_dir/trantor.tar.gz" \
  -C "$deps_dir/drogon-1.9.14/trantor" --strip-components=1
cmake -S "$deps_dir/drogon-1.9.14" -B "$deps_dir/build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$deps_prefix" \
  -DBUILD_CTL=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
  -DBUILD_ORM=OFF -DBUILD_BROTLI=OFF -DBUILD_YAML_CONFIG=OFF
cmake --build "$deps_dir/build" --parallel 4
cmake --install "$deps_dir/build"
```

Keep this prefix available while building. `/tmp` may be cleaned on reboot; choose
a persistent user-owned directory for a long-lived installation. See the
[Drogon releases](https://github.com/drogonframework/drogon/releases/tag/v1.9.14)
and [Vite requirements](https://vite.dev/guide/) for upstream information.

Install the locked frontend dependencies from the repository root:

```sh
npm --prefix frontend ci
```

If your default npm cache is read-only, append `--cache /tmp/liberomnia-npm-cache`.
Dependency installation requires network access. After installation, builds and
tests do not contact the planned library providers.

## Build and run

From the repository root, with Drogon installed in a standard prefix:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

For the local dependency installation above, append
`-DCMAKE_PREFIX_PATH="$deps_prefix"` to the configuration command. The backend
can also be configured directly with `cmake -S backend -B build/backend-only`.
Use `-DBUILD_TESTING=OFF` for a build without GoogleTest or Python requirements.

Configure and start the backend in one terminal:

```sh
cp .env.example .env
set -a
. ./.env
set +a
./build/backend/liberomnia_server
```

The executable reads the process environment; it does **not** parse `.env` files.
The shell commands above export the example settings. Stop the server with Ctrl+C.

| Variable | Default | Behavior |
| --- | --- | --- |
| `LIBEROMNIA_ADDRESS` | `127.0.0.1` | Numeric IPv4/IPv6 listener address; loopback by default |
| `LIBEROMNIA_PORT` | `8080` | Integer in 1–65535; invalid values fail startup |
| `LIBEROMNIA_DATA_DIR` | `./data` | Local runtime root, relative to the process working directory |
| `LIBEROMNIA_FRONTEND_ORIGIN` | `http://localhost:5173` | Exact HTTP(S) origin without path/trailing slash; empty disables CORS |

Drogon creates runtime directories under `LIBEROMNIA_DATA_DIR/runtime`. No upload
route is exposed. `Config::database_path()` reserves
`LIBEROMNIA_DATA_DIR/liberomnia.sqlite3` for future persistence, without creating it.
There is no wildcard CORS or credentialed access. Preflight is checked before
routing and allows only the configured origin, the route's GET/POST method, and
`Content-Type` when requested. CORS is a browser policy, not authentication.
Static files are not served by the API.

Start the frontend in another terminal, from the repository root:

```sh
npm --prefix frontend run dev
```

Open **http://localhost:5173**. Vite proxies `/api` to the backend. It reads the
root `.env` (and exported variables) for `LIBEROMNIA_ADDRESS` and
`LIBEROMNIA_PORT`; restart it after changing those settings. Backend binding
addresses `0.0.0.0` and `::` map to their loopback equivalents in the proxy.
Frontend HTTP calls use relative URLs and require no exposed configuration secrets.

Search resource settings (all positive integers):

| Variable | Default | Maximum |
| --- | --- | --- |
| `LIBEROMNIA_SEARCH_CONCURRENCY` | 4 | 32 |
| `LIBEROMNIA_SEARCH_MAX_PENDING` | 64 | 1024 |
| `LIBEROMNIA_SEARCH_MAX_SESSIONS` | 32 | 256 |
| `LIBEROMNIA_SEARCH_MAX_RECORDS` | 200 | 1000 |
| `LIBEROMNIA_SEARCH_TIMEOUT_MS` | 8000 | 300000 |
| `LIBEROMNIA_SEARCH_SESSION_TTL_MS` | 900000 | 86400000 |

Invalid values fail startup. Text is limited to 4096 UTF-8 bytes, HTTP request bodies
to 16 KiB, source selection to 16 unique IDs, and requested results to 1–100 per
source. Normalized records accept at most 32 KiB of string data each (8 KiB per
field, with bounded nested arrays), plus their original structured copy. Session,
record, queue, and concurrency limits jointly bound retained data; choose limits
appropriate for available memory. Adapters must also bound upstream response bytes
before decoding them. Oversized provider record counts are treated as failures.

Build and preview production assets:

```sh
npm --prefix frontend run build
npm --prefix frontend run preview
```

The preview is available at **http://localhost:4173**, with the same API proxy.
For deployment, serve `frontend/dist` and reverse-proxy `/api` to the backend on
the same origin. Vite's development/preview servers are local tools, not production
servers. No deployment is included in this stage.

## HTTP API

`GET /api/health` returns HTTP 200:

```json
{"status":"ok","application":"LiberOmnia","apiVersion":"1"}
```

`GET /api/sources` returns a versioned envelope with `searchAvailable: false` and
five source descriptors. An illustrative entry from the real catalog is:

```json
{
  "id": "annas-archive",
  "name": "Anna's Archive",
  "integrationType": "external",
  "implementationStatus": "planned",
  "capabilities": [{"id": "externalSearch", "status": "planned"}],
  "notes": "External search only is planned. No scraping or automation of protected pages."
}
```

The envelope has `apiVersion`, `searchAvailable`, and `sources`. Integration types
are `api` or `external`; implementation statuses are `planned`, `available`, or
`temporarily_unavailable`; capability statuses are `planned` or `confirmed`.
Capability IDs are extensible;
clients preserve unknown IDs and may ignore additive JSON fields. All five current
sources and all listed capabilities are **planned**, not validated integrations.
Health indicates that this application responds, not that a provider is reachable.
The catalog also exposes `filterSupport` for publication, language, and availability.
Planned sources advertise `unsupported` operational filters until an adapter exists.

### Search sessions

| Route | Success contract |
| --- | --- |
| `POST /api/search` | 202, `{apiVersion, session}` and a `Location` header |
| `GET /api/search/{id}` | 200, `{apiVersion, session}` without bibliographic records |
| `GET /api/search/{id}/results` | 200, `{apiVersion, session, results}` |
| `POST /api/search/{id}/cancel` | 200, `{apiVersion, session}`; idempotent |

Creation requires `Content-Type: application/json`. Only `text` is mandatory; omitted
fields default to general mode, all registered sources, no filters, and 20 results
per source. Example input (currently returns **503**, not an accepted session):

```json
{
  "text": "history of libraries",
  "field": "subject",
  "publication": {"from": 1801, "to": 1900},
  "language": "en",
  "sourceIds": ["google-books"],
  "maxResultsPerSource": 20
}
```

An optional `availability` filter accepts `viewable`, `pdf`, `epub`, `restricted`,
`metadata_only`, or `unknown`. Optional language, availability, and year bounds may
be null; the publication container must be an object. Unknown JSON properties,
duplicate keys, invalid types, duplicate sources, and out-of-range values are rejected.
Clients cannot supply provider URLs. A source that cannot support the requested mode
or any filter is skipped rather than silently running a weaker query. If at least one
source can run, the session includes all selected sources with their own statuses.

A session contains `id`, `state`, `createdAt` (UTC), `finished`,
`cancellationRequested`, `hasErrors`, and `sources`. Session states are `pending`,
`running`, `completed`, `partial`, `failed`, and `cancelled`. Each source includes
`sourceId`, `state`, `recordsRetrieved`, `recordsAccepted`, `recordsRejected`,
`searchCompleted`, `localFiltersApplied`, `coverageIncomplete`, `resultLimitReached`,
`filters` (ID and application support), and a nullable structured `error`.

Source states are `pending`, `running`, `completed`, `empty`, `rate_limited`,
`blocked`, `failed`, `timed_out`, `cancelled`, `unsupported`, `disabled`, and
`external_only`. Retrieved counts precede normalization and filtering; accepted counts
refer to stored unique records. Rejected counts refer to invalid records, not normal
filter exclusions or duplicates. A finished session may contain partial results and
errors. Cancellation preserves records already accepted and prevents later additions.
Cancelling an already-finished session leaves its outcome unchanged.

Each results entry contains `record` (normalized) and `original` (the bounded
structured source record). Optional values are null, collections are arrays. Access
entries distinguish actual formats from restricted, metadata-only, or unknown access;
textual evidence distinguishes `confirmed_occurrence`, `source_excerpt`,
`approximate_match`, and `unverified`. The core does not independently verify source
claims or promote a snippet into a confirmed occurrence.

Errors use `{ "apiVersion": "1", "error": { "code": "...", "message": "..." } }`:

| HTTP | Meaning / representative code |
| --- | --- |
| 400 | Malformed JSON or duplicate keys: `invalid_json` |
| 403 | Disallowed origin/preflight: `origin_not_allowed`, `invalid_preflight` |
| 404 | Missing or forgotten session: `session_not_found` |
| 410 | Remembered expired session: `session_expired` |
| 413 | Request body exceeds the size limit |
| 415 | Missing/wrong JSON content type: `unsupported_media_type` |
| 422 | Invalid input, unknown source, unsupported search, or insufficient record budget |
| 429 | `session_limit_reached` or `work_limit_reached` |
| 503 | No selected operational adapter: `no_available_providers` |
| 500 | Sanitized internal error |

Provider failures inside an existing session are source statuses, not HTTP failures
of the polling request. The frontend's `services/search.ts` exposes create, progress,
results, and cancel operations with runtime validation and typed `ApiError` values.
These functions remain disconnected from the main search form.

```sh
curl -i http://127.0.0.1:8080/api/health
curl -i http://127.0.0.1:8080/api/sources
```

## Tests

```sh
ctest --test-dir build --output-on-failure
npm --prefix frontend run typecheck
npm --prefix frontend test
npm --prefix frontend run build
git diff --check
```

GoogleTest covers configuration and domain validation, native/local/unsupported
filters, concurrent dispatch, partial failures, timeout, cancellation, duplicate/late
callbacks, expiry, resource limits, stable ordering, provenance, and API contracts.
Controlled providers use explicit completion gates; coordinator deadline/expiry
tests inject a clock instead of waiting for wall-clock time. The Python integration
test starts the actual server on loopback with temporary storage, validates HTTP responses and CORS, checks
invalid startup configuration and unsupported routes/methods, and stops the process.
A second HTTP integration test runs the same production routes with test-only injected
adapters and a virtual clock to exercise accepted sessions, progress, results,
cancellation, resource limits, timeouts, and expiry. Both require permission to
bind a local TCP socket. To run only the C++ unit tests
in an environment that prohibits local sockets:

```sh
ctest --test-dir build --output-on-failure -E 'http_integration'
```

Vitest covers initial rendering, real-client integration through mocked `fetch`,
loading and independent failure states, retry, cancellation, stale response
handling, timeouts, invalid contracts, safe text rendering, historical filters,
and the prepared search-session services including structured errors and provenance.
No test requires Google Books, Internet Archive, Open Library, Gallica, or Anna's
Archive to be available. DOM rendering tests do not replace visual or screen-reader
testing in a real browser.

## Next stage

Implement a real Google Books adapter with `SearchProvider`, declaring only
verified modes and native/local filter support. Bound upstream response size and
records, translate 403/429 and transport failures into typed provider errors, honor
the stop token, and complete exactly once. Register it explicitly alongside its
available source descriptor, then validate against fixture-based contract tests and
opt-in live checks before enabling the UI. The coordinator does not need provider-
specific changes. Streaming, pagination, retries/backoff, persistent history, cross-
source edition matching, OCR, and actual library integrations remain unimplemented.
Add SQLite repositories and migrations only when a concrete feature needs them.

## License

[MIT](LICENSE), copyright 2026 Jefferson Tadeu dos Santos.
