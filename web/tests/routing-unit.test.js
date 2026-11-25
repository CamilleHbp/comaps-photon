import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {buildValhallaRequest, parseValhallaResponse, decodePolyline6, endpointUrl, calculateRoute} from '../src/routing.js';

const fixture = JSON.parse(readFileSync(new URL('../../data/test_data/valhalla/route.json', import.meta.url)));
const fresh = () => structuredClone(fixture.response);
const rejects = (fn, code) => assert.throws(fn, e => e.code === code);

test('shared request contract and polyline6 coordinates', () => {
  assert.deepEqual(buildValhallaRequest(fixture.options), fixture.request);
  assert.deepEqual(decodePolyline6(fixture.response.trip.legs[0].shape), fixture.coordinates);
  const route = parseValhallaResponse(fresh(), fixture.options);
  assert.equal(route.distance, 300); assert.equal(route.duration, 180);
  assert.deepEqual(route.geometry.coordinates, fixture.coordinates);
});
test('avoidance fails closed on tolls, missing indicators and server warnings', () => {
  let r = fresh(); r.trip.summary.has_toll = true;
  rejects(() => parseValhallaResponse(r, fixture.options), 'routeTollsRejected');
  r = fresh(); delete r.trip.legs[0].summary.has_toll;
  rejects(() => parseValhallaResponse(r, fixture.options), 'routeRestrictionUnknown');
  r = fresh(); r.trip.legs[0].maneuvers[0].toll = true;
  rejects(() => parseValhallaResponse(r, fixture.options), 'routeTollsRejected');
  r = fresh(); r.warnings = [{code: 208}];
  rejects(() => parseValhallaResponse(r, fixture.options), 'routeServerWarning');
});
test('malformed geometry, nonfinite totals, disjoint legs and units are rejected', () => {
  for (const shape of ['', '?', '~~~~~~~?', '\u0000']) rejects(() => decodePolyline6(shape), 'routeInvalidResponse');
  let r=fresh();r.trip.summary.time=Infinity;
  rejects(()=>parseValhallaResponse(r),'routeInvalidResponse');
  r=fresh();r.trip.units='miles'; rejects(()=>parseValhallaResponse(r),'routeInvalidResponse');
  r=fresh();r.trip.legs.push(structuredClone(r.trip.legs[0]));
  rejects(()=>parseValhallaResponse(r),'routeInvalidResponse');
});
test('endpoint normalization never permits credentials, cleartext or query strings', () => {
  assert.equal(endpointUrl('https://valhalla1.openstreetmap.de/isochrone'),'https://valhalla1.openstreetmap.de/route');
  assert.equal(endpointUrl('https://example.org/valhalla/'),'https://example.org/valhalla/route');
  for(const endpoint of ['http://example.org','https://user:pass@example.org','https://example.org?token=secret','https://example.org/#x'])
    rejects(()=>endpointUrl(endpoint),'routeEndpointError');
});
test('motorways allowed use neutral preference; hard exclusions are opt-in', () => {
  assert.equal(buildValhallaRequest(fixture.options).costing_options.auto.use_highways,0.5);
  assert.deepEqual(buildValhallaRequest({...fixture.options,avoidMotorways:true,hardExclusions:true,heading:90}).costing_options.auto,
    {use_tolls:0,use_highways:0,use_ferry:0.5,exclude_tolls:true,exclude_highways:true});
});
test('no network without consent or for removed providers', async () => {
  let calls=0;const transport=()=>{calls++;throw Error('unexpected network');};
  await assert.rejects(calculateRoute({...fixture.options,provider:'valhalla',endpoint:'https://example.org'},transport),{code:'routeConsentRequired'});
  await assert.rejects(calculateRoute({...fixture.options,provider:'osrm'},transport),{code:'routeInvalidResponse'});
  assert.equal(calls,0);
});
test('POST request parity, throttling and abort after response', async () => {
  const options={...fixture.options,provider:'valhalla',endpoint:'https://example.org',consent:true};
  await calculateRoute(options,async(url,init)=>{
    assert.equal(url,'https://example.org/route');assert.equal(init.method,'POST');
    assert.equal(init.cache,'no-store');assert.equal(init.redirect,'error');
    assert.deepEqual(JSON.parse(init.body),fixture.request);
    return new Response(JSON.stringify(fresh()));
  });
  await assert.rejects(calculateRoute(options,async()=>new Response('',{status:429})),{code:'routeBusy'});
  const controller=new AbortController();
  await assert.rejects(calculateRoute({...options,signal:controller.signal},async()=>{
    controller.abort();return new Response(JSON.stringify(fresh()));
  }),{name:'AbortError'});
});
