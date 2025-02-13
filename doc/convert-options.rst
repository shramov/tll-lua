Positional arguments
~~~~~~~~~~~~~~~~~~~~

``INPUT``
source file or channel

``OUTPUT``
destination file or channel

Options
~~~~~~~

``-h`` ``--help``
show this help message and exit

``--no-overwrite``
do not overwrite output file

``-c`` ``--config``
load CONFIG for additional parameters

``-C``
compression type: none or lz4, passed to output channel as compression parameter

``--io``
IO type: posix or mmap, passed to input and output channels as file.io parameter

``-S`` ``--scheme``
output scheme, by default input scheme is copied into output

``--loglevel``
logging level

``-L FILE`` ``--lua-file FILE``
lua file with conversion script, for supported functions see manpage for tll-channel-lua

``-m MODULE`` ``--module MODULE``
additional channel modules, can be given several times

``-E CHANNEL`` ``--extra-channel CHANNEL``
extra channels, can be given several times

``-O KEY=VALUE`` ``--open KEY=VALUE``
open parameters, can be specified multiple times

``--defaults KEY=VALUE``
default parameters, can be specified multiple times

``--poll``
enable polling, can reduce CPU load if script is used to monitor network channel like pub+tcp

``--help-rst``
print help in RST format for manpage

