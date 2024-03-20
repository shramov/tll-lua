#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import pytest

from tll.config import Config
from tll.channel.mock import Mock

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

DATA = '''yamls://
- name: Activate
  id: 10
- name: Done
  id: 20
- name: Data
  id: 30
  fields:
    - {name: seq, type: int64}
'''

CONTROL = '''yamls://
- name: Time
  id: 10
  fields:
    - {name: time, type: int64}
'''

def test(context, asyncloop):
    config = Config.load(f'''yamls://
mock:
  input:
    url: direct://
  output:
    url: direct://
channel:
  url: 'lua-measure://;tll.channel.input=input;tll.channel.output=output'
  open-mode: lua
  dump: yes
  quantile: '95,50,75'
''')

    config['mock.input.scheme'] = DATA
    config['mock.output.scheme-control'] = CONTROL
    config['channel.code'] = '''
function tll_on_data(seq, name, data)
    if name == 'Activate' then
        return 'active', -1
    elseif name == 'Done' then
        return 'close', -1
    elseif name == 'Data' then
        return data.seq
    end
    return -1
end
'''

    mock = Mock(asyncloop, config)
    mock.open()

    measure = mock.channel
    ic, oc = mock.io('input', 'output')
    assert measure.state == measure.State.Opening

    assert measure.scheme != None
    assert [m.name for m in measure.scheme.messages] == ['Data']

    oc.post({'time': 100}, seq=10, name='Time', type=oc.Type.Control)
    ic.post({}, name='Activate')

    assert [(m.type, m.msgid) for m in measure.result] == []

    assert measure.state == measure.State.Active

    ic.post({'seq': 10}, time=200, name='Data')

    assert [(m.type, m.seq) for m in measure.result] == [(measure.Type.Data, 10)]
    assert measure.unpack(measure.result[0]).as_dict() == {'name': '', 'value': 100}

    ic.post({}, name='Done')
    assert measure.state == measure.State.Closed
