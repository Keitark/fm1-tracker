const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname,'remote_library.html'),'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const image = Buffer.alloc(0x100000).toString('base64');
function bundle(id='nes-demo') {
  return {id,profile:'nes',title:'Demo <b>app</b>',description:'Local app',variant:'Test',
    request:{op:'plan',image,sha256:'a'.repeat(64),baseline_sha256:'b'.repeat(64)}};
}
function page(hash='#'+'a'.repeat(43)) {
  const elements = new Map();
  function element() {
    return {value:'',disabled:false,textContent:'',dataset:{},children:[],listeners:{},
      replaceChildren(...children){this.children=children;},append(...children){this.children.push(...children);},
      addEventListener(event,fn){this.listeners[event]=fn;}};
  }
  for(const id of ['message','name','members','add','files','folder']) elements.set(id,element());
  const requests=[]; const histories=[];
  const context=vm.createContext({document:{getElementById:id=>elements.get(id),createElement:()=>element()},
    location:{hash,pathname:'/library-import'},history:{replaceState:(...args)=>histories.push(args)},
    fetch:async(url,options)=>{requests.push({url,options});return {ok:true,status:201,json:async()=>({ok:true,data:{id:'c'.repeat(32),name:'My apps',members:[{id:'nes-demo'}]}})}},
    console});
  vm.runInContext(script,context);
  return {elements,requests,histories,context};
}
async function choose(harness,packages,folder=false) {
  const files=packages.map((value,index)=>({name:'bundle'+index+'.json',size:JSON.stringify(value).length,
    webkitRelativePath:folder?'Local apps/bundle'+index+'.json':'',text:async()=>JSON.stringify(value)}));
  harness.elements.get(folder?'folder':'files').listeners.change({target:{files}});
  await new Promise(resolve=>setImmediate(resolve));
}

test('import fragment is immediately removed and invalid grant cannot add',async()=>{
  const h=page('');assert.deepEqual(h.histories,[[null,'','/library-import']]);
  h.elements.get('name').value='My apps';await choose(h,[bundle()]);
  assert.equal(h.elements.get('add').disabled,true);
  await h.elements.get('add').listeners.click();assert.equal(h.requests.length,0);
});
test('file selection only reviews, explicit Add uses only scoped import route',async()=>{
  const h=page();h.elements.get('name').value='My apps';await choose(h,[bundle()]);
  assert.equal(h.requests.length,0);assert.equal(h.elements.get('add').disabled,false);
  const title=h.elements.get('members').children[0].children[0];
  assert.equal(title.textContent,'Demo <b>app</b> · Test');assert.equal(title.innerHTML,undefined);
  await h.elements.get('add').listeners.click();
  assert.equal(h.requests.length,1);assert.equal(h.requests[0].url,'/v1/library/sets');
  assert.equal(h.requests[0].options.headers.Authorization,'Bearer '+'a'.repeat(43));
  assert.equal(h.requests[0].options.redirect,'error');
  const submitted=JSON.parse(h.requests[0].options.body);
  assert.deepEqual(Object.keys(submitted).sort(),['bundles','name']);assert.equal(submitted.bundles.length,1);
  assert.equal(h.elements.get('add').disabled,true);
  await h.elements.get('add').listeners.click();assert.equal(h.requests.length,1);
});
test('folder selection names the set and never automatically imports',async()=>{
  const h=page();await choose(h,[bundle('a'),bundle('b')],true);
  assert.equal(h.elements.get('name').value,'Local apps');assert.equal(h.requests.length,0);
  assert.equal(h.elements.get('members').children.length,2);
});
test('a bad member or duplicate invalidates entire selection before Add',async()=>{
  const h=page();h.elements.get('name').value='My apps';const bad=bundle('bad');bad.path='private';
  await choose(h,[bundle(),bad]);assert.equal(h.elements.get('add').disabled,true);assert.equal(h.requests.length,0);
  await choose(h,[bundle(),bundle()]);assert.equal(h.elements.get('add').disabled,true);
  assert.match(h.elements.get('message').textContent,/duplicate/);
});
test('size/count bounds prevent unreviewable selections',async()=>{
  const h=page();h.elements.get('name').value='My apps';
  await choose(h,Array.from({length:9},(_,index)=>bundle('demo-'+index)));
  assert.equal(h.elements.get('add').disabled,true);assert.match(h.elements.get('message').textContent,/1–8/);
  h.elements.get('files').listeners.change({target:{files:[{name:'huge.json',size:1500001,text:()=>{throw Error('must not read')}}]}});
  await new Promise(resolve=>setImmediate(resolve));assert.equal(h.elements.get('add').disabled,true);
  assert.match(h.elements.get('message').textContent,/size limit/);assert.equal(h.requests.length,0);
});
