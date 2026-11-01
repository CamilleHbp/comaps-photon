import L from 'leaflet';
import {createIcons, Settings2, Search, ArrowRight, Compass, Bookmark, Coffee, Trees, Utensils, MapPin, LocateFixed, Plus, Minus, ArrowLeft, BookmarkCheck, Share2, Route, ExternalLink} from 'lucide';
const icons = {Settings2, Search, ArrowRight, Compass, Bookmark, Coffee, Trees, Utensils, MapPin, LocateFixed, Plus, Minus, ArrowLeft, BookmarkCheck, Share2, Route, ExternalLink};
import {messages, resolveLanguage} from './i18n.js';
import './style.css';

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
let statusKey = '', hasSearched = false;
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
 document.querySelectorAll('[data-i18n]').forEach(el => el.textContent = t(el.dataset.i18n));
 document.querySelectorAll('[data-label]').forEach(el => {el.setAttribute('aria-label', t(el.dataset.label));el.title=t(el.dataset.label);});
 document.querySelectorAll('[data-placeholder]').forEach(el => el.placeholder = t(el.dataset.placeholder));
 $('map').setAttribute('aria-label',t('map')); document.querySelector('.panel').setAttribute('aria-label',t('explore')); document.querySelector('.tabs').setAttribute('aria-label',t('places')); $('place').setAttribute('aria-label',t('details'));
 $('language').value = preference; connection(); renderList(); if(selected) renderPlace(); iconsRefresh();
}
function placeName(p) { return p.pin ? t('droppedPin') : p.name || t('untitled'); }
function key(p) { return `${p.lat.toFixed(6)},${p.lon.toFixed(6)}`; }
function isSaved(p) { return saved.some(s => key(s) === key(p)); }
function pin(p, index='') {
 const marker = L.marker([p.lat,p.lon], {icon:L.divIcon({className:'pin',html:`<div class="pin-inner"><span>${index}</span></div>`,iconSize:[30,38],iconAnchor:[15,36]}),title:placeName(p)}).addTo(markers);
 marker.on('click', () => selectPlace(p)); return marker;
}
function renderList() {
 const list = tab === 'saved' ? saved : results;
 $('saved-count').textContent = saved.length;
 $('results').replaceChildren();
 $('intro').hidden = tab === 'saved' || hasSearched || !!selected;
 $('results-header').hidden = tab !== 'saved' && !hasSearched;
 $('results-title').textContent = t(tab === 'saved' ? 'savedTitle' : 'results');
 $('clear').hidden = tab === 'saved';
 $('status').textContent = tab === 'saved' ? (!saved.length ? t('savedEmpty') : '') : (statusKey ? t(statusKey) : '');
 list.forEach((p, i) => { const li = document.createElement('li'); const button = document.createElement('button'); button.className='result'; button.innerHTML=`<span class="result-number">${i+1}</span><span><strong>${esc(placeName(p))}</strong><small>${esc(p.address || `${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}`)}</small></span>`;button.onclick=()=>selectPlace(p);li.append(button);$('results').append(li); });
}
function showTab(next) {
 controller?.abort(); statusKey=''; tab=next; selected=null; clearRoute(); $('place').hidden=true; $('browse').hidden=false; $('settings').hidden=true; $('settings-toggle').setAttribute('aria-expanded','false');
 for(const name of ['explore','saved']) {$(name+'-tab').classList.toggle('active',tab===name);$(name+'-tab').setAttribute('aria-pressed',String(tab===name));}
 markers.clearLayers(); (tab === 'saved' ? saved : results).forEach((p,i)=>pin(p,i+1)); renderList();
}
function selectPlace(p, move=true) {
 routeController?.abort(); clearRoute(); selected=p; $('settings').hidden=true; $('settings-toggle').setAttribute('aria-expanded','false'); $('browse').hidden=true; $('place').hidden=false; markers.clearLayers(); pin(p);
 if(move) map.setView([p.lat,p.lon], Math.max(map.getZoom(),15),{animate:false});
 const url=new URL(location.href); url.hash=new URLSearchParams({lat:p.lat.toFixed(6),lon:p.lon.toFixed(6),name:placeName(p)}).toString(); history.replaceState(null,'',url);
 renderPlace();
 if(move) map.panBy(innerWidth>720?[-207,0]:[0,-document.querySelector('.panel').getBoundingClientRect().height/2],{animate:false});
}
function renderPlace() {
 const p=selected; if(!p)return;
 $('place').innerHTML=`<button class="back" id="back"><i data-lucide="arrow-left"></i>${t('back')}</button><h2>${esc(placeName(p))}</h2><p>${esc(p.address || '')}</p><p class="coords">${t('coordinates')} · ${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}</p><div class="actions"><button id="save" class="primary"><i data-lucide="${isSaved(p)?'bookmark-check':'bookmark'}"></i>${t(isSaved(p)?'remove':'save')}</button><button id="share"><i data-lucide="share-2"></i>${t('share')}</button><button id="route" class="wide"><i data-lucide="route"></i>${t('route')}</button><a class="wide" href="comaps://map?v=1&ll=${p.lat},${p.lon}&n=${encodeURIComponent(placeName(p))}"><i data-lucide="external-link"></i>${t('openNative')}</a></div><div id="route-summary"></div>`;
 $('back').onclick=()=>{showTab(tab);history.replaceState(null,'',location.pathname);};
 $('save').onclick=()=>{const next=isSaved(p)?saved.filter(s=>key(s)!==key(p)):[...saved,p];if(write('comaps.saved',next)){saved=next;renderPlace();renderList();}};
 $('share').onclick=async()=>{try{if(navigator.share){await navigator.share({title:placeName(p),url:location.href});}else{await navigator.clipboard.writeText(location.href);toast(t('copied'));}}catch(e){if(e.name!=='AbortError')toast(t('copyError'));}};
 $('route').onclick=()=>drive(p); renderRoute(); iconsRefresh();
}
function clearRoute(){routeController?.abort();if(routeLayer)map.removeLayer(routeLayer);routeLayer=null;routeData=null;}
function renderRoute(){if(!routeData || !$('route-summary'))return;const minutes=Math.max(1,Math.round(routeData.duration/60));const duration=minutes>=60?`${Math.floor(minutes/60)} ${t('hours')} ${minutes%60} ${t('minutes')}`:`${minutes} ${t('minutes')}`;$('route-summary').innerHTML=`<p>${t('routeSummary')}</p><strong>${(routeData.distance/1000).toLocaleString(lang,{maximumFractionDigits:1})} ${t('distance')} · ${duration}</strong><p>${t('routeNotice')}</p><button id="clear-route" class="text-button">${t('clearRoute')}</button>`;$('clear-route').onclick=()=>{clearRoute();$('route-summary').replaceChildren();};}
function locate(){return new Promise((resolve,reject)=>{if(!navigator.geolocation)return reject(new Error('unsupported'));navigator.geolocation.getCurrentPosition(p=>resolve(p.coords),reject,{enableHighAccuracy:true,timeout:15000,maximumAge:60000});});}
async function drive(p){
 if(!navigator.onLine){toast(t('offlineSearch'));return;}
 clearRoute(); const abort=new AbortController();routeController=abort;const button=$('route');button.disabled=true;toast(t('routeLocation'));
 try{const c=await locate();if(abort.signal.aborted)return;toast(t('routing'));const r=await fetch(`https://router.project-osrm.org/route/v1/driving/${c.longitude},${c.latitude};${p.lon},${p.lat}?overview=full&geometries=geojson`,{signal:AbortSignal.any([abort.signal,AbortSignal.timeout(15000)])});if(!r.ok)throw new Error('route');const data=await r.json();if(data.code!=='Ok'||!data.routes?.length)throw new Error('route');if(selected!==p)return;routeData=data.routes[0];routeLayer=L.geoJSON(routeData.geometry,{style:{color:'#315c45',weight:6,opacity:.85}}).addTo(map);map.fitBounds(routeLayer.getBounds(),{paddingTopLeft:innerWidth>720?[440,40]:[30,innerHeight*.56],paddingBottomRight:[70,50]});renderRoute();}
 catch(e){if(!abort.signal.aborted)toast(t(e.code===1||e.code===2||e.code===3?'locationError':'routeError'));}finally{button.disabled=false;}
}
async function search(){
 const query=$('query').value.trim();if(!query)return;showTab('explore');hasSearched=true;results=[];
 if(!navigator.onLine){statusKey='offlineSearch';renderList();return;}
 statusKey='searching';renderList();controller=new AbortController();const current=controller;const center=map.getCenter();const url=new URL('https://photon.komoot.io/api/');url.search=new URLSearchParams({q:query,limit:'8',lang,lat:center.lat,lon:center.lng});
 try{const response=await fetch(url,{signal:AbortSignal.any([current.signal,AbortSignal.timeout(12000)])});if(!response.ok)throw new Error(response.status===429?'busy':'searchError');const data=await response.json();if(current.signal.aborted)return;results=(data.features||[]).map(f=>{const p=f.properties||{};return {name:p.name||[p.street,p.housenumber].filter(Boolean).join(' ')||p.city||t('untitled'),address:[p.street&&[p.street,p.housenumber].filter(Boolean).join(' '),p.postcode,p.city||p.town||p.village,p.country].filter(Boolean).join(', '),lon:f.geometry?.coordinates?.[0],lat:f.geometry?.coordinates?.[1]};}).filter(validPlace);statusKey=results.length?'':'noResults';markers.clearLayers();results.forEach((p,i)=>pin(p,i+1));if(results.length)map.fitBounds(L.latLngBounds(results.map(p=>[p.lat,p.lon])),{maxZoom:15,paddingTopLeft:innerWidth>720?[440,40]:[30,innerHeight*.56],paddingBottomRight:[70,50]});}
 catch(e){if(current.signal.aborted)return;statusKey=e.message==='busy'?'busy':'searchError';}renderList();
}
$('search-form').onsubmit=e=>{e.preventDefault();search();};
$('query').oninput=()=>{controller?.abort();if(statusKey==='searching'){statusKey='';renderList();}};
$('clear').onclick=()=>{hasSearched=false;results=[];$('query').value='';showTab('explore');history.replaceState(null,'',location.pathname);};
$('explore-tab').onclick=()=>showTab('explore');$('saved-tab').onclick=()=>showTab('saved');
$('settings-toggle').onclick=()=>{const open=$('settings').hidden;$('settings').hidden=!open;$('browse').hidden=open||!!selected;$('place').hidden=open||!selected;$('settings-toggle').setAttribute('aria-expanded',String(open));};
$('language').onchange=()=>{preference=$('language').value;write('comaps.language',preference);lang=resolveLanguage(preference);translate();};
window.addEventListener('languagechange',()=>{if(preference==='auto'){lang=resolveLanguage(preference);translate();}});
for(const button of document.querySelectorAll('[data-query]'))button.onclick=()=>{$('query').value=t(button.dataset.query);search();};
$('zoom-in').onclick=()=>map.zoomIn();$('zoom-out').onclick=()=>map.zoomOut();
$('locate').onclick=async()=>{const button=$('locate');button.disabled=true;toast(t('locating'));try{const c=await locate();if(locationMarker)map.removeLayer(locationMarker);locationMarker=L.circleMarker([c.latitude,c.longitude],{radius:8,color:'#fff',weight:3,fillColor:'#315c45',fillOpacity:1}).addTo(map);map.setView([c.latitude,c.longitude],16);$('toast').hidden=true;}catch{toast(t('locationError'));}finally{button.disabled=false;}};
map.on('click',e=>selectPlace({name:'',pin:true,lat:e.latlng.lat,lon:((e.latlng.lng+180)%360+360)%360-180,address:''},false));
window.addEventListener('online',()=>{connection();tileWarning=false;tiles.redraw();});window.addEventListener('offline',connection);
window.addEventListener('beforeinstallprompt',e=>{e.preventDefault();installPrompt=e;$('install').textContent=t('installReady');});
$('install').onclick=async()=>{if(installPrompt){await installPrompt.prompt();installPrompt=null;}else toast(t('installHelp'));};
if('serviceWorker'in navigator){navigator.serviceWorker.register('/sw.js').then(reg=>{reg.addEventListener('updatefound',()=>{reg.installing?.addEventListener('statechange',e=>{if(e.target.state==='installed'&&navigator.serviceWorker.controller)toast(t('appUpdated'));});});}).catch(()=>{});}
translate();
const hash=new URLSearchParams(location.hash.slice(1));const lat=Number(hash.get('lat')),lon=Number(hash.get('lon'));if(hash.has('lat')&&hash.has('lon')&&validPlace({lat,lon,name:''}))selectPlace({lat,lon,name:hash.get('name')?.slice(0,200)||'',pin:!hash.get('name'),address:''});
