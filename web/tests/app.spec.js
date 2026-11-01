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
 await page.route('https://router.project-osrm.org/**',r=>r.fulfill({json:{code:'Ok',routes:[{distance:5000,duration:600,geometry:{type:'LineString',coordinates:[[2.35,48.85],[2.2945,48.8584]]}}]}}));
 await page.goto('/#lat=48.8584&lon=2.2945&name=Tour+Eiffel');await page.locator('#route').click();await expect(page.locator('#route-summary')).toContainText('5 km');await page.locator('#clear-route').click();await expect(page.locator('#route-summary')).toBeEmpty();
 const manifest=await(await request.get('/manifest.webmanifest')).json();expect(manifest.display).toBe('standalone');for(const icon of manifest.icons)expect((await request.get(icon.src)).ok()).toBe(true);
 await page.evaluate(()=>navigator.serviceWorker.ready);const urls=await page.evaluate(async()=>{const cache=await caches.open((await caches.keys())[0]);return(await cache.keys()).map(r=>r.url);});expect(urls.every(u=>new URL(u).origin==='http://127.0.0.1:4173')).toBe(true);
});
test('map renders without script errors',async({page},testInfo)=>{
 const errors=[];page.on('pageerror',e=>errors.push(e.message));await page.goto('/');await expect(page.locator('#intro h2')).toHaveText('Make room for a detour.');await expect(page.locator('.leaflet-tile-loaded').first()).toBeVisible();await page.screenshot({path:`.impeccable/review/${testInfo.project.name}.png`});expect(errors).toEqual([]);
});
