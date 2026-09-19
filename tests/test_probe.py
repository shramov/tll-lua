#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import pathlib
import pytest

from tll.asynctll import asyncloop_run
from tll.config import Config
from tll.test_util import ports

scripts = {
    'global': '''
require('test_probe')
function tll_probe_select(probes)
    return _tll_probe_select(probes)
end
''',
    'function': '''
Probe = require('test_probe')
function Probe:metric()
    return self._metric
end
''',
    'member': '''
Probe = require('test_probe')
function Probe:on_login(type, seq, name, data)
	print("On login", self.index, data)
	self.metric = tonumber(data)
	return "done"
end
''',
}

@pytest.mark.parametrize('script', list(scripts.keys()))
@asyncloop_run
async def test_probe(asyncloop, script):
    cfg = Config.load(f'''yamls://
tll.proto: lua-probe+tcp
tll.host: 127.0.0.1:{ports.TCP4}
path: {pathlib.Path(__file__).parent}/?.lua
mode: client
name: lua
dump: yes
tcp.dump: yes
''')
    cfg['code'] = scripts[script]

    server = asyncloop.Channel(f'tcp://*:{ports.TCP4}', mode='server', name='server', dump='yes')
    server.open()

    c = asyncloop.Channel(cfg)
    c.open()

    hello = set(range(1, 5))
    for _ in range(10):
        m = await server.recv()
        if m.type != m.Type.Data:
            continue
        body = m.data.tobytes()
        assert body[:5] == b'Hello'
        idx = int(body[6:].decode('utf-8'))
        hello.remove(idx)
        server.post(b'%d' % (10 + idx), addr=m.addr)
        if not hello:
            break

    assert (await c.recv_state()) == c.State.Active

    for _ in range(3):
        m = await server.recv()
        assert m.type == m.Type.Control # Disconnects

    c.post(b'data')
    m = await server.recv()
    assert m.data.tobytes() == b'data'
    server.post(b'reply', addr=m.addr)
    m = await c.recv(0.1)
    assert m.data.tobytes() == b'reply'
