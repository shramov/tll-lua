#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import decorator
import pytest

from tll.config import Config

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

def test(context):
    i = context.Channel('direct://;name=input', scheme=DATA, dump='yes')
    ic = context.Channel('direct://;name=input-client', master=i, scheme=DATA)

    o = context.Channel('direct://;name=output', **{'scheme-control': CONTROL})
    oc = context.Channel('direct://;name=output-client', master=o, **{'scheme-control': CONTROL})

    measure = context.Channel('lua-measure://;name=lua;tll.channel.input=input;tll.channel.output=output;open-mode=lua', code = f'''
function luatll_on_data(seq, name, data)
    if name == 'Activate' then
        return 'active', -1
    elseif name == 'Done' then
        return 'close', -1
    elseif name == 'Data' then
        return data.seq
    end
    return -1
end
''')
    i.open()
    ic.open()

    o.open()
    oc.open()

    measure.open()
    assert measure.state == measure.State.Opening

    oc.post({'time': 100}, seq=10, name='Time', type=oc.Type.Control)
    ic.post({}, name='Activate')

    assert measure.state == measure.State.Active

    ic.post({'seq': 10}, time=200, name='Data')

    ic.post({}, name='Done')
    assert measure.state == measure.State.Closed