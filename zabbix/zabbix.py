#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import json

from tll.channel.logic import Logic

class Zabbix(Logic):
    def _init(self, cfg, master=None):
        super()._init(cfg, master)
        self._input = set(self._channels.get('input', []))
        self._uplink = self.context.Channel(self._child_url_fill(cfg.get_url('uplink'), 'uplink'))
        self._uplink.callback_add(self)
        self._child_add(self._uplink, 'uplink')

    def _free(self):
        if getattr(self, '_uplink', None):
            self._uplink.close()
        self._uplink = None

    def _close(self):
        super()._close()
        self._uplink.close()

    def _open(self, cfg):
        super()._open(cfg)
        self._pending = []

    def _logic(self, channel, msg):
        if channel in self._input:
            return self._on_input(channel, msg)

    def _on_input(self, channel, msg):
        if msg.type != msg.Type.Data:
            return
        m = channel.unpack(msg)
        if m.SCHEME.name != 'Page':
            return

        seconds = m.time.convert('second', int)
        ns = m.time.convert('ns', int).value % 1000000000

        for f in m.fields:
            if f.value.type == 'ivalue':
                value = f.value.value.value
            elif f.value.type == 'fvalue':
                value = f.value.value.value
            elif f.value.type == 'igroup':
                value = f.value.avg
            elif f.value.type == 'fgroup':
                value = f.value.avg
            else:
                continue
            key = f'{m.name}.{f.name}'
            if f.unit == f.unit.Bytes:
                key += '.bytes'
            self._pending.append({'host': m.node, 'key': key, 'value': value, 'clock': seconds.value, 'ns': ns})

        if self._uplink.state == self._uplink.State.Closed:
            self._uplink.open()

    def __call__(self, channel, msg):
        if msg.type == msg.Type.State:
            state = channel.State(msg.msgid)
            if state == channel.State.Active:
                print(self._pending)
                data = {'request': 'sender data', 'data': self._pending}
                self._pending = []
                self._uplink.post(json.dumps(data).encode('utf-8'))
            elif state in (channel.State.Error, channel.State.Closed):
                self._update_dcaps(self.DCaps.Process | self.DCaps.Pending)

    def _process(self, timeout, flags):
        self._update_dcaps(self.DCaps.Zero, self.DCaps.Process | self.DCaps.Pending)
        state = self._uplink.state
        if state == self._uplink.State.Error:
            self._uplink.close()
        elif state == self._uplink.State.Closed:
            if self._pending:
                self._uplink.open()
