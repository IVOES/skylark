require("eu_conf")
require("eu_theme")
require("eu_accel")

function switch_theme(name)
    if (not eu_theme.load_default(name)) then
      do return 1 end
    end
    return 0
end

function excute_accel(none)
    if (not eu_accel.loadaccel()) then
        do return 1 end
    end
    return 0
end

function run(none)
    local name = eu_conf.loadconf()
    if (name == nil) then
        return 1
    elseif (name == "") then
        name = "default"
    end
    local ret = switch_theme(name)
    ret = excute_accel()
    return ret
end
