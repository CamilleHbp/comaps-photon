import {readdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
async function walk(dir){const entries=await readdir(dir,{withFileTypes:true});return (await Promise.all(entries.map(e=>e.isDirectory()?walk(`${dir}/${e.name}`):`${dir}/${e.name}`))).flat();}
const files=(await walk('dist')).filter(p=>!p.endsWith('/sw.js'));
const hash=createHash('sha256');hash.update(await readFile('scripts/build-sw.mjs'));for(const file of files.sort())hash.update(await readFile(file));
const version=hash.digest('hex').slice(0,16);
const assets=files.map(f=>f.slice(4));assets.push('/');
await writeFile('dist/sw.js',`const CACHE='comaps-${version}';const ASSETS=${JSON.stringify(assets)};
self.addEventListener('install',e=>e.waitUntil(caches.open(CACHE).then(c=>c.addAll(ASSETS))));
self.addEventListener('activate',e=>e.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(k=>k.startsWith('comaps-')&&k!==CACHE).map(k=>caches.delete(k)))).then(()=>self.clients.claim())));
self.addEventListener('fetch',e=>{const u=new URL(e.request.url);if(e.request.method!=='GET'||u.origin!==self.location.origin)return;
if(e.request.mode==='navigate'){e.respondWith(fetch(e.request).catch(()=>caches.open(CACHE).then(c=>c.match('/'))));return;}
if(ASSETS.includes(u.pathname))e.respondWith(caches.open(CACHE).then(async c=>(await c.match(u.pathname,{ignoreVary:true}))||fetch(e.request)));
});\n`);
console.log(`Service worker ${version}: ${assets.length} local assets; no tile prefetch or offline tile cache.`);
