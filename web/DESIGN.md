---
name: CoMaps-Photon web companion
description: A planning map with a warm paper panel, forest-green pins and numbered place results.
colors:
  green: "#315c45"
  paper: "#fffef8"
  ink: "#20392b"
  muted: "#5e6c60"
  line: "#dce1d6"
  field: "#eef1e9"
  focus: "#bb6b26"
typography:
  body: {fontFamily: "Avenir Next, Avenir, Segoe UI, sans-serif", fontSize: "14px", lineHeight: 1.6}
  title: {fontSize: "23px", fontWeight: 650, lineHeight: 1.2, letterSpacing: "-0.025em"}
  input: {fontSize: "1rem"}
  stop-label: {fontSize: "0.75rem", lineHeight: 1.4}
rounded:
  panel: "16px"
  field: "12px"
  action: "9px"
  chip: "8px"
spacing:
  small: "8px"
  gap: "12px"
  inset: "24px"
components:
  preview-route: {backgroundColor: "{colors.green}", textColor: "{colors.paper}", rounded: "{rounded.action}"}
  place-action: {backgroundColor: "#e9eee3", rounded: "{rounded.action}"}
  search-box: {backgroundColor: "{colors.field}", rounded: "{rounded.field}", typography: "{typography.input}"}
  active-tab: {textColor: "{colors.green}"}
  category-chip: {backgroundColor: "#eaf0e4", rounded: "{rounded.chip}"}
  panel: {backgroundColor: "{colors.paper}", rounded: "{rounded.panel}"}
---

# CoMaps-Photon web companion

## Overview

**Planning map**. A warm paper panel gathers the decisions about a place or a drive. The map stays visible beside or below it, so a person can match a result to its surroundings or check where a stop falls along a route.

- The same number identifies a search result in the list and on the map.
- Forest green connects selected tabs, pins and primary actions.
- Exploring, inspecting a place and planning a drive use the same panel.

## Colors

Use forest green for the active tab, route preview action and pin fill. Dark green ink carries names and headings. Muted green-gray carries addresses, coordinates and guidance. The paper surface gives text a consistent background across dense tiles. A light divider separates adjacent rows. Amber marks keyboard focus on standalone controls. [Stylesheet](src/style.css)

## Typography

The interface uses Avenir Next with system fallbacks. The place name leads the address. Result names and search input use a full rem, while supporting text sits a step lower. The route estimate pairs a large time value with a quieter distance on the same baseline. Use tabular numerals for coordinates and changing trip measurements.

## Layout

On desktop, the panel sits 24px from the top and left and is 390px wide. Location and zoom controls occupy the opposite edge. At 720px and below, the panel sits within 12px side margins and the map tools move toward the lower right. The browsing panel can occupy 55% of the viewport height and the planner 64%. A long mobile result list initially shows two rows and can expand to 80% of the viewport height. Keep the list scrollable and the map attribution visible.

## Elevation & Depth

One soft green-tinted shadow lifts the panel, map tools and route suggestions above the tiles. Borders and pale fills separate controls within the panel. Suggestions float above the active stop so the traveler can see which field they will fill. The short panel entrance animation is enabled when the person permits motion.

## Shapes

The main panel has the broadest corner radius. Search fields and map controls have smaller rounded corners, and category chips are tighter again. A search pin has a pointed tip, a paper rim and an upright number. The route diagram joins open circles for the start and intermediate stops to a filled square for the destination.

## Components

- **Explore, Saved and Directions.** A heavier label and underline identify the selected tab. Saved includes a count. The planner header makes room for Back and Settings. [Page structure](index.html)
- **Search field and categories.** A tinted field groups the input with its submit button and shows a green outline on focus within. Pale chips offer nearby categories. In the planner, each stop has its own label and suggestion list.
- **Result and place detail.** Align the number beside the name and address. Opening a result reveals details and actions. Keep the context-specific Back action reachable and restore keyboard focus when returning to the list.
- **Route preview.** The full-width forest-green button follows the stops, options and any required service disclosure. The result groups the estimate, ordered stops and edit action. Place actions of secondary importance use a pale fill.

## Do's and Don'ts

- Do keep result numbers identical in the list and on the pins.
- Do preserve a visible focus indicator and room for long place names.
- Don't obscure attribution when the panel or suggestion list expands.
- Don't use turn-by-turn navigation language for a driving preview.
