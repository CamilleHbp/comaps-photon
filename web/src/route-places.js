import {validPoint} from './routing.js';

export const categories = {
  fuel: ['amenity','fuel'], hostels: ['tourism','hostel'], restaurants: ['amenity','restaurant'],
  cafes: ['amenity','cafe'], charging: ['amenity','charging_station'], toilets: ['amenity','toilets']
};

const metresPerDegree = Math.PI * 6371000 / 180;
const lonDelta = (a, b) => ((a - b + 540) % 360) - 180;
const longitudeScale = lat => metresPerDegree * Math.cos(lat * Math.PI / 180);
function length(a, b) {
  return Math.hypot(lonDelta(b[0], a[0]) * longitudeScale((a[1] + b[1]) / 2), (b[1] - a[1]) * metresPerDegree);
}

export function routeSegments(route) {
  const coords = route.geometry.coordinates;
  const segments = [];
  let start = 0, leg = 0;
  for (let i = 1; i < coords.length; i++) {
    const a = coords[i-1], b = coords[i], size = length(a, b);
    segments.push({a,b,leg,size,start});
    start += size;
    if (i === route.legEnds[leg]) leg++;
  }
  return segments;
}

function nearestSegment(point, segments) {
  let best = {offset:Infinity};
  const scale = longitudeScale(point.lat);
  for (const {a,b,leg,size,start} of segments) {
    const dx = lonDelta(b[0], a[0]) * scale, dy = (b[1] - a[1]) * metresPerDegree;
    const x = lonDelta(point.lon, a[0]) * scale, y = (point.lat - a[1]) * metresPerDegree;
    const squared = dx * dx + dy * dy;
    const fraction = squared ? Math.max(0, Math.min(1, (x * dx + y * dy) / squared)) : 0;
    const offset = Math.hypot(x - fraction * dx, y - fraction * dy);
    if (offset < best.offset) best = {offset, along:start + fraction * size, leg};
  }
  return best;
}

function searchBounds(segments, radius) {
  const first = segments[0].a;
  let west = first[0], east = west, south = first[1], north = south;
  for (const {b} of segments) {
    const lon = first[0] + lonDelta(b[0],first[0]);
    west = Math.min(west, lon); east = Math.max(east, lon);
    south = Math.min(south,b[1]); north = Math.max(north,b[1]);
  }
  const latPadding = radius / metresPerDegree;
  south = Math.max(-90, south - latPadding); north = Math.min(90, north + latPadding);
  const lonPadding = radius / longitudeScale(Math.max(Math.abs(south),Math.abs(north)));
  west -= lonPadding; east += lonPadding;
  // Photon expects ordinary west-to-east boxes on either side of the date line.
  if (east - west >= 360) return [[-180,south,180,north]];
  if (east > 180) return [[west,south,180,north],[-180,south,east-360,north]];
  if (west < -180) return [[west+360,south,180,north],[-180,south,east,north]];
  return [[west,south,east,north]];
}

export async function searchAlongRoute(segments, {category, query, radius, language, fallbackName, signal}) {
  const tagsForType = categories[category];
  const places = new Map();
  const last = segments.at(-1);
  const span = Math.max(10000, radius * 2, (last.start + last.size) / 8);
  const boxes = [];
  let first = 0;
  for (let i = 0; i < segments.length; i++) {
    const end = segments[i].start + segments[i].size;
    if (end - segments[first].start < span && i !== segments.length - 1) continue;
    boxes.push(...searchBounds(segments.slice(first, i+1), radius));
    first = i+1;
  }
  for (const bbox of boxes) {
    const params = new URLSearchParams({bbox:bbox.join(','), lang:language, limit:'100', dedupe:'0'});
    if (tagsForType) params.set('include', 'osm.' + tagsForType.join('.'));
    if (query) params.set('q', query);
    const response = await fetch(`https://photon.komoot.io/api/?${params}`, {
      credentials:'omit', cache:'no-store', signal:AbortSignal.any([signal,AbortSignal.timeout(15000)])});
    if (!response.ok) throw new Error('nearbyUnavailable');
    const data = await response.json();
    if (!Array.isArray(data.features)) throw new Error('nearbyUnavailable');
    for (const feature of data.features) {
      const tags = feature.properties;
      const [lon,lat] = feature.geometry?.coordinates || [];
      const point = {lat,lon};
      if (!validPoint(point) || !tags) continue;
      if (tagsForType && (tags.osm_key !== tagsForType[0] || tags.osm_value !== tagsForType[1])) continue;
      const nearest = nearestSegment(point,segments);
      if (nearest.offset > radius) continue;
      const label = tags.name || fallbackName;
      const address = [tags.housenumber,tags.street,tags.city].filter(Boolean).join(' ');
      const id = tags.osm_id ? `${tags.osm_type}:${tags.osm_id}` : `${lat},${lon},${label}`;
      places.set(id, {label,address,point,...nearest});
    }
  }
  return [...places.values()].sort((a,b) => a.along - b.along || a.offset - b.offset);
}
