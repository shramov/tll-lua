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

function check_scheme(scheme)
    compare_keys(scheme.enums, { EGlobal = 0 }, "enums")
    compare_tables(scheme.enums.EGlobal.values, { GA = 100, GB = 200 }, "EGlobal.values")
    compare_tables(scheme.enums.EGlobal.options, { ea = "eb", ec = "ed" }, "EGlobal.options")

    compare_keys(scheme.bits, { BGlobal = 0 }, "bits")
    compare_tables(scheme.bits.BGlobal.options, { ba = "bb", bc = "bd" }, "BGlobal.options")
    compare_tables(scheme.bits.BGlobal.values, { GA = { name = "GA", size = 1, offset = 0, value = 1}, GB = { name = "GB", size = 2, offset = 2, value = 12} }, "BGlobal.values")

    compare_keys(scheme.messages, { Sub = 0, Data = 0 }, "messages")

    msg = scheme.messages.Sub
    assert(msg.name == "Sub", "Invalid name for Sub: " .. msg.name)
    compare_keys(msg.enums, {}, "Sub.enums")
    compare_keys(msg.fields, { s0 = 0 }, "Sub.fields")

    msg = scheme.messages.Data
    assert(msg.name == "Data", "Invalid name for Data: " .. msg.name)
    compare_tables(msg.options, { ma = "mb", mc = "md" }, "Data.options")
    compare_keys(msg.enums, { fenum = 0 }, "Data.enums")
    compare_tables(msg.enums.fenum.values, { A = 10, B = 20 }, "Data.enums.fenum.values")
    compare_keys(msg.bits, { fbits = 0 }, "Data.bits")
    compare_tables(msg.bits.fbits.values, { A = { name = "A", size = 1, offset = 0, value = 1 }, B = { name = "B", size = 1, offset = 1, value = 2 } }, "Data.bits.fbits.values")
    compare_keys(msg.fields, { fi32 = 0, fenum = 0, fbits = 0 }, "Data.fields")
    compare_tables(msg.fields.fi32.options, { fa = "fb", fc = "fd" }, "Data.fi32.options")
    assert(msg.fields.fenum.type_enum ~= nil, "Data.fenum.type_enum is nil")
    check_equals(msg.fields.fenum.type_enum.name, "fenum", "Data.fenum.type_enum.name")
    check_equals(msg.fields.fbits.type_bits.name, "fbits", "Data.fbits.type_bits.name")
end

function tll_on_open()
    check_scheme(tll_child_scheme)
    check_scheme(tll_self_scheme)
end

