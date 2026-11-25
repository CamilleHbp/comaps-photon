import {test, expect} from '@playwright/test';

function encodeSegment(a,b) {
  function component(delta) {
    let n = delta < 0 ? -2*delta-1 : 2*delta, encoded = '';
    while (n >= 32) { encoded += String.fromCharCode((n%32)+95); n = Math.floor(n/32); }
    return encoded + String.fromCharCode(n+63);
  }
  const lat = Math.round(a.lat*1e6), lon = Math.round(a.lon*1e6);
  return component(lat)+component(lon)+component(Math.round(b.lat*1e6)-lat)+component(Math.round(b.lon*1e6)-lon);
}

test('stops can be reordered and a hostel is inserted on the correct leg', async ({page},testInfo) => {
  const trips = [], searches = [];
  await page.route('https://valhalla1.openstreetmap.de/route', req => {
    const points = req.request().postDataJSON().locations;
    trips.push(points.map(p => p.lon));
    const legs = points.slice(1).map((p,i) => ({summary:{length:1,time:120},shape:encodeSegment(points[i],p)}));
    return req.fulfill({json:{trip:{status:0,units:'kilometers',summary:{length:legs.length,time:legs.length*120},legs}}});
  });
  await page.route('https://photon.komoot.io/api/**include=*', req => {
    searches.push(new URL(req.request().url()).searchParams);
    return req.fulfill({json:{features:[
      {geometry:{coordinates:[2.315,48.8505]},properties:{osm_key:'tourism',osm_value:'hostel',name:'Canal Hostel'}},
      {geometry:{coordinates:[2.315,49]},properties:{osm_key:'tourism',osm_value:'hostel',name:'Far away'}}
    ]}});
  });
  await page.route('https://photon.komoot.io/api/**q=*', req => req.fulfill({json:{features:[{
    geometry:{coordinates:[2.32,48.85]},properties:{name:'Market Square'}
  }]}}));
  await page.goto('/');
  await page.locator('#directions-tab').click();
  await page.locator('#route-origin').fill('48.85, 2.30');
  await page.locator('#route-destination').fill('48.85, 2.33');
  await page.getByRole('button',{name:'Add stop',exact:true}).click();
  await page.getByLabel('Stop 1',{exact:true}).fill('Market');
  await page.getByRole('option',{name:'Market Square'}).click();
  await page.locator('#add-stop').click();
  await page.getByLabel('Stop 2',{exact:true}).fill('48.85, 2.31');
  await page.getByRole('button',{name:'Move Stop 2 earlier'}).click();
  await page.locator('#route').click();
  await expect(page.locator('#route-summary')).toContainText('Market Square');
  expect(trips).toEqual([[2.30,2.31,2.32,2.33]]);
  await page.locator('#route-places summary').click();
  expect(searches).toEqual([]);
  await page.getByRole('button',{name:'Hostels',exact:true}).click();
  const add = page.getByRole('button',{name:'Add Canal Hostel as a stop',exact:true});
  await expect(add).toBeVisible();
  expect(searches[0].get('include')).toBe('osm.tourism.hostel');
  const [west,,east] = searches[0].get('bbox').split(',').map(Number);
  expect(west).toBeLessThan(2.30); expect(east).toBeGreaterThan(2.33);
  await expect(page.locator('#along-results')).not.toContainText('Far away');
  await page.screenshot({path:`.impeccable/review/${testInfo.project.name}-along-route.png`});
  await add.click();
  await expect(page.locator('.trip-stops > div')).toHaveCount(5);
  expect(trips[1]).toEqual([2.30,2.31,2.315,2.32,2.33]);
  await page.locator('#edit-route').click();
  await page.getByRole('button',{name:'Remove Stop 2'}).click();
  await page.locator('#route').click();
  await expect(page.locator('.trip-stops > div')).toHaveCount(4);
  expect(trips[2]).toEqual(trips[0]);
});

test('failed place search leaves the trip available for another category',async ({page}) => {
  await page.route('https://valhalla1.openstreetmap.de/route', req => req.fulfill({json:{trip:{
    status:0,units:'kilometers',summary:{length:120,time:3600},legs:[{summary:{length:120,time:3600},
    shape:encodeSegment({lat:48,lon:2},{lat:48,lon:3}) + '??'}]
  }}}));
  let query;
  await page.route('https://photon.komoot.io/api/**include=*', req => {
    query = new URL(req.request().url()).searchParams;
    return query.get('include') === 'osm.amenity.fuel' ? req.fulfill({status:503}) : req.fulfill({json:{features:[]}});
  });
  await page.goto('/'); await page.locator('#directions-tab').click();
  await page.locator('#route-origin').fill('48,2'); await page.locator('#route-destination').fill('48,3');
  await page.locator('#route').click(); await page.locator('#route-places summary').click();
  await page.getByRole('button',{name:'Gas stations'}).click();
  await expect(page.locator('#along-status')).toContainText('unavailable');
  await expect(page.locator('#route-summary')).toContainText('120 km');
  const firstBox = query.get('bbox');
  const slider = page.getByRole('slider',{name:'Maximum distance from route'});
  await slider.focus(); await slider.press('End');
  await expect(page.locator('#radius-value')).toHaveText('100 km');
  await expect.poll(()=>query.get('bbox')).not.toBe(firstBox);
  await page.getByRole('button',{name:'Hostels'}).click();
  await expect(page.locator('#along-status')).toContainText('No matching places');
});

test('named search spans the whole trip and respects the selected radius', async ({page}) => {
  await page.route('https://valhalla1.openstreetmap.de/route', req => {
    const points = req.request().postDataJSON().locations;
    const legs = points.slice(1).map((end,i) => ({summary:{length:50,time:3000},shape:encodeSegment(points[i],end)}));
    return req.fulfill({json:{trip:{status:0,units:'kilometers',summary:{length:legs.length*50,time:legs.length*3000},legs}}});
  });
  const boxes = [];
  await page.route('https://photon.komoot.io/api/**q=*', req => {
    const params = new URL(req.request().url()).searchParams;
    expect(params.get('q')).toBe('Harbour');
    const box = params.get('bbox').split(',').map(Number);
    boxes.push(box);
    const features = box[2] >= 3.98 ? [{geometry:{coordinates:[3.98,48.04]},
      properties:{name:'Harbour Inn',osm_type:'N',osm_id:45}}] : [];
    return req.fulfill({json:{features}});
  });
  await page.goto('/'); await page.locator('#directions-tab').click();
  await page.locator('#route-origin').fill('48,2'); await page.locator('#route-destination').fill('48,4');
  await page.locator('#add-stop').click(); await page.getByLabel('Stop 1',{exact:true}).fill('48,3');
  await page.locator('#route').click(); await page.locator('#route-places summary').click();
  await page.locator('#along-query').fill('Harbour'); await page.locator('#along-query').press('Enter');
  await expect(page.locator('#along-status')).toContainText('No matching places');
  expect(boxes.some(box => box[0] < 2)).toBeTruthy();
  expect(boxes.some(box => box[2] > 4)).toBeTruthy();
  await page.locator('#place-radius').focus(); await page.locator('#place-radius').press('End');
  await expect(page.locator('#along-results li')).toHaveCount(1);
  await expect(page.locator('#along-results')).toContainText('4.4 km from route');
  await page.getByRole('button',{name:'Add Harbour Inn as a stop'}).click();
  await expect(page.locator('.trip-stops > div').nth(2)).toHaveText('Harbour Inn');
});
