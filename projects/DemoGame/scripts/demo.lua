-- SukaEngine DemoGame (compact). on_start/scene_end NOT called -> lazy init.

function pairIs(a,b,x,y) return (a==x and b==y) or (a==y and b==x) end
function rnd(v) return math.floor(v+0.5) end
function clamp(v,lo,hi) if v<lo then return lo end if v>hi then return hi end return v end

function playSnd(key)
    local id = get_var(key)
    if id > 0 then play_sound(id, 0.9, false) end
end

function refreshHud()
    if exists("Stats") then
        set_text("Stats", string.format("score %d hits %d coll %d grav %s fx %s",
            rnd(get_score()), rnd(get_var("hits")), rnd(get_var("collisions")),
            get_var("grav")==1 and "on" or "off",
            get_var("fx")==1 and "on" or "off"))
    elseif exists("Stats2") then
        set_text("Stats2", string.format("score %d  left=joystick  JUMP/ATK  ZOOM/CAM", rnd(get_score())))
    end
end

-- auto-demo of setters (no buttons needed)
function autoDemo(t)
    if not exists("Hero") then return end
    local ci = rnd(get_var("_ci"))
    set_var("_ci", (ci+1) % 360)
    -- color cycle
    local cols = {"#00FF88","#FF8000","#FF40A0","#4CC9F0","#FFD700"}
    set_color("Hero", cols[(rnd(get_var("_cc"))%5)+1])
    set_var("_cc", rnd(get_var("_cc"))+1)
    -- scale pulse
    set_scale("Hero", 1.0 + 0.12*math.sin(t*2.0))
    -- halo rotate
    set_var("_hr", get_var("_hr") + 30)
    set_rot("Halo", get_var("_hr"))
    -- box alpha pulse
    set_alpha("Box", 0.6 + 0.4*math.abs(math.sin(t*1.3)))
    -- ball shape morph every ~2s
    set_var("_mt", get_var("_mt") + 1)
    if rnd(get_var("_mt")) % 120 == 0 then
        local sh = {"circle","square","diamond","triangle"}
        set_var("_si", (rnd(get_var("_si"))+1)%4)
        set_shape("Ball", sh[rnd(get_var("_si"))+1])
    end
    -- enemy1 blink (hide/show) every ~2s
    set_var("_bt", get_var("_bt") + 1)
    if rnd(get_var("_bt")) % 90 == 0 then
        set_var("_bv", 1 - rnd(get_var("_bv")))
        if rnd(get_var("_bv"))==1 then show("Enemy1") else hide("Enemy1") end
    end
end

function ensureMain()
    if get_var("_main_ready")==1 then return end
    set_var("_main_ready",1)
    set_score(0)
    set_var("hits",0); set_var("collisions",0); set_var("goal_done",0)
    set_var("px",220); set_var("py",360)
    set_var("grav",1); set_var("hb",0); set_var("fx",0)
    set_var("_ci",0); set_var("_cc",0); set_var("_hr",0); set_var("_mt",0); set_var("_si",0); set_var("_bt",0); set_var("_bv",1)
    set_var("audio_mode",0); set_var("vol",1)
    set_var("dead_Box",0); set_var("dead_Ball",0); set_var("dead_Enemy1",0); set_var("dead_Enemy2",0); set_var("dead_Pickup",0)

    audio_init(); set_master_volume(1.0)
    local j=load_sound("assets/sounds/jump.wav",true); set_var("snd_jump", j and j or 0)
    local c=load_sound("assets/sounds/coin.wav",true); set_var("snd_coin", c and c or 0)
    local h=load_sound("assets/sounds/hit.wav",true);  set_var("snd_hit",  h and h or 0)

    -- fixed camera (no follow) -> hitboxes cannot desync
    set_cam("Cam",640,360); set_zoom("Cam",1.0)

    add_trigger("Hero"); add_trigger("Goal")
    add_rigidbody("Box",2.0); add_rigidbody("Ball",1.0)
    add_rigidbody("Enemy1",1.5); add_rigidbody("Enemy2",1.5)
    if exists("Pickup") then set_pos("Pickup",850,300); show("Pickup"); add_rigidbody("Pickup",1.0) end
    set_bounce("Box",0.45); set_friction("Box",0.3)
    set_bounce("Ball",0.8); set_friction("Ball",0.1); no_gravity("Ball")
    set_bounce("Enemy1",0.2); set_bounce("Enemy2",0.2)
    if exists("Pickup") then set_bounce("Pickup",0.5) end
    set_gravity(900); show_hitboxes(false)
    emit("FireEmitter",false); emit("SparkEmitter",false)
    print("DemoGame main ready")
end

function ensureL2()
    if get_var("_l2_ready")==1 then return end
    set_var("_l2_ready",1)
    set_var("_zv",0)
    print("DemoGame level2 ready")
end

function moveHero(dx,dy)
    set_var("px", clamp(get_var("px")+dx,150,1130))
    set_var("py", clamp(get_var("py")+dy,130,540))
    set_pos("Hero", get_var("px"), get_var("py"))
end
function moveLeft()  moveHero(-22,0) end
function moveRight() moveHero(22,0) end
function moveUp()    moveHero(0,-22) end
function moveDown()  moveHero(0,22) end

function heroTap()
    add_score(5); set_var("hits",get_var("hits")+1)
    set_pos("SparkEmitter",get_var("px"),get_var("py")); spawn("SparkEmitter",20)
    playSnd("snd_coin"); refreshHud()
end
function pulse()
    add_score(1); set_var("hits",get_var("hits")+1)
    add_velocity("Box",0,-240); add_velocity("Ball",100,-160)
    if exists("Pickup") then add_velocity("Pickup",-60,-200) end
    set_pos("FireEmitter",520,560); emit("FireEmitter",true); spawn("FireEmitter",25)
    playSnd("snd_coin"); refreshHud()
end
function shoot()
    local px=get_var("px"); local py=get_var("py")
    local n,hx,hy = raycast(px,py,px+1200,py,"Hero")
    if n then
        if n=="Enemy1" or n=="Enemy2" or n=="Pickup" or n=="Box" or n=="Ball" then
            killEntity(n,10,hx,hy)
        else
            set_pos("SparkEmitter",hx,hy); spawn("SparkEmitter",8)
        end
    else set_text("Stats","raycast miss") end
end
function killEntity(name,pts,hx,hy)
    local k="dead_"..name
    if get_var(k)==1 then return end
    set_var(k,1); hide(name); remove_body(name)
    add_score(pts); set_var("hits",get_var("hits")+1)
    if hx then set_pos("SparkEmitter",hx,hy) else set_pos("SparkEmitter",get_var("px"),get_var("py")) end
    spawn("SparkEmitter",30); playSnd("snd_hit"); refreshHud()
end
function toggleGrav()
    local g=1-rnd(get_var("grav")); set_var("grav",g)
    set_gravity(g==1 and 900 or 0); refreshHud()
end
function toggleFx()
    local on=1-rnd(get_var("fx")); set_var("fx",on)
    emit("FireEmitter",on==1); if on==1 then spawn("FireEmitter",20) end; refreshHud()
end
function audioCycle()
    local id=get_var("snd_jump"); local m=rnd(get_var("audio_mode"))
    if id<=0 then set_text("Stats","sound missing: jump.wav in assets/sounds") return end
    if m==0 then play_sound(id,1.0,false)
    elseif m==1 then pause_sound(id)
    elseif m==2 then resume_sound(id)
    elseif m==3 then set_sound_pitch(id,1.5); play_sound(id,0.8,false)
    else stop_sound(id); set_sound_pitch(id,1.0) end
    m=m+1; if m>4 then m=0 end; set_var("audio_mode",m); refreshHud()
end
function toggleZoom()
    local z=rnd(get_var("_zv")); set_var("_zv",(z+1)%3)
    local vals={1.0,1.4,1.0}; set_zoom("Cam2", vals[rnd(get_var("_zv"))+1])
end
function toggleCam()
    set_cam("Cam2", 640, 360)
end
function goLevel2() stop_all_sounds(); stop_music(); set_var("_main_ready",0); transition("fade","scenes/level2.json",0.4) end
function goMain()   stop_all_sounds(); stop_music(); set_var("_l2_ready",0); set_var("_main_ready",0); transition("instant","scenes/main.json",0.05) end
function resetDemo()
    stop_all_sounds(); stop_music(); set_master_volume(1.0)
    set_var("_main_ready",0); set_score(0)
    set_var("hits",0); set_var("collisions",0); set_var("goal_done",0)
    set_var("dead_Box",0); set_var("dead_Ball",0); set_var("dead_Enemy1",0); set_var("dead_Enemy2",0); set_var("dead_Pickup",0)
    transition("instant","scenes/main.json",0.05)
end
function resetLevel2() set_var("_l2_ready",0); set_score(0); transition("instant","scenes/level2.json",0.05) end

function on_update(dt)
    if exists("Hero") then ensureMain(); autoDemo(get_var("_t")+dt); set_var("_t",get_var("_t")+dt)
    elseif exists("PlatformerPlayer") then ensureL2() end
    local acc=get_var("_ui")+dt
    if acc>=0.25 then set_var("_ui",0); refreshHud() else set_var("_ui",acc) end
end

function on_collide(a,b) set_var("collisions",get_var("collisions")+1) end
function on_trigger(a,b)
    if pairIs(a,b,"Hero","Goal") and get_var("goal_done")==0 then
        set_var("goal_done",1); add_score(50)
        set_pos("FireEmitter",1080,360); spawn("FireEmitter",60); emit("FireEmitter",true)
        playSnd("snd_coin"); set_text("Stats","goal +50"); refreshHud()
    end
    if pairIs(a,b,"Hero","Enemy1") then killEntity("Enemy1",15) end
    if pairIs(a,b,"Hero","Enemy2") then killEntity("Enemy2",15) end
end
function on_trigger_exit(a,b)
    if pairIs(a,b,"Hero","Goal") then set_var("goal_done",0) end
end
