# Maps for Android

<!-- impeccable:product-schema 1 -->

## Platform

android

## Users

Walkers, cyclists and drivers use the map to find a place, choose a route and follow it in the field. A traveler can download a region before leaving coverage and use it for search and navigation during the trip.

## Product Purpose

Carry a detailed OpenStreetMap map on the device. A place found in search, selected on the map or reopened from a saved list can become a route endpoint. During navigation, the next maneuver, distance and voice guidance help the traveler follow the route.

## Capabilities and Constraints

- Downloaded maps support offline search and routing. Search distinguishes local CoMaps matches from online Photon matches. With Photon enabled, its server receives the query and map center and can see the device’s IP address. Privacy settings offer offline-only search.
- For driving, the fork supports a configurable Valhalla service. Online calculation sends the start, destination, intermediate stops and, when available, starting direction. CoMaps handles navigation. If calculation falls back to offline routing, the traveler can explicitly retry Valhalla for the journey.
- Saved lists group places and tracks. A traveler can show or hide a list on the map, filter lists, and import or export bookmarks and tracks. Outdoor map detail and elevation profiles support trips on foot or by bicycle.

## Product Principles

- Keep downloaded maps useful throughout a trip, including when a network service fails.
- Identify the source of search and routing results at the point of use. Explain what an online request shares.
- Carry the selected place into route planning. Keep the start, destination and intermediate stops distinguishable when the traveler changes the trip.

## Accessibility & Inclusion

The map exposes features for touch exploration and spoken feedback. Name icon-only actions, keep modal focus within the open surface and provide a reachable way back to the map. Use scalable text for place names, maneuver distances and search status. [Screen reader guidance](../docs/SCREEN_READERS.md)
