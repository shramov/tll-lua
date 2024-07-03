#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import os
import pytest

from tll.config import Config, Url
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
