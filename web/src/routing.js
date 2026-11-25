// Provider adapters deliberately have no UI, storage, or implicit provider fallback.
export const MAX_ROUTE_POINTS = 50;

export class RoutingError extends Error {
  constructor(code) { super(code); this.name = 'RoutingError'; this.code = code; }
}
const fail = code => { throw new RoutingError(code); };
const number = v => typeof v === 'number' && Number.isFinite(v) && v >= 0;
const warnings = object => object?.warnings !== undefined && (!Array.isArray(object.warnings) || object.warnings.length > 0);
export const validPoint = p => p && Number.isFinite(p.lat) && Number.isFinite(p.lon) && Math.abs(p.lat) <= 85 && Math.abs(p.lon) <= 180;

export function endpointUrl(value) {
  let url;
  try { url = new URL(value); } catch { fail('routeEndpointError'); }
  if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash || /[\s]/.test(value))
    fail('routeEndpointError');
  // Accept a service root or one of the action URLs copied from the demo.
  url.pathname = url.pathname.replace(/\/(route|isochrone|status)\/?$/, '').replace(/\/$/, '') + '/route';
  return url.href;
}

export function buildValhallaRequest({points, avoidTolls = false, avoidMotorways = false, avoidFerries = false,
  hardExclusions = false, heading, language = 'en'}) {
  if (!Array.isArray(points) || points.length < 2 || points.length > MAX_ROUTE_POINTS || !points.every(validPoint)) fail('routeCoordinatesError');
  if (points.some((p, i) => i && p.lat === points[i-1].lat && p.lon === points[i-1].lon)) fail('routeCoordinatesError');
  const locations = points.map(p => ({lat: p.lat, lon: p.lon, type: 'break'}));
  if (heading !== undefined) {
    if (!Number.isFinite(heading) || heading < 0 || heading >= 360) fail('routeCoordinatesError');
    locations[0].heading = heading;
  }
  const auto = {use_tolls: avoidTolls ? 0 : 0.5, use_highways: avoidMotorways ? 0 : 0.5, use_ferry: avoidFerries ? 0 : 0.5};
  if (hardExclusions) {
    if (avoidTolls) auto.exclude_tolls = true;
    if (avoidMotorways) auto.exclude_highways = true;
    if (avoidFerries) auto.exclude_ferries = true;
  }
  return {locations, costing: 'auto', costing_options: {auto}, units: 'kilometers',
    language: language.split(/[-_]/)[0] === 'fr' ? 'fr-FR' : 'en-US', shape_format: 'polyline6'};
}

export function decodePolyline6(encoded) {
  if (typeof encoded !== 'string' || encoded.length > 8000000) fail('routeInvalidResponse');
  let index = 0, lat = 0, lon = 0;
  const coordinates = [];
  function component() {
    let result = 0, shift = 0, byte;
    do {
      if (index >= encoded.length || shift > 30) fail('routeInvalidResponse');
      byte = encoded.charCodeAt(index++) - 63;
      if (byte < 0 || byte > 63) fail('routeInvalidResponse');
      result += (byte & 31) * 2 ** shift;
      shift += 5;
    } while (byte >= 32);
    return result % 2 ? -(Math.floor(result / 2) + 1) : result / 2;
  }
  while (index < encoded.length) {
    lat += component(); lon += component();
    if (!validPoint({lat: lat / 1e6, lon: lon / 1e6})) fail('routeInvalidResponse');
    coordinates.push([lon / 1e6, lat / 1e6]);
    if (coordinates.length > 500000) fail('routeInvalidResponse');
  }
  if (coordinates.length < 2) fail('routeInvalidResponse');
  return coordinates;
}

export function parseValhallaResponse(data, options = {}) {
  const trip = data?.trip;
  if (!trip || trip.status !== 0 || trip.units !== 'kilometers' || !Array.isArray(trip.legs) || !trip.legs.length || trip.legs.length > 49)
    fail('routeInvalidResponse');
  // A server can ignore exclusions and return warnings with HTTP 200.
  if (warnings(data) || warnings(trip)) fail('routeServerWarning');
  const checkSummary = summary => {
    if (!summary || !number(summary.time) || !number(summary.length)) fail('routeInvalidResponse');
    for (const [option, flag] of [['avoidTolls','has_toll'], ['avoidMotorways','has_highway'], ['avoidFerries','has_ferry']]) {
      if (options[option] && typeof summary[flag] !== 'boolean') fail('routeRestrictionUnknown');
      if (options[option] && summary[flag]) fail(option === 'avoidTolls' ? 'routeTollsRejected' : 'routeRestrictionRejected');
    }
  };
  checkSummary(trip.summary);
  const coordinates = [];
  const legEnds = [];
  let time = 0, length = 0;
  for (const leg of trip.legs) {
    checkSummary(leg.summary);
    if (warnings(leg)) fail('routeServerWarning');
    if (leg.maneuvers !== undefined && !Array.isArray(leg.maneuvers)) fail('routeInvalidResponse');
    // Check maneuver flags too, even if a summary contradicts them.
    if (options.avoidTolls && leg.maneuvers?.some(m => m.toll === true)) fail('routeTollsRejected');
    if (options.avoidMotorways && leg.maneuvers?.some(m => m.highway === true)) fail('routeRestrictionRejected');
    const shape = decodePolyline6(leg.shape);
    if (coordinates.length) {
      const end = coordinates.at(-1);
      if (end[0] !== shape[0][0] || end[1] !== shape[0][1]) fail('routeInvalidResponse');
      shape.shift();
    }
    for (const point of shape) coordinates.push(point);
    if (coordinates.length > 500000) fail('routeInvalidResponse');
    legEnds.push(coordinates.length - 1);
    time += leg.summary.time; length += leg.summary.length;
  }
  if (Math.abs(time - trip.summary.time) > Math.max(1, time * 0.001) || Math.abs(length - trip.summary.length) > Math.max(0.01, length * 0.001))
    fail('routeInvalidResponse');
  return {provider: 'Valhalla', distance: trip.summary.length * 1000, duration: trip.summary.time,
    legEnds, geometry: {type: 'LineString', coordinates}};
}

export async function calculateRoute({provider = 'valhalla', endpoint, consent, points, signal, ...options}, fetcher = fetch) {
  const request = buildValhallaRequest({points, ...options});
  let url, init = {signal, headers: {Accept: 'application/json'}};
  if (provider === 'valhalla') {
    if (!consent) fail('routeConsentRequired');
    url = endpointUrl(endpoint);
    init = {...init, method: 'POST', redirect: 'error', cache: 'no-store', credentials: 'omit',
      headers: {...init.headers, 'Content-Type': 'application/json', 'X-Client-Id': 'comaps-photon'}, body: JSON.stringify(request)};
  } else fail('routeInvalidResponse');
  const response = await fetcher(url, init);
  signal?.throwIfAborted();
  if (!response.ok) fail(response.status === 429 || response.status === 503 ? 'routeBusy' : 'routeServiceError');
  const reader = response.body?.getReader();
  if (!reader) fail('routeInvalidResponse');
  const decoder = new TextDecoder();
  let body = '', bytes = 0;
  try {
    while (true) {
      const {value, done} = await reader.read();
      if (done) break;
      bytes += value.byteLength;
      if (bytes > 16000000) { await reader.cancel(); fail('routeInvalidResponse'); }
      body += decoder.decode(value, {stream: true});
    }
    body += decoder.decode();
  } finally { reader.releaseLock(); }
  signal?.throwIfAborted();
  if (body.length > 16000000) fail('routeInvalidResponse');
  let data;
  try { data = JSON.parse(body); } catch { fail('routeInvalidResponse'); }
  const route = parseValhallaResponse(data, options);
  if (route.legEnds.length !== points.length - 1) fail('routeInvalidResponse');
  return route;
}
