all: read-options.rst convert-options.rst

read-options.rst: ../tll-read
	../tll-read --help-rst > $@
convert-options.rst: ../tll-convert
	../tll-convert --help-rst '' '' > $@
