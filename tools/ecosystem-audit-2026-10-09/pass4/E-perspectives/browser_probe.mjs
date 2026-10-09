#!/usr/bin/env node
/** Environment only; no product UI or external navigation is exercised. */
import {createRequire} from 'node:module';import fs from 'node:fs';import path from 'node:path';
const [modules,out]=process.argv.slice(2);if(!modules||!out)throw Error('usage: browser_probe.mjs NODE_MODULES OUTPUT_JSON');if(fs.existsSync(out))throw Error('fresh output required');
const req=createRequire(path.join(path.resolve(modules),'__audit_loader.cjs'));let result;
try{const {chromium}=req('playwright');const browser=await chromium.launch({headless:true,args:['--no-sandbox']});const page=await browser.newPage();await page.setContent('<!doctype html><p id="audit">synthetic browser gate</p>');result={status:'PASS',kind:'environment',actual_text:await page.locator('#audit').textContent(),browser_version:browser.version(),product_E_ui_test:false};await browser.close();}catch(e){result={status:'BLOCKED_ENVIRONMENT',kind:'environment',message:String(e).slice(0,2000),product_E_ui_test:false};}fs.mkdirSync(path.dirname(path.resolve(out)),{recursive:true});fs.writeFileSync(out,JSON.stringify(result,null,2)+'\n');console.log(result.status);
