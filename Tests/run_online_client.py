#!/usr/bin/env python3
"""Build/run the real C++ Online client and relay data-link handshake under sanitizers."""
import argparse
import asyncio
import importlib.util
from pathlib import Path
import shlex
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import relay

async def run(binary, module=relay, extra=()):
    legacy=module.Room(log=lambda _:None);hub=module.lobby.Hub(legacy,log=lambda _:None)
    server=await asyncio.start_server(hub.handle,'127.0.0.1',0);hub.start()
    try:
        proc=await asyncio.create_subprocess_exec(str(binary),str(server.sockets[0].getsockname()[1]), *extra)
        try: assert await asyncio.wait_for(proc.wait(),150)==0
        finally:
            if proc.returncode is None:proc.kill();await proc.wait()
    finally:
        server.close();await server.wait_closed();await hub.close();await legacy.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true')
    p.add_argument('--legacy-indy-relay', type=Path, help='optional pre-room-options Indy relay.py for fallback checks')
    p.add_argument('--pre-srallyc-relay', type=Path, help='optional Indy room-options relay.py without srallyc')
    p.add_argument('--pre-daytona-relay', type=Path, help='optional previous Rev C relay.py without Daytona')
    p.add_argument('--pre-model2-video-relay', type=Path, help='optional Daytona-capable relay.py before the Model 2 video option')
    p.add_argument('--pre-sgt24h-relay', type=Path, help='optional pre-SGT relay.py for greeting fallback and capability isolation')
    p.add_argument('--pre-indy-nocpu-relay', type=Path, help='previous relay for players-only capability fallback')
    p.add_argument("--indy-nocpu-only", action="store_true")
    p.add_argument("--self-host-only", action="store_true")
    p.add_argument("--indy-self-host-only", action="store_true")
    a=p.parse_args()
    binary=ROOT/'build/online-client-test';binary.parent.mkdir(exist_ok=True)
    cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-g','-O1','-pthread','-ISrc','-ISrc/OSD','-ISrc/OSD/SDL','-ISrc/Util','Tests/OnlineClientTest.cpp','Src/Network/Online/LobbyClient.cpp','Src/Model3/NetBoardLink.cpp','Src/Network/NetServer.cpp','Src/Network/TCPConnection.cpp','Src/Util/Format.cpp','Src/OSD/Thread.cpp']
    cmd+=shlex.split(subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True))+['-lSDL2_net','-o',str(binary)]
    if a.sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run(cmd,cwd=ROOT,check=True)
    asyncio.run(run(binary, extra=("indy-self-host-only",) if a.indy_self_host_only else ("self-host-only",) if a.self_host_only else ("indy-nocpu",) if a.indy_nocpu_only else ()))
    for endpoint, mode in ((a.legacy_indy_relay, 'legacy-indy'), (a.pre_srallyc_relay, 'pre-srallyc'), (a.pre_daytona_relay, 'pre-daytona'),
                           (a.pre_model2_video_relay, 'pre-model2-video'), (a.pre_sgt24h_relay, 'pre-sgt24h'), (a.pre_indy_nocpu_relay, 'pre-indy-nocpu')):
        if endpoint:
            spec=importlib.util.spec_from_file_location(mode,endpoint)
            old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
            asyncio.run(run(binary,old,(mode,)))
