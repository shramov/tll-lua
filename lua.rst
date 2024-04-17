tll-channel-lua
===============

:Manual Section: 7
:Manual Group: TLL
:Subtitle: Lua interpreter in the channel

Synopsis
--------

``lua+CHILD://;lua.code=file://FILE.lua``

::

  init:
    tll.proto: lua+CHILD
    code: |
      EMBEDDED
      LUA
      CODE

Description
-----------

Channel embeds Lua interpreter and allows user to create relatively fast scripted implementations.
Not everything available to full bindings like Python is supported but most of functionality is in
place.

Init parameters
~~~~~~~~~~~~~~~

All parameters can be specified with ``lua.`` prefix.

``code=<string>`` - inline code or filename in ``file://FILENAME`` format

``path=<string>`` - additional Lua search path in format of ``LUA_PATH`` environment variable, list of
directories or modules separated with ``;``

``lua.path.**=<string>`` - additional search paths that are appended to ``path`` parameter

``lua.preload.**=<string>`` - additional code that is executed before loading main ``code``. Given
in the same format as ``code``: either inline or with ``file://`` prefix.

``fragile=<bool>``, default ``no`` - break on errors or tolerate them. Failed message is logged in
both cases.

``scheme=SCHEME``, default is none - data scheme that is used to encode and decode messages coming
into the channel. In cases of data conversion this parameter can be used to override child scheme.
If scheme is not present only binary body can be specified in the script.

``scheme-control=SCHEME``, default is none - scheme used for control messages.

Encode and reflection parameters, described in ``Reflection`` and ``Encode`` sections in details.

``enum-mode={string|int|object}``, default ``string`` - represent enum fields as string, raw integer
value or Lua object with ``string`` and ``int`` fields.

``bits-mode={object|int}``, default ``object`` - represent bits fields as raw integer value or Lua
object with named bit fields.

``fixed-mode={float|int}``, default ``float`` - represent fixed decimal fields as float value or
integer mantissa.

Script hooks
~~~~~~~~~~~~

Script can define any (or none) hook functions that are called when related event occures:

``tll_on_open(params)`` - called when channel becomes active, params is table filled with open
parameters

``tll_on_close()`` - called when channel is closed, either by user or by child channel

``tll_on_post(seq, name, body, msgid, addr, time)`` - called when message is posted into the
channel:

  ``seq`` - sequence number of the message, ``msg->seq``
  ``name`` - message name from the scheme, if there is no scheme - ``nil``
  ``body`` - table-like message reflection, if there is no scheme - string value of message body
  ``msgid`` - message id, integer
  ``addr`` - message address, signed integer (``i64`` of ``tll_addr_t``)

``tll_on_data(...)`` - called when child produces message, arguments as in ``tll_on_post``

``tll_filter(...)`` - special form of message callback used for filtering, replaced ``tll_on_data``.
If this function is defined then prefix is working in filtering mode (if not overriden by
``tll_prefix_mode``). Returns boolean value: true if message should be forwarded, false if it should
be dropped.

``tll_prefix_mode`` string variable that can be used to override filter detection rules: can be one
of ``filter`` or ``normal``.

Library API
~~~~~~~~~~~

Some functions and variables are pushed into global namespace:

``tll_child_post(seq, name, body, addr)``: post message into child channel, two modes are available
- table with message parameters (see below) or limited list of arguments as a fast path. Arguments
starting from ``body`` are optional and can be omitted.

  - ``seq`` - message sequence number, do not fill if it is not integer
  - ``name`` - name or message id of the message. If there is no scheme - only message id is
    supported. If message with this name is not found function fails.
  - ``body`` - message body: string, reflection or table.

    * string is placed into message data as is, without any checks or conversions. Only available
      mode without scheme.
    * reflection - if message is the same and no scheme conversion is needed - use it as is without
      parsing, otherwise treat it as table.
    * table - iterate over the table and take fields needed for the message. Extra fields in the
      table are ignored. Encoding rules are described in ``Encoding`` section.

``tll_child_post(table)`` second variant of the function, executed if first parameter is of table
type. Following fields are taken from the table:

  - ``type={Data|Control}``, default ``Data`` - message type, also defines scheme that is used for
    encoding

  - ``seq=<int>``, default ``0`` - message sequence number.

  - ``name=<string>`` - message name, either name or message id is needed for data encoding.

  - ``msgid=<int>`` - message id, ``name`` and ``msgid`` are mutually exclusive, only one should be
    used.

  - ``addr=<int>`` - message address.

  - ``data=<object>`` - message body, see description of ``body`` argument in previous function.

All fields are optional, however it is not possible to use ``data`` with table and without ``name``
or ``msgid`` fields. This function call is slower then previous one but gives more options.

``tll_callback(...)`` - generate message from the channel, arguments are same as in
``tll_child_post`` function.

``tll_msg_copy(msg)`` - convert message reflection into Lua table. Reflection is read-only and can
not be modified or extended so if message conversion is required - it should be first copied. This
function performs shallow copy - submessages and arrays are placed into new table as is. If user
wants to modify element in submessage it should be copied too:

.. code-block:: lua

   copy = tll_msg_copy(msg)
   copy.header = tll_msg_copy(copy.header)
   copy.header.field = 10

``tll_self_scheme`` - data scheme of the channel, not set if there is no scheme

``tll_child_scheme`` - data scheme of the child channel, not set if there is no scheme

Reflection
~~~~~~~~~~

Message body is passed into Lua as ``Message`` reflection, readonly object that behaves like table
filled with fields by name. If message has presence map (``pmap``) then accessing field that is
missing returns ``nil`` value, otherwise zeroed value is returned. If user tries to get field that
is not in the message then error is generated.

Field types are handled as following:

 - integer types that are not handled according to their sub type are pushed as integers

 - double values are pushed as numbers

 - bytes are pushed as strings but its size depends on sub type: for string it is ``strnlen(value,
   field->size)`` and ``field->size`` otherwise

 - offset string are pushed as Lua string honoring its length

 - ``Decimal128`` is represented as reflection with ``float`` key returning it floating point value
   and ``string`` with its string representation. Also ``tostring(value)`` function is working too but is
   slower then ``value.string``.

 - arrays and offset pointers are represented as ``Array`` reflection that emulates Lua list. It
   provides index access (starting from 1), length function and both ``pairs`` and ``ipairs``
   iteration methods.

 - submessages are pushed as ``Message`` reflection

 - unions are pushed as ``Union`` reflection with following access rules: special ``_tll_type`` key
   returns name of active union field, if requested key is equals to the name of current
   field - return it value or ``nil`` otherwise.

Supported field sub types:

 - representation of Enum is configurable:

   * ``string`` - pushed as its name

   * ``int`` - pushed as its integer value

   * ``object`` - pushed as ``Enum`` reflection with ``int`` and ``string`` fields (as above) and
     ``eq`` field that can be used to compare it to either string, int or another enum value.

 - Bits are also configurable:

   * ``object`` (default) - pushed as ``Bits`` reflection with key for every bit field with its
     value, boolean for 1 bit keys and integer for wider variants.

   * ``int`` - pushed as raw integer value

 - Fixed decimal fields are also configurable:

   * ``float``: converted into floating point value, suited for most cases but can lead to rounding
     errors.

   * ``int``: pushed as integer mantissa value without any math operations, for example for
     ``fixed3`` and value 123.456 it will be 123456.

Encoding
~~~~~~~~

Messages are encoded from tables in the following way: for each field value is taken from the table
using field name. If key is missing (or value is ``nil`` which is same in Lua) then field is
skipped. Then value is converted depending on the field type:

 - integer fields (for subtypes that are not supported) expects number types. Value is converted with
   boundary checks, for example 1000 is invalid for ``int8`` and -1 for ``uint16``.

 - Double fields expects number type, converted from Lua number to double (which is same nowdays).

 - Decimal128 fields expects number, string or Decimal128 reflection.

 - Bytes expects string, checked if string lenght is too large.

 - string (offset pointer) expects string, copied as is.

 - Array expects table with non-negative length, checks for overflow.

 - Pointer behaves like Array but without size check

 - Message expect table and encodes submessage.

Subtype rules:

 - Enums can be encoded either from string, integer value or ``Enum`` reflection.

 - Bits can be encoded from raw integer value or table that behaves like ``Bits`` reflection
   described in ``Reflection`` section: table filled with bit names, missing fields are filled with
   0

 - Fixed decimal fields are encoded from string or number values. String is parsed as decimal value
   without temporary binary floating point form. Number values are treated differently depending on
   configuration:

   * ``float`` mode - convert binary floating point value into decimal fixed point by multiplying it
     with 10^precision

   * ``int`` mode - treat value as a mantissa, do not perform multiplication

Examples
--------

Count Heartbeat messsages in the file, print result and generate control message with counter:

::

  lua+file://file.dat;code=file://count.lua;scheme-control=yaml://control.yaml

Control scheme::

  - name: Count
    id: 100
    fields:
      - {name: count, type: uint32}

Lua code:

.. code-block:: lua

  count = 0
  function tll_on_open(cfg)
    print("Start counting")
  end

  function tll_on_data(seq, name, data)
    if name == "Heartbeat" then
      count = count + 1
    end
  end

  function tll_on_close()
    print("Heartbeat messages: ", count)
    tll_callback({type = "Control", name = "Count", data = { count = count }})
  end

Include seq into header in posted messages that are not Heartbeat:

.. code-block:: lua

  function tll_on_post(seq, name, data, msgid, addr)
    if name != "Heartbeat" then
      data = tll_msg_copy(data)
      data.header = tll_msg_copy(data.header)
      data.header.embedded_seq = seq
    end
    tll_child_post(seq, name, data, addr)
  end

See also
--------

``tll-channel-common(7)``

..
    vim: sts=4 sw=4 et tw=100
