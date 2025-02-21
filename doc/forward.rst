tll-logic-lua-forward
=====================

:Manual Section: 7
:Manual Group: TLL
:Subtitle: Message forwarding logic with Lua conversion or filtering

Synopsis
--------

::

    tll.proto: lua-forward
    tll.channel.input: <input>
    tll.channel.output: <output>

Defined in module ``tll-logic-forward``


Description
-----------

Logic passes all data messages from input into Lua script and API to send them to output. Main
difference from ``lua+`` prefix is that message is parsed using input channel scheme and packed with
one from output channel.

Channels
~~~~~~~~

``input`` - input channel.

``output`` - output channel.

Init parameters
~~~~~~~~~~~~~~~

Logic supports all init parameters as ``lua+`` prefix described in ``tll-channel-lua(7)`` except
``fragile`` and scheme related settings.

``prefix-compat=<bool>``, default ``false``: register ``tll_callback`` function as an alias for
``tll_output_post`` so scripts for ``lua+`` prefix are compatible with forwarding logic.

Lua API
~~~~~~~

Logic exports basic TLL Lua functions like ``tll_msg_copy``, ``tll_msg_deepcopy`` as described in
``tll-channel-lua(7)`` and some specific ones.

``tll_self`` - self Channel object with keys ``close``, ``post``, ``config`` and others.

``tll_self_input``, ``tll_self_output`` - input and output Channel object.

``tll_on_data(seq, name, body, msgid, addr, time)`` - function called on each data message from
input, see description of ``tll_on_data`` in ``lua+`` documentation.

``tll_output_post(seq, name, body, addr)`` - post into output channel, faster variant of
``tll_self_output:post(...)``. When encoding message it uses output scheme. For full description of
parameters see ``tll_child_post`` in ``lua+`` documentation.

``tll_callback(...)`` registered as alias for ``tll_output_post`` in prefix compat mode.

Examples
--------

Forward data from input into output with scheme conversion and exit afterwards::

  processor.module:
    - module: tll-lua

  processor.objects:
    forward:
      init:
        tll.proto: lua-forward
        lua.child-mode: relaxed
        lua.code: >
          function tll_on_data(seq, name, data)
            tll_callback(seq, name, data)
          end
      channels: {input: input, output: output}
      depends: output
    output:
      init: file://output.dat;scheme=yaml://output.yaml;dir=w
    input:
      init: file://input.dat;autoclose=yes;shutdown-on=close
      depends: forward

See also
--------

``tll-channel-lua(7)``, ``tll-logic-common(7)``

..
    vim: sts=4 sw=4 et tw=100
