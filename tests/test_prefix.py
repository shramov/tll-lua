#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decimal
import decorator
import enum
import pytest

from tll.config import Config, Url
from tll.error import TLLError

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

class f0(enum.Enum):
    A = 10
    B = 20
    C = 30

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
    ('uint16, options.type: bits, bits: [A, B, C]', ({'A': True, 'B': False, 'C': True}, '{A = 0x3, C = true}')),
    ('decimal128', (decimal.Decimal('123.456'), '"123.456"')),
    ('decimal128', (decimal.Decimal('123.456'), '123.456')),
    ('uint16, options.type: enum, enum: {A: 10, B: 20, C: 30}', (f0.A, '"A"')),
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
yaml.dump: yes
lua.dump: yes
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
    assert c.state == c.State.Active
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
async def test_iter(asyncloop):
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
async def test_iter(asyncloop):
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
async def test_copy(asyncloop):
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

@asyncloop_run
async def test_scheme(asyncloop, tmp_path):
    url = Config.load('''yamls://
tll.proto: lua+null
name: lua
fragile: yes
lua.dump: yes
null.dump: yes
''')

    url['scheme'] = '''yamls://
- enums:
    EGlobal: {type: int16, enum: {GA: 100, GB: 200}, options: {ea: eb, ec: ed}}
  bits:
    BGlobal: {type: uint8, bits: [GA, {name: GB, size: 2, offset: 2}], options: {ba: bb, bc: bd}}
- name: Sub
  fields:
    - {name: s0, type: uint16}
- name: Data
  id: 10
  options: {ma: mb, mc: md}
  fields:
    - {name: fi32, type: int32, options: {fa: fb, fc: fd}}
    - {name: fenum, type: uint16, options.type: enum, enum: {A: 10, B: 20}}
    - {name: fbits, type: uint32, options.type: bits, bits: [A, B]}
'''
    url['code'] = '''
function compare_keys(t0, t1, msg)
    k0 = {}
    for k,v in pairs(t0) do print("k0 " .. k); table.insert(k0, k) end
    table.sort(k0)
    k1 = {}
    for k,v in pairs(t1) do print("k1 " .. k); table.insert(k1, k) end
    table.sort(k1)

    for i,v in ipairs(k0) do
        assert(k1[i] ~= nil, msg .. ": Extra key in first table: " .. tostring(v))
        assert(v == k1[i], msg .. ": Mismatched keys " .. tostring(v) .. " != " .. tostring(k1[i]))
    end
    assert(k1[#k0 + 1] == nil, msg .. ": Extra key in second table: " .. tostring(k1[#k0 + 1]))
end

function compare_tables(t0, t1, msg)
    compare_keys(t0, t1, msg)
    for k,v0 in pairs(t0) do
        print("Compare key " .. tostring(k))
        v1 = t1[k]
        if type(v0) == "table" and type(v1) == "table" then
            print("Compare tables at " .. tostring(k))
            compare_tables(v0, v1, msg .. "[" .. tostring(k) .. "]")
        else
            assert (v0 == v1, msg .. ": Mismatched values at " .. tostring(k) .. ": " .. tostring(v0) .. " != " .. tostring(v1))
        end
    end
end

function check_equals(a, b, msg)
    assert(a == b, msg .. " non equals: " .. tostring(a) .. " != " .. tostring(b))
end

function check_scheme(scheme)
    compare_keys(scheme.enums, { EGlobal = 0 }, "enums")
    compare_tables(scheme.enums.EGlobal.values, { GA = 100, GB = 200 }, "EGlobal.values")
    compare_tables(scheme.enums.EGlobal.options, { ea = "eb", ec = "ed" }, "EGlobal.options")

    compare_keys(scheme.bits, { BGlobal = 0 }, "bits")
    compare_tables(scheme.bits.BGlobal.options, { ba = "bb", bc = "bd" }, "BGlobal.options")
    compare_tables(scheme.bits.BGlobal.values, { GA = { name = "GA", size = 1, offset = 0, value = 1}, GB = { name = "GB", size = 2, offset = 2, value = 12} }, "BGlobal.values")

    compare_keys(scheme.messages, { Sub = 0, Data = 0 }, "messages")

    msg = scheme.messages.Sub
    assert(msg.name == "Sub", "Invalid name for Sub: " .. msg.name)
    compare_keys(msg.enums, {}, "Sub.enums")
    compare_keys(msg.fields, { s0 = 0 }, "Sub.fields")

    msg = scheme.messages.Data
    assert(msg.name == "Data", "Invalid name for Data: " .. msg.name)
    compare_tables(msg.options, { ma = "mb", mc = "md" }, "Data.options")
    compare_keys(msg.enums, { fenum = 0 }, "Data.enums")
    compare_tables(msg.enums.fenum.values, { A = 10, B = 20 }, "Data.enums.fenum.values")
    compare_keys(msg.bits, { fbits = 0 }, "Data.bits")
    compare_tables(msg.bits.fbits.values, { A = { name = "A", size = 1, offset = 0, value = 1 }, B = { name = "B", size = 1, offset = 1, value = 2 } }, "Data.bits.fbits.values")
    compare_keys(msg.fields, { fi32 = 0, fenum = 0, fbits = 0 }, "Data.fields")
    compare_tables(msg.fields.fi32.options, { fa = "fb", fc = "fd" }, "Data.fi32.options")
    assert(msg.fields.fenum.type_enum ~= nil, "Data.fenum.type_enum is nil")
    check_equals(msg.fields.fenum.type_enum.name, "fenum", "Data.fenum.type_enum.name")
    check_equals(msg.fields.fbits.type_bits.name, "fbits", "Data.fbits.type_bits.name")
end

function tll_on_open()
    check_scheme(tll_child_scheme)
    check_scheme(tll_self_scheme)
end
'''
    c = asyncloop.Channel(url)
    c.open()
    assert c.state == c.State.Active
