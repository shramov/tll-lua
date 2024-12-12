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

``message-mode={auto|reflection|object|binary}``, default ``auto`` - how to wrap message that is
passed to message callbacks (``tll_on_post`` or ``tll_callback``):

  - ``auto`` - if scheme is available for this message type - pass reflection object (and fail
    if it is not found in the scheme), otherwise pass binary string.
  - ``reflection`` - pass reflection object, fail if there is no scheme or message is not found.
  - ``binary`` - always pass binary string as message body. Fastest method.
  - ``object`` - wrap ``tll_msg_t`` structure in Lua object, described in `Message API`_ section.

Encode and reflection parameters, described in ``Reflection`` and ``Encode`` sections in details.

``preset={filter|convert|convert-fast}``, default ``convert`` - choose field representation modes
according to task: filtering, where exact representation of decimal numbers is not important, or
conversion, where Decimal128 or Fixed fields can not be passed using binary floating point numbers.
This parameter changes default values for ``enum-mode``, ``bits-mode``, ``fixed-mode`` and
``decimal128-mode``:

  - ``filter``: easy comparison but may be rounding errors,
    ``fixed-mode=float``, ``decimal128-mode=float``, ``bits-mode=object``, ``enum-mode=string``;

  - ``convert``: comparison of Decimal128 and Fixed fields is done via attributes, but conversion is
    done without rounding errors between different field types (for example ``fixed3`` is converted
    to ``fixed6`` correctly),
    ``fixed-mode=object``, ``decimal128-mode=object``, ``bits-mode=object``, ``enum-mode=string``;

  - ``convert-fast``: fast conversion, can only be used to modify messages when scheme is not
    changed, otherwise it can lead to incorrect results (for example ``fixed3`` to ``fixed6``
    conversion will be incorrect),
    ``fixed-mode=int``, ``decimal128-mode=object``, ``bits-mode=int``, ``enum-mode=int``;

``enum-mode={string|int|object}``, default ``string`` - represent enum fields as string, raw integer
value or Lua object with ``string`` and ``int`` fields.

``bits-mode={object|int}``, default ``object`` - represent bits fields as raw integer value or Lua
object with named bit fields.

``decimal128-mode={float|object}``, default ``float`` - represent decimal128 fields as float value or
userdata with float/string members.

``fixed-mode={float|int|object}``, default ``float`` - represent fixed decimal fields as float value or
integer mantissa.

``overflow-mode={error|trim}``, default ``error`` - overflow policy, fail or trim values when
encoding.

``child-mode={strict|relaxed}``, default ``strict`` - child policy, raise error if unknown field is
requested from Message or Bits object or return ``nil``.

``pmap-mode={enable|disable}``, default ``enable`` - pmap policy, if disabled - return value as if
presence map fields does not exists. Also affects copy functions (``tll_msg_copy`` and
``tll_msg_deepcopy``). ``tll_msg_pmap_check`` function still can be used to check if field is
present in the message.

Script hooks
~~~~~~~~~~~~

Script can define any (or none) hook functions that are called when related event occures:

``tll_on_open(params)`` - called when channel is opening, for prefix channels scheme is not yet
filled and child not ready. Params is table filled with ``lua`` subtree of open parameters.

``tll_on_active()`` - called when channel becomes active, available only for prefix channels.

``tll_on_close()`` - called when channel is closed, either by user or by child channel

``tll_on_post(seq, name, body, msgid, addr, time)`` - called when message is posted into the
channel:

  ``seq`` - sequence number of the message, ``msg->seq``
  ``name`` - message name from the scheme, if there is no scheme - ``nil``
  ``body`` - table-like message reflection, if there is no scheme - string value of message body,
  unpacked using self scheme
  ``msgid`` - message id, integer
  ``addr`` - message address, signed integer (``i64`` of ``tll_addr_t``)
  ``time`` - message time in nanoseconds, signed integer

``tll_on_data(...)`` - called when child produces message (which is unpacked using child scheme),
arguments as in ``tll_on_post``

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
starting from ``body`` are optional and can be omitted. Child channel scheme is used to pack message
from reflection or table.

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
``tll_child_post`` function but self scheme is used to pack messages.

``tll_msg_copy(msg)`` - convert message reflection into Lua table. Reflection is read-only and can
not be modified or extended so if message conversion is required - it should be first copied. This
function performs shallow copy - submessages and arrays are placed into new table as is. If user
wants to modify element in submessage it should be copied too:

.. code-block:: lua

   copy = tll_msg_copy(msg)
   copy.header = tll_msg_copy(copy.header)
   copy.header.field = 10

``tll_msg_deepcopy(msg)`` - convert message reflection into Lua table recursively, traversing all
arrays (both fixed and offset), messages and unions. This operation is more expensive then
``tll_msg_copy`` and should be used only when really needed.

``tll_msg_pmap_check(msg, field)`` - check if field exists in the message: returns false if field is
optional and is not present, otherwise returns true.

``tll_self_scheme`` - data scheme of the channel, not set if there is no scheme. Deprecated, should
be replaced with ``tll_self.scheme``.

``tll_child_scheme`` - data scheme of the child channel, not set if there is no scheme. Deprecated,
should be replaced with ``tll_self_child.scheme``.

``tll_self`` - channel object for self (see `Channel API`_)

``tll_self_child`` - channel object for child (see `Channel API`_)

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

 - ``Decimal128`` is representation depends on ``decimal128-mode`` parameter:

   * ``float`` - simple floating point value that can be not exact but is more simple to use in
     scripts, should not be used when data is converted

   * ``object`` - reflection with ``float`` key returning it floating point value and ``string``
     with its string representation. Also ``tostring(value)`` function is working too but is slower
     then ``value.string``.

 - arrays and offset pointers are represented as ``Array`` reflection that emulates Lua list. It
   provides index access (starting from 1), length function and both ``pairs`` and ``ipairs``
   iteration methods.

 - submessages are pushed as ``Message`` reflection

 - unions are pushed as ``Union`` reflection with following access rules: special ``_tll_type`` key
   returns name of active union field, if requested key is equals to the name of current
   field - return it value or ``nil`` otherwise.

Supported field sub types:

 - representation of Enum is configurable:

   * ``string`` - pushed as its name, unknown values are not allowed

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

 - integer fields (for subtypes that are not supported) expects number types. Overflow or underflow,
   for example 1000 is invalid for ``int8`` and -1 for ``uint16``, is either an error or in ``trim``
   mode closest representable value is choosen for field type.

 - Double fields expects number type, converted from Lua number to double (which is same nowdays).

 - Decimal128 fields expects number, string or Decimal128 reflection.

 - Bytes expects string, checked if string lenght is too large. In ``trim`` overflow mode long
   strings are truncated to fit into the field.

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

   * ``object`` mode - wrap value into Lua object with ``float`` field, should be used when
     exact conversion without temporary float form is needed.

Channel API
~~~~~~~~~~~

Channel object has following properties and functions:

``post(self, ...)`` - post message, first argument is the channel object and other arguments are same as for
``tll_child_post`` descriped in `Library API`_.

``name`` - channel name, string

``scheme`` - channel scheme object, ``nil`` if not present.

``config`` - channel config object, behaves like table with indexing and iteration.

``context`` - channel context object.

``close(self, force=false)`` - close the channel, has optional boolean parameter ``force``.

Functions expects first argument to be channel object so they should be called with Lua ``:`` syntax
like ``channel:post(...)`` or ``channel:close()``.

Message API
~~~~~~~~~~~

Message wraps ``tll_msg_t`` structure pointer and provides access to it. However it's not allowed to
store this object for later use since it's data can be invalidated. Has following fields:

``seq`` - message sequence number, integer

``type`` - message type, for example Data or Control, integer

``msgid`` - message identifier, integer

``data`` - data, string that can contains data

``addr`` - message address, integer

``name`` - message name, available only if there was valid scheme for this message, otherwise
``nil``

``reflection`` - message reflection (see ``Reflection``), available only if there is valid scheme,
otherwise raises error on access

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
    if name ~= "Heartbeat" then
      data = tll_msg_copy(data)
      data.header = tll_msg_copy(data.header)
      data.header.embedded_seq = seq
    end
    tll_child_post(seq, name, data, addr)
  end

External variables
~~~~~~~~~~~~~~~~~~

Both init and open parameters can be used to pass variables into Lua script from processor config or
from user program that creates channel. These params are stored inside channel config under ``init``
and ``open`` keys respectively and can be accessed with ``tll_self.config["key..."]``. Additionaly
``lua`` subtree of open config is passed into ``tll_on_open`` hook. Following Python code
demonstrates all available ways::

  c = Channel('lua+null://;code=file://script.lua;a=b;c.d=e')
  c.open('lua.f=g')

Lua script:

.. code-block:: lua

  function tll_on_open(cfg)
    assert(cfg.f == "g")
    assert(tll_self.config["open.lua.f"] == "g")

    assert(tll_self.config["init.a"] == "b")
    assert(tll_self.config["init.c.d"] == "e")
  end

  function tll_on_data(seq, name, data)
    assert(tll_self.config["open.lua.f"] == "g")

    assert(tll_self.config["init.a"] == "b")
    assert(tll_self.config["init.c.d"] == "e")
  end

Data conversion
~~~~~~~~~~~~~~~

Lua can be used to convert data when scheme is changed in incompatible way - something is added in
the middle or field type is changed::

  lua+file://file.dat;lua.scheme=yaml://new.yaml;code=file://script.lua;child-mode=relaxed;fragile=yes

Lua script, that initializes new field for some messages and use implicit conversion for everything
else (``child-mode=relaxed`` parameter is needed to get ``nil`` for fields that are added in new
scheme):

.. code-block:: lua

  function tll_on_data(seq, name, data)
    if name == "Middle" and data.f0 > 10 then
      copy = tll_msg_copy(data)
      copy.middle = "f0 > 10"
      tll_callback(seq, name, copy)
    else
      tll_callback(seq, name, data)
    end
  end

New scheme:

.. code-block:: yaml

  - name: TypeChange
    id: 10
    fields:
      - { name: f0, type: int64 } # Was int32
      - { name: f1, type: byte16, options.type: string } # Was 8 byte string

  - name: Middle
    id: 20
    fields:
      - { name: f0, type: int32 }
      - { name: middle, type: string } # Added in new scheme
      - { name: f1, type: int32 }

Passing parameters
~~~~~~~~~~~~~~~~~~

There are 3 ways to pass external parameters to lua script:

 * init parameters that can be accessed anywhere via ``tll_self.config['init.PARAM']``, for example
   ``lua+null://;a=b;c.d=e`` can be retrieved as ``tll_self.config['init.a']`` and
   ``tll_self.config['init.c.d']``
 * open parameters are stored in same config as ``open.PARAM``, for example ``channel.open(a='b')``
   will be accessable as ``tll_self.config['open.a']``
 * open parameter prefixed with ``lua.`` are passed to ``tll_on_open(cfg)`` function as ``cfg``
   table with ``lua.`` part stripped, for example ``channel.open(**{'a': 'b', 'lua.c': 'd'})``
   provides ``cfg`` equals to ``{c = 'd'}``.

For example following code when used with ``lua+null://;a=init`` channel which is opened with ``{a:
open, lua.a: open-prefixed}`` will print ``open-prefix``, ``init`` and ``open``:

.. code-block:: lua

  function tll_on_open(cfg)
    print(cfg.a)
    print(tll_self.config['init.a'])
    print(tll_self.config['open.a'])
  end

See also
--------

``tll-channel-common(7)``

..
    vim: sts=4 sw=4 et tw=100
