import {attachPlaceAutocomplete} from './place-autocomplete.js';
import L from 'leaflet';
import {categories, routeSegments, searchAlongRoute} from './route-places.js';
import {createIcons, Settings2, Search, ArrowRight, Compass, Bookmark, Coffee, Trees, Utensils, MapPin, LocateFixed, Plus, Minus, ArrowLeft, BookmarkCheck, Share2, Route, ExternalLink, SlidersHorizontal, ChevronDown, Car, Pencil, ChevronUp, X} from 'lucide';
const icons = {Settings2, Search, ArrowRight, Compass, Bookmark, Coffee, Trees, Utensils, MapPin, LocateFixed, Plus, Minus, ArrowLeft, BookmarkCheck, Share2, Route, ExternalLink, SlidersHorizontal, ChevronDown, Car, Pencil, ChevronUp, X};
import {messages, resolveLanguage} from './i18n.js';
import './style.css';
import {calculateRoute, endpointUrl, RoutingError, validPoint, MAX_ROUTE_POINTS} from './routing.js';

const $ = (id) => document.getElementById(id);
const read = (key, fallback) => { try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; } };
const write = (key, value) => { try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch { toast(t('storageError')); return false; } };
let preference = read('comaps.language', 'auto');
let lang = resolveLanguage(preference);
const t = (key) => messages[lang][key] || messages.en[key] || key;
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const validPlace = p => p && Number.isFinite(p.lat) && Number.isFinite(p.lon) && Math.abs(p.lat) <= 90 && Math.abs(p.lon) <= 180 && typeof p.name === 'string';
let saved = read('comaps.saved', []); saved = Array.isArray(saved) ? saved.filter(validPlace) : [];
let results = [], selected = null, tab = 'explore', controller, routeController, installPrompt, routeLayer, locationMarker, toastTimer, routeData;
let statusKey = '', hasSearched = false, exploreQuery = '';
let listExpanded = false;
let routingEndpoint = read('comaps.routingEndpoint', '') || 'https://valhalla1.openstreetmap.de';
let routingConsent = read('comaps.routingConsent', '') === routingEndpoint && !!routingEndpoint;
let avoidTolls = false, avoidMotorways = false;
const origin = {id:'origin', label:'', point:null};
const destination = {id:'destination', label:'', point:null};
let stops = [], nextStopId = 1;
const itinerary = () => [origin, ...stops, destination];
let closeSuggestions = [];
let plannerOpen = false;
let settingsOpen = false;
let routeMessage = '';
let editingRoute = false;
let retryRoute = false;

const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
const map = L.map('map', {zoomControl:false}).setView([48.8566,2.3522], 13);
const markers = L.layerGroup().addTo(map);
const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom:19, attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>', crossOrigin:true}).addTo(map);
L.control.scale({imperial:false,position:'bottomleft'}).addTo(map);
let tileWarning = false;
tiles.on('tileerror', () => { if (navigator.onLine && !tileWarning) { tileWarning = true; toast(t('mapError')); } });
function iconsRefresh() { createIcons({icons, attrs:{'aria-hidden':'true'}}); }
function toast(message) { clearTimeout(toastTimer); $('toast').textContent = message; $('toast').hidden = false; toastTimer = setTimeout(() => $('toast').hidden = true, 5500); }
function connection() { document.body.classList.toggle('offline', !navigator.onLine); $('connection-text').textContent = t(navigator.onLine ? 'online' : 'offline'); $('offline-banner').hidden = navigator.onLine; }
function translate() {
 document.documentElement.lang = lang;
 document.title = t('appName');
 document.querySelector('link[rel="manifest"]').href = lang === 'fr' ? '/manifest-fr.webmanifest' : '/manifest.webmanifest';
 document.querySelectorAll('[data-i18n]').forEach(el => el.textContent = t(el.dataset.i18n));
 document.querySelectorAll('[data-label]').forEach(el => {el.setAttribute('aria-label', t(el.dataset.label));el.title=t(el.dataset.label);});
 document.querySelectorAll('[data-placeholder]').forEach(el => el.placeholder = t(el.dataset.placeholder));
 $('map').setAttribute('aria-label',t('map')); document.querySelector('.panel').setAttribute('aria-label',t('explore')); document.querySelector('.tabs').setAttribute('aria-label',t('places')); $('place').setAttribute('aria-label',t('details'));
 $('route-endpoint').value = routingEndpoint; $('route-consent').checked = routingConsent;
 resetRoutePlaces();
 $('language').value = preference; connection(); renderList(); if(selected) renderPlace();
 if(plannerOpen) bindRouteFields();
 renderPlanner(); iconsRefresh();
}
function placeName(p) { return p.pin ? t('droppedPin') : p.name || t('untitled'); }
function key(p) { return `${p.lat.toFixed(6)},${p.lon.toFixed(6)}`; }
function isSaved(p) { return saved.some(s => key(s) === key(p)); }
function pin(p, index='') {
 const marker = L.marker([p.lat,p.lon], {icon:L.divIcon({className:'pin',html:`<div class="pin-inner"><span>${index}</span></div>`,iconSize:[30,38],iconAnchor:[15,36]}),title:placeName(p),alt:placeName(p)}).addTo(markers);
 marker.on('click', () => selectPlace(p)); return marker;
}
function renderList() {
 const list = tab === 'saved' ? saved : results;
 updateListSize(list.length);
 $('saved-count').textContent = saved.length;
 $('results').replaceChildren();
 $('intro').hidden = tab === 'saved' || hasSearched || !!selected;
 $('results-header').hidden = tab !== 'saved' && !hasSearched;
 const countText = results.length === 1 ? t('foundOne') : t('foundCount').replace('{n}', results.length);
 $('results-title').textContent = tab === 'saved' ? t('savedTitle') : results.length ? countText : t('searchResults');
 $('clear').hidden = tab === 'saved';
 $('status').textContent = tab === 'saved' ? (!saved.length ? t('savedEmptyHint') : '') : (statusKey ? t(statusKey) : '');
 $('retry-search').hidden = tab !== 'explore' || !['busy','searchError','offlineSearch'].includes(statusKey);
 $('query').placeholder = t(tab === 'saved' ? 'mapSearch' : 'searchPlaceholder');
 list.forEach((p, i) => { const li = document.createElement('li'); const button = document.createElement('button'); button.className='result'; button.innerHTML=`<span class="result-number">${i+1}</span><span><strong>${esc(placeName(p))}</strong><small>${esc(p.address || `${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}`)}</small></span>`;button.onclick=()=>selectPlace(p);li.append(button);$('results').append(li); });
}
function updateListSize(count) {
 if (count <= 2) listExpanded=false;
 const panel=document.querySelector('.panel');
 panel.classList.toggle('has-long-list',count>2);
 panel.classList.toggle('expanded-list',listExpanded);
 $('list-size').hidden=count<=2;
 $('list-size').setAttribute('aria-expanded',String(listExpanded));
 $('list-size').textContent=listExpanded ? t('showFewerPlaces') : t('showAllPlaces').replace('{n}',count);
}
$('list-size').onclick=()=>{
 listExpanded=!listExpanded;
 updateListSize((tab==='saved' ? saved : results).length);
 if (listExpanded) $('results').children[2].firstElementChild.focus();
 else $('results').scrollTop=0;
};
function showTab(next) {
 controller?.abort();
 if (statusKey === 'searching') { statusKey=''; hasSearched=false; }
 tab=next; selected=null; listExpanded=false; closePlanner(); settingsOpen=false; updatePanel();
 $('query').value = next === 'saved' ? '' : exploreQuery;
 for(const name of ['explore','saved']) {$(name+'-tab').classList.toggle('active',tab===name);$(name+'-tab').setAttribute('aria-pressed',String(tab===name));}
 markers.clearLayers(); (tab === 'saved' ? saved : results).forEach((p,i)=>pin(p,i+1)); renderList();
}
function selectPlace(p, move=true) {
 navigate(() => displayPlace(p, move));
}
function displayPlace(p, move=true) {
 controller?.abort();
 closePlanner(); selected=p; settingsOpen=false; updatePanel(); markers.clearLayers(); pin(p);
 if(move) map.setView([p.lat,p.lon], Math.max(map.getZoom(),15),{animate:false});
 renderPlace();
 if(move) map.panBy(innerWidth>720?[-207,0]:[0,-document.querySelector('.panel').getBoundingClientRect().height/2],{animate:false});
 $('place-title').focus({preventScroll:true});
}
function renderPlace() {
 const p=selected; if(!p)return;
 $('place').innerHTML=`
   <button class="back" id="back"><i data-lucide="arrow-left"></i>${t(tab === 'saved' ? 'fromSaved' : hasSearched ? 'fromResults' : 'fromMap')}</button>
   <h2 id="place-title" tabindex="-1">${esc(placeName(p))}</h2>
   ${p.address ? `<p class="place-address">${esc(p.address)}</p>` : ''}
   <p class="coords">${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}</p>
   <div class="actions">
     <button id="place-directions" class="primary wide"><i data-lucide="route"></i>${t('directions')}</button>
     <button id="save" class="${isSaved(p)?'is-saved':''}"><i data-lucide="${isSaved(p)?'bookmark-check':'bookmark'}"></i>${t(isSaved(p)?'remove':'save')}</button>
     <button id="share"><i data-lucide="share-2"></i>${t('share')}</button>
     <a class="wide native-place" href="comaps://map?v=1&ll=${p.lat},${p.lon}&n=${encodeURIComponent(placeName(p))}"><i data-lucide="external-link"></i>${t('openNative')}</a>
   </div>`;
 $('place-directions').onclick=()=>navigate(openPlanner);
 $('back').onclick=goBack;
 $('save').onclick=()=>{
   const removing = isSaved(p);
   const next = removing ? saved.filter(s=>key(s)!==key(p)) : [...saved,p];
   if (!write('comaps.saved',next)) return;
   saved=next; renderPlace(); renderList();
   $('save').focus({preventScroll:true});
   $('place-feedback').textContent = t(removing ? 'removedNotice' : 'savedNotice').replace('{name}',placeName(p));
 };
 $('share').onclick=async()=>{try{if(navigator.share){await navigator.share({title:placeName(p),url:location.href});}else{await navigator.clipboard.writeText(location.href);toast(t('copied'));}}catch(e){if(e.name!=='AbortError')toast(t('copyError'));}};
 iconsRefresh();
}

function updatePanel() {
 const planning = plannerOpen && !settingsOpen;
 $('planner').hidden = !planning;
 $('settings').hidden = !settingsOpen;
 $('place').hidden = settingsOpen || plannerOpen || !selected;
 $('browse').hidden = settingsOpen || plannerOpen || !!selected;
 $('settings-toggle').setAttribute('aria-expanded', String(settingsOpen));
 document.querySelector('.panel').classList.toggle('is-planning', planning);
 document.querySelector('.panel').classList.toggle('is-place', !$('place').hidden);
 document.querySelector('.panel').scrollTop = 0;
}

function openPlanner() {
 plannerOpen = true;
 settingsOpen = false;
 controller?.abort();
 stops = [];
 destination.label = selected ? (selected.pin ? `${selected.lat}, ${selected.lon}` :
   [placeName(selected),selected.address].filter(Boolean).join(', ')) : '';
 destination.point = selected ? {lat:selected.lat, lon:selected.lon} : null;
 updatePanel();
 bindRouteFields();
 renderPlanner();
 $('planner-title').focus();
}

function closePlanner() {
 plannerOpen = false;
 closeSuggestions.forEach(close => close.destroy());
 closeSuggestions = [];
 clearRoute();
}

function bindRouteFields() {
 closeSuggestions.forEach(close => close.destroy());
 document.querySelectorAll('.via-stop').forEach(row => row.remove());
 const lastRow = $('route-destination').parentElement;
 stops.forEach((stop,index) => {
   const row = document.createElement('div');
   row.className = 'route-stop via-stop';
   const label = t('stopNumber').replace('{n}', index + 1);
   row.innerHTML = `<label for="route-${stop.id}">${label}</label>
     <input id="route-${stop.id}" placeholder="${t('destinationHint')}" maxlength="500">
     <div class="stop-tools"></div>`;
   function control(icon, action, onClick) {
     const button = document.createElement('button');
     button.type = 'button'; button.className = 'icon-button';
     button.title = t(action).replace('{stop}', label);
     button.setAttribute('aria-label', button.title);
     button.innerHTML = `<i data-lucide="${icon}"></i>`;
     button.onclick = onClick;
     row.lastElementChild.append(button);
   }
   function move(to) {
     stops.splice(index, 1);
     stops.splice(to, 0, stop);
     refreshStops(stop.id);
   }
   if (index > 0) control('chevron-up','moveEarlier',() => move(index-1));
   if (index < stops.length-1) control('chevron-down','moveLater',() => move(index+1));
   control('x','removeStop',() => {
     stops.splice(index, 1); refreshStops(stops[index]?.id || 'destination');
   });
   lastRow.before(row);
 });
 closeSuggestions = itinerary().map(stop => {
   const input = $(`route-${stop.id}`);
   input.value = stop.label;
   function update(value, item) {
     stop.label = value;
     stop.point = item?.point || null;
     clearRoute();
     renderPlanner();
   }
   return attachPlaceAutocomplete(input, {language: lang, t,
     isSelected: () => !!stop.point && stop.label === input.value.trim(),
     onEdit: value => update(value, null),
     onSelect: item => update(item.label, item)
   });
 });
 iconsRefresh();
}

function refreshStops(focusId) {
 clearRoute();
 bindRouteFields();
 renderPlanner();
 $(`route-${focusId}`).focus();
}

function newStop(label='', point=null) {
 return {id:`stop-${nextStopId++}`, label, point};
}

function renderPlanner() {
 const completed = !!routeData && !editingRoute;
 $('route-form').hidden = completed;
 $('add-stop').disabled = itinerary().length >= MAX_ROUTE_POINTS;
 $('route-places').hidden = !completed || $('add-stop').disabled;
 $('route-feedback').textContent = routeMessage ? t(routeMessage) : '';
 const needsConsent = !routingConsent;
 $('route-disclosure').hidden = !needsConsent;
 $('route-disclosure').textContent = t('routeInlineConsent').replace('{endpoint}', routingEndpoint);
 let submitLabel = 'previewRoute';
 if (retryRoute) submitLabel = 'retryRoute';
 if (needsConsent) submitLabel = 'allowPreview';
 if (routeController) submitLabel = 'routing';
 $('route').querySelector('span').textContent = t(submitLabel);
 $('route').disabled = !!routeController;
 $('route-options-label').textContent = [avoidTolls && t('avoidTolls'), avoidMotorways && t('avoidMotorways')].filter(Boolean).join(' · ') || t('routeOptions');
 $('route-summary').replaceChildren();
 if (!completed) return;
 const minutes = Math.max(1, Math.round(routeData.duration / 60));
 const duration = minutes >= 60 ? [`${Math.floor(minutes / 60)} ${t('hours')}`, minutes % 60 && `${minutes % 60} ${t('minutes')}`].filter(Boolean).join(' ') : `${minutes} ${t('minutes')}`;
 const distance = (routeData.distance / 1000).toLocaleString(lang, {maximumFractionDigits:1});
 $('route-summary').innerHTML = `
   <div class="trip-estimate" tabindex="-1"><i data-lucide="car"></i><strong>${duration}</strong><span>${distance} ${t('distance')}</span></div>
   <div class="trip-stops">${itinerary().map(stop => `<div>${esc(stop.label || t('myLocation'))}</div>`).join('')}</div>
   <p class="trip-source">${t('routePreviewBy')} ${esc(routeData.provider)}</p>
   <div class="trip-actions"><button id="edit-route"><i data-lucide="pencil"></i>${t('editRoute')}</button><button id="clear-route" class="text-button">${t('clearRoute')}</button></div>`;
 $('edit-route').onclick = () => { resetRoutePlaces(); editingRoute = true; renderPlanner(); $('route-origin').focus(); };
 $('clear-route').onclick = () => { clearRoute(); renderPlanner(); $('planner-title').focus(); };
 iconsRefresh();
}

function clearRoute() {
 for (const input of document.querySelectorAll('#route-form [aria-invalid]')) {
   input.removeAttribute('aria-invalid');
   input.removeAttribute('aria-describedby');
 }
 resetRoutePlaces();
 routeController?.abort();
 routeController = null;
 if (routeLayer) {map.removeLayer(routeLayer); markers.addTo(map);}
 routeLayer = null;
 routeData = null;
 routeMessage = '';
 editingRoute = false;
 retryRoute = false;
}

let segments = [], nearbyRequest, foundPlaces = [], shownPlaces = 0, activeCategory = '';
const nearbyMarkers = L.layerGroup();
function clearPlaceResults() {
 nearbyRequest?.abort();
 nearbyRequest = null;
 nearbyMarkers.clearLayers();
 nearbyMarkers.remove();
 foundPlaces = [];
 shownPlaces = 0;
 $('along-results').replaceChildren();
 $('along-status').textContent = '';
 $('more-places').hidden = true;
}

function resetRoutePlaces() {
 clearPlaceResults();
 segments = [];
 activeCategory = '';
 $('route-places').open = false;
 $('along-query').value = '';
}

const km = metres => (metres / 1000).toLocaleString(lang,{maximumFractionDigits:1});
function openRoutePlaces() {
 if (!routeData || editingRoute) return;
 segments = routeSegments(routeData);
 const buttons = Object.keys(categories).map(category => {
   const button = document.createElement('button');
   button.type = 'button'; button.dataset.category = category;
   button.setAttribute('aria-pressed','false');
   button.textContent = t(category);
   button.onclick = () => findPlaces(category === activeCategory ? '' : category);
   return button;
 });
 $('place-categories').replaceChildren(...buttons);
}

async function findPlaces(category) {
 clearPlaceResults();
 const query = $('along-query').value.trim();
 activeCategory = category;
 for (const button of $('place-categories').children)
   button.setAttribute('aria-pressed', String(button.dataset.category === category));
 if (!routeData || (!category && !query)) return;
 if (!segments.length) segments = routeSegments(routeData);
 if (!segments.length) return;
 const request = new AbortController();
 nearbyRequest = request;
 try {
   if (!navigator.onLine) { $('along-status').textContent = t('nearbyOffline'); return; }
   $('along-status').textContent = t('searching');
   const places = await searchAlongRoute(segments, {category, query, language:lang,
     radius:Number($('place-radius').value) * 1000, fallbackName:category ? t(category) : query, signal:request.signal});
   if (request.signal.aborted) return;
   foundPlaces = places.filter(place => !routeData.points.some(stop =>
     Math.abs(stop.lat-place.point.lat) < 0.00003 && Math.abs(stop.lon-place.point.lon) < 0.00003));
   $('along-status').textContent = foundPlaces.length ? t('placesFound').replace('{n}',foundPlaces.length) : t('noPlacesAlong');
   showMorePlaces();
   ($('along-results').firstElementChild || $('along-status')).scrollIntoView({block:'nearest'});
 } catch {
   if (!request.signal.aborted) $('along-status').textContent = t('nearbyUnavailable');
 } finally {
   if (nearbyRequest === request) nearbyRequest = null;
 }
}

function addFoundPlace(place) {
 if (!routeData || itinerary().length >= MAX_ROUTE_POINTS) return;
 const start = routeData.points[0];
 stops.splice(place.leg, 0, newStop(place.label, place.point));
 bindRouteFields();
 drive(start);
}

function showMorePlaces() {
 const end = Math.min(shownPlaces + 12, foundPlaces.length);
 for (let i = shownPlaces; i < end; i++) {
   const place = foundPlaces[i];
   const li = document.createElement('li');
   const button = document.createElement('button');
   button.className = 'result';
   button.setAttribute('aria-label', t('addPlace').replace('{name}', place.label));
   const progress = t('tripProgress').replace('{km}',km(place.along));
   const offset = t('fromRoute').replace('{km}',km(place.offset));
   const detail = [place.address,offset,progress].filter(Boolean).join(' · ');
   button.innerHTML = `<span class="result-number">${i+1}</span><span><strong>${esc(place.label)}</strong><small>${esc(detail)}</small></span><i data-lucide="plus"></i>`;
   button.onclick = () => addFoundPlace(place);
   li.append(button); $('along-results').append(li);
   const popup = document.createElement('button');
   popup.className = 'stop-action';
   popup.textContent = t('addPlace').replace('{name}',place.label);
   popup.onclick = () => addFoundPlace(place);
   L.marker([place.point.lat,place.point.lon], {
     icon:L.divIcon({className:'along-pin',html:String(i+1),iconSize:[26,26]}),
     title:place.label, alt:place.label
   }).bindPopup(popup).addTo(nearbyMarkers);
 }
 shownPlaces = end;
 nearbyMarkers.addTo(map);
 $('more-places').hidden = end >= foundPlaces.length;
 iconsRefresh();
}

$('route-places').ontoggle = () => {
 if ($('route-places').open) openRoutePlaces();
 else resetRoutePlaces();
};
$('along-form').onsubmit = event => { event.preventDefault(); findPlaces(activeCategory); };
$('along-query').oninput = clearPlaceResults;
$('along-query').onkeydown = event => {
 if (event.key === 'Enter' && !event.isComposing) {
   event.preventDefault();
   $('along-form').requestSubmit();
 }
};
$('place-radius').oninput = () => {
 $('radius-value').textContent = $('place-radius').value + ' km';
 clearPlaceResults();
};
$('place-radius').onchange = () => findPlaces(activeCategory);
$('more-places').onclick = showMorePlaces;

function locate(){return new Promise((resolve,reject)=>{if(!navigator.geolocation)return reject(new RoutingError('routeLocationError'));navigator.geolocation.getCurrentPosition(p=>resolve(p.coords),reject,{enableHighAccuracy:true,timeout:15000,maximumAge:60000});});}
function resolveRoutePlace(stop) {
 const query=stop.label.trim();
 if(!query)throw new RoutingError(stop === destination ? 'destinationRequired' : 'stopRequired');
 if(stop.point)return stop.point;
 // Numeric coordinate pairs remain supported alongside place names and addresses.
 if(/^[+\-\d.\s,]+$/.test(query)) {
  const parts=query.split(',').map(x=>x.trim());
  const point={lat:Number(parts[0]),lon:Number(parts[1])};
  if(parts.length!==2 || parts.some(x=>!x) || !validPoint(point))throw new RoutingError('routeCoordinatesError');
  return point;
 }
 throw new RoutingError('routeChoosePlace');
}
function drawRoute() {
 map.removeLayer(markers);
 routeLayer = L.featureGroup().addTo(map);
 L.geoJSON(routeData.geometry, {style:{color:'#315c45', weight:6, opacity:.9}}).addTo(routeLayer);
 for (const [index,point] of routeData.points.entries())
   L.circleMarker([point.lat,point.lon], {radius:6, color:'#20392b', weight:2, fillColor:index ? '#315c45' : '#fffef8', fillOpacity:1}).addTo(routeLayer);
}
function fitRoute() {
 const panel = document.querySelector('.panel').getBoundingClientRect();
 map.fitBounds(routeLayer.getBounds(), {animate:!reduced, paddingTopLeft:innerWidth>720 ? [panel.right+20,36] : [20,panel.bottom+16], paddingBottomRight:[65,40]});
}
async function drive(previousStart) {
 clearRoute();
 closeSuggestions.forEach(close => close());
 const abort = new AbortController();
 routeController = abort;
 let fieldToCorrect;
 function readStop(stop) {
   fieldToCorrect=$('route-'+stop.id);
   return resolveRoutePlace(stop);
 }
 try {
   if (!navigator.onLine) throw new RoutingError('routeOffline');
   endpointUrl(routingEndpoint);
   const remaining = [...stops, destination].map(readStop);
   let start = origin.label.trim() ? readStop(origin) : previousStart;
   if (!start) {
     fieldToCorrect=$('route-origin');
     routeMessage = 'locating'; renderPlanner();
     const position = await locate();
     start = {lat: position.latitude, lon: position.longitude};
   }
   abort.signal.throwIfAborted();
   const points = [start, ...remaining];
   const duplicate=points.findIndex((p,i)=>i && p.lat===points[i-1].lat && p.lon===points[i-1].lon);
   if (!validPoint(start) || duplicate !== -1) {
     fieldToCorrect=$('route-'+(duplicate !== -1 ? itinerary()[duplicate].id : 'origin'));
     throw new RoutingError('routeCoordinatesError');
   }
   fieldToCorrect=null;
   if (!routingConsent) {
     routingConsent = true;
     $('route-consent').checked = true;
     write('comaps.routingConsent', routingEndpoint);
   }
   routeMessage = 'routing'; renderPlanner();
   const result = await calculateRoute({endpoint:routingEndpoint, consent:routingConsent, avoidTolls, avoidMotorways,
     language:lang, points, signal:AbortSignal.any([abort.signal, AbortSignal.timeout(15000)])});
   abort.signal.throwIfAborted();
   routeMessage = '';
   routeData = {...result, points};
   routeController = null;
   drawRoute();
   renderPlanner();
   fitRoute();
   $('route-summary').firstElementChild.focus();
 } catch (error) {
   if (abort.signal.aborted) return;
   retryRoute = true;
   routeMessage = error instanceof RoutingError ? error.code : [1,2,3].includes(error.code) ? 'routeLocationError' :
     error.name === 'TimeoutError' ? 'routeTimeout' : 'routeServiceError';
 } finally {
   if (routeController === abort) {
     routeController = null;
     renderPlanner();
     if (fieldToCorrect) {
       fieldToCorrect.setAttribute('aria-invalid','true');
       fieldToCorrect.setAttribute('aria-describedby','route-feedback');
       fieldToCorrect.focus();
     } else {
       $('route').focus();
     }
   }
 }
}
async function search(){
 const query=$('query').value.trim();
 const trigger=document.activeElement;
 if (!query) { $('query').focus(); return; }
 if (selected || plannerOpen || settingsOpen || tab !== 'explore') navigate(()=>showTab('explore'));
 else showTab('explore');
 exploreQuery=query; $('query').value=query; hasSearched=true; results=[]; markers.clearLayers();
 if(!navigator.onLine){statusKey='offlineSearch';renderList();return;}
 statusKey='searching';renderList();controller=new AbortController();const current=controller;const center=map.getCenter();const url=new URL('https://photon.komoot.io/api/');url.search=new URLSearchParams({q:query,limit:'8',lang,lat:center.lat,lon:center.lng});
 try{const response=await fetch(url,{signal:AbortSignal.any([current.signal,AbortSignal.timeout(12000)])});if(!response.ok)throw new Error(response.status===429?'busy':'searchError');const data=await response.json();if(current.signal.aborted)return;results=(data.features||[]).map(f=>{const p=f.properties||{};return {name:p.name||[p.street,p.housenumber].filter(Boolean).join(' ')||p.city||t('untitled'),address:[p.street&&[p.street,p.housenumber].filter(Boolean).join(' '),p.postcode,p.city||p.town||p.village,p.country].filter(Boolean).join(', '),lon:f.geometry?.coordinates?.[0],lat:f.geometry?.coordinates?.[1]};}).filter(validPlace);statusKey=results.length?'':'noResults';markers.clearLayers();results.forEach((p,i)=>pin(p,i+1));if(results.length)map.fitBounds(L.latLngBounds(results.map(p=>[p.lat,p.lon])),{maxZoom:15,paddingTopLeft:innerWidth>720?[440,40]:[30,innerHeight*.56],paddingBottomRight:[70,50]});}
 catch(e){if(current.signal.aborted)return;statusKey=e.message==='busy'?'busy':'searchError';}renderList();
 const stillInSearch=document.activeElement===trigger || document.activeElement===document.body;
 if (results.length) {
   $('place-feedback').textContent=$('results-title').textContent;
   if (stillInSearch) $('results').firstElementChild.firstElementChild.focus();
 } else if (stillInSearch && document.activeElement===document.body) {
   ($('retry-search').hidden ? $('query') : $('retry-search')).focus();
 }
 history.replaceState(captureView(),'',viewUrl());
}
$('search-form').onsubmit=e=>{e.preventDefault();search();};
$('query').oninput=()=>{
 if (tab !== 'explore') return;
 exploreQuery=$('query').value;
 controller?.abort();
 if (hasSearched) {
   statusKey=''; hasSearched=false; results=[]; markers.clearLayers(); renderList();
   $('place-feedback').textContent='';
   history.replaceState(captureView(),'',viewUrl());
 }
};
$('clear').onclick=()=>{
 hasSearched=false; statusKey=''; results=[]; exploreQuery=''; showTab('explore'); $('query').focus();
};
$('retry-search').onclick=search;
for (const name of ['explore','saved']) {
 $(name+'-tab').onclick=()=>{
   if (!selected && !settingsOpen && !plannerOpen && tab===name) return;
   navigate(()=>showTab(name));
 };
}
$('settings-toggle').onclick=()=>{
 if (settingsOpen) {goBack();return;}
 navigate(()=>{
   settingsOpen=true;
   resetRoutePlaces(); closeSuggestions.forEach(close=>close());
   if (routeController) {clearRoute(); renderPlanner();}
   updatePanel(); $('settings-title').focus();
 });
};
$('directions-tab').onclick = ()=>navigate(openPlanner);
$('planner-back').onclick = goBack;
$('route-form').onsubmit = event => {event.preventDefault(); if (!routeController) drive();};
$('add-stop').onclick = () => {
 if (itinerary().length >= MAX_ROUTE_POINTS) return;
 const stop = newStop(); stops.push(stop); refreshStops(stop.id);
};
$('route-my-location').onclick = () => {
 clearRoute(); closeSuggestions[0]?.();
 origin.label = ''; origin.point = null; $('route-origin').value = '';
 routeMessage = 'locationSelected'; renderPlanner();
};
$('avoid-tolls').onchange = e => {avoidTolls=e.target.checked; clearRoute(); renderPlanner();};
$('avoid-motorways').onchange = e => {avoidMotorways=e.target.checked; clearRoute(); renderPlanner();};
function routingSettingsChanged() {
 clearRoute();
 const next=$('route-endpoint').value.trim();
 if(next!==routingEndpoint){routingConsent=false;$('route-consent').checked=false;}
 routingEndpoint=next;
 write('comaps.routingEndpoint',routingEndpoint);
 write('comaps.routingConsent',routingConsent?routingEndpoint:'');
 renderPlanner();
}
$('route-endpoint').oninput=routingSettingsChanged;
$('route-consent').onchange=()=>{routingConsent=$('route-consent').checked;routingSettingsChanged();};
$('language').onchange=()=>{preference=$('language').value;write('comaps.language',preference);lang=resolveLanguage(preference);translate();};
window.addEventListener('languagechange',()=>{if(preference==='auto'){lang=resolveLanguage(preference);translate();}});
for(const button of document.querySelectorAll('[data-query]'))button.onclick=()=>{$('query').value=t(button.dataset.query);search();};
$('zoom-in').onclick=()=>map.zoomIn();$('zoom-out').onclick=()=>map.zoomOut();
$('locate').onclick=async()=>{const button=$('locate');button.disabled=true;toast(t('locating'));try{const c=await locate();if(locationMarker)map.removeLayer(locationMarker);locationMarker=L.circleMarker([c.latitude,c.longitude],{radius:8,color:'#fff',weight:3,fillColor:'#315c45',fillOpacity:1}).addTo(map);map.setView([c.latitude,c.longitude],16);$('toast').hidden=true;}catch{toast(t('locationError'));}finally{button.disabled=false;}};
map.on('click',e=>selectPlace({name:'',pin:true,lat:e.latlng.lat,lon:((e.latlng.lng+180)%360+360)%360-180,address:''},false));
window.addEventListener('online',()=>{connection();renderPlanner();tileWarning=false;tiles.redraw();});window.addEventListener('offline',()=>{connection(); renderPlanner();});
window.addEventListener('beforeinstallprompt',e=>{e.preventDefault();installPrompt=e;$('install').textContent=t('installReady');});
$('install').onclick=async()=>{if(installPrompt){await installPrompt.prompt();installPrompt=null;}else toast(t('installHelp'));};
if('serviceWorker'in navigator){navigator.serviceWorker.register('/sw.js').then(reg=>{reg.addEventListener('updatefound',()=>{reg.installing?.addEventListener('statechange',e=>{if(e.target.state==='installed'&&navigator.serviceWorker.controller)toast(t('appUpdated'));});});}).catch(()=>{});}
// A history entry describes one panel over the map. Keep completed geometry in
// this document only; navigation and refresh never submit a route request.
let entryId = history.state?.comapsId || crypto.randomUUID();
let parentEntry = history.state?.parentEntry || null;
const sessionViews = new Map();

function captureView() {
 const center = map.getCenter();
 return {
   comapsId:entryId, parentEntry, tab, selected, plannerOpen, settingsOpen,
   results, hasSearched, listExpanded, statusKey:statusKey === 'searching' ? '' : statusKey,
   exploreQuery, center:[center.lat,center.lng], zoom:map.getZoom(),
   panelScroll:document.querySelector('.panel').scrollTop,
   route:plannerOpen ? {origin, destination, stops, avoidTolls, avoidMotorways,
     optionsOpen:$('route-options').open, editingRoute, endpoint:routingEndpoint} : null
 };
}
function viewUrl() {
 const url = new URL(location.href);
 url.hash = selected ? new URLSearchParams({lat:selected.lat.toFixed(6),
   lon:selected.lon.toFixed(6),name:placeName(selected)}).toString() : '';
 return url;
}
function rememberView() {
 const snapshot = captureView();
 sessionViews.set(entryId, structuredClone({...snapshot, preview:routeData}));
 return snapshot;
}
function navigate(change) {
 history.replaceState(rememberView(),'',viewUrl());
 parentEntry=entryId;
 entryId=crypto.randomUUID();
 change();
 history.pushState(captureView(),'',viewUrl());
}
function goBack() {
 if (parentEntry) { history.back(); return; }
 showTab(tab);
 history.replaceState(captureView(),'',viewUrl());
 $('query').focus();
}
function restoreView(view, departed) {
 entryId=view.comapsId; parentEntry=view.parentEntry;
 results=view.results; hasSearched=view.hasSearched; statusKey=view.statusKey; exploreQuery=view.exploreQuery;
 showTab(view.tab);
 listExpanded=!!view.listExpanded; updateListSize((tab==='saved' ? saved : results).length);
 if (view.selected) displayPlace(view.selected,false);
 if (view.plannerOpen) {
   plannerOpen=true;
   const draft=view.route;
   Object.assign(origin,draft.origin); Object.assign(destination,draft.destination);
   stops=structuredClone(draft.stops);
   nextStopId=Math.max(nextStopId,...stops.map(s=>Number(s.id.slice(5))+1));
   avoidTolls=draft.avoidTolls; avoidMotorways=draft.avoidMotorways;
   $('avoid-tolls').checked=avoidTolls; $('avoid-motorways').checked=avoidMotorways;
   $('route-options').open=draft.optionsOpen;
   if (view.preview && routingConsent && draft.endpoint===routingEndpoint) {
     routeData=view.preview; editingRoute=draft.editingRoute; drawRoute();
   }
   updatePanel(); bindRouteFields(); renderPlanner();
 }
 settingsOpen=view.settingsOpen; updatePanel();
 map.setView(view.center,view.zoom,{animate:false});
 let focus;
 if (settingsOpen) focus=$('settings-title');
 else if (departed?.settingsOpen) focus=$('settings-toggle');
 else if (plannerOpen) focus=$('planner-title');
 else if (departed?.plannerOpen) focus=$(selected ? 'place-directions' : 'directions-tab');
 else if (selected) focus=$('place-title');
 else if (departed?.selected) {
   const list=tab==='saved' ? saved : results;
   const index=list.findIndex(p=>key(p)===key(departed.selected) && p.name===departed.selected.name);
   if (index>=2 && matchMedia('(max-width:720px)').matches) {
     listExpanded=true; updateListSize(list.length);
   }
   focus=$('results').children[index]?.firstElementChild;
 }
 (focus || $(tab+'-tab')).focus({preventScroll:true});
 document.querySelector('.panel').scrollTop=view.panelScroll;
 if (focus?.classList.contains('result')) focus.scrollIntoView({block:'nearest'});
}
window.addEventListener('popstate',event=>{
 const departed=rememberView();
 const view=event.state;
 if (view?.comapsId) restoreView(sessionViews.get(view.comapsId) || view,departed);
 else openSharedPlace();
});
function openSharedPlace() {
 const hash=new URLSearchParams(location.hash.slice(1));
 const point={lat:Number(hash.get('lat')),lon:Number(hash.get('lon')),name:hash.get('name')?.slice(0,200)||'',pin:!hash.get('name'),address:''};
 showTab('explore');
 if (hash.has('lat') && hash.has('lon') && validPlace(point))
   displayPlace(saved.find(p=>key(p)===key(point)) || point);
 history.replaceState(captureView(),'',viewUrl());
}
translate();
if (history.state?.comapsId && !history.state.settingsOpen) restoreView(history.state);
else openSharedPlace();
