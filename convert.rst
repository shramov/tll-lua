tll-convert
===========

:Manual Section: 1
:Manual Group: TLL
:Subtitle: Copy or convert data from input to output

Synopsis
--------

``tll-convert [-L LUA-SCRIPT] INPUT OUTPUT``


Description
-----------

Read messages from input and write them into output channel with optional conversion.

Options
-------

.. include::
        convert-options.rst

Examples
--------

Compress file with lz4:

::

    tll-convert -c lz4 input.dat output.dat

Change field in the message:

::

    tll-convert -L convert.lua input.dat output.dat

Script file::

    function tll_on_data(seq, name, data)
        if name ~= "Heartbeat" and data.header.user == "User" then
            data = tll_msg_copy(data)
            data.header = tll_msg_copy(data.header)
            data.header = "NewUSer"
        end
        tll_callback(seq, name, data)
    end

See also
--------

``tll-channel-lua(7)``

..
    vim: sts=4 sw=4 et tw=100
