#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import datetime
import decorator
import pathlib
import pytest
import time

from tll.chrono import TimePoint, Duration

@decorator.decorator
def asyncloop_run(f, asyncloop, *a, **kw):
    asyncloop.run(f(asyncloop, *a, **kw))

@pytest.fixture
def marker(tmp_path):
    return tmp_path / 'marker'

def channels(asyncloop, marker, hour, minute):
    SCHEME = '''yamls://[{name: absolute, id: 10, fields: [{name: ts, type: int64, options: {type: time_point, resolution: ns}}]}]'''

    srcdir = pathlib.Path(__file__).parent.parent / 'examples'
    s = asyncloop.Channel('direct://', name='timer', scheme=SCHEME)
    c = asyncloop.Channel(f'lua+direct://;start-hour={hour};start-minute={minute}', master=s, name='daily', code=f'file://{srcdir}/daily.lua', filename=marker)

    return s, c

def mfmt(ts):
    return ts.strftime('%Y-%m-%d')

@asyncloop_run
async def test_daily(asyncloop, marker):
    now = datetime.datetime.now()
    if now.hour == 23 and now.minute in (58, 59):
            pytest.skip("Bad time for this tests")

    s, c = channels(asyncloop, marker, 23, 59)

    s.open()
    c.open()

    assert c.state == c.State.Active

    m = await s.recv()
    prev = s.unpack(m).ts
    assert prev.datetime == datetime.datetime(now.year, now.month, now.day, 23, 59, 0)

    s.post({}, name='absolute')
    m = await c.recv()
    assert s.unpack(m).ts == TimePoint(0)
    m = await s.recv()

    assert s.unpack(m).ts == prev + Duration(1, 'day')
    assert marker.exists()
    with open(marker) as fp:
        fp.read() == mfmt(now)

today = datetime.date.today()
@pytest.mark.parametrize('v,r', [
        (None, 'now'),
        (mfmt(today + datetime.timedelta(days=1)), 'now'),
        (mfmt(today), 'next'),
        ])
@asyncloop_run
async def test_marker(asyncloop, tmp_path, marker, v, r):
    now = datetime.datetime.now()
    if now.hour == 23 and now.minute in (58, 59):
        pytest.skip("Bad time for this tests")

    s, c = channels(asyncloop, marker, 0, 0)

    if v is not None:
        with open(marker, 'w') as fp:
            fp.write(v)

    s.open()
    c.open()

    assert c.state == c.State.Active

    m = await s.recv()
    prev = s.unpack(m).ts
    if r == 'next':
        ts = datetime.datetime(now.year, now.month, now.day, 0, 0, 0) + datetime.timedelta(days=1)
        assert prev.datetime == ts
    else:
        assert prev >= TimePoint(now.replace(microsecond=0))
        assert prev <= TimePoint(time.time(), 'second')
