tll-read
========

:Manual Section: 1
:Manual Group: TLL
:Subtitle: Read and print data from TLL channel

Synopsis
--------

``tll-read [--config CONFIG] [--message MESSAGES] [-s BEGIN[:END]] CHANNEL``


Description
-----------

Read messages from the channel and print them in human readable format if possible.

Options
-------

.. include::
        read-options.rst

Examples
--------

Seq ranges
~~~~~~~~~~

Examples below are for ``-s`` or ``--seq`` argument. Note that when ``-N:`` variant is used it
should be passed with ``=`` like ``--seq=-10:``, otherwise argument parser will treat it as an
option.

``:+10``: read first 10 messages

``+10:``: skip first 10 messages, print everything else

``+10``: skip first 10 messages and print one after

``-10:``: print everything starting from ``last-seq - 10``, where ``last-seq`` is last
sequence number reported by channel, not available if channel lacks ``info.seq`` variable in config.

``1000:+10``: print 10 messages starting from one with sequence number of ``1000``. If channel does
not support opening from seq (``seq`` open parameter) then 10 messages from the start would be
printed.

Additional channels
~~~~~~~~~~~~~~~~~~~

Read udp data from PCAP dump and treat first 4 bytes as a seq number of the message and print each
100 message:

::

    tll-read -m tll-pcap --master pcap://file.pcap --filter 'seq % 100 == 0' \
        'frame+pcap+udp://239.0.0.1:4444;frame.type=udp;frame=seq32'

Same can be done using config:

::

    tll-read -c pcap.yaml --filter 'seq % 100 == 0'

::

  modules:
  - tll-pcap
  master: 'pcap://file.pcap'
  channel: 'frame+pcap+udp://239.0.0.1:4444;frame.type=udp;frame=seq32'

Lua filters
~~~~~~~~~~~

Select messages that are not ``Heartbeat`` and have ``user`` field in the ``header`` submessage
equals to ``User``::

    name ~= "Heartbeat" and data.header.user == "User"

Alternatively same can be achieved using ``--message`` flag::

    tll-read --message Heartbeat --filter 'data.header.user == "User"' ...

Count all such messages without printing them (script file that should be used with ``-F`` flag)::

    count = nil
    function tll_on_open(cfg)
        count = 0
    end
    function tll_on_close()
        print("Heartbeat messages", count)
    end
    function tll_filter(seq, name, data, msgid, addr, time)
        if name ~= "Heartbeat" and data.header.user == "User" then
            count = count + 1
        end
        return false
    end

See also
--------

``tll-channel-lua(7)``

..
    vim: sts=4 sw=4 et tw=100
