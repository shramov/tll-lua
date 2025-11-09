Lua for TLL framework
=====================

Repository contains TLL_ module that allows writing channel implementations in Lua_ language and two
utilities: ``tll-read`` that reads and prints data from the channel (with different filtering
options, like user-defined Lua script) and ``tll-convert`` that forwards data from one channel into
another.

Lua prefixes can be used for data filtering or modification on the fly, for example there is error
in some data message following code can be used to fix it::

  function tll_on_data(seq, name, data)
      if name == 'SomeMessage' and data.field == "bad-value" then
          data = tll_msg_copy(data)
          data.field = "good-value"
      fi
      tll_callback(seq, name, data)
  end

And this code then can be hooked with ``lua+input-proto://...;lua.code=file://file.lua`` on input
channel. Documentation can be found in
`doc/lua.rst <https://github.com/shramov/tll-lua/blob/master/doc/lua.rst>`_.

Read and Convert utils
-----------------------

Command line utilities that can be used to inspect data files (and network streams like
``stream+pub+tcp://``) and to copy data into another channel with optional modification. For example
checking last message in the file (written by ``file://`` channel)::

  tll-read --seq -1: file.dat

Compress file::

  tll-convert --compression lz4 input.dat output.dat

Full documentation (with more examples) can be found in ``doc/`` directory:
`doc/read.rst <https://github.com/shramov/tll-lua/blob/master/doc/read.rst>`_ and
`doc/convert.rst <https://github.com/shramov/tll-lua/blob/master/doc/convert.rst>`_

Compilation
-----------

Module depends on TLL_, ``liblua5.3-dev``, ``libfmt-dev`` and uses meson_ build system (on
non-Debian systems names can differ). Compilation is straightforward::

  meson setup build
  ninja -vC build

.. _Lua: https://lua.org/
.. _TLL: https://github.com/shramov/tll/
.. _meson: https://mesonbuild.com/

..
  vim: sts=2 sw=2 et tw=100
