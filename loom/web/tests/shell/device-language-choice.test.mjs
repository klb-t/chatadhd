// Only the newly diagnosed AUTO/profile provenance boundary; no old locale suite replay.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader=await createModuleLoader();
test.after(()=>loader.close());
const {resolveShellLanguage:resolve}=await loader.load('src/shell/language.ts');
test('AUTO/device vs explicit and generated preference provenance',()=>{
 const available=['en','pl'];
 assert.deepEqual(resolve(undefined,undefined,['pl-PL','en-US'],available,'none'),{language:'pl',mode:'auto',source:'device'});
 assert.deepEqual(resolve(undefined,undefined,['en-US'],available,'none'),{language:'en',mode:'auto',source:'device'});
 assert.deepEqual(resolve(undefined,'en',['pl-PL'],available,'builtin'),{language:'pl',mode:'auto',source:'device'});
 assert.deepEqual(resolve(undefined,'en',['pl-PL'],available,'generated'),{language:'pl',mode:'auto',source:'device'});
 assert.deepEqual(resolve(undefined,'en',['pl-PL'],available,'user'),{language:'en',mode:'explicit',source:'user_preference'});
 assert.deepEqual(resolve('en','pl',['pl-PL'],available,'user'),{language:'en',mode:'explicit',source:'config'});
 assert.deepEqual(resolve('auto','en',['pl-PL'],available,'user'),{language:'pl',mode:'auto',source:'device'});
 assert.deepEqual(resolve('','en',['pl-PL'],available,'user'),{language:'pl',mode:'auto',source:'device'});
 assert.deepEqual(resolve('auto','pl',['zz-ZZ','en-US'],available,'user'),{language:'en',mode:'auto',source:'device'});
 assert.deepEqual(resolve('auto','pl',['zz-ZZ'],available,'user'),{language:'en',mode:'auto',source:'catalog_fallback'});
});
