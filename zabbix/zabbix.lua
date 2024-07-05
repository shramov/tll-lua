-- Frame size
frame_size = 13

function frame_pack(msg)
	return string.pack("<c5 I4 I4", "ZBXD\x01", msg.size, 0)
end

function frame_unpack(frame, msg)
	prefix, size = string.unpack("<c5 I4", frame)
	msg.size = size
	return frame_size
end
