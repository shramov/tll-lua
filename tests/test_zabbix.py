#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import json
import pathlib
import pytest

from tll.config import Config, Url
from tll.error import TLLError
from tll.channel.mock import Mock

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

@asyncloop_run
async def test(asyncloop):
    cfg = Config.load(f'''yamls://
mock:
  input: direct://;scheme=yaml://tll/logic/stat.yaml
channel:
  tll.proto: python
  python: {pathlib.Path(__file__).parent}/../zabbix/zabbix:Zabbix
  uplink: direct://;master=uplink;dump=yes
  tll.channel:
    input: input
  name: zabbix
''')
    uplink = asyncloop.Channel('direct://', name='uplink')
    uplink.open()

    mock = Mock(asyncloop, cfg)
    mock.open()

    mock.io('input').post(
        {
            'node': 'test-node',
            'name': 'page',
            'fields': [
                {'name': 'fi', 'unit': 'Bytes', 'value': {'ivalue': {'value': 1024}}},
                {'name': 'ff', 'unit': 'Unknown', 'value': {'fvalue': {'value': 123.123}}},
                {'name': 'gi', 'unit': 'Unknown', 'value': {'igroup': {'count': 10, 'min': 0, 'max': 100, 'avg': 50}}},
                {'name': 'gf', 'unit': 'Unknown', 'value': {'fgroup': {'count': 10, 'min': 0.1, 'max': 100.1, 'avg': 50.5}}},
            ],
        },
        name='Page')

    m = await uplink.recv()
    r = json.loads(m.data.tobytes())
    data = {i['key']: i for i in r.pop('data')}

    assert r == {'request': 'sender data'}
    assert sorted(data.keys()) == ['page.ff', 'page.fi.bytes', 'page.gf', 'page.gi']
    assert data['page.ff'] == {'clock': 0, 'ns': 0, 'host': 'test-node', 'key': 'page.ff', 'value': 123.123}
    assert data['page.fi.bytes'] == {'clock': 0, 'ns': 0, 'host': 'test-node', 'key': 'page.fi.bytes', 'value': 1024}
    assert data['page.gf'] == {'clock': 0, 'ns': 0, 'host': 'test-node', 'key': 'page.gf', 'value': 50.5}
    assert data['page.gi'] == {'clock': 0, 'ns': 0, 'host': 'test-node', 'key': 'page.gi', 'value': 50}
