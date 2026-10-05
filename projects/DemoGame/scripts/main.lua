-- DemoGame v3: физика + триггеры + raycast + звук на нодах сцены
-- Box = игрок, Spinner = статичная цель, Gem = триггер-зона
-- Тап Box = импульс+звук+raycast, тап Gem = звук, GO = гравитация/hitboxes

local JUMP, HIT, SHOOT

function on_start()
    set_var("taps", 0)
    set_var("spinning", 0)
    set_var("angle", 0)
    set_var("grav", 0)

    JUMP  = load_sound("assets/sounds/jump.wav", true)
    HIT   = load_sound("assets/sounds/hit.wav",  true)
    SHOOT = load_sound("assets/sounds/shoot.wav",true)
    play_music("assets/music/demo.wav", 0.3)

    set_gravity(0)                 -- плаваем, чтобы без ground ничего не улетело
    add_rigidbody("Box", 1.0)
    add_staticbody("Spinner")
    add_trigger("Gem")
    set_friction("Box", 0.0)
    set_bounce("Box", 0.9)
    show_hitboxes(false)

    print("demo3 ready: physics+triggers+raycast+sound")
end

function on_update(dt)
    if get_var("spinning") == 1 then
        set_var("angle", get_var("angle") + 3)
        set_rot("Spinner", get_var("angle"))
    end
end

function boxTap()
    set_var("taps", get_var("taps") + 1)
    set_color("Box", "#2EC4B6")

    add_velocity("Box", (math.random()-0.5)*600, (math.random()-0.5)*600)
    set_spin("Box", (math.random()-0.5)*8)
    if JUMP then play_sound(JUMP, 0.9, false) end

    local bx,by = get_body_x("Box"), get_body_y("Box")
    local sx,sy = get_body_x("Spinner"), get_body_y("Spinner")
    local name = raycast(bx, by, sx, sy, "Box")
    if name then
        set_color("Spinner", "#FF5555")
        if HIT then play_sound(HIT, 0.7, false) end
        print("raycast hit: "..name)
    else
        print("raycast: no hit")
    end
    print("box tapped: "..get_var("taps"))
end

function gemTap()
    set_scale("Gem", 1.5, 1.5)
    set_color("Gem", "#FFD966")
    if SHOOT then play_sound(SHOOT, 0.8, 1.2) end
    print("gem tapped")
end

function go()
    set_var("spinning", 1)
    if get_var("grav") == 0 then
        set_gravity(900); set_var("grav",1); show_hitboxes(true)
        print("GO! gravity ON + hitboxes")
    else
        set_gravity(0);   set_var("grav",0); show_hitboxes(false)
        print("gravity OFF")
    end
end

-- глобальные хуки физики (Physics.hpp дёргает по имени)
function on_collide(a,b)
    if (a=="Box" and b=="Spinner") or (b=="Box" and a=="Spinner") then
        set_color("Box","#FF9933")
        if HIT then play_sound(HIT,0.5,0.9) end
    end
end
function on_trigger(a,b)
    if (a=="Box" and b=="Gem") or (b=="Box" and a=="Gem") then
        set_color("Gem","#33FF99"); print("TRIGGER enter")
    end
end
function on_trigger_exit(a,b)
    if (a=="Box" and b=="Gem") or (b=="Box" and a=="Gem") then
        set_color("Gem","#FF5555"); print("TRIGGER exit")
    end
end
