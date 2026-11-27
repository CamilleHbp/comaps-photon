import {readFileSync, writeFileSync} from 'node:fs';
import {parseValhallaResponse} from '../src/routing.js';

// Offline inspection uses the production parser; it makes no network requests.
const [input, output, avoid = 'false', caseFile] = process.argv.slice(2);
if (!input || !output) throw Error('Usage: inspect-valhalla.mjs RESPONSE GEOJSON [AVOID_TOLLS] [CASE_JSON]');
try {
  const route = parseValhallaResponse(JSON.parse(readFileSync(input)), {avoidTolls: avoid === 'true'});
  const radians = d => d * Math.PI / 180;
  function meters([lon,lat], p) {
    const a = Math.sin(radians(lat-p.lat)/2)**2 + Math.cos(radians(lat))*Math.cos(radians(p.lat))*Math.sin(radians(lon-p.lon)/2)**2;
    return 6371000 * 2 * Math.asin(Math.sqrt(Math.min(1,a)));
  }
  const corridors = caseFile ? JSON.parse(readFileSync(caseFile)).evaluation_corridors || [] : [];
  const distances = Object.fromEntries(corridors.map(p => [p.name,
    Math.round(route.geometry.coordinates.reduce((distance, c) => Math.min(distance,meters(c,p)), Infinity))]));
  writeFileSync(output, JSON.stringify({type:'Feature',properties:{provider:route.provider,meters:route.distance,
    seconds:route.duration,attribution:'© OpenStreetMap contributors'},geometry:route.geometry}));
  console.log(JSON.stringify({adapter_accepted:true,geometry_points:route.geometry.coordinates.length,
    nearest_corridor_vertex_m:distances}));
} catch (error) {
  if (!error.code?.startsWith('route')) throw error;
  console.log(JSON.stringify({adapter_accepted:false,adapter_error:error.code}));
}
