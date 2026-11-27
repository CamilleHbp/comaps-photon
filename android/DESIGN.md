---
name: CoMaps-Photon for Android
description: A field map with legible maneuvers, native place sheets and distinct day and night themes.
colors:
  bar: "#588157"
  bar-night: "#37653F"
  accent: "#37653F"
  accent-night: "#7CBC7B"
  card: "#FFFFFF"
  card-night: "#3C4044"
  field: "#DEE5D9"
  field-night: "#424940"
typography:
  toolbar: {fontSize: "20sp"}
  body: {fontSize: "16sp"}
  supporting: {fontSize: "14sp"}
  next-turn: {fontSize: "24sp"}
rounded:
  search: "8dp"
spacing:
  quarter: "4dp"
  half: "8dp"
  base: "16dp"
  section: "24dp"
components:
  route-action: {backgroundColor: "{colors.accent}", typography: "{typography.supporting}"}
  secondary-action: {textColor: "{colors.accent}", typography: "{typography.body}"}
  list-search: {backgroundColor: "{colors.field}", rounded: "{rounded.search}"}
  search-section: {typography: "{typography.body}"}
  layer-filter: {typography: "{typography.supporting}"}
---

# CoMaps-Photon for Android

## Overview

**Field map**. A driver approaching a turn can read the arrow, distance and road name together while the route ahead stays visible. When someone stops to inspect a place, the sheet gives them its name, details and actions against a solid background.

- Navigation groups related instructions in olive bars above the route.
- Place details open over the map and expand as more information is needed.
- Day and night resources assign contrast separately to controls and map overlays.

## Colors

Olive anchors the toolbar and navigation chrome. Forest green marks primary actions in daylight. At night the bar darkens, the action accent lightens and cards become charcoal. Resolve colors through the current theme so text, icons, ripples and fields change together. The map has its own colors for roads, trails, routes and points of interest. [Day resources](app/src/main/res/values/colors.xml) · [Night resources](app/src/main/res/values-night/colors.xml)

## Typography

Use Android text appearances and scalable sizes. In search, a bold source heading precedes the smaller status and privacy disclosure. In navigation, give the next maneuver and its number more prominence than the unit or street label. Keep place names readable across translations. [Type scale](app/src/main/res/values/font_sizes.xml)

## Layout

Keep the map visible around the active task. Place details rise from the bottom of the portrait map. The route planner pairs From and To with controls for choosing or swapping the endpoints. Use start and end spacing for localized layouts. The landscape search header puts source and status on one line and gives the disclosure the width below.

## Elevation & Depth

Raised map controls stay distinguishable over both street labels and terrain. The app bar has a dedicated elevation (4dp), and navigation uses a separate level (6dp). Inside a sheet, use spacing and dividers to group rows. [Dimensions](app/src/main/res/values/dimens.xml)

## Shapes

Material buttons and filter chips carry their own shape and pressed state. The saved-list search field uses a softly rounded rectangle, a surface-variant fill and a separate clear target. Circular controls suit standalone map actions.

## Components

- **Directions and secondary actions.** The place sheet gives Directions a filled Material button with an icon before its label. Outlined secondary buttons use the action color for text. [Place route action](app/src/main/res/layout/place_route_action.xml)
- **Search source header.** Keep “Online · Photon” and “Offline · CoMaps” attached to their results. Place loading, empty and unavailable feedback in the source section. The status is a polite accessibility live region. [Search header](app/src/main/res/layout/item_search_section.xml)
- **Saved-list search and filters.** The search and clear controls have a minimum height of 48dp. Checkable Material chips also reserve a 48dp touch target. Separate the list title and visibility count from the filtering controls. [List header](app/src/main/res/layout/item_layers_header.xml) · [Filter chip](app/src/main/res/layout/item_layer_filter.xml)

## Do's and Don'ts

- Do preserve a readable view of the upcoming route around maneuver controls.
- Do check source labels, icons and action contrast against both native themes.
- Don't present an online failure as an empty offline search.
- Don't shrink the clear or filter touch target to the size of its glyph.
