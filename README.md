# Maps

Download a region before a trip to search streets, save places and navigate offline on Android. In a browser, you can plan a drive with several stops and share a place by its coordinates.

[Self-host the web map](web/README.md#hosting) · [Build Maps for Android](docs/INSTALL.md)

## Plan a drive

Search for a destination and save it if you’ll need it again. Open directions, choose a starting point and add stops in the order you want to visit them. The web planner can avoid tolls or motorways. To find somewhere for lunch along the way, search near the route and add a result as a stop.

<a href="docs/screenshots/along-route.png"><img src="docs/screenshots/along-route.png" width="640" alt="Driving route on an OpenStreetMap map of Paris with a place search alongside the route"></a>

*Adding a place inserts it on the nearest leg and recalculates the drive. Map data © OpenStreetMap contributors.*

## Take saved places with you

On Android, download the area while you have a connection. Search the offline map or use Photon for an online lookup. The layers panel controls which saved places appear. If you already use OsmAnd, the favorites importer checks for duplicates.

The browser keeps bookmarks on the device where you saved them. Share coordinates to open a place elsewhere. Web searches use Photon and route previews send stop coordinates to Valhalla with your permission. Use Android for downloaded maps and turn-by-turn directions.

Maps started as a fork of [CoMaps](https://codeberg.org/comaps/comaps), descended from Organic Maps and MAPS.ME. This project independently maintains the bilingual web planner, Android Photon search and OsmAnd favorites import. [Web guide](web/README.md) · [Build the native app](docs/INSTALL.md)

## Credits and licenses

The Android update checker uses the [Apache-licensed updater 1.0.6 source](https://github.com/CamilleHbp/android-updater/tree/v1.0.6).

The native code uses the [Apache License 2.0](LICENSE). See [NOTICE](NOTICE) and the [component credits](data/copyright.html) for the original authors, libraries and artwork. Map data comes from [OpenStreetMap contributors](https://www.openstreetmap.org/copyright) under the Open Database License.
