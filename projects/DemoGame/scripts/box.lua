-- SukaEngine: скрипт ноды ScriptBox
-- Управление: джойстик слева, прыжок = звук

function on_start()
  speed = 200
  print("box started at x=" .. get_x())
end

function on_update(dt)
  local jx = input_joy_x()
  local jy = input_joy_y()

  set_x(get_x() + jx * speed * dt)
  set_y(get_y() + jy * speed * dt)

  -- не выпускаем за правую границу
  if get_x() > 500 then
    set_x(500)
  end

  if input_pressed("jump") then
    play_sound("jump")
    add_var("jumps", 1)
  end
end