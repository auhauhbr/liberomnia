# LiberOmnia

LiberOmnia is an open source platform being built for federated bibliographic
research: one place to discover books, authors, subjects, and text passages across
digital libraries and archives, while preserving the identity and metadata of
individual editions.

**Current stage: architectural and visual foundation (0.1.0).** The HTTP health
endpoint and source catalog work. The responsive Portuguese-language frontend
consumes both endpoints. Search execution is disabled, and no bibliographic
results are fabricated. No library integration is operational yet.

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

- `liberomnia_core`: environment configuration, bibliographic/query models, and
  source descriptors, with no HTTP dependency. Optional fields distinguish
  missing metadata from invented defaults. Publication years currently use
  positive calendar years from 1 through 9999, with inclusive ranges.
- `liberomnia_http`: Drogon routes, JSON serialization, and exact-origin CORS.
  `liberomnia_server` composes the application and reports startup errors.
- `liberomnia_persistence`: an interface CMake target exposing `SQLite::SQLite3`
  for future repositories. There are no database connections, tables, or migrations.

`search::SearchProvider` defines a small callback-based adapter boundary with a
virtual destructor, source descriptor, and explicit result/error container.
Adapters must complete once, including on failure. There are no implementations
yet. A future federated engine will own provider lifetimes, schedule independent
requests, handle timeouts/partial failures, and pass records through normalization
before aggregation. Normalization must preserve source and edition identifiers.
Those components are deliberately deferred until real adapter requirements exist.

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
There is no wildcard CORS or credentialed access. CORS is a browser policy, not
authentication. Static files are not served by the API.

Start the frontend in another terminal, from the repository root:

```sh
npm --prefix frontend run dev
```

Open **http://localhost:5173**. Vite proxies `/api` to the backend. It reads the
root `.env` (and exported variables) for `LIBEROMNIA_ADDRESS` and
`LIBEROMNIA_PORT`; restart it after changing those settings. Backend binding
addresses `0.0.0.0` and `::` map to their loopback equivalents in the proxy.
Frontend HTTP calls use relative URLs and require no exposed configuration secrets.

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
are `api` or `external`; implementation statuses are `planned` or `available`;
capability statuses are `planned` or `confirmed`. Capability IDs are extensible;
clients preserve unknown IDs and may ignore additive JSON fields. All five current
sources and all listed capabilities are **planned**, not validated integrations.
Health indicates that this application responds, not that a provider is reachable.
There is no `/api/search` endpoint.

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

GoogleTest covers configuration validation, bibliographic/query models, health
JSON, and catalog structure. The Python integration test starts the actual server
on loopback with temporary storage, validates HTTP responses and CORS, checks
invalid startup configuration and unsupported routes/methods, and stops the process.
It requires permission to bind a local TCP socket. To run only the C++ unit tests
in an environment that prohibits local sockets:

```sh
ctest --test-dir build --output-on-failure -E http_integration
```

Vitest covers initial rendering, real-client integration through mocked `fetch`,
loading and independent failure states, retry, cancellation, stale response
handling, timeouts, invalid contracts, safe text rendering, and historical filters.
No test requires Google Books, Internet Archive, Open Library, Gallica, or Anna's
Archive to be available. DOM rendering tests do not replace visual or screen-reader
testing in a real browser.

## Next stage

Validate provider-specific capabilities, implement the first real adapter and its
contract tests, design normalized results without collapsing distinct editions,
and add federated orchestration with cancellation, timeouts, and partial failures.
Enable search only when an actual end-to-end implementation exists. Add SQLite
repositories and migrations when a concrete persistence feature needs them.

## License

[MIT](LICENSE), copyright 2026 Jefferson Tadeu dos Santos.
