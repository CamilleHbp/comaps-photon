import {defineConfig} from '@playwright/test';
export default defineConfig({testIgnore: '**/*-unit.test.js',
 testDir:'./tests',use:{baseURL:process.env.PWA_BASE_URL || 'http://127.0.0.1:4173',locale:'en-GB'},webServer:process.env.PWA_BASE_URL ? undefined : {command:'npm run preview -- --port 4173',url:'http://127.0.0.1:4173',reuseExistingServer:true},projects:[{name:'desktop',use:{viewport:{width:1440,height:1000}}},{name:'mobile',use:{viewport:{width:390,height:844},isMobile:true,hasTouch:true}}]});
