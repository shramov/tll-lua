#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import os
import pytest

from tll.config import Config, Url

def test_scheme(context):
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
    url['lua.path.test'] = f'{os.path.dirname(os.path.abspath(__file__))}/?.lua'
    url['code'] = f'file://{os.path.dirname(os.path.abspath(__file__))}/test_scheme.lua'
    c = context.Channel(url)
    c.open()
    assert c.state == c.State.Active
