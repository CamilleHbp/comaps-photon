import {readFileSync} from 'node:fs';
const fixture=JSON.parse(readFileSync(new URL('../../data/test_data/valhalla/route.json',import.meta.url)));
import {test,expect} from '@playwright/test';
import {messages} from '../src/i18n.js';
const feature={type:'Feature',geometry:{type:'Point',coordinates:[2.2945,48.8584]},properties:{name:'Tour Eiffel',city:'Paris',country:'France'}};
test('localizations cover the same interface',()=>{expect(Object.keys(messages.fr).sort()).toEqual(Object.keys(messages.en).sort());});
test('search, select, save, reopen offline, and remove',async({page,context})=>{
 await page.route('https://photon.komoot.io/**',route=>route.fulfill({json:{features:[feature]}}));
 await page.goto('/');await page.locator('#query').fill('Eiffel');await page.locator('#search-form').press('Enter');
 await page.getByRole('button',{name:/Tour Eiffel/}).click();await page.getByRole('button',{name:'Save place',exact:true}).click();
 await expect(page.locator('#saved-count')).toHaveText('1');
 await page.evaluate(()=>navigator.serviceWorker.ready);await page.waitForFunction(()=>!!navigator.serviceWorker.controller);
 await context.setOffline(true);await page.reload();await expect(page.getByRole('heading',{name:'Tour Eiffel'})).toBeVisible();
 await page.locator('#saved-tab').click();await expect(page.locator('#results')).toContainText('Tour Eiffel');
 await page.getByRole('button',{name:/Tour Eiffel/}).click();await page.getByRole('button',{name:'Remove from saved'}).click();await expect(page.locator('#saved-count')).toHaveText('0');
});
test('language persists and content fits viewport',async({page})=>{
 await page.goto('/');await page.locator('#settings-toggle').click();await page.locator('#language').selectOption('fr');await expect(page.locator('html')).toHaveAttribute('lang','fr');await page.reload();await expect(page.locator('#query')).toHaveAttribute('placeholder','Où voulez-vous aller ?');
 expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true);
});
test('failed search recovers, unsafe names remain text',async({page})=>{
 let calls=0;await page.route('https://photon.komoot.io/**',r=>++calls===1?r.fulfill({status:429,body:''}):r.fulfill({json:{features:[{...feature,properties:{name:'<img src=x onerror=alert(1)>'}}]}}));
 await page.goto('/');await page.locator('#query').fill('test');await page.locator('#search-form').press('Enter');await expect(page.locator('#status')).toContainText('busy');await page.locator('#search-form').press('Enter');await expect(page.locator('#results')).toContainText('<img');await expect(page.locator('#results img')).toHaveCount(0);
});
test('route preview, manifest and offline cache boundaries',async({page,context,request})=>{
 await context.grantPermissions(['geolocation']);await context.setGeolocation({latitude:48.85,longitude:2.35});
 await page.route('https://valhalla1.openstreetmap.de/route',r=>r.fulfill({json:fixture.response}));
 await page.goto('/#lat=48.8584&lon=2.2945&name=Tour+Eiffel');await page.locator('#place-directions').click();await page.locator('#route').click();await expect(page.locator('#route-summary')).toContainText('0.3 km');await page.locator('#clear-route').click();await expect(page.locator('#route-summary')).toBeEmpty();
 const manifest=await(await request.get('/manifest.webmanifest')).json();expect(manifest.display).toBe('standalone');for(const icon of manifest.icons)expect((await request.get(icon.src)).ok()).toBe(true);
 await page.evaluate(()=>navigator.serviceWorker.ready);const urls=await page.evaluate(async()=>{const cache=await caches.open((await caches.keys())[0]);return(await cache.keys()).map(r=>r.url);});expect(urls.every(u=>new URL(u).origin==='http://127.0.0.1:4173')).toBe(true);
});
test('map renders without script errors',async({page},testInfo)=>{
 const errors=[];page.on('pageerror',e=>errors.push(e.message));await page.goto('/');await expect(page.locator('#intro h2')).toHaveText('Explore nearby');await expect(page.locator('.leaflet-tile-loaded').first()).toBeVisible();await page.screenshot({path:`.impeccable/review/${testInfo.project.name}.png`});expect(errors).toEqual([]);
});

test('browser history restores the saved place, route draft, and original result', async ({page}) => {
  await page.route('https://photon.komoot.io/**', route => route.fulfill({json:{features:[feature]}}));
  await page.goto('/');
  await page.locator('#query').fill('Eiffel');
  await page.locator('#query').press('Enter');
  const result = page.getByRole('button',{name:/Tour Eiffel/});
  await result.press('Enter');
  await expect(page.locator('#place-title')).toBeFocused();
  await page.locator('#save').press('Enter');
  await expect(page.locator('#save')).toBeFocused();
  await expect(page.locator('#place-feedback')).toHaveText('Tour Eiffel saved.');
  await page.locator('#saved-tab').click();
  await result.click();
  await page.locator('#place-directions').click();
  await page.locator('#route-origin').fill('48.85, 2.35');
  await page.goBack();
  await expect(page.locator('#place-directions')).toBeFocused();
  await expect(page.locator('#place')).toContainText('Paris, France');
  await page.goForward();
  await expect(page.locator('#route-origin')).toHaveValue('48.85, 2.35');
  await expect(page.locator('#route-destination')).toHaveValue(/Tour Eiffel/);
  await page.goBack();
  await page.reload();
  await expect(page.locator('#place')).toContainText('Paris, France');
  await page.locator('#back').click();
  await expect(result).toBeFocused();
  await expect(page.locator('#saved-tab')).toHaveAttribute('aria-pressed','true');
});

test('a failed search clears obsolete map pins and offers a retry', async ({page}) => {
  let attempts = 0;
  await page.route('https://photon.komoot.io/**', route => ++attempts === 2
    ? route.fulfill({status:429, body:''})
    : route.fulfill({json:{features:[feature]}}));
  await page.goto('/');
  await page.locator('#query').fill('Eiffel');
  await page.locator('#query').press('Enter');
  await expect(page.locator('.pin')).toHaveCount(1);
  await page.locator('#query').fill('Paris');
  await expect(page.locator('.pin')).toHaveCount(0);
  await expect(page.locator('#results')).toBeEmpty();
  await page.locator('#query').press('Enter');
  await expect(page.locator('#status')).toContainText('busy');
  await expect(page.locator('#results-title')).toHaveText('Search results');
  await expect(page.locator('.pin')).toHaveCount(0);
  await page.locator('#retry-search').click();
  await expect(page.locator('#results-title')).toHaveText('1 place found');
  await expect(page.locator('#results button')).toBeFocused();
  await expect(page.locator('.pin')).toHaveCount(1);
});

test('long results stay accessible on the map and in the list', async ({page,isMobile}) => {
  const features=Array.from({length:5},(_,i)=>({...feature,properties:{...feature.properties,name:'Paris '+(i+1)}}));
  await page.route('https://photon.komoot.io/**',r=>r.fulfill({json:{features}}));
  await page.goto('/');
  await page.locator('#query').fill('Paris');
  await page.locator('#query').press('Enter');
  await expect(page.locator('#results button').first()).toBeFocused();
  await expect(page.locator('.pin')).toHaveCount(5);
  if (isMobile) {
    await expect(page.locator('#results button:visible')).toHaveCount(2);
    await page.getByRole('button',{name:'Show all 5 places'}).click();
    await expect(page.locator('#results button').nth(2)).toBeFocused();
  }
  await page.locator('#results button').last().click();
  await page.locator('#back').click();
  await expect(page.locator('#results button').last()).toBeFocused();
});
