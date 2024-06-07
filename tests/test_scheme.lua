require 'compare'

function check_scheme(scheme)
    compare_keys(scheme.enums, { EGlobal = 0 }, "enums")
    check_equals(scheme.enums.EGlobal.type, "int16", "EGlobal.type")
    compare_tables(scheme.enums.EGlobal.values, { GA = 100, GB = 200 }, "EGlobal.values")
    compare_tables(scheme.enums.EGlobal.options, { ea = "eb", ec = "ed" }, "EGlobal.options")

    compare_keys(scheme.bits, { BGlobal = 0 }, "bits")
    check_equals(scheme.bits.BGlobal.type, "uint8", "BGlobal.type")
    compare_tables(scheme.bits.BGlobal.options, { ba = "bb", bc = "bd" }, "BGlobal.options")
    compare_tables(scheme.bits.BGlobal.values, { GA = { name = "GA", size = 1, offset = 0, value = 1}, GB = { name = "GB", size = 2, offset = 2, value = 12} }, "BGlobal.values")

    compare_keys(scheme.messages, { Sub = 0, Data = 0 }, "messages")

    msg = scheme.messages.Sub
    assert(msg.name == "Sub", "Invalid name for Sub: " .. msg.name)
    compare_keys(msg.enums, {}, "Sub.enums")
    compare_keys(msg.fields, { s0 = 0 }, "Sub.fields")
    check_equals(msg.fields.s0.type, "uint16", "Sub.s0")

    msg = scheme.messages.Data
    assert(msg.name == "Data", "Invalid name for Data: " .. msg.name)
    compare_tables(msg.options, { ma = "mb", mc = "md" }, "Data.options")
    compare_keys(msg.enums, { fenum = 0 }, "Data.enums")
    check_equals(msg.enums.fenum.type, "uint16", "Data.enums.fenum.type")
    compare_tables(msg.enums.fenum.values, { A = 10, B = 20 }, "Data.enums.fenum.values")
    compare_keys(msg.bits, { fbits = 0 }, "Data.bits")
    check_equals(msg.bits.fbits.type, "uint32", "Data.bits.fbits.type")
    compare_tables(msg.bits.fbits.values, { A = { name = "A", size = 1, offset = 0, value = 1 }, B = { name = "B", size = 1, offset = 1, value = 2 } }, "Data.bits.fbits.values")
    compare_keys(msg.fields, { fi32 = 0, fenum = 0, fbits = 0 }, "Data.fields")
    check_equals(msg.fields.fi32.type, "int32", "Data.fi32.type")
    compare_tables(msg.fields.fi32.options, { fa = "fb", fc = "fd" }, "Data.fi32.options")
    assert(msg.fields.fenum.type_enum ~= nil, "Data.fenum.type_enum is nil")
    check_equals(msg.fields.fenum.type, "uint16", "Data.fenum.type")
    check_equals(msg.fields.fenum.type_enum.name, "fenum", "Data.fenum.type_enum.name")
    check_equals(msg.fields.fbits.type, "uint32", "Data.fbits.type")
    check_equals(msg.fields.fbits.type_bits.name, "fbits", "Data.fbits.type_bits.name")
end

function tll_on_open()
    check_scheme(tll_child_scheme)
    check_scheme(tll_self_scheme)
end
