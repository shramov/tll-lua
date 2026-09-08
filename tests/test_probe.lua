local Probe = {}
Probe.__index = Probe

-- Constructor
function Probe.new(init, open)
	local self = setmetatable({}, Probe)
	self.init = init
	self.open = open
	self.metric = nil
	return self
end

-- Generate tests
function tll_probe_generate(cfg)
	local r = {}
	for i = 1, 4 do
		init = {idx =  tostring(i)}
		open = {bind =  '127.0.0.' .. tostring(i) .. ':0'}
		r[i] = Probe.new(init, open)
		print("Add probe", i)
	end
	return r
end

function tll_probe_select(list)
	local r = nil
	for i,p in pairs(list) do
		if r == nil then r = p end
		print("Check", p.index, p.metric)
		if p.metric > r.metric then r = p end
	end
	if r == nil then
		error("No suitable probes")
	end
	return r.index
end

function Probe:on_active(type, seq, name, data)
	print("On active", self.index, type, name)
	if type ~= TLL_MESSAGE_STATE or name ~= "Active" then return end
	self.channel:post(10, nil, "Hello " .. tostring(self.index))
	return "login"
end

function Probe:on_login(type, seq, name, data)
	print("On login", self.index, data)
	self.metric = tonumber(data)
	return "done"
end
