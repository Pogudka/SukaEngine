-- SukaEngine DemoGame. Единый скрипт на обе сцены.
-- ВАЖНО: on_start / scene_end движком НЕ вызываются -> ленивый init в on_update.
-- ВАЖНО: set_var хранит ТОЛЬКО числа; текст HUD -> set_text; {var} -> целые.
-- ВАЖНО: в тексте нельзя '|' (разделитель DRAW-протокола).
-- Движение level2 = удержание кнопок через get_button_pressed (патч Script.hpp).

function rnd(v) return math.floor(v + 0.5) end
function clamp(v, lo, hi) if v < lo then return lo end if v > hi then return hi end return v end
function abs(v) return v < 0 and -v or v end
function pairIs(a, b, x, y) return (a == x and b == y) or (a == y and b == x) end

-- =====================================================================
--  MAIN SCENE  (Node2D-аркада, витрина API; движение = тап-шаг)
-- =====================================================================

function playSnd(key)
    local id = get_var(key)
    if id > 0 then play_sound(id, 0.9, false) end
end

function refreshHud()
    if not exists("Stats") then return end
    set_text("Stats", string.format("score %d hits %d coll %d grav %s fx %s",
        rnd(get_score()), rnd(get_var("hits")), rnd(get_var("collisions")),
        get_var("grav") == 1 and "on" or "off",
        get_var("fx") == 1 and "on" or "off"))
end

function autoDemo(t)
    if not exists("Hero") then return end
    set_var("_cc", rnd(get_var("_cc")) + 1)
    local cols = { "#00FF88", "#FF8000", "#FF40A0", "#4CC9F0", "#FFD700" }
    set_color("Hero", cols[(rnd(get_var("_cc")) % 5) + 1])
    set_scale("Hero", 1.0 + 0.12 * math.sin(t * 2.0))
    set_var("_hr", get_var("_hr") + 30)
    set_rot("Halo", get_var("_hr"))
    set_alpha("Box", 0.6 + 0.4 * abs(math.sin(t * 1.3)))
    set_var("_mt", get_var("_mt") + 1)
    if rnd(get_var("_mt")) % 120 == 0 then
        local sh = { "circle", "square", "diamond", "triangle" }
        set_var("_si", (rnd(get_var("_si")) + 1) % 4)
        set_shape("Ball", sh[rnd(get_var("_si")) + 1])
    end
    set_var("_bt", get_var("_bt") + 1)
    if rnd(get_var("_bt")) % 90 == 0 then
        set_var("_bv", 1 - rnd(get_var("_bv")))
        if rnd(get_var("_bv")) == 1 then show("Enemy1") else hide("Enemy1") end
    end
end

function ensureMain()
    if get_var("_main_ready") == 1 then return end
    set_var("_main_ready", 1)
    set_score(0)
    set_var("hits", 0); set_var("collisions", 0); set_var("goal_done", 0)
    set_var("px", 220); set_var("py", 360)
    set_var("grav", 1); set_var("hb", 0); set_var("fx", 0)
    set_var("_ci", 0); set_var("_cc", 0); set_var("_hr", 0); set_var("_mt", 0); set_var("_si", 0); set_var("_bt", 0); set_var("_bv", 1)
    set_var("audio_mode", 0); set_var("vol", 1)
    set_var("dead_Box", 0); set_var("dead_Ball", 0); set_var("dead_Enemy1", 0); set_var("dead_Enemy2", 0); set_var("dead_Pickup", 0)

    audio_init(); set_master_volume(1.0)
    local j = load_sound("assets/sounds/jump.wav", true); set_var("snd_jump", j and j or 0)
    local c = load_sound("assets/sounds/coin.wav", true);  set_var("snd_coin", c and c or 0)
    local h = load_sound("assets/sounds/hit.wav", true);   set_var("snd_hit",  h and h or 0)

    set_cam("Cam", 640, 360); set_zoom("Cam", 1.0)

    add_trigger("Hero"); add_trigger("Goal")
    add_rigidbody("Box", 2.0); add_rigidbody("Ball", 1.0)
    add_rigidbody("Enemy1", 1.5); add_rigidbody("Enemy2", 1.5)
    if exists("Pickup") then set_pos("Pickup", 850, 300); show("Pickup"); add_rigidbody("Pickup", 1.0) end
    set_bounce("Box", 0.45); set_friction("Box", 0.3)
    set_bounce("Ball", 0.8); set_friction("Ball", 0.1); no_gravity("Ball")
    set_bounce("Enemy1", 0.2); set_bounce("Enemy2", 0.2)
    if exists("Pickup") then set_bounce("Pickup", 0.5) end
    set_gravity(900); show_hitboxes(false)
    emit("FireEmitter", false); emit("SparkEmitter", false)
    print("DemoGame main ready")
end

function moveHero(dx, dy)
    set_var("px", clamp(get_var("px") + dx, 150, 1130))
    set_var("py", clamp(get_var("py") + dy, 130, 540))
    set_pos("Hero", get_var("px"), get_var("py"))
end
function moveLeft()  moveHero(-22, 0) end
function moveRight() moveHero(22, 0) end
function moveUp()    moveHero(0, -22) end
function moveDown()  moveHero(0, 22) end

function heroTap()
    add_score(5); set_var("hits", get_var("hits") + 1)
    set_pos("SparkEmitter", get_var("px"), get_var("py")); spawn("SparkEmitter", 20)
    playSnd("snd_coin"); refreshHud()
end
function pulse()
    add_score(1); set_var("hits", get_var("hits") + 1)
    add_velocity("Box", 0, -240); add_velocity("Ball", 100, -160)
    if exists("Pickup") then add_velocity("Pickup", -60, -200) end
    set_pos("FireEmitter", 520, 560); emit("FireEmitter", true); spawn("FireEmitter", 25)
    playSnd("snd_coin"); refreshHud()
end
function shoot()
    local px = get_var("px"); local py = get_var("py")
    local n, hx, hy = raycast(px, py, px + 1200, py, "Hero")
    if n then
        if n == "Enemy1" or n == "Enemy2" or n == "Pickup" or n == "Box" or n == "Ball" then
            killEntity(n, 10, hx, hy)
        else
            set_pos("SparkEmitter", hx, hy); spawn("SparkEmitter", 8)
        end
    else set_text("Stats", "raycast miss") end
end
function killEntity(name, pts, hx, hy)
    local k = "dead_" .. name
    if get_var(k) == 1 then return end
    set_var(k, 1); hide(name); remove_body(name)
    add_score(pts); set_var("hits", get_var("hits") + 1)
    if hx then set_pos("SparkEmitter", hx, hy) else set_pos("SparkEmitter", get_var("px"), get_var("py")) end
    spawn("SparkEmitter", 30); playSnd("snd_hit"); refreshHud()
end
function toggleGrav()
    local g = 1 - rnd(get_var("grav")); set_var("grav", g)
    set_gravity(g == 1 and 900 or 0); refreshHud()
end
function toggleHBox()
    local h = 1 - rnd(get_var("hb")); set_var("hb", h)
    show_hitboxes(h == 1); refreshHud()
end
function toggleFx()
    local on = 1 - rnd(get_var("fx")); set_var("fx", on)
    emit("FireEmitter", on == 1); if on == 1 then spawn("FireEmitter", 20) end; refreshHud()
end
function audioCycle()
    local id = get_var("snd_jump"); local m = rnd(get_var("audio_mode"))
    if id <= 0 then set_text("Stats", "sound missing: jump.wav in assets/sounds") return end
    if m == 0 then play_sound(id, 1.0, false)
    elseif m == 1 then pause_sound(id)
    elseif m == 2 then resume_sound(id)
    elseif m == 3 then set_sound_pitch(id, 1.5); play_sound(id, 0.8, false)
    else stop_sound(id); set_sound_pitch(id, 1.0) end
    m = m + 1; if m > 4 then m = 0 end; set_var("audio_mode", m); refreshHud()
end
function goLevel2()
    stop_all_sounds(); stop_music()
    set_var("_main_ready", 0)
    transition("fade", "scenes/level2.json", 0.4)
end
function resetDemo()
    stop_all_sounds(); stop_music(); set_master_volume(1.0)
    set_var("_main_ready", 0); set_score(0)
    set_var("hits", 0); set_var("collisions", 0); set_var("goal_done", 0)
    set_var("dead_Box", 0); set_var("dead_Ball", 0); set_var("dead_Enemy1", 0); set_var("dead_Enemy2", 0); set_var("dead_Pickup", 0)
    transition("instant", "scenes/main.json", 0.05)
end

-- =====================================================================
--  LEVEL 2 SCENE  (кастомный платформер на Node2D + Lua-физика)
--  Нативный джойстик не используется; движение = удержание DL/DR/DU.
-- =====================================================================

L2_PLATS = {
    { name = "L2Ground", x = 640, y = 640, w = 1280, h = 36 },
    { name = "L2PlatA",  x = 380, y = 500, w = 170,  h = 22 },
    { name = "L2PlatB",  x = 760, y = 400, w = 170,  h = 22 },
}
L2_ENEMIES = {
    { name = "L2Enemy1", homeX = 980, homeY = 580, minX = 820, maxX = 1180, dir = 1,  spd = 70 },
    { name = "L2Enemy2", homeX = 520, homeY = 320, minX = 420, maxX = 700,  dir = -1, spd = 55 },
}
L2_COINS = {
    { name = "L2CoinA", x = 380, y = 450 },
    { name = "L2CoinB", x = 760, y = 350 },
}
L2_HERO_W = 44; L2_HERO_H = 44
L2_ENEMY_W = 48; L2_ENEMY_H = 48
L2_COIN_W = 36; L2_COIN_H = 36
L2_GRAV = 1100; L2_RUN = 220; L2_JUMP = 420; L2_REACH = 300; L2_VTOL = 40

function l2Overlap(hx, hy, hw, hh, rx, ry, rw, rh)
    return abs(hx - rx) < (hw + rw) * 0.5 and abs(hy - ry) < (hh + rh) * 0.5
end

function l2RefreshHud()
    if not exists("L2Stats") then return end
    set_text("L2Stats", string.format("score %d  coins %d/2  enemies %d",
        rnd(get_score()), rnd(get_var("coins")), rnd(get_var("enemies"))))
    local hp = rnd(get_var("hp"))
    local bar = ""
    for i = 1, hp do bar = bar .. "#" end
    for i = hp + 1, 3 do bar = bar .. "." end
    set_text("L2Hp", "HP " .. bar)
    set_color("L2Hp", hp >= 2 and "#40C040" or (hp == 1 and "#FFD700" or "#D62828"))
end

function l2Ensure()
    if get_var("_l2_ready") == 1 then return end
    set_var("_l2_ready", 1)
    set_score(0)
    set_var("hp", 3); set_var("coins", 0); set_var("enemies", #L2_ENEMIES); set_var("over", 0)
    set_var("hx", 120); set_var("hy", 480); set_var("hvx", 0); set_var("hvy", 0)
    set_var("grounded", 0); set_var("facing", 1)
    set_var("invuln", 0); set_var("invuln_t", 0); set_var("atk_cd", 0)
    set_var("_l2jumpwas", 0); set_var("_zv", 0)

    for i, p in ipairs(L2_PLATS) do
        add_staticbody(p.name)
        set_pos(p.name, p.x, p.y)
    end
    for i, e in ipairs(L2_ENEMIES) do
        add_staticbody(e.name)
        set_var("e" .. i .. "x", e.homeX); set_var("e" .. i .. "y", e.homeY)
        set_var("e" .. i .. "dir", e.dir); set_var("e" .. i .. "dead", 0)
        set_pos(e.name, e.homeX, e.homeY); show(e.name)
    end
    for i, c in ipairs(L2_COINS) do
        set_var("c" .. i .. "dead", 0)
        set_pos(c.name, c.x, c.y); show(c.name)
    end

    add_staticbody("L2Hero")
    set_pos("L2Hero", 120, 480); show("L2Hero"); set_alpha("L2Hero", 1.0); set_color("L2Hero", "#40C040")
    set_gravity(0)
    show_hitboxes(false)
    set_zoom("L2Cam", 1.0)
    set_cam("L2Cam", 640, 360)
    set_text("L2Warn", "hold <- -> to run, ^ to jump, ATK to strike")
    print("level2 custom ready")
end

function l2Left()  end
function l2Right() end
function l2Jump()  end
function l2Attack()
    if get_var("over") == 1 then return end
    if get_var("atk_cd") > 0 then return end
    set_var("atk_cd", 0.25)
    local hx = get_var("hx"); local hy = get_var("hy"); local f = get_var("facing")
    for i, e in ipairs(L2_ENEMIES) do
        if get_var("e" .. i .. "dead") == 0 then
            local ex = get_var("e" .. i .. "x"); local ey = get_var("e" .. i .. "y")
            local inFront = (f > 0 and ex > hx and ex - hx < L2_REACH) or (f < 0 and ex < hx and hx - ex < L2_REACH)
            if inFront and abs(ey - hy) < L2_VTOL then
                set_var("e" .. i .. "dead", 1)
                set_var("enemies", get_var("enemies") - 1)
                hide(e.name); remove_body(e.name)
                add_score(20)
                l2RefreshHud()
                if get_var("enemies") == 0 then l2Win() end
                return
            end
        end
    end
end

function l2Damage()
    if get_var("invuln") == 1 or get_var("over") == 1 then return end
    local hp = get_var("hp") - 1
    if hp < 0 then hp = 0 end
    set_var("hp", hp)
    set_var("invuln", 1); set_var("invuln_t", 1.0)
    local hx = get_var("hx")
    for i, e in ipairs(L2_ENEMIES) do
        if get_var("e" .. i .. "dead") == 0 then
            local ex = get_var("e" .. i .. "x")
            if abs(ex - hx) < 90 then set_var("hvx", (hx < ex) and -180 or 180) end
        end
    end
    l2RefreshHud()
    if hp == 0 then l2GameOver() end
end
function l2GameOver()
    set_var("over", 1)
    set_text("L2Warn", "GAME OVER - press RESET")
    set_color("L2Hero", "#555555")
end
function l2Win()
    set_var("over", 1)
    set_text("L2Warn", "YOU WIN - press RESET")
end

function l2Update(dt)
    if get_var("over") == 1 then return end

    -- удержание d-pad -> горизонтальная скорость + facing
    local left = get_button_pressed("DL") and 1 or 0
    local right = get_button_pressed("DR") and 1 or 0
    local hvx = 0
    if left == 1 and right == 0 then hvx = -L2_RUN; set_var("facing", -1)
    elseif right == 1 and left == 0 then hvx = L2_RUN; set_var("facing", 1) end
    set_var("hvx", hvx)

    -- прыжок по краю нажатия (edge), только с земли
    local jumpNow = get_button_pressed("DU") and 1 or 0
    if jumpNow == 1 and get_var("_l2jumpwas") == 0 and get_var("grounded") == 1 then
        set_var("hvy", -L2_JUMP); set_var("grounded", 0)
    end
    set_var("_l2jumpwas", jumpNow)

    -- гравитация
    set_var("hvy", get_var("hvy") + L2_GRAV * dt)

    -- интеграция X + разрешение о платформам
    local hx = get_var("hx") + get_var("hvx") * dt
    local hy = get_var("hy")
    for i, p in ipairs(L2_PLATS) do
        if l2Overlap(hx, hy, L2_HERO_W, L2_HERO_H, p.x, p.y, p.w, p.h) then
            if get_var("hx") < p.x then hx = p.x - p.w * 0.5 - L2_HERO_W * 0.5
            else hx = p.x + p.w * 0.5 + L2_HERO_W * 0.5 end
            set_var("hvx", 0)
        end
    end
    hx = clamp(hx, L2_HERO_W * 0.5, 1280 - L2_HERO_W * 0.5)

    -- интеграция Y + разрешение + grounded
    local vy = get_var("hvy")
    hy = hy + vy * dt
    local grounded = 0
    for i, p in ipairs(L2_PLATS) do
        if l2Overlap(hx, hy, L2_HERO_W, L2_HERO_H, p.x, p.y, p.w, p.h) then
            if vy > 0 then hy = p.y - p.h * 0.5 - L2_HERO_H * 0.5; vy = 0; grounded = 1
            elseif vy < 0 then hy = p.y + p.h * 0.5 + L2_HERO_H * 0.5; vy = 0 end
        end
    end
    if hy > 700 then hy = 700; vy = 0; grounded = 1 end

    set_var("hx", hx); set_var("hy", hy); set_var("hvy", vy); set_var("grounded", grounded)
    set_pos("L2Hero", hx, hy)

    -- неуязвимость мигает
    if get_var("invuln") == 1 then
        local t = get_var("invuln_t") - dt
        if t <= 0 then set_var("invuln", 0); set_var("invuln_t", 0); set_alpha("L2Hero", 1.0)
        else set_var("invuln_t", t); set_alpha("L2Hero", math.sin(t * 30) > 0 and 1.0 or 0.25) end
    end
    if get_var("atk_cd") > 0 then set_var("atk_cd", get_var("atk_cd") - dt) end

    -- патруль врагов + урон при касании
    for i, e in ipairs(L2_ENEMIES) do
        if get_var("e" .. i .. "dead") == 0 then
            local ex = get_var("e" .. i .. "x") + get_var("e" .. i .. "dir") * e.spd * dt
            if ex < e.minX then ex = e.minX; set_var("e" .. i .. "dir", 1) end
            if ex > e.maxX then ex = e.maxX; set_var("e" .. i .. "dir", -1) end
            set_var("e" .. i .. "x", ex)
            set_pos(e.name, ex, e.homeY)
            if l2Overlap(hx, hy, L2_HERO_W, L2_HERO_H, ex, e.homeY, L2_ENEMY_W, L2_ENEMY_H) then
                l2Damage()
            end
        end
    end

    -- сбор монет
    for i, c in ipairs(L2_COINS) do
        if get_var("c" .. i .. "dead") == 0 then
            if l2Overlap(hx, hy, L2_HERO_W, L2_HERO_H, c.x, c.y, L2_COIN_W, L2_COIN_H) then
                set_var("c" .. i .. "dead", 1)
                set_var("coins", get_var("coins") + 1)
                add_score(10)
                hide(c.name)
                l2RefreshHud()
            end
        end
    end

    -- камера следует за игроком (мягко, в пределах кадра)
    set_cam("L2Cam", clamp(hx, 320, 960), 360)
end

function l2Zoom()
    local z = rnd(get_var("_zv")); set_var("_zv", (z + 1) % 3)
    set_zoom("L2Cam", ({ 1.0, 1.4, 1.0 })[rnd(get_var("_zv")) + 1])
end
function l2CamReset() set_cam("L2Cam", clamp(get_var("hx"), 320, 960), 360) end
function l2GoMain()
    stop_all_sounds(); stop_music()
    set_var("_l2_ready", 0); set_var("_main_ready", 0)
    transition("instant", "scenes/main.json", 0.05)
end
function l2Reset()
    set_var("_l2_ready", 0); set_score(0)
    transition("instant", "scenes/level2.json", 0.05)
end

-- =====================================================================
--  ЕДИНЫЙ TOЧКА ВХОДА: on_update / on_collide / on_trigger
-- =====================================================================

function on_update(dt)
    if exists("Hero") then
        ensureMain()
        set_var("_t", get_var("_t") + dt)
        autoDemo(get_var("_t"))
        local acc = get_var("_ui") + dt
        if acc >= 0.25 then set_var("_ui", 0); refreshHud() else set_var("_ui", acc) end
    elseif exists("L2Hero") then
        l2Ensure()
        l2Update(dt)
        local acc = get_var("_ui2") + dt
        if acc >= 0.2 then set_var("_ui2", 0); l2RefreshHud() else set_var("_ui2", acc) end
    end
end

function on_collide(a, b)
    if exists("Hero") then set_var("collisions", get_var("collisions") + 1) end
end

function on_trigger(a, b)
    if not exists("Hero") then return end
    if pairIs(a, b, "Hero", "Goal") and get_var("goal_done") == 0 then
        set_var("goal_done", 1); add_score(50)
        set_pos("FireEmitter", 1080, 360); spawn("FireEmitter", 60); emit("FireEmitter", true)
        playSnd("snd_coin"); set_text("Stats", "goal +50"); refreshHud()
    end
    if pairIs(a, b, "Hero", "Enemy1") then killEntity("Enemy1", 15) end
    if pairIs(a, b, "Hero", "Enemy2") then killEntity("Enemy2", 15) end
end

function on_trigger_exit(a, b)
    if exists("Hero") and pairIs(a, b, "Hero", "Goal") then set_var("goal_done", 0) end
end
