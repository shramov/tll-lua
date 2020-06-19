-- Frame size
frame_size = 13

function frame_pack(msg)
	return string.pack("c5 I8", "ZBXD\x01", msg.size)
end

function frame_unpack(frame, msg)
	prefix, size = string.unpack("c5 I8", frame)
	msg.size = size
	return frame_size
end
