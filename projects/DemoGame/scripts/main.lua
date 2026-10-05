-- DemoGame v3: физика + триггеры + raycast + звук + hitboxes, тап-управление.
-- Только подтверждённый API: set_var/get_var/set_rot/set_color/set_scale/print
-- + физика/звук из движка. Никаких set_position/set_visible/draw_text/клавиатуры.

local S, snd, ehp, eresp, cresp

local ENEMIES = { "Enemy1", "Enemy2" }
local EMAXHP  = { Enemy1 = 2, Enemy2 = 3 }
local ECOLOR  = { Enemy1 = "#FF7755", Enemy2 = "#AA55FF" }
local DEAD    = "#3A3A44"

local function sync()
    set_var("score",   S.score)
    set_var("taps",    S.hits)
    set_var("enemies", S.alive)
    set_var("grav",    S.grav and "on" or "off")
    set_var("hb",      S.hb and "on" or "off")
    set_var("snd",     S.mute and "off" or "on")
    set_var("nog",     S.nog and "on" or "off")
end

local function cnt_alive()
    local n = 0
    for _, e in ipairs(ENEMIES) do if ehp[e] and ehp[e] > 0 then n = n + 1 end end
    return n
end

local function nearest_enemy(px, py)
    local best, bd = nil, 1e9
    for _, e in ipairs(ENEMIES) do
        if ehp[e] and ehp[e] > 0 and is_body(e) then
            local dx = get_body_x(e) - px
            local dy = get_body_y(e) - py
            local d = dx * dx + dy * dy
            if d < bd then bd = d; best = e end
        end
    end
    return best
end

local function kill_enemy(e)
    if not ehp[e] then return end
    ehp[e] = nil
    eresp[e] = 3.0
    remove_body(e)
    set_color(e, DEAD)
    S.alive = cnt_alive()
    sync()
end

local function respawn_enemy(e)
    add_rigidbody(e, e == "Enemy1" and 1.0 or 1.2)
    set_bounce(e, 0.1)
    set_friction(e, 0.45)
    ehp[e] = EMAXHP[e]
    eresp[e] = nil
    set_color(e, ECOLOR[e])
    S.alive = cnt_alive()
    sync()
end

local function collect_coin()
    if S.coin then return end
    S.coin = true
    cresp = 5.0
    S.score = S.score + 5
    remove_body("Coin")
    set_color("Coin", DEAD)
    if snd.pick then play_sound(snd.pick, 0.9, false) end
    sync()
end

local function respawn_coin()
    add_trigger("Coin")
    S.coin = false
    cresp = 0
    set_color("Coin", "#FFD966")
    sync()
end

local function fire()
    if S.cd > 0 then return end
    S.cd = 0.18
    local px, py = get_body_x("Box"), get_body_y("Box")
    local tgt = nearest_enemy(px, py)
    local tx, ty = tgt and get_body_x(tgt) or (px + 900), tgt and get_body_y(tgt) or py
    local name = raycast(px, py, tx, ty, "Box")
    if snd.shoot then play_sound(snd.shoot, 0.7, 0.9 + math.random() * 0.16) end
    if name and ehp[name] then
        S.hits = S.hits + 1
        ehp[name] = ehp[name] - 1
        local dx, dy = tx - px, ty - py
        local l = math.sqrt(dx * dx + dy * dy); if l < 0.001 then l = 1 end
        add_velocity(name, dx / l * 320, dy / l * 320)
        set_spin(name, (math.random() - 0.5) * 10)
        if snd.hit then play_sound(snd.hit, 0.7, 0.9 + math.random() * 0.2) end
        if ehp[name] <= 0 then kill_enemy(name) else set_color(name, "#FFFFFF") end
    else
        set_color("Box", "#FFFFFF")
    end
    sync()
end

local function jump()
    add_velocity("Box", 0, -470)
    if snd.jump then play_sound(snd.jump, 0.9, 0.95 + math.random() * 0.1) end
end

local function reset_all()
    for _, e in ipairs(ENEMIES) do
        eresp[e] = nil
        if not is_body(e) then respawn_enemy(e) else ehp[e] = EMAXHP[e]; set_color(e, ECOLOR[e]) end
    end
    if S.coin then respawn_coin() end
    set_color("Box", "#66CCFF")
    set_color("Checkpoint", "#33FF99")
    S.score = 0; S.hits = 0; S.cd = 0
    S.alive = cnt_alive()
    sync()
    print("demo3 reset")
end

function on_start()
    S = { score = 0, hits = 0, alive = 0, cd = 0,
          grav = true, hb = false, mute = false, nog = false,
          coin = false, bnc = false }
    snd = {}; ehp = {}; eresp = {}; cresp = 0

    snd.jump  = load_sound("assets/sounds/jump.wav",  true)
    snd.hit   = load_sound("assets/sounds/hit.wav",   true)
    snd.shoot = load_sound("assets/sounds/shoot.wav", true)
    snd.pick  = load_sound("assets/sounds/pickup.wav",true)
    play_music("assets/music/demo.wav", 0.3)

    add_staticbody("Ground"); add_staticbody("WallL"); add_staticbody("WallR")
    add_staticbody("Plat1");  add_staticbody("Plat2")
    add_rigidbody("Box", 1.0)
    set_friction("Box", 0.35); set_bounce("Box", 0.0)
    add_rigidbody("Enemy1", 1.0); add_rigidbody("Enemy2", 1.2)
    set_friction("Enemy1", 0.45); set_bounce("Enemy1", 0.1)
    set_friction("Enemy2", 0.25); set_bounce("Enemy2", 0.2)
    add_trigger("KillZone"); add_trigger("Checkpoint"); add_trigger("Coin")

    ehp.Enemy1 = 2; ehp.Enemy2 = 3
    S.alive = cnt_alive()

    set_gravity(900)
    show_hitboxes(false)
    set_master_volume(0.8)
    sync()
    print("demo3 ready: physics+triggers+raycast+sound+hitboxes")
end

function on_update(dt)
    if not S then return end
    dt = dt or 0.016
    if S.cd > 0 then S.cd = math.max(0, S.cd - dt) end

    if S.coin then cresp = cresp - dt; if cresp <= 0 then respawn_coin() end end
    for _, e in ipairs(ENEMIES) do
        if eresp[e] then
            eresp[e] = eresp[e] - dt
            if eresp[e] <= 0 then respawn_enemy(e) end
        end
    end

    -- AI: живые враги тянутся к игроку
    local px, py = get_body_x("Box"), get_body_y("Box")
    for _, e in ipairs(ENEMIES) do
        if ehp[e] and ehp[e] > 0 and is_body(e) then
            local dx = px - get_body_x(e)
            if math.abs(dx) < 560 then
                local vx = get_velocity_x(e) or 0
                if math.abs(vx) < 140 then add_velocity(e, (dx > 0 and 1 or -1) * 420 * dt, 0) end
            end
        end
    end
end

-- тапы по нодам
function boxTap()    jump() end
function doJump()    jump() end
function doFire()    fire() end
function enemyTap()  fire() end
function coinTap()   collect_coin() end
function cpTap()
    S.score = S.score + 1
    set_color("Checkpoint", "#FFFFFF")
    if snd.pick then play_sound(snd.pick, 0.45, 1.2) end
    sync()
end
function go()        reset_all() end

-- переключатели
function toggleGrav()
    S.grav = not S.grav
    set_gravity(S.grav and 900 or 0)
    sync()
end
function toggleBounce()
    S.bnc = not S.bnc
    set_bounce("Box", S.bnc and 0.9 or 0.0)
    sync()
end
function toggleHB()
    S.hb = not S.hb
    show_hitboxes(S.hb)
    sync()
end
function toggleMute()
    S.mute = not S.mute
    set_master_volume(S.mute and 0.0 or 0.8)
    sync()
end
function toggleNoGrav()
    S.nog = not S.nog
    no_gravity("Box", S.nog)
    sync()
end

-- физ-хуки (движок дёргает по имени)
function on_collide(a, b)
    if (a == "Box" and (b == "Enemy1" or b == "Enemy2"))
        or (b == "Box" and (a == "Enemy1" or a == "Enemy2")) then
        set_color("Box", "#FF9933")
        if snd.hit then play_sound(snd.hit, 0.4, 0.9) end
    end
end

function on_trigger(a, b)
    local p = (a == "Box" and b) or (b == "Box" and a)
    if p == "KillZone" then
        add_velocity("Box", -520, -300)
        set_color("Box", "#FF3333")
        if snd.hit then play_sound(snd.hit, 0.8, 0.8) end
    elseif p == "Checkpoint" then
        S.score = S.score + 1
        set_color("Checkpoint", "#FFFFFF")
        if snd.pick then play_sound(snd.pick, 0.45, 1.2) end
    elseif p == "Coin" then
        collect_coin()
    end
    local e = (a == "KillZone" and b) or (b == "KillZone" and a)
    if e == "Enemy1" or e == "Enemy2" then kill_enemy(e) end
    sync()
end

function on_trigger_exit(a, b)
    local p = (a == "Box" and b) or (b == "Box" and a)
    if p == "KillZone" then set_color("Box", "#66CCFF") end
    if p == "Checkpoint" then set_color("Checkpoint", "#33FF99") end
end

function scene_end()
    stop_music()
    stop_all_sounds()
end
