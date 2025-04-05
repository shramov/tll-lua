io = require('io')
os = require('os')

local next = 0
local FILENAME = nil

function marker_read(filename)
	local fp, err, code = io.open(filename, 'r')
	if fp == nil then
		if code == 2 then -- ENOENT
			return nil
		end
		error("Failed to open file '" .. filename .. "': " .. err)
	end
	local marker = fp:read('*line')
	fp:close()
	return marker
end

function marker_write(filename, marker)
	local fp, err = io.open(filename .. '.tmp', 'w')
	if fp == nil then error("Failed to open file '" .. filename .. ".tmp': " .. err) end
	fp:write(marker)
	fp:close()
	local r, err = os.rename(filename .. '.tmp', filename)
	if r == nil then error("Failed to rename temporary file to '" .. filename .. "': " .. err) end
end

function tll_on_active()
	FILENAME = tll_self.config['init.filename']
	if FILENAME == nil then error("'filename' property is missing") end

	local now = os.time()
	local d = os.date("*t", now)
	d.hour = tll_self.config['init.start-hour']
	d.min = tll_self.config['init.start-minute']
	d.sec = 0
	if d.min == nil then d.min = 0 end
	if d.hour == nil then error("start-hour parameter is missing") end

	next = os.time(d)
	if next < now then
		if marker_read(FILENAME) ~= os.date("%Y-%m-%d", now) then
			tll_child_post(0, "absolute", {ts = now})
			return
		end
		next = next + 86400
	end
	tll_child_post(0, "absolute", {ts = next})
end

function tll_on_data(seq, name, data)
	next = next + 86400
	marker_write(FILENAME, os.date("%Y-%m-%d\n", os.time()))
	tll_child_post(0, "absolute", {ts = next})
	tll_callback(seq, name, data)
end
