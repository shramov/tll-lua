#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import pytest

from tll.test_util import Accum
from tll import asynctll
from tll.config import Config

@pytest.fixture
def asyncloop(context):
    loop = asynctll.Loop(context)
    yield loop
    loop.destroy()
    loop = None

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

'''
@asyncloop_run
async def test_filter(asyncloop):
    pcap = asyncloop.Channel('pcap://./tests/udp.pcap', name='pcap', speed='100')
    u0 = asyncloop.Channel('pcap+udp://10.22.17.253:5555', dump='frame', master=pcap, name='udp0')
    u1 = asyncloop.Channel('pcap+udp://10.22.17.253:5556', dump='frame', master=pcap, name='udp1')

    pcap.open()
    u0.open()
    u1.open()

    r0, r1 = [], []

    assert await pcap.recv_state() == pcap.State.Active

    for _ in range(6):
        r0 += [await u0.recv(0.011)]
        r1 += [await u1.recv(0.001)]
        with pytest.raises(TimeoutError):
            await u0.recv(0.005)

    assert [m.data.tobytes() for m in r0] == [b'ipv4:5555'] * 6
    assert [m.data.tobytes() for m in r1] == [b'ipv4:5556'] * 6
'''

@pytest.mark.parametrize("t,v", [
    ('int8', 123),
    ('int16', 12323),
    ('int32', 123123),
    ('int64', 123123123),
    ('uint8', 231),
    ('uint16', 53123),
    ('uint32', 123123),
    ('uint64', 123123123),
    ('double', 123.123),
    ('byte8', (r'"abcd\0\0\0\0"', 'abcd')),
    ('byte8, options.type: string', ('"abcd"', 'abcd')),
    ('string', ('"abcd"', 'abcd')),
#    ('decimal128', (decimal.Decimal('1234567890.e-5'), '1234567890.e-5')),
#    ('int32, options.type: fixed3', (decimal.Decimal('123.456'), '123456.e-3')),
#    ('int32, options.type: duration, options.resolution: us', (Duration(123000, Resolution.us), '123ms')),
#    ('int64, options.type: time_point, options.resolution: s', (TimePoint(1609556645, Resolution.second), '2021-01-02T03:04:05')),
])
@asyncloop_run
async def test_simple(asyncloop, tmp_path, t, v):
    if isinstance(v, tuple):
        v, s = v
    else:
        s = str(v)
    scheme = f'''yamls://
- name: msg
  id: 10
  fields:
    - {{name: f0, type: {t} }}
'''
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: yaml
yaml.dump: scheme
lua.dump: scheme
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
config.1:
  seq: 1
  name: msg
''')
    url['code'] = str(tmp_path / "code.lua")
    url['scheme'] = scheme
    url['config.1.data.f0'] = s
    with open(tmp_path / "code.lua", "w") as fp:
        fp.write(f'''
function luatll_filter(name, seq, msg)
    print(msg.f0)
    print({v})
    return msg.f0 == {v}
end
''')
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 1)
