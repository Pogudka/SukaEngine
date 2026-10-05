-- SukaEngine DemoGame
-- Full feature demo.
--
-- Important:
-- on_start and scene_end are NOT called by the engine.
-- Initialization is lazy inside on_update.

function pairIs(a, b, x, y)
    return (a == x and b == y) or (a == y and b == x)
end

function roundNum(v)
    return math.floor(v + 0.5)
end

function playCoin()
    local id = get_var("snd_coin")
    if id > 0 then
        play_sound(id, 0.9, false)
    end
end

function playHit()
    local id = get_var("snd_hit")
    if id <= 0 then
        id = get_var("snd_jump")
    end
    if id > 0 then
        play_sound(id, 0.8, false)
    end
end

function refreshHud()
    if exists("Stats") then
        local score = roundNum(get_score())
        local hits = roundNum(get_var("hits"))
        local collisions = roundNum(get_var("collisions"))
        local demo = roundNum(get_var("demo"))

        local grav = "off"
        if get_var("grav") == 1 then grav = "on" end

        local hb = "off"
        if get_var("hb") == 1 then hb = "on" end

        local fx = "off"
        if get_var("fx") == 1 then fx = "on" end

        local music = "off"
        if get_var("music_on") == 1 then music = "on" end

        local playing = "no"
        local snd = get_var("snd_jump")
        if snd > 0 and is_sound_playing(snd) then
            playing = "yes"
        end

        set_text("Stats", string.format(
            "score %d hits %d coll %d demo %d grav %s hb %s fx %s music %s sfx %s",
            score,
            hits,
            collisions,
            demo,
            grav,
            hb,
            fx,
            music,
            playing
        ))
    elseif exists("Stats2") then
        local score = roundNum(get_score())
        set_text("Stats2", string.format(
            "score %d  left side = joystick  JUMP button",
            score
        ))
    end
end

function commonUpdate(dt)
    local acc = get_var("_ui_acc") + dt
    if acc >= 0.2 then
        set_var("_ui_acc", 0)
        refreshHud()
    else
        set_var("_ui_acc", acc)
    end
end

function ensureInitMain()
    if get_var("_main_ready") == 1 then
        return
    end

    set_var("_main_ready", 1)

    set_score(0)

    set_var("hits", 0)
    set_var("collisions", 0)
    set_var("demo", 0)
    set_var("goal_done", 0)

    set_var("px", 220)
    set_var("py", 360)

    set_var("shape_i", 0)
    set_var("color_i", 0)
    set_var("size_i", 1)
    set_var("scale_i", 1)
    set_var("alpha_i", 0)
    set_var("visible", 1)

    set_var("fx", 0)
    set_var("grav", 1)
    set_var("hb", 0)
    set_var("light_on", 1)

    set_var("audio_mode", 0)
    set_var("music_on", 0)
    set_var("music_id", 0)
    set_var("vol", 1)

    set_var("dead_Box", 0)
    set_var("dead_Ball", 0)
    set_var("dead_Enemy1", 0)
    set_var("dead_Enemy2", 0)
    set_var("dead_Pickup", 0)

    audio_init()
    set_master_volume(1.0)

    local jump = load_sound("assets/sounds/jump.wav", true)
    if jump then
        set_var("snd_jump", jump)
    else
        set_var("snd_jump", 0)
    end

    local coin = load_sound("assets/sounds/coin.wav", true)
    if coin then
        set_var("snd_coin", coin)
    else
        set_var("snd_coin", 0)
    end

    local hit = load_sound("assets/sounds/hit.wav", true)
    if hit then
        set_var("snd_hit", hit)
    else
        set_var("snd_hit", 0)
    end

    set_pos("Hero", 220, 360)
    set_rot("Hero", 0)
    set_scale("Hero", 1.0)
    set_size("Hero", 64, 64)
    set_alpha("Hero", 1.0)
    set_color("Hero", "#00FF88")
    set_shape("Hero", "circle")
    set_action("Hero", "call:heroTap")
    show("Hero")

    set_pos("Box", 500, 200)
    set_pos("Ball", 600, 180)
    set_pos("Enemy1", 900, 300)
    set_pos("Enemy2", 1000, 260)

    show("Box")
    show("Ball")
    show("Enemy1")
    show("Enemy2")

    add_trigger("Hero")
    add_trigger("Goal")

    add_rigidbody("Box", 2.0)
    add_rigidbody("Ball", 1.0)
    add_rigidbody("Enemy1", 1.5)
    add_rigidbody("Enemy2", 1.5)

    if exists("Pickup") then
        set_pos("Pickup", 850, 250)
        show("Pickup")
        add_rigidbody("Pickup", 1.0)
    end

    set_bounce("Box", 0.45)
    set_friction("Box", 0.3)

    set_bounce("Ball", 0.8)
    set_friction("Ball", 0.1)
    no_gravity("Ball")

    set_bounce("Enemy1", 0.2)
    set_bounce("Enemy2", 0.2)

    if exists("Pickup") then
        set_bounce("Pickup", 0.5)
    end

    set_gravity(900)
    show_hitboxes(false)

    set_cam("Cam", 640, 360)
    set_zoom("Cam", 1.0)

    emit("FireEmitter", false)
    emit("SmokeEmitter", false)
    emit("RainEmitter", false)
    emit("SnowEmitter", false)
    emit("SparkEmitter", false)

    set_alpha("Glow", 1.0)

    set_text("Stats", "ready: tap HERO or use buttons")
    set_text("HudVars", "hits {hits} collisions {collisions} demo {demo}")

    print("DemoGame main initialized")
end

function ensureInitLevel2()
    if get_var("_lvl2_ready") == 1 then
        return
    end

    set_var("_lvl2_ready", 1)
    set_text("Stats2", "use left half joystick and JUMP button")
    print("DemoGame level2 initialized")
end

function clampHero()
    local px = get_var("px")
    local py = get_var("py")

    if px < 40 then px = 40 end
    if px > 1240 then px = 1240 end
    if py < 40 then py = 40 end
    if py > 680 then py = 680 end

    set_var("px", px)
    set_var("py", py)
    set_pos("Hero", px, py)
end

function moveLeft()
    set_var("px", get_var("px") - 24)
    clampHero()
end

function moveRight()
    set_var("px", get_var("px") + 24)
    clampHero()
end

function moveUp()
    set_var("py", get_var("py") - 24)
    clampHero()
end

function moveDown()
    set_var("py", get_var("py") + 24)
    clampHero()
end

function heroTap()
    add_score(5)
    set_var("hits", get_var("hits") + 1)
    set_pos("SparkEmitter", get_var("px"), get_var("py"))
    spawn("SparkEmitter", 25)
    playCoin()
    refreshHud()
end

function pulse()
    add_score(1)
    set_var("hits", get_var("hits") + 1)

    add_velocity("Box", 0, -260)
    add_velocity("Ball", 120, -180)

    if exists("Pickup") then
        add_velocity("Pickup", -80, -220)
    end

    spawn("FireEmitter", 35)
    emit("FireEmitter", true)

    playCoin()
    refreshHud()
end

function spin()
    set_spin("Box", 4.0)
    set_spin("Ball", -3.0)

    if exists("Pickup") then
        set_spin("Pickup", 2.0)
    end

    refreshHud()
end

function stopBodies()
    set_velocity("Box", 0, 0)
    set_spin("Box", 0)

    set_velocity("Ball", 0, 0)
    set_spin("Ball", 0)

    set_velocity("Enemy1", 0, 0)
    set_spin("Enemy1", 0)

    set_velocity("Enemy2", 0, 0)
    set_spin("Enemy2", 0)

    if exists("Pickup") then
        set_velocity("Pickup", 0, 0)
        set_spin("Pickup", 0)
    end

    refreshHud()
end

function toggleGrav()
    local g = 1 - roundNum(get_var("grav"))
    set_var("grav", g)

    if g == 1 then
        set_gravity(900)
    else
        set_gravity(0)
    end

    refreshHud()
end

function toggleHBox()
    local h = 1 - roundNum(get_var("hb"))
    set_var("hb", h)
    show_hitboxes(h == 1)
    refreshHud()
end

function cycleShape()
    local i = roundNum(get_var("shape_i")) + 1
    if i > 4 then i = 0 end
    set_var("shape_i", i)

    if i == 0 then
        set_shape("Hero", "circle")
    elseif i == 1 then
        set_shape("Hero", "square")
    elseif i == 2 then
        set_shape("Hero", "diamond")
    elseif i == 3 then
        set_shape("Hero", "triangle")
    else
        set_shape("Hero", "none")
    end
end

function cycleColor()
    local i = roundNum(get_var("color_i")) + 1
    if i > 4 then i = 0 end
    set_var("color_i", i)

    if i == 0 then
        set_color("Hero", "#00FF88")
    elseif i == 1 then
        set_color("Hero", 255, 128, 0)
    elseif i == 2 then
        set_color("Hero", "255,64,160")
    elseif i == 3 then
        set_color("Hero", "#4CC9F0")
    else
        set_color("Hero", 0xFFD700)
    end
end

function cycleSize()
    local i = roundNum(get_var("size_i")) + 1
    if i > 3 then i = 0 end
    set_var("size_i", i)

    if i == 0 then
        set_size("Hero", 48, 48)
    elseif i == 1 then
        set_size("Hero", 64, 64)
    elseif i == 2 then
        set_size("Hero", 80, 80)
    else
        set_size("Hero", 96, 96)
    end
end

function cycleScale()
    local i = roundNum(get_var("scale_i")) + 1
    if i > 3 then i = 0 end
    set_var("scale_i", i)

    if i == 0 then
        set_scale("Hero", 0.8)
    elseif i == 1 then
        set_scale("Hero", 1.0)
    elseif i == 2 then
        set_scale("Hero", 1.25)
    else
        set_scale("Hero", 1.0, 0.7)
    end
end

function cycleAlpha()
    local i = roundNum(get_var("alpha_i")) + 1
    if i > 3 then i = 0 end
    set_var("alpha_i", i)

    if i == 0 then
        set_alpha("Hero", 1.0)
    elseif i == 1 then
        set_alpha("Hero", 0.75)
    elseif i == 2 then
        set_alpha("Hero", 0.5)
    else
        set_alpha("Hero", 25)
    end
end

function toggleVis()
    local v = 1 - roundNum(get_var("visible"))
    set_var("visible", v)

    if v == 1 then
        show("Hero")
    else
        hide("Hero")
    end
end

function toggleFx()
    local on = 1 - roundNum(get_var("fx"))
    set_var("fx", on)

    emit("FireEmitter", on == 1)
    emit("SmokeEmitter", on == 1)
    emit("RainEmitter", on == 1)
    emit("SnowEmitter", on == 1)

    if on == 1 then
        spawn("SparkEmitter", 20)
    end

    refreshHud()
end

function toggleLight()
    local on = 1 - roundNum(get_var("light_on"))
    set_var("light_on", on)

    if on == 1 then
        set_alpha("Glow", 1.0)
    else
        set_alpha("Glow", 0.0)
    end
end

function audioCycle()
    local id = get_var("snd_jump")
    local mode = roundNum(get_var("audio_mode"))

    if id <= 0 then
        set_text("Stats", "sound missing: put jump.wav in assets/sounds")
        return
    end

    if mode == 0 then
        play_sound(id, 1.0, false)
    elseif mode == 1 then
        pause_sound(id)
    elseif mode == 2 then
        resume_sound(id)
    elseif mode == 3 then
        set_sound_pitch(id, 1.5)
        play_sound(id, 0.8, false)
    else
        stop_sound(id)
        set_sound_pitch(id, 1.0)
    end

    mode = mode + 1
    if mode > 4 then mode = 0 end
    set_var("audio_mode", mode)

    refreshHud()
end

function toggleVol()
    local v = get_var("vol")

    if v > 0.5 then
        set_master_volume(0.3)
        set_var("vol", 0.3)
    else
        set_master_volume(1.0)
        set_var("vol", 1.0)
    end

    refreshHud()
end

function toggleMusic()
    if get_var("music_on") == 1 then
        stop_music()
        set_var("music_on", 0)
        set_var("music_id", 0)
    else
        local id = play_music("assets/sounds/music.ogg", 0.35)
        if id then
            set_var("music_id", id)
            set_var("music_on", 1)
        else
            set_text("Stats", "music missing: assets/sounds/music.ogg")
        end
    end

    refreshHud()
end

function stopAllAudio()
    stop_all_sounds()
    stop_music()

    set_var("music_on", 0)
    set_var("music_id", 0)
    set_var("audio_mode", 0)
    set_var("vol", 1.0)
    set_master_volume(1.0)

    refreshHud()
end

function killEntity(name, points, hx, hy)
    local key = "dead_" .. name

    if get_var(key) == 1 then
        return
    end

    set_var(key, 1)
    hide(name)
    remove_body(name)

    add_score(points)
    set_var("hits", get_var("hits") + 1)

    if hx ~= nil and hy ~= nil then
        set_pos("SparkEmitter", hx, hy)
    else
        set_pos("SparkEmitter", get_var("px"), get_var("py"))
    end

    spawn("SparkEmitter", 40)
    playHit()
    refreshHud()
end

function shoot()
    local px = get_var("px")
    local py = get_var("py")

    local n, hx, hy, nx, ny, f = raycast(px, py, px + 1200, py, "Hero")

    if n then
        if n == "Enemy1" or n == "Enemy2" or n == "Pickup" or n == "Box" or n == "Ball" then
            killEntity(n, 10, hx, hy)
        else
            set_pos("SparkEmitter", hx, hy)
            spawn("SparkEmitter", 10)
        end
    else
        set_text("Stats", "raycast miss")
    end
end

function pickupTap()
    if not exists("Pickup") then
        return
    end

    if get_var("dead_Pickup") == 1 then
        set_var("dead_Pickup", 0)
        set_pos("Pickup", 850, 250)
        set_velocity("Pickup", 0, 0)
        set_spin("Pickup", 0)
        show("Pickup")
        add_rigidbody("Pickup", 1.0)
    else
        add_score(25)
        set_var("hits", get_var("hits") + 1)
        set_var("dead_Pickup", 1)

        hide("Pickup")
        remove_body("Pickup")

        set_pos("SparkEmitter", 850, 250)
        spawn("SparkEmitter", 50)
        playCoin()
    end

    refreshHud()
end

function resetDemo()
    stopAllAudio()

    set_var("_main_ready", 0)
    set_score(0)

    set_var("hits", 0)
    set_var("collisions", 0)
    set_var("demo", 0)
    set_var("goal_done", 0)

    set_var("dead_Box", 0)
    set_var("dead_Ball", 0)
    set_var("dead_Enemy1", 0)
    set_var("dead_Enemy2", 0)
    set_var("dead_Pickup", 0)

    transition("instant", "scenes/main.json", 0.05)
end

function goLevel2()
    stopAllAudio()
    set_var("_main_ready", 0)
    transition("fade", "scenes/level2.json", 0.4)
end

function goMain()
    stopAllAudio()
    set_var("_lvl2_ready", 0)
    set_var("_main_ready", 0)
    transition("instant", "scenes/main.json", 0.05)
end

function resetLevel2()
    set_var("_lvl2_ready", 0)
    set_score(0)
    transition("instant", "scenes/level2.json", 0.05)
end

function on_update(dt)
    if exists("Hero") then
        ensureInitMain()
    elseif exists("PlatformerPlayer") then
        ensureInitLevel2()
    end

    commonUpdate(dt)
end

function on_collide(a, b)
    set_var("collisions", get_var("collisions") + 1)
end

function on_trigger(a, b)
    if pairIs(a, b, "Hero", "Goal") then
        if get_var("goal_done") == 0 then
            set_var("goal_done", 1)
            add_score(50)

            set_pos("FireEmitter", 1100, 360)
            spawn("FireEmitter", 80)
            emit("FireEmitter", true)

            playCoin()
            set_text("Stats", "goal reached +50")
            refreshHud()
        end
    end

    if pairIs(a, b, "Hero", "Enemy1") then
        killEntity("Enemy1", 15, nil, nil)
    end

    if pairIs(a, b, "Hero", "Enemy2") then
        killEntity("Enemy2", 15, nil, nil)
    end
end

function on_trigger_exit(a, b)
    if pairIs(a, b, "Hero", "Goal") then
        set_var("goal_done", 0)
    end
end
