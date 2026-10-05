# SukaEngine — code-verified reference

Версия: `0.18.7`  
Источник истины: `src/`, `android/`, `CMakeLists.txt`, `MainActivity.java`, CI-workflow.  
Этот документ заменяет раннюю telegra.ph-справку там, где код и документ расходились.

---

## 0. Главный принцип

SukaEngine — это не «просто игра на Lua». Это Android-приложение с тремя режимами внутри одного процесса:

- **Hub** — список проектов;
- **Editor** — визуальный редактор сцен/префабов/скриптов;
- **Game** — запуск проекта.

Нативная часть пишет текстовый кадр-протокол, Java-часть его рисует через `Canvas`.  
Lua — скриптовый слой внутри Game-режима.

---

## 1. Сборка

### Android / Gradle

- AGP: `8.1.0`
- Gradle в CI: `8.2`
- JDK: `17`
- `compileSdk`: `34`
- `targetSdk`: `34`
- `minSdk`: `24`
- NDK: `25.2.9519653`
- CMake: `3.22.1`
- C++ standard: `17`
- RTTI / exceptions: включены в `CMakeLists.txt`

### Нативные зависимости

- Lua `5.4.6` — статическая библиотека `lua_static`
- Box2D `v2.4.1`
- miniaudio `0.11.21` — header-only, реализация в `src/Sound.cpp`

### Цель сборки

Библиотека `suka` компилирует:

```text
android/app/src/main/cpp/android_bridge.cpp
src/Sound.cpp
```

Остальные `src/*.hpp` подключаются через include-граф.

---

## 2. Android asset pipeline

Это важно, потому что пути ассетов в рантайме не очевидны из репозитория.

### Gradle

```gradle
assets.srcDirs = ['src/main/assets', '../../projects', '../../assets']
```

Gradle сливает содержимое всех трёх директорий в `android_asset/` **без имён самих srcDir**.

Условно в APK получается:

```text
android_asset/
├── DemoGame/
├── fonts/
└── sounds/
```

### Startup Java

При старте `MainActivity`:

```java
copyAssets("");
restructure();
File root = getFilesDir();
nativeInit(root.getAbsolutePath(), GAME_DIR);
```

`copyAssets("")` рекурсивно раскладывает `android_asset/` в `filesDir`.

Потом `restructure()` проходит по верхним папкам `filesDir`:

- если внутри есть `project.json` → переносит в `projects/<имя>`;
- иначе → переносит в `assets/<имя>`.

Итоговая живая структура:

```text
files/
├── projects/
│   └── DemoGame/
└── assets/
    ├── fonts/
    └── sounds/
```

Runtime-корень движка:

```cpp
projectRootRef() == context.getFilesDir()
```

CMake-дефайн:

```cmake
PROJECT_ROOT="/data/data/com.sukaengine.app/files"
```

фактически мёртв, потому что `CommonTypes.hpp` переопределяет макрос на `suka::projectRootRef()`, а Java вызывает `setProjectRoot(root.getAbsolutePath())`.

---

## 3. Структура проекта

Живой проект лежит здесь:

```text
files/projects/<ProjectDir>/
```

### Обязательные/основные файлы

```text
project.json
scenes/main.json
scripts/
assets/
```

### `project.json`

```json
{
  "name": "DemoGame",
  "engine": "SukaEngine",
  "package": "com.suka.demo",
  "version": "2.0",
  "main_scene": "scenes/main.json",
  "orientation": "landscape",
  "default_font": "assets/fonts/Ubuntu-Regular.ttf"
}
```

Поля:

| Поле | Значение |
|---|---|
| `name` | отображаемое имя |
| `engine` | идентификатор движка |
| `package` | Android applicationId-подобное поле |
| `version` | версия проекта |
| `main_scene` | стартовая сцена, путь относительно корня проекта |
| `orientation` | `landscape` / `portrait` |
| `default_font` | шрифт по умолчанию, путь относительно корня проекта |

### Другие файлы проекта

| Файл | Назначение |
|---|---|
| `editor_camera.json` | размер кадра и ориентация редактора/игры |
| `funcs.txt` | редакторские теги физики и звуковые привязки |
| `save.vars` | прогресс игры, если `SAVE: ON` |
| `scripts.json` | метаdata для редактора/создания скриптов |
| `scenes/*.json` | сцены |
| `prefabs/*.prf` | префабы |
| `scripts/*.lua` | Lua-скрипты |
| `assets/` | картинки, шрифты и project-ассеты |
| `sounds/` | звуки для Java-звукового стека |

### Важно про `scripts.json`

Runtime **не маршрутизирует скрипты по нодам через `scripts.json`**.

`ScriptSystem::load()` просто грузит все `.lua` из:

```text
scripts/
```

в одно общее `lua_State`.

Значит:

- все глобальные функции всех скриптов видны всем;
- `call:имя` вызывает глобальную функцию `имя`;
- `scripts.json` — это редакторская/служебная метаdata, не рантайм-контракт.

---

## 4. Режимы приложения

### Hub

Список проектов, Play/Edit/New/Theme/Save toggle/Rename/Delete/Reset.

Хаб всегда работает в landscape.

### Editor

Редактор сцен, префабов, файлов, ассетов, настроек и Lua-скриптов.

Редактор всегда работает в landscape.

### Game

Запуск проекта. Ориентация берётся из настроек проекта/камеры.

Во время перехода между сценами:

- `on_update` не вызывается;
- ввод игрока игнорируется;
- коллизии/триггеры не диспатчатся.

---

## 5. Игровой кадр

`GameApp::stepGame()` использует фиксированный шаг:

```cpp
dt = 1.0 / 60.0
```

Порядок:

1. overlay actions;
2. screen ratio;
3. Lua transition command;
4. transition update;
5. pending node action;
6. UI actions;
7. `SceneManager::update()`:
   - платформерная симуляция;
   - `prune()`;
8. `ScriptSystem::update()`:
   - `on_update(dt)`;
9. tweens update;
10. Box2D `physicsUpdate()`;
11. collide/trigger dispatch;
12. progress load/save;
13. sound commands drain;
14. render scene to `DRAW` protocol;
15. particles update/draw;
16. debug hitboxes;
17. transition/overlay/stat/log output.

### Прогресс

Если в хабе включён `SAVE: ON`:

- при входе в игру устанавливается `pendingLoadVars_ = true`;
- после первого кадра вызывается `loadVars()`;
- раз в секунду и при выходе вызывается `saveVars()`.

Формат `save.vars`:

```text
__score__=123
varName=45.6
```

Только числа.

---

## 6. Рендер-протокол

Натив возвращает Java текстовый кадр. Java парсит строки.

### Служебные команды

```text
MODE|hub
MODE|editor
MODE|game

RES|1280|720

ORIENT|landscape
ORIENT|portrait

TRANS|type|phase|progress

PROJ|/path/to/project

SOUND|name
MUSIC|name|1
MUSIC|name|0
MUSICSTOP

OVSTAT|...
OVLOG|...

IME_ON
IME_OFF

REQ_TEXT|current
REQ_NAME|current
REQ_ACTION|current
REQ_NUM|current
REQ_IMPORT|category|projectRoot
```

### DRAW-команды

```text
DRAW bg|#RRGGBB

DRAW clipon
DRAW clipoff

DRAW rect|x|y|w|h|#AARRGGBB|angle
DRAW text|string|x|y|size|#AARRGGBB|angle
DRAW shape|kind|x|y|w|h|#AARRGGBB|angle
DRAW button|text|x|y|w|h|#AARRGGBB|angle|texture
DRAW tex|path|x|y|w|h|angle

DRAW codeline|index|text|y|size|#color
DRAW caret|prefix|x|y|height|#color
```

`shape` включает:

```text
square
circle
diamond
triangle
glow
```

### Текст и `|`

Разделитель протокола — `|`.

Поэтому `sanitizeLine()` заменяет:

```text
| → /
tab → два пробела
control chars → пробел
```

Вывод: в `Label.text`, `Button.text`, `set_text()` нельзя использовать `|`.

### Transform режима

- Hub/Editor: stretch на весь экран.
- Game: contain + letterbox.
- Slide transition: дополнительный translate по X.
- Fade transition: чёрный оверлей с альфой.

---

## 7. Формат сцены

Корень сцены:

```json
{
  "name": "Main",
  "gravity": 900,
  "bg": "#101018",
  "nodes": [],
  "ui": []
}
```

| Поле | Значение |
|---|---|
| `name` | имя сцены |
| `gravity` | гравитация для платформерной симуляции |
| `bg` | фон сцены |
| `nodes` | дерево нод |
| `ui` | экранные кнопки |

### Единицы

В JSON:

- `rotation` — градусы;
- `alpha` — `0..1`;
- `scale_x`, `scale_y` — множители, например `1.0`, `2.0`;
-颜色 — рекомендуется `#RRGGBB`.

Внутри C++:

- `Node2D::rotation` — радианы.

### Цвет

Внутренний канон цвета в рендере:

```text
0xRRGGBBAA
```

Но в JSON сцены лучше использовать только:

```text
#RRGGBB
```

Причина: `CommonTypes::parseColor()` для `#AARRGGBB` возвращает сырое значение и ломает соглашение альфы.  
Lua `set_color()` имеет свой парсер и для `#AARRGGBB` просто отбрасывает AA.

### Типы нод

#### `Node2D`

```json
{
  "type": "Node2D",
  "name": "Box",
  "x": 420,
  "y": 320,
  "w": 90,
  "h": 90,
  "shape": "square",
  "color": "#D62828",
  "action": "call:boxTap"
}
```

Поля:

| Поле | Значение |
|---|---|
| `name` | уникальное имя |
| `x`, `y` | локальная позиция |
| `w`, `h` | размер |
| `shape` | `square`, `circle`, `diamond`, `triangle`, `none` |
| `color` | цвет |
| `texture` | путь к текстуре |
| `action` | строковое действие |
| `rotation` | градусы |
| `scale_x`, `scale_y` | масштаб |
| `locked` | `0/1` |
| `alpha` | `0..1` |
| `children` | вложенные ноды |

#### `Label`

```json
{
  "type": "Label",
  "name": "HUD",
  "x": 420,
  "y": 40,
  "text": "score: {score}",
  "font_size": 22,
  "color": "#FCBF49"
}
```

`{var}` подставляется из `ctx.vars` и печатается как целое число.

#### `Sprite2D`

```json
{
  "type": "Sprite2D",
  "name": "Hero",
  "x": 200,
  "y": 300,
  "texture": "assets/hero.png",
  "w": 64,
  "h": 64
}
```

#### `Camera2D`

```json
{
  "type": "Camera2D",
  "name": "Cam",
  "x": 640,
  "y": 360,
  "zoom": 1.0,
  "follow": "Player"
}
```

`zoom` — множитель: `1.0 = 100%`.

#### `Light2D`

```json
{
  "type": "Light2D",
  "name": "Glow",
  "x": 500,
  "y": 300,
  "radius": 180,
  "intensity": 1.0,
  "color": "#FFD700"
}
```

Рисуется как `DRAW shape|glow`.

#### `Particle2D`

```json
{
  "type": "Particle2D",
  "name": "Fire",
  "x": 300,
  "y": 500,
  "rate": 40,
  "burst": 40,
  "vx": 0,
  "vy": -180,
  "spread": 140,
  "gravity": 80,
  "life": 0.8,
  "life_spread": 0.3,
  "size": 18,
  "size_end": 4,
  "drag": 0.5,
  "glyph": "•",
  "color": "#FF6020",
  "emitting": 1
}
```

#### `Prefab2D`

```json
{
  "type": "Prefab2D",
  "name": "EnemyPrefab",
  "source": "prefabs/enemy.prf",
  "x": 500,
  "y": 300
}
```

Дети префаба не сериализуются в сцене; они инстансируются из `.prf`.

### Платформерные типы

Есть отдельный legacy/встроенный платформерный слой:

- `Player`
- `Enemy`
- `Coin`
- `Solid2D`

Они работают через `Context`, а не через Box2D.

#### `Player`

Поля:

- `speed`
- `mode`: `topdown` или `platformer`
- `jump_speed`

Поведение:

- `topdown`: двигается джойстиком;
- `platformer`: двигается/прыгает через платформерную симуляцию сцены.

#### `Enemy`

Преследует `ctx.playerPos`.

#### `Coin`

При пересечении с игроком:

```cpp
ctx.score += 1;
ctx.coinCollectedThisFrame = true;
```

#### `Solid2D`

Статичная платформа для платформерной AABB-симуляции.

### UI-кнопки

```json
{
  "id": "Start",
  "x": 500,
  "y": 600,
  "w": 180,
  "h": 60,
  "text": "START",
  "action": "call:start",
  "color": "#2EC4B6",
  "angle": 0,
  "alpha": 1.0,
  "texture": "",
  "group": ""
}
```

`group` привязывает кнопку к ноде для совместного перемещения в редакторе.

---

## 8. Строковые действия

Поддерживаются в `action` нод и UI-кнопок.

| Действие | Значение |
|---|---|
| `call:имя` | вызвать глобальную Lua-функцию |
| `set_var:имя:число` | записать переменную |
| `add_var:имя:число` | прибавить к переменной |
| `restart_scene:путь` | мгновенная перезагрузка сцены |
| `change_scene:путь` | мгновенная смена сцены |
| `hub:` | выйти в хаб |
| `dbg:` | переключить отладку |
| `close:` | выйти из игры в хаб/редактор |
| `editor_return:имя` | вернуться в редактор |

Важно: в live-коде `restart_scene:` и `change_scene:` оба идут как instant.  
Для анимированного перехода используй Lua `transition()`.

---

## 9. Lua-контракт

### Состояние скриптов

Все `.lua` из `scripts/` грузятся в один `lua_State`.

Top-level код выполняется один раз при загрузке.

Локальные переменные и upvalue **переживают** вызовы `on_update`, `call:`, коллизий.

Но:

### `on_start` и `scene_end` не вызываются

Ни C++, ни мост, ни Java не дёргают:

```lua
on_start()
scene_end()
```

Это не системные хуки.

Правильная инициализация:

```lua
local function ensureInit()
    if get_var("_ready") == 1 then return end
    set_var("_ready", 1)

    -- setup
end

function on_update(dt)
    ensureInit()
    -- logic
end
```

### Переменные

`ctx.vars` — это:

```cpp
std::map<std::string, double>
```

Значит:

- только числа;
- строки в `set_var()` вызовут ошибку Lua;
- отсутствующая переменная в `get_var()` даёт `0`;
- `{var}` в `Label` печатает только целую часть;
- человекочитаемый текст делается через `set_text()`.

### Счёт

Отдельный числовой канал:

```lua
get_score()
set_score(n)
add_score(n)
```

Он попадает в:

- DBG `score`;
- `save.vars` как `__score__`.

Не храните счёт в `set_var("score", ...)`, если хотите штатный HUD/прогресс.

---

## 10. Lua API

### Состояние

```lua
set_var(name, number)
get_var(name) -> number
add_var(name, number)
```

### Счёт

```lua
get_score() -> integer
set_score(number)
add_score(number)
```

### Вывод

```lua
print(...)
log(...)
```

Пишут в `g_luaLog`, максимум 80 строк.

### Ноды: трансформы

```lua
set_pos(name, x, y)
set_x(name, x)
set_y(name, y)

set_rot(name, degrees)
set_scale(name, s)
set_scale(name, sx, sy)
set_size(name, w, h)
set_alpha(name, a) -- 0..1 или 0..100
```

`set_rot()` принимает градусы, внутри сохраняет радианы.

### Ноды: вид

```lua
set_color(name, "#RRGGBB")
set_color(name, "r,g,b")
set_color(name, r, g, b)
set_color(name, 0xRRGGBB)

set_texture(name, path)
set_texture(name, "") -- снять текстуру

set_shape(name, shape)
set_text(name, text) -- Label или UI-кнопка

hide(name)  -- alpha = 0
show(name)  -- alpha = 1
```

Важно:

- `hide()` не убирает ноду из физики;
- `hide()` не убирает ноду из editor/game hit-test;
- для удаления тела физики используй `remove_body(name)`.

### Ноды: поведение

```lua
set_action(name, actionString)
exists(name) -> boolean
```

`exists()` ищет и ноды, и UI-кнопки текущей сцены.

### Камера

```lua
set_cam(cameraName, x, y)
set_zoom(cameraName, zoom)
```

Работает только если нода — `Camera2D`.

### Частицы

```lua
spawn(particleName, count)
emit(particleName, on)
emit(particleName) -- включить
```

Работает только если нода — `Particle2D`.

### Переходы

```lua
transition(type, scenePath, duration)
```

Типы:

| Канон | Синонимы |
|---|---|
| `fade` | `black`, `dissolve` |
| `slide_left` | `swipe left`, `l` |
| `slide_right` | `swipe right`, `r` |
| `instant` | `cut`, `none`, `direct` |

Длительность:

- по умолчанию `0.4`;
- если `< 0.05`, становится `0.4`;
- максимум `5.0`.

Переход асинхронный: Lua пишет команду, движок исполняет на следующем кадре.

---

## 11. Физика Box2D

Интеграция: Box2D `v2.4.1`.

Масштаб:

```cpp
PPM = 100
```

То есть:

```text
100 px = 1 meter
```

Гравитация по умолчанию:

```cpp
900 px/s^2
```

Шаг:

```cpp
fixed dt = 1/60
Box2D substeps = 2
velocity iterations = 8
position iterations = 3
```

### Тела

```lua
add_rigidbody(name, mass?)
add_staticbody(name)
add_trigger(name)

set_trigger(name, bool)
is_trigger(name) -> bool

remove_body(name)
is_body(name) -> bool
```

Редакторские теги `RIGIDBODY`, `STATICBODY`, `NOGRAV`, `BOUNCY` пишутся в `funcs.txt` и применяются через `applyProjFuncs()` при входе в игру/сцену.

Lua и редактор используют один реестр `g_bodies`.

Важно:

- `bodyAdd()` идемпотентен для существующего имени;
- если тело уже триггер, обычный `add_rigidbody/add_staticbody` не сбросит `isTrigger`;
- триггер всегда создаётся как static sensor.

### Кинематика

```lua
set_velocity(name, vx, vy)
add_velocity(name, dvx, dvy)

get_velocity_x(name) -> px/s
get_velocity_y(name) -> px/s

set_spin(name, angularVelocityRadPerSec)
get_spin(name) -> rad/s
```

Скорости в Lua — px/s.  
Угловая скорость — rad/s.

### Материалы и гравитация

```lua
set_bounce(name, restitution)
set_friction(name, friction)
set_mass(name, mass)

no_gravity(name)
no_gravity(name, on)

set_gravity(pxPerSec2)
get_gravity() -> pxPerSec2
```

`set_gravity()` меняет глобальную Box2D-гравитацию.  
Он не меняет `scene.gravity` платформерного слоя.

### Позиция и земля

```lua
is_on_ground(name) -> bool
```

Эвристика: любой касающийся не-сенсорный контакт с точкой ниже центра тела более чем на `0.01` метра, то есть примерно `1 px`.

### Raycast

```lua
raycast(x1, y1, x2, y2, ignoreName?)
```

Возвращает:

```lua
name, hx, hy, nx, ny, fraction
```

или `nil`.

Особенности:

- координаты в px;
- сенсоры/триггеры пропускаются;
- останавливается на первом попадании;
- используется Box2D v2.4.1 API с 3 аргументами.

### Отладка

```lua
show_hitboxes(true)
show_hitboxes(false)
```

Хитбоксы рисуются и в редакторе, и в игре.

### Известные ограничения физики

1. **Box2D использует локальную позицию ноды, а не мировую трансформацию иерархии.**  
   Для вложенных нод с ненулевым родителем физика может не совпадать с рендером.

2. **Динамические тела перезаписывают позицию ноды каждый кадр.**  
   `set_pos()` для динамического тела будет перебит физикой.

3. **Статика и триггеры следуют за нодой.**  
   Их можно двигать через `set_pos()`/редактор.

4. **Флаги тел переживают смену сцены внутри одной игровой сессии.**  
   `physicsReset()` обнуляет указатели `b2Body*`, но не стирает записи `Body`.  
   Если имя ноды повторяется в другой сцене, старые `isTrigger/mass/friction/...` могут остаться.

5. **`diamond` и `triangle` получают момент инерции прямоугольника.**  
   Вращение не будет физически точным для этих форм.

6. **`get_body_x`, `get_body_y`, `get_body_rotation` в текущем `Physics.hpp` отсутствуют.**  
   Если демо их использует, нужен патч.

---

## 12. Коллизии и триггеры

Хуки:

```lua
on_collide(a, b)
on_trigger(a, b)
on_trigger_exit(a, b)
```

Правила:

- порядок `(a,b)` не гарантирован;
- проверяй обе перестановки;
- триггеры — это сенсоры Box2D;
- raycast триггеры не бьёт;
- `is_on_ground()` сенсорные контакты игнорирует.

---

## 13. Звук: два независимых стека

Это одна из самых коварных частей движка.

### Стек A: Lua / miniaudio

Регистрируется в `Sound.cpp` и побеждает в Lua-глобалах, потому что вызывается после `registerGameLuaApi()`.

API:

```lua
audio_init()
audio_shutdown()

load_sound(path, decode?) -> id|nil
unload_sound(id) -> bool

play_sound(id, volume?, loop?) -> id|nil
stop_sound(id) -> bool
pause_sound(id) -> bool
resume_sound(id) -> bool

set_sound_volume(id, volume) -> bool
set_sound_pitch(id, pitch) -> bool
is_sound_playing(id) -> bool

set_master_volume(volume)
get_master_volume() -> number
stop_all_sounds()

play_music(path, volume?) -> id|nil
stop_music()
set_music_volume(volume) -> bool
is_music_playing() -> bool
```

Пути miniaudio резолвятся от runtime-корня:

```cpp
projectRootRef()
```

То есть на Android:

```text
files/assets/sounds/...
```

Корректный Lua-путь:

```lua
load_sound("assets/sounds/jump.wav", true)
```

### Стек B: Java / SoundPool + MediaPlayer

Используется редакторскими Sound-объектами, платформерными событиями `coin/jump` и старой строковой очередью `g_soundQ`.

Java слушает команды:

```text
SOUND|name
MUSIC|name|loop
MUSICSTOP
```

Java грузит звуки из текущего проекта:

```text
files/projects/<Project>/sounds/
```

и использует голое имя без расширения:

```text
jump
coin
music
```

Форматы:

- `.wav`, `.ogg` → SoundPool;
- `.mp3` → MediaPlayer;
- музыка → MediaPlayer loop.

### Главный подводный камень

Lua `stop_music()` останавливает **miniaudio-музыку**.

Он не останавливает Java `MediaPlayer`-музыку, запущенную редакторским автоплеем или платформерным стеком.

Host при выходе/смене сцены вызывает C++ `lua_stop_music()`, который кладёт `MS` в Java-очередь.  
Но если Lua сам вызывает глобальный `stop_music()`, это попадёт в miniaudio, а не в Java.

### Практическое правило

Для Lua-скриптов:

- используй miniaudio;
- клади звуки в глобальный `assets/sounds/`;
- пути вида `assets/sounds/name.wav`;
- не рассчитывай, что редакторский импорт в `<project>/sounds/` автоматически увидит Lua без патча `Sound.cpp`.

Для редакторских Sound-объектов и платформерных событий:

- звуки лежат в `<project>/sounds/`;
- имя в `funcs.txt`/панели SOUND без расширения.

---

## 14. Платформерный слой

Отдельный от Box2D.

Включается, если в сцене есть `gravity > 0` и ноды типов:

- `Player`
- `Solid2D`
- `Enemy`
- `Coin`

Работает через `Context`:

- `ctx.input.joystickX/Y`
- `ctx.input.jumpPressed`
- `ctx.playerPos`
- `ctx.hasPlayer`
- `ctx.score`
- `ctx.coinCollectedThisFrame`
- `ctx.jumpPressedThisFrame`

Джойстик:

- активен на левой половине экрана;
- в текущем live-пути используется статический центр `(320,360)` из `TouchProcessor`;
- `VirtualJoystick` из `Input.hpp` фактически не вызывается в live-пути.

Кнопки `jump` и `attack`:

- распознаются по фиксированным id UI-кнопок;
- попадают в `ctx.input`;
- доступны платформерным типам;
- в Lua прямых геттеров ввода нет.

---

## 15. Редактор

Основные элементы:

- Scene hierarchy;
- Inspector;
- viewport с сеткой и рамкой камеры;
- gizmo POS / ROT / SCL;
- FILES;
- ASSETS;
- SETTINGS/import;
- prefabs;
- particles;
- sound objects;
- Lua script editor;
- undo/redo.

### Undo/redo

Снимки:

```text
snap_0.json
snap_1.json
...
```

Хранятся в корне проекта, скрыты из FILES.

Максимум:

```text
12 шагов
```

Undo покрывает только редакторские действия, не Lua-изменения во время игры.

### FN-ряд физики

Кнопки:

```text
RIGIDBODY
STATICBODY
NOGRAV
BOUNCY
HBOX
CLEAR
```

Пишут теги в `funcs.txt`.

### SOUND-панель

Для выделенного Sound-объекта:

```text
SET
LOOP
AUTO
PLAY
STOP
```

Звуки привязываются к `<project>/sounds/` и играют через Java-стек.

### Редактор скриптов

- список `.lua`;
- 24 видимые строки;
- курсор;
- IME;
- SAVE перезаписывает файл и вызывает `scripts_.load()`.

Важно: `scripts_.load()` пересоздаёт Lua-state и чистит:

```cpp
g_bodies
g_collideEvents
g_luaCmd
g_soundQ
```

То есть сохранение скрипта в редакторе сбрасывает игровую физику/звуковые очереди.

---

## 16. EditorCommands

Файл:

```text
editor_commands.json
```

Формат:

```json
{
  "commands": [
    { "action": "add", "type": "Player", "name": "Player", "x": 220, "y": 360 }
  ]
}
```

Механизм существует в `EditorCommands.hpp`.

Но в live-Android-пути вызова `EditorCommands::run()` не видно.  
Он вызывается только в мёртвом `main.cpp`.

Статус:

- experimental / legacy / automation hook;
- не документировать как пользовательскую фичу редактора, пока не подключён в live-путь.

Ограничения:

- нет `set_action`;
- нет создания UI-кнопок;
- нет undo;
- нет перезагрузки скриптов после `create_script`.

---

## 17. Настройки

Файл:

```text
files/settings.json
```

Формат:

```json
{
  "theme": 1
}
```

Темы:

| index | имя |
|---:|---|
| 0 | Cream & Red |
| 1 | Teal & Navy |
| 2 | Night & Paper |

Других настроек сейчас нет.

---

## 18. Мёртвые файлы

Эти файлы не входят в live-Android-путь и/или противоречат живому коду.

### `src/main.cpp`

Десктопный legacy-хост.

Проблемы:

- не входит в CMake-цель `suka`;
- ссылается на несуществующие `SoundManager`, `LogSoundBackend`;
- использует старую сигнатуру `scripts.update(...)`.

Статус: мёртв.

### `src/Hub.hpp`

Старая версия хаба.

Live-код использует:

```cpp
buildHubScene(HubUiInput)
processHubScene(Scene&)
```

из `HubUI.hpp`.

`Hub.hpp` содержит старые:

```cpp
buildHubScene(HubState&)
processHub(Scene&)
printHubLayout(...)
nextGameDir(...)
```

Статус: мёртв/легаси.

### `src/Prefab.hpp`

Содержит конфликтующую версию `Prefab2D`.

Live-версия `Prefab2D` находится в `Scene.hpp`.

Отличия:

- нет `instUiIds`;
- другая сигнатура `instantiate()`;
- собственный `writeJson()`, не используемый живым `SceneWriter`.

Статус: мёртв.

---

## 19. Главные ловушки

### Не используй `on_start` как жизненный цикл

Он не вызывается.

### Не храните строки в `set_var`

`vars` — только числа.

### Не жди `{var}` для текста

`{var}` печатает только целые числа.  
Для `on/off`, имён, сообщений используй `set_text()`.

### Не используй `|` в тексте

Он ломает `DRAW`-протокол.

### Не используй `#AARRGGBB` в JSON сцены

Лучше `#RRGGBB`, альфа отдельно полем `alpha`.

### `hide()` не убирает физику

Он ставит `alpha = 0`.

### `set_pos()` не работает для динамических Box2D-тел

Физика перезапишет позицию.

### Lua-звук и редакторский звук — разные стеки

- Lua miniaudio: `files/assets/sounds/...`
- Java SoundPool/MediaPlayer: `files/projects/<proj>/sounds/...`

### `scripts.json` не маршрутизирует скрипты по нодам

Runtime грузит все `.lua` в одно пространство имён.

### `scene.gravity` — не Box2D gravity

Это платформерная гравитация.  
Box2D гравитация меняется через `set_gravity()`.

### Box2D не понимает иерархию трансформаций

Он использует локальную позицию ноды. Для вложенных нод это может расходиться с рендером.

---

## 20. Рекомендуемый шаблон Lua-скрипта

```lua
-- script for node Player

local function ensureInit()
    if get_var("_ready") == 1 then return end
    set_var("_ready", 1)

    set_score(0)

    set_var("hp", 100)
    set_var("alive", 1)
    set_var("grav", 1)
    set_var("hb", 0)

    add_staticbody("Ground")
    add_rigidbody("Player", 1.0)

    set_gravity(900)
    show_hitboxes(false)

    refreshHud()
end

function refreshHud()
    set_text("HUD", string.format(
        "score %d  hp %d  alive %s  grav %s  hbox %s",
        get_score(),
        get_var("hp"),
        get_var("alive") == 1 and "yes" or "no",
        get_var("grav") == 1 and "on" or "off",
        get_var("hb") == 1 and "on" or "off"
    ))
end

function on_update(dt)
    ensureInit()

    -- gameplay logic
end

function tapPlayer()
    if get_var("alive") ~= 1 then return end

    add_score(1)
    add_velocity("Player", 0, -250)
    refreshHud()
end

function on_collide(a, b)
    -- проверяй обе перестановки
end

function on_trigger(a, b)
end

function on_trigger_exit(a, b)
end
```
