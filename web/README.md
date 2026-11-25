# CoMaps-Photon web

An installable web companion to the native CoMaps fork. Provides an OpenStreetMap map, explicit-submit Photon searches, local saved places, shareable coordinates, geolocation, multi-stop driving routes with toll and motorway avoidance, searches for places along a route, and CoMaps deep links. English and French follow browser preferences, with a persistent language override in Settings.

Saved places and the app shell work offline. This is not a WebAssembly port of the native CoMaps engine: offline map downloads, offline search, and turn-by-turn navigation remain native-app features. Saved places are local to each origin and are not shared between domains.

## Development

Node.js 22.12 or newer is required.

```sh
npm ci
npm run dev
npm run build
npm test
```

Production output is `dist/`. The build generates a content-versioned service worker that precaches local assets only. External tiles use ordinary HTTP caching; there is no tile prefetch, bulk download, or persistent offline tile cache. Service worker updates activate after existing app windows close.

## Hosting

Serve `dist/` over HTTPS at both `maps.camille.studio` and `comaps.camille.studio`. Both names need an A record for the homeserver (`195.15.214.30`). The supplied `deploy/compose.yaml` runs a static, unprivileged Nginx container on the existing `homeserver-web` network. Place its `dist/`, `nginx.conf`, and compose file together under `/srv/homeserver/apps/comaps-photon-web/`. Append `deploy/Caddyfile` to the homeserver's Caddy configuration, validate it, and restart only the Caddy proxy with `docker compose restart caddy` (its admin API is disabled and this binary does not implement SIGUSR1 reloads).

Do not replace the homeserver Caddyfile or restart unrelated application services. Serve HTML, the manifest, and `sw.js` with revalidation; hashed assets may be immutable. Unknown asset paths must return 404.

## Data services and credits

- Map data and tiles: [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), under the [tile usage policy](https://operations.osmfoundation.org/policies/tiles/).
- Geocoding: [Photon](https://github.com/komoot/photon), using the public Komoot demo for moderate personal use. Search is submitted explicitly rather than on every keystroke. The public service has no availability guarantee; use a dedicated instance for heavier traffic.
- Driving route previews use Valhalla after one-time coordinate-sharing consent, with toll and motorway avoidance. The service address is configurable; the default is https://valhalla1.openstreetmap.de. Routes are previews, not navigation instructions.
- Mapping library: [Leaflet](https://leafletjs.com/), BSD-2-Clause. Icons: [Lucide](https://lucide.dev/), ISC. The CoMaps mark is reused from this repository's `docs/badges/logo.svg`.

The browser sends search text and map centre to Photon, and driving stops to Valhalla. Searching along a route sends the place name or type and bounding boxes covering the trip to Photon. The distance slider filters results from 1 to 100 km away from the route. Selecting a place inserts it on the nearest leg and recalculates the trip.

The deployment CSP permits the explicitly requested `valhalla1.openstreetmap.de` demo host. It is selected by default and contacted only after consent. Add your own service origin to `connect-src` in `deploy/Caddyfile` before using it in production; keep this allowlist narrow. Route requests also require the service to allow the web origin through CORS.
