function compare_keys(t0, t1, msg)
    k0 = {}
    for k,v in pairs(t0) do print("k0 " .. k); table.insert(k0, k) end
    table.sort(k0)
    k1 = {}
    for k,v in pairs(t1) do print("k1 " .. k); table.insert(k1, k) end
    table.sort(k1)

    for i,v in ipairs(k0) do
        assert(k1[i] ~= nil, msg .. ": Extra key in first table: " .. tostring(v))
        assert(v == k1[i], msg .. ": Mismatched keys " .. tostring(v) .. " != " .. tostring(k1[i]))
    end
    assert(k1[#k0 + 1] == nil, msg .. ": Extra key in second table: " .. tostring(k1[#k0 + 1]))
end

function compare_tables(t0, t1, msg)
    compare_keys(t0, t1, msg)
    for k,v0 in pairs(t0) do
        print("Compare key " .. tostring(k))
        v1 = t1[k]
        if type(v0) == "table" and type(v1) == "table" then
            print("Compare tables at " .. tostring(k))
            compare_tables(v0, v1, msg .. "[" .. tostring(k) .. "]")
        else
            assert (v0 == v1, msg .. ": Mismatched values at " .. tostring(k) .. ": " .. tostring(v0) .. " != " .. tostring(v1))
        end
    end
end

function check_equals(a, b, msg)
    assert(a == b, msg .. " non equals: " .. tostring(a) .. " != " .. tostring(b))
end
