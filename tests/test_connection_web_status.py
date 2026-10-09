"""Exercise the actual embedded browser refresh code with a failed/recovered Dial."""
import json
import pathlib
import re
import subprocess
import os

source = pathlib.Path(os.environ.get('CONNECTION_WEB_SOURCE', 'idf_app/main/config_server.c')).read_text()
match = re.search(r'<script>(let busy=false.*?)</script>', source)
assert match, "Embedded status refresher missing"
script = match.group(1)
harness = r'''
const vm=require('node:vm'), assert=require('node:assert/strict');
let tick, mode='ok', current={className:'status status-ok',textContent:'Ready',querySelector:()=>null,replaceWith(n){current=n;}};
const fresh=()=>({className:'status status-ok',textContent:'Ready',querySelector:()=>null,replaceWith(n){current=n;}});
const context={Date,Math,Error,AbortController,setTimeout,clearTimeout,
 document:{hidden:false,getElementById:()=>current},location:{pathname:'/'},
 setInterval:f=>{tick=f;},
 fetch:async()=>{if(mode==='offline')throw Error('offline');return {ok:mode!=='http',text:async()=>''};},
 DOMParser:class{parseFromString(){return {getElementById:()=>mode==='missing'?null:fresh()};}}};
vm.runInNewContext(SCRIPT,context);
(async()=>{for(const failure of ['offline','http','missing']){
 mode=failure;await tick();assert.match(current.textContent,/Dial unavailable/);assert.equal(current.className,'status status-warn');
 mode='ok';await tick();assert.equal(current.textContent,'Ready');assert.equal(current.className,'status status-ok');
}console.log('Browser status: network/HTTP/missing-status failures and recovery passed');})().catch(e=>{console.error(e);process.exitCode=1;});
'''
subprocess.run(['node', '-e', harness.replace('SCRIPT', json.dumps(script))], check=True)
