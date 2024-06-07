#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decimal
import decorator
import enum
import pytest

from tll.config import Config, Url
from tll.error import TLLError
from tll.test_util import Accum

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

class f0(enum.Enum):
    A = 10
    B = 20
    C = 30

@pytest.mark.parametrize("t,v", [
    ('int8', 123),
    ('int8', (None, 1123)),
    ('int8', (None, -1123)),
    ('int8', (None, 123.123)),
    ('int8', (None, '"xxx"')),
    ('int8', (None, '{}')),
    ('int16', 12323),
    ('int32', 123123),
    ('int64', 123123123),
    ('uint8', 231),
    ('uint8', (None, -123)),
    ('uint16', 53123),
    ('uint32', 123123),
    ('uint64', 123123123),
    ('double', 123.123),
    ('double', (None, '"xxx"')),
    ('byte8', (b'abcd\0\0\0\0', '"abcd"')),
    ('byte8, options.type: string', ('abcd', '"abcd"')),
    ('string', ('abcd', '"abcd"')),
    ('sub', ({'s0': 100}, '{s0 = 100}')),
    ('"int8[4]"', ([10, 20], '{10, 20}')),
    ('"*int8"', ([10, 20], '{10, 20}')),
    ('"*string"', (["10", "20"], '{"10", "20"}')),
    ('"sub[4]"', ([{'s0': 10}, {'s0': 20}], '{{s0 = 10}, {s0 = 20}}')),
    ('"*sub"', ([{'s0': 10}, {'s0': 20}], '{{s0 = 10}, {s0 = 20}}')),
    ('uint16, options.type: bits, bits: [A, B, C]', ({'A': True, 'B': False, 'C': True}, '{A = 0x3, C = true}')),
    ('decimal128', (decimal.Decimal('123.456'), '"123.456"')),
    ('decimal128', (decimal.Decimal('123.456'), '123.456')),
    ('uint16, options.type: enum, enum: {A: 10, B: 20, C: 30}', (f0.A, '"A"')),
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
yaml.dump: yes
lua.dump: yes
lua.fragile: yes
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
function tll_on_data(seq, name, data)
    print(data.f0)
    print({s})
    tll_callback(seq + 100, "msg", {{ f0 = {s} }})
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.State.Active == await c.recv_state()
    if v is None:
        assert c.State.Error == await c.recv_state()
        return
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'header': 0, 'f0': v}

@asyncloop_run
async def test_pass(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
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
function tll_on_data(seq, name, data)
    tll_callback(seq + 100, "msg", data)
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
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    header: 100
    f0: 1
''')
    url['lua.scheme'] = '''yamls://
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
function tll_on_data(seq, name, data)
    tll_callback(seq + 100, "msg", data)
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
tll.proto: lua+direct
name: lua
direct.dump: yes
lua.dump: yes
''')
    url['direct.scheme'] = SCHEME

    url['code'] = '''
function tll_on_post(seq, name, data)
    tll_child_post(seq + 100, "msg", { f0 = 100 })
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
async def test_iter_msg(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    f0: 10
    f1: 0.5
''')
    url['lua.scheme'] = '''yamls://
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
function tll_on_data(seq, name, data)
    local key = ""
    local sum = 0
    for k,v in pairs(data) do
        key = key .. "/" .. k;
        sum = sum + v;
    end
    tll_callback(seq + 100, "msg", { key = key, sum = sum })
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'key': "/f0/f1", "sum": 10.5}

@asyncloop_run
async def test_iter_list(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    f0: [10, 20, 30]
    f1: [100, 200]
''')

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: '*int32'}
    - {name: f1, type: 'int32[4]'}
'''
    url['code'] = '''
function tll_on_data(seq, name, data)
    local k0 = 0
    local s0 = 0
    local k1 = 0
    local s1 = 0
    for k,v in pairs(data.f0) do
        k0 = k0 + k
        s0 = s0 + v
    end
    for k,v in ipairs(data.f1) do
        k1 = k1 + k
        s1 = s1 + v
    end
    tll_callback(seq + 100, "msg", { f0 = { k0, s0 }, f1 = { k1, s1 } })
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': [1 + 2 + 3, 10 + 20 + 30], 'f1': [1 + 2, 100 + 200]}

@asyncloop_run
async def test_copy(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data:
    f0: 10
    f1: [10, 20, 30]
    f2: [100, 200]
''')

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: int32}
    - {name: f1, type: '*int32'}
    - {name: f2, type: 'int32[4]'}
'''
    url['code'] = '''
function tll_on_data(seq, name, data)
    for k,v in pairs(tll_msg_copy(data)) do
        print(k, v)
    end
    tll_callback(seq + 100, "msg", tll_msg_copy(data))
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': 10, 'f1': [10, 20, 30], 'f2': [100, 200]}

@asyncloop_run
async def test_deepcopy(asyncloop):
    url = Config.load(f'''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: Data
  data:
    f0: 10
    f1: [10, 20, 30]
    f2: [100, 200]
    f3:
        f0: 11
        f1: [11, 21]
        f2: [101]
    f4:
      - f0: 12
        f1: [12, 22]
        f2: [102, 202]
      - f0: 13
        f1: [13]
        f2: [103]
    f5:
      - f0: 14
        f1: [14, 24]
        f2: [104, 204]
      - f0: 15
        f1: [15]
        f2: [105]
      - f0: 16
        f1: [16]
        f2: [106]
''')

    url['scheme'] = '''yamls://
- name: Inner
  fields:
    - {name: f0, type: string}
    - {name: f1, type: '*int32'}
    - {name: f2, type: 'int32[4]'}

- name: Data
  id: 10
  fields:
    - {name: f0, type: int32}
    - {name: f1, type: '*int32'}
    - {name: f2, type: 'int32[4]'}
    - {name: f3, type: Inner}
    - {name: f4, type: '*Inner'}
    - {name: f5, type: 'Inner[4]'}
'''
    url['code'] = '''
function tll_on_data(seq, name, data)
    -- Check that it's really deepcopy
    copy = tll_msg_deepcopy(data)
    copy.f3.extra = 10
    copy.f1[10] = 10
    copy.f4[1].extra = 10
    tll_callback(seq + 100, "Data", tll_msg_deepcopy(data))
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)

    def stringify(value):
        if isinstance(value, dict):
            return {k: stringify(v) for k,v in value.items()}
        elif isinstance(value, list):
            return [stringify(v) for v in value]
        return str(value)
    assert stringify(c.unpack(m).as_dict()) == url.sub('config.0.data').as_dict()

@asyncloop_run
async def test_optional(asyncloop):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data: {}
''')

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: pmap, type: uint8, options.pmap: yes}
    - {name: f0, type: int32, options.optional: yes}
    - {name: f1, type: int32}
    - {name: f2, type: int32, options.optional: yes}
'''
    url['code'] = '''
function tll_on_data(seq, name, data)
    tll_callback(seq + 100, "msg", {f0 = 10})
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': 10, 'f1': 0}

@asyncloop_run
async def test_open_params(asyncloop):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  seq: 0
  name: msg
  data: {}
''')

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: int32}
    - {name: f1, type: string}
'''
    url['code'] = '''
gf0 = 0
gf1 = ""
function tll_on_open(cfg)
    for k,v in pairs(cfg) do
        print(k, v)
    end
    gf0 = tonumber(cfg.f0)
    gf1 = cfg.f1
end
function tll_on_data(seq, name, data)
    tll_callback(seq + 100, "msg", {f0 = gf0, f1 = gf1})
end
'''
    c = asyncloop.Channel(url)
    c.open({"lua.f0": "10", "lua.f1": "string"})
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': 10, 'f1': "string"}

@asyncloop_run
async def test_require(asyncloop, tmp_path):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  name: msg
  data: {}
''')

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: string}
'''
    url['code'] = '''
local extra = require('extra')
tll_on_data = extra.on_data
'''
    url['lua.path.000'] = f'{tmp_path}/?.lua'

    with open(tmp_path / "extra.lua", "w") as fp:
        fp.write("""
function extra_on_data(seq, name, data)
    tll_callback(100, "msg", {f0 = "extra"})
end

return {on_data = extra_on_data}
""")
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': "extra"}

@asyncloop_run
async def test_preload(asyncloop, tmp_path):
    url = Url.parse('lua+null://;name=lua;dump=yes')
    url['lua.preload.0'] = '''
function preloaded_fn()
    tll_callback(100, 10, 'open')
end
'''
    url['code'] = '''
function tll_on_open(cfg)
    preloaded_fn()
end
'''

    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert m.data.tobytes() == b'open'

@asyncloop_run
async def test_table(asyncloop, tmp_path):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  name: msg
  data: {}
''')

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: string}
'''
    url['code'] = '''
function tll_on_data(seq, name, data)
    tll_callback({seq = 100, name = "msg", data = {f0 = "extra"}})
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.msgid, m.seq) == (10, 100)
    assert c.unpack(m).as_dict() == {'f0': "extra"}

@asyncloop_run
async def test_table_noname(asyncloop, tmp_path):
    url = Url.parse('lua+null://;name=lua;dump=yes')
    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: string}
'''
    url['code'] = '''
function tll_on_post(seq, name, data)
    tll_child_post({seq = 100, name = nil, data = {f0 = "extra"}})
end
'''

    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    with pytest.raises(OSError): c.post({}, name='msg')

@asyncloop_run
async def test_control(asyncloop, tmp_path):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
yaml.dump: yes
lua.dump: yes
autoclose: yes
config.0:
  msgid: 0
  data: ""
''')

    url['scheme-control'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: string}
'''
    url['code'] = '''
function tll_on_data(seq, name, data)
    tll_callback({type = "Control", seq = 100, name = "msg", data = {f0 = "extra"}})
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    m = await c.recv(0.001)
    assert (m.type, m.msgid, m.seq) == (m.Type.Control, 10, 100)
    assert c.unpack(m).as_dict() == {'f0': "extra"}

@asyncloop_run
async def test_fragile(asyncloop, tmp_path):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
fragile: yes
lua.dump: yes
null.dump: yes
''')

    url['code'] = '''
function tll_on_post(seq, name, data)
    if seq > 1 then
        somethingbad()
    end
    tll_child_post(seq, name, data)
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
    c.post(b'xxx', seq=0)
    assert c.state == c.State.Active
    with pytest.raises(TLLError): c.post(b'yyy', seq=2)
    assert c.state == c.State.Error

@pytest.mark.parametrize('mode,outer,inner', [
    ('int', '123.456', '"123456.e-3"'),
    ('float', '123.456', '"123456.e-3"'),
    ('int', '123.456', '"123.456"'),
    ('int', '123.456', '123456'),
    ('int', None, '123.456'),
    ('float', '123456', '123456'),
    ('float', '123.456', '123.456'),
    ('', '123.456', '123.456'),
    ('', None, '"xxx"'),
    ('', None, '{ a = 1 }'),
])
def test_fixed(context, mode, outer, inner):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
lua.dump: yes
''')
    url['fixed-mode'] = mode

    url['scheme'] = '''yamls://
- name: msg
  id: 10
  fields:
    - {name: f0, type: int32, options.type: fixed3}
'''
    url['code'] = f'''
function tll_on_open()
    tll_callback(100, "msg", {{ f0 = {inner} }})
end
'''
    c = Accum(url, context=context)
    c.open()
    if outer is None:
        assert c.state == c.State.Error
        return
    assert c.state == c.State.Active
    assert [(m.msgid, m.seq) for m in c.result] == [(10, 100)]
    assert c.unpack(c.result[-1]).as_dict() == {'f0': decimal.Decimal(outer)}

@pytest.mark.parametrize('mode', ['float', 'object'])
@pytest.mark.parametrize('iprec,oprec', [(3, 6), (3, 3), (6, 3)])
@asyncloop_run
async def test_fixed_convert(asyncloop, context, mode, iprec, oprec):
    url = Config.load('''yamls://
tll.proto: lua+yaml
name: lua
lua.dump: yes
yaml.dump: yes
lua.fragile: yes
autoclose: yes
config.0:
  name: Data
  data:
    f0: 123.456
''')
    url['fixed-mode'] = mode

    url['yaml.scheme'] = f'''yamls://
- name: Data
  id: 10
  fields:
    - {{name: f0, type: int32, options.type: fixed{iprec}}}
'''
    url['scheme'] = f'''yamls://
- name: Data
  id: 10
  fields:
    - {{name: f0, type: int32, options.type: fixed{oprec}}}
'''
    url['code'] = f'''
function tll_on_data(seq,name,data)
    tll_callback(seq, name, data)
end
'''
    c = asyncloop.Channel(url, context=context)
    c.open()
    m = await c.recv()
    assert c.unpack(m).as_dict() == {'f0': decimal.Decimal('123.456')}

@pytest.mark.parametrize('mode,t,value,expect', [
    ('error', 'int8', 128, None),
    ('error', 'int8', -129, None),
    ('error', 'uint8', 256, None),
    ('error', 'uint8', -128, None),
    ('error', 'byte4, options.type: string', '"abcde"', None),
    ('trim', 'int8', 128, 127),
    ('trim', 'int8', -129, -128),
    ('trim', 'uint8', 256, 255),
    ('trim', 'uint8', -1, 0),
    ('trim', 'byte4, options.type: string', '"abcde"', "abcd"),
])
def test_overflow(context, mode, t, value, expect):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
lua.dump: yes
''')
    url['overflow-mode'] = mode

    url['scheme'] = f'''yamls://
- name: msg
  id: 10
  fields:
    - {{name: f0, type: {t}}}
'''
    url['code'] = f'''
function tll_on_open()
    tll_callback(100, "msg", {{ f0 = {value} }})
end
'''
    c = Accum(url, context=context)
    c.open()
    if expect is None:
        assert c.state == c.State.Error
        return
    assert c.state == c.State.Active
    assert [(m.msgid, m.seq) for m in c.result] == [(10, 100)]
    assert c.unpack(c.result[-1]).as_dict() == {'f0': expect}

def test_self(context):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
lua.dump: yes
''')

    url['lua.scheme'] = '''yamls://[{name: External, id: 10}]'''
    url['null.scheme'] = '''yamls://[{name: Internal, id: 20}]'''
    url['code'] = f'''
function tll_on_open()
    assert(tll_self:scheme() ~= nil, "Self scheme is nil")
    assert(tll_self:scheme().messages.External ~= nil, "Self scheme does not have External message")
    assert(tll_self_child:scheme() ~= nil, "Child scheme is nil")
    assert(tll_self_child:scheme().messages.Internal ~= nil, "Child scheme does not have Internal message")
end
'''
    c = Accum(url, context=context)
    c.open()
    assert c.state == c.State.Active

def test_child_post(context):
    url = Config.load('''yamls://
tll.proto: lua+direct
name: lua
lua.dump: yes
direct.dump: yes
''')

    scheme = '''yamls://[{name: Data, id: 10, fields: [{name: f0, type: int32}]}]'''
    url['code'] = '''
function tll_on_open()
    tll_self_child:post(10, "Data", { f0 = 20 })
end
'''
    s = Accum('direct://', name='server', context=context, scheme=scheme)
    s.open()
    c = Accum(url, context=context, master=s)
    c.open()
    assert c.state == c.State.Active
    assert [m.seq for m in s.result] == [10]
    assert s.unpack(s.result[-1]).as_dict() == {'f0' : 20}

def test_close_on_open(context):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
''')

    url['code'] = '''
function tll_on_open()
    tll_self_child:close(true)
end
'''
    c = context.Channel(url)
    c.open()
    assert c.state == c.State.Closed

def test_close_on_data(context):
    url = Config.load('''yamls://
tll.proto: lua+zero
name: lua
''')

    url['code'] = '''
function tll_on_data()
    tll_self:close(true)
end
'''
    c = context.Channel(url)
    c.open()
    assert c.state == c.State.Active
    c.children[0].process()
    assert c.state == c.State.Closed

def test_child_config(context):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
''')

    url['code'] = '''
function tll_on_open()
    cfg = tll_self_child.config
    print(cfg)
    print(cfg.state)
    for k,v in pairs(cfg) do
        print(k, v)
    end
    assert(cfg.state == "Active", "Invalid child state: " .. tostring(cfg.state))
    assert(cfg["url.tll.proto"] == "null", "Invalid child proto: " .. tostring(cfg["url.tll.proto"]))
end
'''
    c = context.Channel(url)
    c.open()
    assert c.state == c.State.Active

@pytest.mark.parametrize("code", [
    'print',
    'tll_msg_copy',
    'tll_msg_deepcopy',
    ])
def test_validate(context, code):
    url = Config.load('''yamls://
tll.proto: lua+direct
name: lua
fragile: yes
''')

    url['code'] = f'''
function tll_on_data(seq, name, data)
    {code}(data)
    tll_callback(seq, name, data)
end
'''

    url['scheme'] = '''yamls://
- name: Inner
  id: 10
  fields:
    - {name: f0, type: string}

- name: Data
  id: 20
  fields:
    - {name: f0, type: int32}
    - {name: sub, type: Inner}
    - {name: f1, type: '*Inner'}
'''

    c = context.Channel(url)
    c.open()
    assert c.state == c.State.Active

    d = context.Channel('direct://', master=c)
    d.open()
    d.post(b'x' * 10, name='Data')

    assert c.state == c.State.Error
