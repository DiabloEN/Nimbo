"""Loopback signalling with private ICE candidates; all databases and service keys are temporary."""
from contextlib import contextmanager, ExitStack
from pathlib import Path
import os
import re
import secrets
import socket
import subprocess
import time
from urllib.request import urlopen


def port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET,kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


@contextmanager
def service(command, cwd, env, url, log_name):
    with (cwd/log_name).open("wb") as log:
        p = subprocess.Popen(command, cwd=cwd, env=env, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            for _ in range(100):
                if p.poll() is not None:
                    raise RuntimeError(f"{log_name}: service exited {p.returncode}: "+(cwd/log_name).read_text(errors="replace")[-350:])
                try:
                    with urlopen(url, timeout=.3):
                        break
                except OSError:
                    time.sleep(.08)
            else:
                raise TimeoutError(url)
            yield
        finally:
            p.terminate()
            p.wait(timeout=8)


@contextmanager
def isolated(root, temp, server, suffixes=("", "2"), update_dir=None):
    env = {k:v for k,v in os.environ.items() if not k.startswith(("NIMBO_","LIVEKIT_"))}
    from ipaddress import ip_address
    includes = [ip+"/32" for ip in {"127.0.0.1", *socket.gethostbyname_ex(socket.gethostname())[2]} if ip_address(ip).is_private]
    with ExitStack() as stack:
        for suffix in suffixes:
            # Each API evicts RTC participants absent from its own database.
            signal,tcp,udp=port(),port(),port(socket.SOCK_DGRAM)
            while tcp==signal:tcp=port()
            key,secret="native-test",secrets.token_urlsafe(40)
            config=temp/f"livekit{suffix}.yaml"
            config.write_text(f"port: {signal}\nbind_addresses: ['127.0.0.1']\nrtc:\n  ips:\n    includes: {includes}\n  use_external_ip: false\n  enable_loopback_candidate: true\n  tcp_port: {tcp}\n  udp_port: {udp}\nkeys:\n  {key}: '{secret}'\nlogging:\n  level: warn\n")
            api_url=f"http://127.0.0.1:{signal}"
            stack.enter_context(service([root/".tools/media/server/livekit-server.exe","--config",config],temp,env,api_url+"/",f"rtc{suffix}.log"))
            db=temp/f"workspace{suffix}.json"
            offline=["--offline","--data",str(db)]
            subprocess.run([server,"init",*offline],check=True,stdout=subprocess.DEVNULL)
            out=subprocess.check_output([server,"owner-code",*offline]);code=re.search(rb"(?m)^([A-Za-z0-9_-]{43})\r?$",out)[1].decode()
            address=f"127.0.0.1:{port()}";url="http://"+address
            api_env=dict(env,LIVEKIT_API_KEY=key,LIVEKIT_API_SECRET=secret,LIVEKIT_URL=f"ws://127.0.0.1:{signal}",
                         LIVEKIT_API_URL=api_url,NIMBO_DATA_FILE=str(db),NIMBO_LISTEN=address,NIMBO_ORIGIN=url,NIMBO_TEST_PASSWORD=secrets.token_urlsafe(40))
            if update_dir and not suffix:api_env["NIMBO_UPDATE_DIR"]=str(update_dir)
            stack.enter_context(service([server],temp,api_env,url+"/healthz",f"api{suffix}.log"))
            env["NIMBO_NATIVE_TEST_URL"+suffix]=url;env["NIMBO_NATIVE_TEST_CODE"+suffix]=code
        yield env


CRYPTO=r"""
const fs=require('node:fs'), {webcrypto:{subtle}}=require('node:crypto');
(async()=>{
const password=Buffer.from('native-fixture'), path=process.argv[1];
const profile={format:'nimbo-profile',version:1,name:'Тестер',avatar:'',servers:[],hotkeys:{}};
const material=await subtle.importKey('raw',password,'PBKDF2',false,['deriveKey']);
const aad=Buffer.from('nimbo-encrypted-profile:v1:PBKDF2-SHA256:600000:AES-256-GCM');
const derive=(salt)=>subtle.deriveKey({name:'PBKDF2',iterations:600000,hash:'SHA-256',salt},material,{name:'AES-GCM',length:256},false,['encrypt','decrypt']);
if(process.argv[2]==='write'){
const salt=crypto.getRandomValues(new Uint8Array(16)),iv=crypto.getRandomValues(new Uint8Array(12));
const data=await subtle.encrypt({name:'AES-GCM',iv,additionalData:aad},await derive(salt),Buffer.from(JSON.stringify(profile)));
const b=s=>Buffer.from(s).toString('base64');
fs.writeFileSync(path,JSON.stringify({format:'nimbo-encrypted-profile',version:1,kdf:'PBKDF2-SHA256',iterations:600000,cipher:'AES-256-GCM',salt:b(salt),iv:b(iv),data:b(data)}));
}else{
const p=JSON.parse(fs.readFileSync(path+'.native')),d=s=>Buffer.from(s,'base64');
const data=await subtle.decrypt({name:'AES-GCM',iv:d(p.iv),additionalData:aad},await derive(d(p.salt)),d(p.data));
require('node:assert/strict').deepEqual(JSON.parse(Buffer.from(data)),profile);
}
})().catch(err=>{console.error(err);process.exitCode=1});
"""

def profile_fixture(path, mode):
    subprocess.run(["node","-e",CRYPTO,path,mode],check=True)
