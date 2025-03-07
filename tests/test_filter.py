#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import pytest

from tll.config import Config
from tll.channel import Channel

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
function tll_filter(seq, name, data)
    print(data.f0)
    print({v})
    return data.f0 == {v}
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.1)
    assert (m.msgid, m.seq) == (10, 1)

@asyncloop_run
async def test_seq(asyncloop, tmp_path):
    with open(tmp_path / "code.lua", "w") as fp:
        fp.write(f'''
function tll_filter(seq)
    return seq % 4 == 0
end
''')
    c = asyncloop.Channel(f'lua+zero://;size=8b;name=lua;zero.dump=frame;lua.dump=frame;code=file://{tmp_path}/code.lua')
    c.open()
    assert c.state == c.State.Active
    r = []
    while len(r) < 4:
        m = await c.recv(0.1)
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
function tll_filter(seq, name, data)
    return data == "aaaa"
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.1)
    assert m.data.tobytes() == b'aaaa'

@asyncloop_run
async def test_pmap(asyncloop):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config:
  - seq: 0
    name: msg
    data: { f0: 10 }
  - seq: 1
    name: msg
    data: { f1: 10 }
''')
    url['scheme'] = '''yamls://
- name: msg
  id: 10
  options.defaults.optional: yes
  fields:
    - {name: pmap, type: uint8, options.pmap: yes}
    - {name: f0, type: int32}
    - {name: f1, type: int32}
'''
    url['code'] = '''
function tll_filter(seq, name, data)
    print(data.f0)
    print(data.f1)
    return data.f0 == nil
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.1)
    assert (m.msgid, m.seq) == (10, 1)
    assert c.unpack(m).as_dict() == {'f1': 10}

@pytest.mark.parametrize("mode,compare", [
    ('object', 'data.f0:eq("A")'),
    ('object', 'data.f0:eq(10)'),
    ('object', 'data.f0.int == 10'),
    ('object', 'data.f0.string == "A"'),
    ('object', 'tostring(data.f0) == "A"'),
    ('int', 'data.f0 == 10'),
    ('string', 'data.f0 == "A"'),
    ('', 'data.f0 == "A"'),
    ('int', 'data.f0 == tll_self_scheme.messages["msg"].enums["f0"].values.A'),
    ('int', 'data.f0 == tll_self_scheme.messages["msg"].fields["f0"].type_enum.values.A'),
])
@asyncloop_run
async def test_enum(asyncloop, mode, compare):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
lua.enum-mode: {mode}
autoclose: yes
config.0: {{seq: 0, name: msg, data: {{}}}}
config.1: {{seq: 1, name: msg, data.f0: A}}
''')
    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: uint16, options.type: enum, enum: {A: 10, B: 20}}
'''

    url['code'] = f'''
function tll_filter(seq, name, data)
    print(data.f0)
    return {compare}
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.1)
    assert (m.msgid, m.seq) == (10, 1)

@pytest.mark.parametrize("mode,compare", [
    ('object', 'data.f0.A'),
    ('object', 'data.f0.C'),
    ('object', 'data.f0.A and data.f0.C'),
    ('object', '(data.f0 & 0x1) ~= 0'),
    ('object', '(data.f0 & 0x4) ~= 0'),
    ('object', '(data.f0 | 0x2) == 0x7'),
    ('object', '(data.f0 ~ 0x3) == 0x6'),
    ('int', 'data.f0 == 0x5'),
    ('int', '(data.f0 & 0x1) ~= 0'),
    ('int', '(data.f0 & 0x4) ~= 0'),
    ('int', '(data.f0 | 0x2) == 0x7'),
    ('int', '(data.f0 ~ 0x3) == 0x6'),
    ('int', '(data.f0 & tll_self_scheme.messages["msg"].bits["f0"].values.A.value) ~= 0'),
    ('int', '(data.f0 & tll_self_scheme.messages["msg"].fields["f0"].type_bits.values.A.value) ~= 0'),
    ('', 'data.f0.A'),
])
@asyncloop_run
async def test_bits(asyncloop, mode, compare):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
lua.bits-mode: {mode}
autoclose: yes
config.0: {{seq: 0, name: msg, data: {{}}}}
config.1: {{seq: 1, name: msg, data.f0: "A | C"}}
''')
    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: uint16, options.type: bits, bits: [A, B, C]}
'''

    url['code'] = f'''
function tll_filter(seq, name, data)
    print(data.f0)
    return {compare}
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.1)
    assert (m.msgid, m.seq) == (10, 1)

@pytest.mark.parametrize("mode,compare", [
    ('int', 'data.f0 == 123456'),
    ('float', 'data.f0 == 123.456'),
    ('', 'data.f0 == 123.456'),
])
@asyncloop_run
async def test_fixed(asyncloop, mode, compare):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0: {seq: 0, name: msg, data: {}}
config.1: {seq: 1, name: msg, data.f0: 123.456}
''')
    url['lua.preset'] = 'filter'
    url['lua.fixed-mode'] = mode
    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: uint32, options.type: fixed3}
'''

    url['code'] = f'''
function tll_filter(seq, name, data)
    print(data.f0)
    return {compare}
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.1)
    assert (m.msgid, m.seq) == (10, 1)

@pytest.mark.parametrize("mode,compare,result", [
    ('object', 'data.f0 >= tll_time_point(2010, 01, 02, 03, 04, 05, 123456789)', [1, 2]),
    ('object', 'data.f0 == tll_time_point(2010, 01, 02, 03, 04, 05, 123456789)', [1]),
    ('object', 'data.f0 >  tll_time_point(2010, 01, 02, 03, 04, 05, 123456789)', [2]),
    ('object', 'data.f0 <  tll_time_point(2010, 01, 02, 03, 04, 05, 123456789)', [0]),
    ('object', 'data.f0 <= tll_time_point(2010, 01, 02, 03, 04, 05, 123456789)', [0, 1]),
    ('object', 'data.f0 ~= tll_time_point(2010, 01, 02, 03, 04, 05, 123456789)', [0, 2]),
    ('object', 'data.f0.string == "2010-01-02T03:04:05.123456789"', [1]),
    ('float', 'data.f0 >= 1262401445.123456789', [1, 2]),
    ('int', 'data.f0 >= 1262401445123456789', [1, 2]),
    ('string', 'data.f0 == "2010-01-02T03:04:05.123456789"', [1]),
])
@asyncloop_run
async def test_time_point(asyncloop, mode, compare, result):
    cfg = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0: {seq: 0, name: Data, data.f0: '2000-01-02T03:04:05.123456789'}
config.1: {seq: 1, name: Data, data.f0: '2010-01-02T03:04:05.123456789'}
config.2: {seq: 2, name: Data, data.f0: '2020-01-02T03:04:05.123456789'}
''')
    cfg['lua.preset'] = 'filter'
    cfg['lua.time-mode'] = mode
    cfg['scheme'] = '''yamls://
- name: Data
  id: 10
  fields:
    - {name: f0, type: int64, options.type: time_point, options.resolution: ns}
'''

    cfg['code'] = f'''
function tll_filter(seq, name, data)
    print(tostring(data.f0))
    return {compare}
end
'''
    c = asyncloop.Channel(cfg, async_mask=Channel.MsgMask.Data | Channel.MsgMask.State)
    c.open()
    assert c.state == c.State.Active
    r = []
    while c.state == c.State.Active:
        m = await c.recv(0.1)
        if m.type == m.Type.Data:
            r.append(m.seq)
    assert r == result
