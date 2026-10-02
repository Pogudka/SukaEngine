-- Demo 2: без джойстика, всё через тапы и кнопки
function on_start()
  set_var("taps", 0)
  set_var("spinning", 0)
  set_var("angle", 0)
  print("demo2 started")
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
  print("box tapped: " .. get_var("taps"))
end

function gemTap()
  set_scale("Gem", 1.5, 1.5)
  print("gem scaled")
end

function go()
  set_var("spinning", 1)
  print("GO!")
end
