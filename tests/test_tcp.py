#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator

from tll.config import Url

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

@asyncloop_run
async def test_binary(asyncloop, tmp_path):
    url = Url.parse(f'tcp-lua://{tmp_path}/tcp.sock;mode=server;name=server;dump=frame')
    url['code'] = '''
-- Frame size
frame_size = 13

function frame_pack(msg)
	return string.pack("<c5 I4 I4", "ZBXD\x01", msg.size, 0)
end

function frame_unpack(frame, msg)
	prefix, size = string.unpack("<c5 I4", frame)
	msg.size = size
	return frame_size
end
'''
    s = asyncloop.Channel(url)
    s.open()
    assert s.state == s.State.Active

    c = asyncloop.Channel(f'tcp://{tmp_path}/tcp.sock;frame=none;dump=frame;name=client')
    c.open()
    assert c.State.Active == await c.recv_state(0.01)

    m = await s.recv(0.001)
    assert m.type == m.Type.Control
    assert s.unpack(m).SCHEME.name == 'Connect'

    c.post(b'ZBXD\x01\x08\x00\x00\x00\x00\x00\x00\x0001234567')

    m = await s.recv(0.001)
    assert m.data.tobytes() == b'01234567'

    s.post(b'abcd', addr=m.addr)

    m = await c.recv(0.001)
    assert m.data.tobytes() == b'ZBXD\x01\x04\x00\x00\x00\x00\x00\x00\x00abcd'
