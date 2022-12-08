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
async def test_simple(asyncloop, t, v):
    if isinstance(v, tuple):
        v, s = v
    else:
        s = str(v)
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
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
    url['config.1.data.f0'] = s
    url['scheme'] = f'''yamls://
- name: msg
  id: 10
  fields:
    - {{name: f0, type: {t} }}
'''
    url['code'] = f'''
function luatll_filter(seq, name, data)
    print(data.f0)
    print({v})
    return data.f0 == {v}
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 1)

@asyncloop_run
async def test_seq(asyncloop, tmp_path):
    with open(tmp_path / "code.lua", "w") as fp:
        fp.write(f'''
function luatll_filter(seq)
    return seq % 4 == 0
end
''')
    c = asyncloop.Channel(f'lua+zero://;size=8b;name=lua;zero.dump=frame;lua.dump=frame;code=file://{tmp_path}/code.lua')
    c.open()
    assert c.state == c.State.Active
    r = []
    while len(r) < 4:
        m = await c.recv(0.001)
        r.append(m)
    assert [m.seq for m in r] == [0, 4, 8, 12]

@asyncloop_run
async def test_binary(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: frame
lua.dump: frame
autoclose: yes
config:
  - seq: 0
    msgid: 10
    data: bbbb
  - seq: 1
    msgid: 10
    data: aaaa
''')
    url['code'] = '''
function luatll_filter(seq, name, data)
    return data == "aaaa"
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert m.data.tobytes() == b'aaaa'
