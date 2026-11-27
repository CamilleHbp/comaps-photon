# Maps web companion

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

People planning a trip in a desktop or mobile browser can look up an address, keep a place for later, share coordinates or preview a drive with several stops. A traveler with CoMaps installed can open a selected place in the native app.

## Product Purpose

Put place search, saved places and driving-route previews on one OpenStreetMap map. A person can move from a numbered result to its pin, inspect the address, and take an action on that same place.

## Operating Context

The companion runs in a browser or as an installed web app. English and French follow the browser preference, with an override in Settings. Saved places belong to the current browser origin. Visiting the companion under another hostname opens that origin’s collection.

## Capabilities and Constraints

- Explore sends an explicitly submitted query and map center to Photon. Route-stop fields offer place suggestions. Geolocation can supply the starting point when the person grants access.
- Before the first online route preview, the planner asks to share stop coordinates with Valhalla. Drivers can avoid tolls or motorways and search for places within a chosen distance of the route. Adding a result inserts it on the nearest leg and recalculates the preview.
- The app shell and saved places work offline. New tiles, Photon results and Valhalla previews depend on their services. Downloadable offline maps, offline place search and turn-by-turn guidance belong to the native app.

## Product Principles

- Keep the map and the list in agreement. A selected result, saved place and route destination should retain the same identity.
- Give people a chance to understand a coordinate-sharing request before sending it. Describe a calculated drive as a preview.
- Preserve useful work through back navigation, edits and recoverable network errors. Keep OpenStreetMap attribution visible as panels change.

## Accessibility & Inclusion

Support keyboard travel between search results, place details and route fields. Use accessible names for icon controls and announce search or routing feedback. Allow long addresses and service names to wrap, and keep the map usable beside an expanded list on a phone.
