# Valhalla routing evaluation

The web planner can use an explicitly configured HTTPS Valhalla service. Choose
Valhalla in Settings, enter its service address, and enable coordinate sharing.
Changing the address revokes that consent. Enter start/destination coordinates
or leave the start empty to use browser geolocation. Avoidance is checked against
the returned route; a rejected route never falls back to OSRM.

The C++ request/parser and route converter use the same contract fixture as the
JavaScript provider. Android defaults to CoMaps. Settings → Navigation → Online
car route previews allows an HTTPS endpoint and explicit coordinate-sharing
consent. Endpoint changes revoke consent and cancel the current preview. There
is no default Valhalla server. Optional hard exclusions require explicit service
capability selection. Unsupported surface preferences use CoMaps.

The car coordinator snapshots preferences, recalculates through unvisited
checkpoints, and tries CoMaps once after an online failure. Cancellation never
triggers fallback. Both online and native results are checked for reported toll
roads when toll avoidance is enabled. Successful fallback shows its reason and
stays selected during automatic reroutes until an explicit retry or a new journey.
Failed reroutes retain the previous route and mark the session off-route. Missing
MWM files cannot invalidate an online result.

Valhalla previews cannot start navigation through the phone UI, Android Auto,
the SDK controller or the native session. GPS updates, traffic updates and voice
announcements are also disabled for unvalidated online previews. `kValhallaNavigationValidated` is false
and is not a runtime preference. Enabling it requires independent quality review,
full GPS/voice replay and arranged device validation. CoMaps fallback routes can
navigate normally. This does not provide offline Valhalla calculation or durable
restoration of downloaded routes.

## Tests

```sh
cd web
npm run test:unit
npm run build
npm test
```

The parser tests are independently buildable without map downloads:

```sh
cmake -S libs/routing/valhalla_tests -B .gradle/valhalla-api
cmake --build .gradle/valhalla-api
ctest --test-dir .gradle/valhalla-api --output-on-failure
```

In a desktop CoMaps build configured with `SKIP_QT_GUI=ON` and `SKIP_TESTS=OFF`:

```sh
cmake --build BUILD_DIR --target routing_tests routing_compare_tool
BUILD_DIR/routing_tests --filter='(CarRouter_|Valhalla_|Route_|RouteSegment|RoutingSession|FollowedPolyline)'
```

Android transport checks use injected HTTP connections and make no network requests:

```sh
cd android
./gradlew :sdk:testDebugUnitTest --tests '*ValhallaTransportTest' -Parm64 -PdisableHWAsan
```

Use JDK 21 for this project. Native tests cover fallback latching, cancellation,
preference snapshots, toll rejection, checkpoint indices, stale responses,
missing-map bypass and the navigation gate. They are not the complete GPS/voice
navigation replay suite.

## Benchmark

The [full comparison](results/2026-11-01-full/results.json) contains 40 Valhalla
results and 80 successful CoMaps calculations across all 20 journeys, including
tolls allowed/avoided and leaps enabled/disabled. All 40 native avoidance results
passed the reported-toll check. [CSV measurements](results/2026-11-01-full/comparison.csv)
separate distance, ETA, calculation latency and toll flags. The earlier
[nine-journey capture](results/2026-11-01/results.json) remains available.

For unforced Lansuscle–Bordeaux with complete downloaded coverage and toll
avoidance, CoMaps measured 506.3 km / 8h08, CoMaps without leaps 467.2 km / 7h20,
and Valhalla 476.8 km / 8h39. These are engine estimates, not observed times.
The default native geometry passes about 35.6 km from Toulouse, so this is not
an exact reproduction of the originally reported city detour.

Valhalla is shorter in nine of the 17 accepted paired avoidance results. Four
warning responses and one toll-bearing avoidance response were rejected by both
parsers. These measurements do not complete independent geometry review or the
navigation validation gate.

`cases.json` fixes 20 journeys and their coordinates. Its evaluation corridors
are observations to compare, never routing inputs. The explicit via-Cahors case
is separate from the unforced Saint-Martin-de-Lansuscle → Bordeaux case.

Query the chosen server's `/status` first and supply its exact version:

```sh
python3 tools/routing/valhalla/benchmark.py \
  --endpoint https://YOUR-SERVICE \
  --expected-version EXACT-STATUS-VERSION \
  --output .gradle/valhalla-runs/example
```

The default captures only the example, once with tolls allowed and once avoided.
`--all` explicitly selects the full set. Requests are sequential, at least two
seconds apart. Raw responses, request payloads, timestamps, checksums, latency,
distance and ETA are retained separately. Output directories cannot be reused.
A changed engine version or dataset during capture invalidates the run.

Run capture-helper checks with
`python3 -m unittest discover -s tools/routing/valhalla -p 'test_*.py'`.

For the CoMaps comparison, add:

```sh
  --comaps-tool BUILD_DIR/routing_compare_tool \
  --maps /ABSOLUTE/PATH/TO/BENCHMARK-MAPS \
  --resources /ABSOLUTE/PATH/TO/comaps-photon/data
```

Use a **dedicated copy** of downloaded MWM files in their version directory,
including all corridor regions. The existing native map loader can clean obsolete
map files. Do not point it at a live application's map directory. Include world
resources where required, keep the catalogue and map series compatible, and
verify map sizes and catalogue hashes before running. The tool uses an isolated
settings directory, so avoidance experiments do not change application settings.
Pass the parent of the numeric version directory to `--maps`, for example
`--maps /cache/maps` when files are in `/cache/maps/261004/`.

CoMaps runs use identical points with toll avoidance on/off, and with the normal
algorithm versus long-distance leaps disabled. The diagnostic flag is local to
each router instance and is never enabled by the app. Native errors are retained
as errors; missing maps must not be scored as route-quality failures.

Before enabling Android navigation, review the geometry of several problematic
routes and a held-out set, including motorway alternatives, boundary crossings,
endpoint snapping and waypoints. Then validate GPS replay, maneuver announcements,
network loss and fallback. A lower ETA alone does not pass the quality gate.

## Service and data credits

Map data: © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright).
Routing: [Valhalla](https://github.com/valhalla/valhalla), MIT licensed.
CoMaps code is covered by this repository's license and notices.

The [FOSSGIS demo](https://valhalla.openstreetmap.de/) is suitable for modest
explicit experiments, not an assumed production dependency. Its operators request
an identifying `X-Client-Id` and notification before published end-user apps use
it. Both clients identify requests as `comaps-photon`; production hosting and
operator notification remain release requirements. See the
[server policy](https://valhalla.github.io/valhalla/start/introduction/).

No live GPS stream is uploaded. The web service worker caches local assets only;
route responses are requested with `no-store`. Self-hosted services should avoid
logging route coordinates, support the required browser origins, and keep secrets
server-side.
