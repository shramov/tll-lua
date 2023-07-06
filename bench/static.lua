message = {f0 = 0, f1 = 10, f2 = 20, f3 = 30, f4 = 40, f5 = 50, f6 = 60, f7 = 70, f8 = 80, f9 = 90 }
counter = 0
function luatll_on_post(seq, name, data)
    counter = counter + 1
    message.f0 = counter
    luatll_post(seq, "Many", message)
end
