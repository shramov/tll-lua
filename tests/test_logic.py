#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import os
import pytest

from tll.config import Config, Url
from tll.error import TLLError
from tll.channel.mock import Mock

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

@asyncloop_run
async def test(asyncloop):
    cfg = Config.load('''yamls://
mock:
  input0: direct://
  input1: direct://
  output: direct://
  extra: direct://
channel:
  tll.proto: lua
  tll.channel:
    input: input0, input1
    output: output
    extra: extra
''')
    cfg['channel.lua.path.test'] = f'{os.path.dirname(os.path.abspath(__file__))}/?.lua'
    cfg['channel.code'] = '''
require 'compare'

function tll_on_open(cfg)
    names = {}
    for k,v in pairs(tll_self_channels) do
        print("Tag", k)
        names[k] = {}
        for i,c in ipairs(v) do
            print("Tag", k, "channel", c.name)
            names[k][i] = c.name
        end
    end
    compare_tables(names, { input = {"input0", "input1"}, output = {"output"}, extra = {"extra"}}, "tll_self_channels")
end

function tll_on_channel_input(channel, type, seq, name, data)
    if type ~= 0 then return; end
    for i,c in ipairs(tll_self_channels.output) do
        c:post(seq, name, tostring(data) .. ":" .. channel.name)
    end
end

function tll_on_channel(channel, type, seq, name, data)
    if type ~= 0 then return; end
    for i,c in ipairs(tll_self_channels.extra) do
        c:post(seq, name, tostring(data) .. ":" .. channel.name)
    end

    if data == "close" then
        tll_self:close()
    end
end

function tll_on_post(type, seq, name, data)
    for i,c in ipairs(tll_self_channels.output) do
        c:post(seq, name, tostring(data) .. ":" .. 'post')
    end
end
'''

    mock = Mock(asyncloop, cfg)
    mock.open()

    mock.io('input0').post(b'i0', seq=0)
    m = await mock.io('output').recv()
    assert (m.seq, m.data.tobytes()) == (0, b'i0:input0')

    mock.io('input1').post(b'i1', seq=1)
    m = await mock.io('output').recv()
    assert (m.seq, m.data.tobytes()) == (1, b'i1:input1')

    mock.io('output').post(b'out', seq=2)
    m = await mock.io('extra').recv()
    assert (m.seq, m.data.tobytes()) == (2, b'out:output')

    mock.channel.post(b'post', seq=4)
    m = await mock.io('output').recv()
    assert (m.seq, m.data.tobytes()) == (4, b'post:post')

    mock.io('extra').post(b'close', seq=3)
    m = await mock.io('extra').recv()
    assert (m.seq, m.data.tobytes()) == (3, b'close:extra')

    assert mock.channel.state == mock.channel.State.Closed

def test_forward_checks(context):
    channels = [context.Channel(f'null://;name=c{idx}') for idx in range(4)]
    with pytest.raises(TLLError): context.Channel('lua-forward://;name=forward;tll.channel.input=c0;tll.channel.output=c1,c2')
    with pytest.raises(TLLError): context.Channel('lua-forward://;name=forward;tll.channel.input=c0;tll.channel.output=c1,c2')
    with pytest.raises(TLLError): context.Channel('lua-forward://;name=forward;tll.channel.input=c0,c1;tll.channel.output=c2')
    with pytest.raises(TLLError): context.Channel('lua-forward://;name=forward;tll.channel.input=c0,c1;tll.channel.output=c2,c3')
    context.Channel('lua-forward://;name=forward;tll.channel.input=c0;tll.channel.output=c1;code=""')

@asyncloop_run
async def test_forward(asyncloop):
    cfg = Config.load('''yamls://
mock:
  input.url: direct://
  output.url: direct://
channel:
  tll.proto: lua-forward
  tll.channel:
    input: input
    output: output
  lua.child-mode: relaxed
  name: forward
''')
    cfg['channel.code'] = '''
function tll_on_open(cfg)
    names = {}
    tll_self_output:post(10, "Data", {header = "open"})
end

function tll_on_data(seq, name, data)
    tll_output_post(seq, name, data)
end
'''
    cfg['mock.input.scheme'] = '''yamls://
- name: Data
  id: 10
  fields:
    - {name: f0, type: int32}
'''

    cfg['mock.output.scheme'] = '''yamls://
- name: Data
  id: 10
  fields:
    - {name: header, type: byte16, options.type: string}
    - {name: f0, type: uint8}
'''

    mock = Mock(asyncloop, cfg)
    mock.open()

    out = mock.io('output')
    assert out.unpack(await out.recv()).as_dict() == {'header': 'open', 'f0': 0}

    mock.io('input').post({'f0': 10}, name='Data')
    assert out.unpack(await out.recv()).as_dict() == {'header': '', 'f0': 10}

    mock.io('input').post({'f0': -1}, name='Data')
    assert mock.channel.state == mock.channel.State.Error
