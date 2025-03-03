Positional arguments
~~~~~~~~~~~~~~~~~~~~

``CHANNEL``
source channel, if proto:// is missing treat as file://CHANNEL

Options
~~~~~~~

``-h`` ``--help``
show this help message and exit

``-l`` ``--loglevel``
logging level

``-c`` ``--config``
load CONFIG for additional parameters, if CHANNEL is empty - take it from config

``-S SCHEME`` ``--scheme SCHEME``
scheme url

``--no-autoclose``
do not autoclose channel

``--ignore-errors``
Skip messages that can not be unpacked

``--hex``
Print hex dump for all messages

``--text``
Print text representation instead of binary

``--dump-seq``
dump first and last seq of the channel and exit

``--dump-info``
dump info subtree from channel config

``--help-rst``
print help in RST format for manpage

``-s BEGIN[:[END]]`` ``--seq BEGIN[:[END]]``
define range of messages that should be printed.
One of three different forms can be used:

 - BEGIN - print only one message at BEGIN
 - BEGIN: - print all messages starting with BEGIN
 - BEGIN:END - print messages between BEGIN and END (including borders)

Borders (BEGIN and END) may be in one of the following forms:

 - +N - number of messages, for BEGIN - number of messages skipped from the start of the data
   stream, for END - number of messages printed from the BEGIN. For example ``:+1`` will print
   one message from the start and ``+1:`` will skip one and print everything else
 - -N - sequence number equals to last seq - N, for example -0 is last message, can be used only
   for BEGIN. Notice that -10 can print only one message if sequence numbers are not contingous,
   for example for stream (0, 100, 200, ..., 1000) -10 will be 990 and only 1000 matches range
   of [990, inf)
 - SEQ - sequence number of the message, integer value without + or - in the front


Channel options
~~~~~~~~~~~~~~~

``-m MODULE`` ``--module MODULE``
additional channel modules, can be given several times

``-E CHANNEL`` ``--extra-channel CHANNEL``
extra channels, can be given several times

``-M`` ``--master``
master object for reading channel

``--resolve ADDRESS``
address of resolve server to use in resolve:// channels

``-O KEY=VALUE`` ``--open KEY=VALUE``
open parameters, can be specified multiple times

``--poll``
enable polling, can reduce CPU load if script is used to monitor network channel like pub+tcp

Filtering
~~~~~~~~~

``-f`` ``--filter``
lua filter expression that can use variables ``seq``, ``name``, ``data``, ``addr`` and ``time``
and evaluates to boolean value. If expression is true then message is printed, otherwise it is skipped

``-F FILE`` ``--filter-file FILE``
lua file with filter script, for supported functions see manpage for tll-channel-lua

``--seq-list SEQ0[,SEQ1...]``
limit output to given seq numbers, comma separated list

``--message``
filter by message name, can be specified several times

``--dump-code``
dump generated lua filter code and exit, can be used to create skeleton file with empty functions

