#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import pytest

from tll.config import Config

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
    ('byte8', (b'abcd\0\0\0\0', '"abcd"')),
    ('byte8, options.type: string', ('abcd', '"abcd"')),
    ('string', ('abcd', '"abcd"')),
    ('sub', ({'s0': 100}, '{s0 = 100}')),
    ('"int8[4]"', ([10, 20], '{10, 20}')),
    ('"*int8"', ([10, 20], '{10, 20}')),
    ('"*string"', (["10", "20"], '{"10", "20"}')),
    ('"sub[4]"', ([{'s0': 10}, {'s0': 20}], '{{s0 = 10}, {s0 = 20}}')),
    ('"*sub"', ([{'s0': 10}, {'s0': 20}], '{{s0 = 10}, {s0 = 20}}')),
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
tll.proto: lua-prefix+yaml
name: lua
yaml.dump: yes
lua-prefix.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    f0: 1
''')
    url['scheme'] = f'''yamls://
- name: sub
  fields:
    - {{name: s0, type: uint16}}
- name: msg
  id: 10
  fields:
    - {{name: header, type: int32}}
    - {{name: f0, type: {t} }}
'''
    url['yaml.scheme'] = '''yamls://
- name: msg
  id: 20
  fields:
    - {name: f0, type: int32}
'''
    url['code'] = f'''
function luatll_on_data(seq, name, data)
    print(data.f0)
    print({s})
    luatll_callback(seq + 100, "msg", {{ f0 = {s} }})
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'header': 0, 'f0': v}

@asyncloop_run
async def test_pass(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua-prefix+yaml
name: lua
yaml.dump: yes
lua-prefix.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    f0: 1
''')
    url['yaml.scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: int32}
'''
    url['code'] = f'''
function luatll_on_data(seq, name, data)
    luatll_callback(seq + 100, "msg", data)
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': 1}

@asyncloop_run
async def test_convert(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua-prefix+yaml
name: lua
yaml.dump: yes
lua-prefix.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    header: 100
    f0: 1
''')
    url['lua-prefix.scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: int32}
'''

    url['yaml.scheme'] = '''yamls://
- name: msg
  id: 20
  fields:
    - {name: header, type: int32}
    - {name: f0, type: int32}
'''
    url['code'] = f'''
function luatll_on_data(seq, name, data)
    luatll_callback(seq + 100, "msg", data)
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': 1}

@asyncloop_run
async def test_post(asyncloop):
    SCHEME = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: int32}
'''
    url = Config.load(f'''yamls://
tll.proto: lua-prefix+direct
name: lua
direct.dump: yes
lua-prefix.dump: yes
''')
    url['direct.scheme'] = SCHEME

    url['code'] = '''
function luatll_on_post(seq, name, data)
    luatll_post(seq + 100, "msg", { f0 = 100 })
end
'''
    c = asyncloop.Channel(url)
    c.open()

    out = asyncloop.Channel('direct://', master=c, scheme=SCHEME)
    out.open()

    c.post({'f0': 10}, name='msg', seq=10)

    m = await out.recv(0.001)
    assert (m.msgid, m.seq) == (10, 110)
    assert c.unpack(m).as_dict() == {'f0': 100}

@asyncloop_run
async def test_iter(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua-prefix+yaml
name: lua
yaml.dump: yes
lua-prefix.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    f0: 10
    f1: 0.5
''')
    url['lua-prefix.scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: key, type: string}
    - {name: sum, type: double}
'''

    url['yaml.scheme'] = '''yamls://
- name: msg
  id: 20
  fields:
    - {name: f0, type: int32}
    - {name: f1, type: double}
'''
    url['code'] = '''
function luatll_on_data(seq, name, data)
    local key = ""
    local sum = 0
    for k,v in pairs(data) do
        key = key .. "/" .. k;
        sum = sum + v;
    end
    luatll_callback(seq + 100, "msg", { key = key, sum = sum })
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'key': "/f0/f1", "sum": 10.5}
