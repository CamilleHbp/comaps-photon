import {test, expect} from '@playwright/test';
import {readFileSync} from 'node:fs';
const fixture=JSON.parse(readFileSync(new URL('../../data/test_data/valhalla/route.json',import.meta.url)));

async function configure(page) {
  await page.goto('/#lat=48.852&lon=2.351&name=Destination');
  await page.locator('#place-directions').click();
  await page.locator('#settings-toggle').click();
  await page.locator('#route-endpoint').fill('https://valhalla1.openstreetmap.de/isochrone');
  await page.locator('#route-consent').check();
  await page.locator('#settings-toggle').click();
  await page.locator('#route-origin').fill('48.85, 2.35');
  await page.locator('#route-options summary').click();
  await page.locator('#avoid-tolls').check();
}

test('Valhalla uses editable points and survives language changes',async({page,context},testInfo)=>{
  let calls=0;
  await page.route('https://valhalla1.openstreetmap.de/route',r=>{
    calls++;const body=r.request().postDataJSON();
    expect(body.locations).toEqual(fixture.request.locations);
    expect(body.costing_options.auto.use_tolls).toBe(0);
    return r.fulfill({json:fixture.response});
  });
  await configure(page);
  expect(calls).toBe(0);
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');
  await expect(page.locator('#route-summary')).toContainText('0.3 km');
  await page.screenshot({path:`.impeccable/review/${testInfo.project.name}-valhalla.png`,fullPage:true});
  await page.locator('#settings-toggle').click();
  await page.locator('#language').selectOption('fr');
  await page.locator('#settings-toggle').click();
  await expect(page.locator('#route-summary')).toContainText('0,3 km');
  await context.setOffline(true);
  await page.locator('#edit-route').click();
  await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).toContainText('connexion');
  expect(calls).toBe(1);
});

test('changing an endpoint revokes consent and invalidates the preview',async({page})=>{
  let calls=0;await page.route('https://valhalla1.openstreetmap.de/**',r=>{calls++;return r.fulfill({json:fixture.response});});
  await configure(page);await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');
  await page.locator('#settings-toggle').click();
  await page.locator('#route-endpoint').fill('https://other.example');
  await expect(page.locator('#route-consent')).not.toBeChecked();
  await page.locator('#settings-toggle').click();
  await expect(page.locator('#route-disclosure')).toContainText('https://other.example');
  await expect(page.locator('#route')).toHaveText('Allow and preview route');
  await expect(page.locator('#route-summary')).toBeEmpty();expect(calls).toBe(1);
});

test('toll rejection never falls back to OSRM',async({page})=>{
  const response=structuredClone(fixture.response);response.trip.summary.has_toll=true;
  let osrmCalls=0;
  await page.route('https://router.project-osrm.org/**',r=>{osrmCalls++;return r.abort();});
  await page.route('https://valhalla1.openstreetmap.de/**',r=>r.fulfill({json:response}));
  await configure(page);await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).toContainText('contains tolls');
  await expect(page.locator('#route-summary')).toBeEmpty();expect(osrmCalls).toBe(0);
});

test('editing an endpoint cancels an in-flight route and blocks stale results',async({page})=>{
  let release;const ready=new Promise(resolve=>{release=resolve;});
  let requested;const started=new Promise(resolve=>{requested=resolve;});
  await page.route('https://valhalla1.openstreetmap.de/**',async r=>{requested();await ready;await r.fulfill({json:fixture.response}).catch(()=>{});});
  await configure(page);await page.locator('#route').click();await started;
  await page.locator('#route-destination').fill('44.84, -0.58');release();
  await expect(page.locator('#route')).toBeEnabled();
  await expect(page.locator('#route-summary')).toBeEmpty();
});

test('denied geolocation keeps manual origin available without sending a request',async({page})=>{
  await page.addInitScript(()=>{
    Object.defineProperty(navigator,'geolocation',{value:{getCurrentPosition:(_ok,fail)=>fail({code:1})}});
  });
  let calls=0;
  await page.route('https://valhalla1.openstreetmap.de/**',r=>{calls++;return r.fulfill({json:fixture.response});});
  await configure(page);
  await page.locator('#route-my-location').click();
  await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).not.toBeEmpty();
  await expect(page.locator('#route')).toBeEnabled();
  expect(calls).toBe(0);
  await page.locator('#route-origin').fill('48.85, 2.35');
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');
  expect(calls).toBe(1);
});

test('city names resolve for both endpoints and stay readable',async({page})=>{
  const queries=[];
  await page.route('https://photon.komoot.io/api/**',r=>{
    const q=new URL(r.request().url()).searchParams.get('q');queries.push(q);
    return r.fulfill({json:{features:[{geometry:{coordinates:q==='Paris'?[2.35,48.85]:[2.351,48.852]},properties:{name:q,country:'France'}}]}});
  });
  await page.route('https://valhalla1.openstreetmap.de/route',r=>{
    expect(r.request().postDataJSON().locations).toEqual(fixture.request.locations);
    return r.fulfill({json:fixture.response});
  });
  await configure(page);
  await expect(page.locator('#route-destination')).toHaveValue('Destination');
  await page.locator('#route-origin').fill('Paris');
  await page.locator('#route-origin-suggestions').getByRole('option').click();
  await page.locator('#route-destination').fill('Notre-Dame');
  await page.locator('#route-destination-suggestions').getByRole('option').click();
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');
  await page.locator('#edit-route').click();
  await expect(page.locator('#route-origin')).toHaveValue('Paris, France');
  await expect(page.locator('#route-destination')).toHaveValue('Notre-Dame, France');
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');
  expect(queries).toEqual(['Paris','Notre-Dame']);
});

test('unknown city shows a useful error without routing',async({page})=>{
  let calls=0;
  await page.route('https://photon.komoot.io/api/**',r=>r.fulfill({json:{features:[]}}));
  await page.route('https://valhalla1.openstreetmap.de/**',r=>{calls++;return r.abort();});
  await configure(page);await page.locator('#route-origin').fill('Unknown place');
  await expect(page.locator('.route-search-status').first()).toContainText('Place not found');
  await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).toContainText('Choose a place');
  expect(calls).toBe(0);
});

test('editing a city cancels its pending lookup',async({page})=>{
  let release, requested;
  const ready=new Promise(resolve=>{release=resolve;});
  const started=new Promise(resolve=>{requested=resolve;});
  let calls=0;
  await page.route('https://photon.komoot.io/api/**',async r=>{
    requested();await ready;
    await r.fulfill({json:{features:[{geometry:{coordinates:[2.35,48.85]},properties:{name:'Old city'}}]}}).catch(()=>{});
  });
  await page.route('https://valhalla1.openstreetmap.de/**',r=>{calls++;return r.abort();});
  await configure(page);await page.locator('#route-origin').fill('Old city');
  await started;
  await page.locator('#route-origin').fill('New city');release();
  await expect(page.locator('#route')).toBeEnabled();
  await expect(page.locator('#route-origin')).toHaveValue('New city');
  await expect(page.locator('#route-summary')).toBeEmpty();expect(calls).toBe(0);
});

test('ambiguous names require an explicit choice and keyboard selects the second match',async({page})=>{
  let calls=0;
  await page.route('https://photon.komoot.io/api/**',r=>r.fulfill({json:{features:[
    {geometry:{coordinates:[1,45]},properties:{name:'Paris',country:'Wrong place'}},
    {geometry:{coordinates:[2.35,48.85]},properties:{name:'Paris',country:'France'}}
  ]}}));
  await page.route('https://valhalla1.openstreetmap.de/route',r=>{
    calls++;expect(r.request().postDataJSON().locations).toEqual(fixture.request.locations);
    return r.fulfill({json:fixture.response});
  });
  await configure(page);await page.locator('#route-origin').fill('Paris');
  await expect(page.locator('#route-origin-suggestions').getByRole('option')).toHaveCount(2);
  await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).toContainText('Choose a place');expect(calls).toBe(0);
  await page.locator('#route-origin').fill('Paris ');
  await expect(page.locator('#route-origin-suggestions').getByRole('option')).toHaveCount(2);
  await page.locator('#route-origin').press('ArrowDown');
  await page.locator('#route-origin').press('ArrowDown');
  await page.locator('#route-origin').press('Enter');
  await expect(page.locator('#route-origin')).toHaveValue('Paris, France');
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');expect(calls).toBe(1);
  await page.locator('#edit-route').click();
  await page.locator('#route-origin').fill('Paris');
  await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).toContainText('Choose a place');expect(calls).toBe(1);
});

test('suggestions recover after service errors and support French and offline status',async({page,context},testInfo)=>{
  let fail=true;
  await page.route('https://photon.komoot.io/api/**',r=> fail?r.fulfill({status:429}):r.fulfill({json:{features:[{geometry:{coordinates:[2.35,48.85]},properties:{name:'Paris',country:'France'}}]}}));
  await configure(page);await page.locator('#route-origin').fill('Paris');
  await expect(page.locator('.route-search-status').first()).toContainText('busy');
  await expect(page.locator('#route-origin')).toHaveAttribute('aria-expanded','false');
  fail=false;await page.locator('#route-origin').fill('Paris France');
  await expect(page.locator('#route-origin-suggestions').getByRole('option')).toHaveText('Paris, France');
  await page.screenshot({path:`.impeccable/review/${testInfo.project.name}-autocomplete.png`,fullPage:true});
  await page.locator('#route-origin').press('Escape');
  await expect(page.locator('#route-origin')).toHaveAttribute('aria-expanded','false');
  await page.locator('#settings-toggle').click();await page.locator('#language').selectOption('fr');await page.locator('#settings-toggle').click();
  await page.locator('#route-origin').fill('Paris');
  await expect(page.locator('.route-search-status').first()).toContainText('Choisissez');
  await context.setOffline(true);await page.locator('#route-origin').fill('Lyon');
  await expect(page.locator('#route-origin')).toHaveAttribute('aria-expanded','false');
  await expect(page.locator('.route-search-status').first()).not.toBeEmpty();
});

test('suggestions float without moving planner controls',async({page})=>{
  await page.route('https://photon.komoot.io/api/**',r=>r.fulfill({json:{features:Array.from({length:6},(_,i)=>({geometry:{coordinates:[2.35,48.85]},properties:{name:`Paris ${i}`,country:'France'}}))}}));
  await configure(page);await page.locator('#route-origin').focus();
  const before=await page.locator('#route-destination').evaluate(el=>el.offsetTop);
  await page.locator('#route-origin').fill('Paris');
  await expect(page.locator('#route-origin-suggestions').getByRole('option')).toHaveCount(6);
  expect(await page.locator('#route-destination').evaluate(el=>el.offsetTop)).toBe(before);
  expect(await page.locator('.route-autocomplete-popup').first().evaluate(el=>getComputedStyle(el).position)).toBe('fixed');
  await page.locator('#route-origin-suggestions').getByRole('option').nth(1).click();
  await expect(page.locator('#route-origin')).toHaveValue('Paris 1, France');
});

test('avoidance offers inline Valhalla consent and calculates without opening settings',async({page})=>{
  let calls=0;
  await page.route('https://valhalla1.openstreetmap.de/route',r=>{
    calls++;expect(r.request().postDataJSON().costing_options.auto.use_tolls).toBe(0);
    return r.fulfill({json:fixture.response});
  });
  await page.goto('/#lat=48.852&lon=2.351&name=Destination');
  await page.locator('#place-directions').click();
  await page.locator('#route-origin').fill('48.85, 2.35');
  await page.locator('#route-options summary').click();
  await page.locator('#avoid-tolls').check();
  await expect(page.locator('#route-disclosure')).toContainText('allow Valhalla');expect(calls).toBe(0);
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');expect(calls).toBe(1);
  await expect(page.locator('#avoid-tolls')).toBeChecked();
  await expect(page.locator('#settings')).toBeHidden();
});

test('opt-in persists with Valhalla as the only routing service',async({page})=>{
  let calls=0;
  await page.route('https://valhalla1.openstreetmap.de/route',r=>{calls++;return r.fulfill({json:fixture.response});});
  await page.goto('/#lat=48.852&lon=2.351&name=Destination');
  await page.locator('#place-directions').click();
  await page.locator('#settings-toggle').click();
  await page.locator('#route-endpoint').fill('https://valhalla1.openstreetmap.de');
  await page.locator('#route-consent').check();
  await expect(page.locator('#route-provider')).toHaveCount(0);
  await page.reload();await page.locator('#place-directions').click();await page.locator('#settings-toggle').click();
  await expect(page.locator('#route-provider')).toHaveCount(0);
  await expect(page.locator('#route-consent')).toBeChecked();
  await expect(page.locator('#route-provider')).toHaveCount(0);
  await page.locator('#settings-toggle').click();
  await page.locator('#route-origin').fill('48.85, 2.35');
  await page.locator('#route-options summary').click();
  await page.locator('#avoid-tolls').check();
  await expect(page.locator('#route-disclosure')).toBeHidden();
  expect(calls).toBe(0);
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');expect(calls).toBe(1);
});

test('revoked consent is never reused for avoidance',async({page})=>{
  await configure(page);
  await page.locator('#settings-toggle').click();await page.locator('#route-consent').uncheck();
  await page.locator('#settings-toggle').click();
  await expect(page.locator('#route')).toHaveText('Allow and preview route');
  await expect(page.locator('#route-disclosure')).toBeVisible();
  await expect(page.locator('#route-summary')).toBeEmpty();
});

test('old OSRM preferences migrate to Valhalla without granting consent',async({page})=>{
  await page.addInitScript(()=>localStorage.setItem('comaps.routingProvider',JSON.stringify('osrm')));
  let calls=0;await page.route('https://valhalla1.openstreetmap.de/route',r=>{calls++;return r.fulfill({json:fixture.response});});
  await page.goto('/#lat=48.852&lon=2.351&name=Destination');
  await page.locator('#place-directions').click();
  await expect(page.locator('#place')).not.toContainText('OSRM');
  await expect(page.locator('#place')).not.toContainText('Enter a city or address');
  await page.locator('#route-origin').fill('48.85, 2.35');
  await expect(page.locator('#route-disclosure')).toBeVisible();expect(calls).toBe(0);
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Valhalla');expect(calls).toBe(1);
});

test('Directions opens directly and retries Valhalla with the same stops after an outage', async ({page}) => {
  const calls = [];
  await page.route('https://valhalla1.openstreetmap.de/route', route => {
    calls.push(route.request().postDataJSON());
    return calls.length === 1 ? route.fulfill({status:503}) : route.fulfill({json:fixture.response});
  });
  await page.goto('/');
  await page.getByRole('button',{name:'Directions'}).click();
  await page.locator('#route-origin').fill('48.85, 2.35');
  await page.locator('#route-destination').fill('48.852, 2.351');
  await expect(page.locator('#route-options')).not.toHaveAttribute('open', '');
  await expect(page.locator('#route')).toBeInViewport({ratio:1});
  await page.locator('#route').click();
  await expect(page.locator('#route-feedback')).toContainText('busy');
  await page.getByRole('button',{name:'Try again'}).click();
  await expect(page.locator('#route-summary')).toContainText('3 min');
  expect(calls[0]).toEqual(calls[1]);
  await page.locator('#edit-route').click();
  await expect(page.locator('#route-destination')).toHaveValue('48.852, 2.351');
  await page.locator('#planner-back').click();
  await expect(page.locator('#directions-tab')).toBeFocused();
});
