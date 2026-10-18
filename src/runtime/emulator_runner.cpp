#include "runtime/emulator_runner.hpp"

#include "runtime/mame_config.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWindow>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace rl::runtime {

namespace {
constexpr int kLogLines = 400;
constexpr int kStartupPollMs = 150;
constexpr int kStartupPolls = 200;      // up anyway after 30 s without the ready file
constexpr int kQuietStartPolls = 10;    // no ready file named: up after 1.5 s
constexpr int kStopGraceMs = 2000;      // then the process is killed
constexpr int kStatusPollMs = 200;      // the control script's status file
constexpr int kMovieFlushMs = 3000;
constexpr int kRewriteMs = 15;          // a control file the script was reading     // a recording written before the emulator stops

// MAME runs this as its autoboot script on every machine start, a hard reset
// included, reading it from disk each time; the runner rewrites it before a
// reload. It
// - gives the game's script the environment of the current launch (the
//   process environment is fixed when MAME starts);
// - when the game names a ready file, runs unthrottled and muted until the
//   game reports ready, so the boot and the steering to the start point take
//   a fraction of a second (the Playtest view covers the picture meanwhile);
// - watches the command file: a line naming a cartridge copy mounts it, which
//   hard-resets the machine into the new build;
// - presses the game's inputs for the keys the studio forwards (MAME cannot
//   read the keyboard of a window it does not own): each frame it reads the
//   keys file and sets every I/O port field whose MAME input assignment
//   names one of those keys;
// - runs the plan's own scripts unchanged, in the order the plan gives them
//   (MAME alone would keep only the last -autoboot_script).
const char* kControlScript = R"LUA(-- Written by Retrolarium Studio for the running playtest.
local cart = [==[@CART@]==]
local command_file = [==[@COMMAND@]==]
local keys_file = [==[@KEYS@]==]
local picture_file = [==[@PICTURE@]==]
local studio_palette = {@PALETTE@}
local ready_file = [==[@READY@]==]
local ready_when = [==[@READYWHEN@]==]
local game_scripts = {@SCRIPTS@}
local actions_file = [==[@ACTIONS@]==]
local status_file = [==[@STATUS@]==]
local movie_mode = [==[@MOVIEMODE@]==]
local movie_file = [==[@MOVIEFILE@]==]
local movie_header = [==[@MOVIEHEADER@]==]
-- A movie that plays may end the run at a frame, or when it ends.
local movie_stop_at = tonumber([==[@MOVIESTOPAT@]==]) or 0
local movie_close = [==[@MOVIECLOSE@]==] == "1"
local environment = {
@ENVIRONMENT@}
local machine = manager.machine
local video, sound = machine.video, machine.sound

-- Frame callbacks outlive the machine: notifiers and the legacy
-- emu.register_frame_done list survive a hard reset and would run against
-- the machine that is gone. Each run of this script is a generation; the
-- previous one's subscriptions are dropped and its callbacks stay quiet.
_G.retrolarium_generation = (_G.retrolarium_generation or 0) + 1
local generation = _G.retrolarium_generation
for _, subscription in ipairs(_G.retrolarium_subscriptions or {}) do
    subscription:unsubscribe()
end
_G.retrolarium_subscriptions = {}
local function on_frame(callback)
    local subscription = emu.add_machine_frame_notifier(function()
        if _G.retrolarium_generation == generation then callback() end
    end)
    table.insert(_G.retrolarium_subscriptions, subscription)
    return subscription
end
-- The game's script hooks frames through these; route them to on_frame.
-- MAME's own frame-done callback runs before each frame is drawn, the moment
-- to draw over the picture: kept from the first run for the movie counter.
_G.retrolarium_draw_hook = _G.retrolarium_draw_hook or emu.register_frame_done
emu.register_frame_done = function(callback) return on_frame(callback) end
emu.register_frame = emu.register_frame_done
-- A hard reset (the Disk System BIOS start does one) brings up the next
-- machine, which runs frames before this script runs on it: the generation
-- ends when its machine stops.
table.insert(_G.retrolarium_subscriptions, emu.add_machine_stop_notifier(function()
    if _G.retrolarium_generation == generation then _G.retrolarium_generation = generation + 1 end
end))

_G.retrolarium_getenv = _G.retrolarium_getenv or os.getenv
os.getenv = function(name)
    local value = environment[name]
    if value ~= nil then return value end
    return _G.retrolarium_getenv(name)
end

local function exists(path)
    local file = io.open(path, "r")
    if file == nil then return false end
    file:close()
    return true
end

local function take_command()
    local file = io.open(command_file, "r")
    if file == nil then return end
    local next_cart = file:read("l")
    file:close()
    os.remove(command_file)
    if next_cart == nil or next_cart == "" then return end
    -- From here on this generation's callbacks are done.
    _G.retrolarium_generation = generation + 1
    local fallback = nil
    for _, image in pairs(machine.images) do
        if image.exists then
            if string.lower(image.filename) == string.lower(cart) then
                image:load(next_cart)
                return
            end
            fallback = fallback or image
        end
    end
    if fallback ~= nil then fallback:load(next_cart) end
end

-- Fields by the keyboard keys MAME's input settings give them.
local fields_by_key = {}
for _, port in pairs(machine.ioport.ports) do
    for _, field in pairs(port.fields) do
        local tokens = machine.input:seq_to_tokens(field:input_seq("standard")) or ""
        for token in string.gmatch(tokens, "KEYCODE_[%w_]+") do
            fields_by_key[token] = fields_by_key[token] or {}
            table.insert(fields_by_key[token], field)
        end
    end
end
local key_aliases = {
    KEYCODE_Z = "KEYCODE_LCONTROL", KEYCODE_X = "KEYCODE_LALT",
    KEYCODE_ENTER = "KEYCODE_1", KEYCODE_RSHIFT = "KEYCODE_5",
}
local held, keys_text = {}, ""
-- Holding Tab runs the game unthrottled and muted (fast forward).
local fast_forward = false
-- A movie that plays owns the pads: the keys then only fast-forward.
local function apply_keys(press)
    local file = io.open(keys_file, "r")
    local text = ""
    if file ~= nil then
        text = file:read("a") or ""
        file:close()
    end
    fast_forward = string.find(text, "KEYCODE_TAB", 1, true) ~= nil
    if not press then
        for field in pairs(held) do field:clear_value() end
        held, keys_text = {}, ""
        return
    end
    if text == keys_text then return end
    keys_text = text
    local wanted = {}
    for token in string.gmatch(text, "KEYCODE_[%w_]+") do
        for _, field in ipairs(fields_by_key[token] or {}) do wanted[field] = true end
        -- Keys the studio adds, since Alt and Ctrl belong to the window:
        -- Z and X are buttons 1 and 2, Enter is start, right Shift select.
        local alias = key_aliases[token]
        if alias ~= nil then
            for _, field in ipairs(fields_by_key[alias] or {}) do wanted[field] = true end
        end
    end
    for field in pairs(held) do
        if not wanted[field] then field:clear_value() end
    end
    for field in pairs(wanted) do
        if not held[field] then field:set_value(1) end
    end
    held = wanted
end

-- A ready condition the studio checks itself, for games whose script
-- reports nothing: three frames in a row write the ready file.
local ready_check, ready_frames = nil, 0
if ready_when ~= "" then
    local cpu = machine.devices[":maincpu"]
    local memory = cpu ~= nil and cpu.spaces["program"] or nil
    local chunk = load("local memory = ... return " .. ready_when)
    if memory ~= nil and chunk ~= nil then
        ready_check = function()
            local ok, value = pcall(chunk, memory)
            return ok and value
        end
    else
        print("Retrolarium: the ready condition cannot run; waiting for the time limit.")
    end
end

-- The picture file: "square" shows square pixels instead of the machine's
-- 4:3 screen, "studio" the studio's colours instead of the emulator's.
local picture_text, palette_device, original_pens = nil, nil, nil
local function apply_picture()
    local file = io.open(picture_file, "r")
    local text = ""
    if file ~= nil then
        text = file:read("a") or ""
        file:close()
    end
    if text == picture_text then return end
    picture_text = text
    local square = string.find(text, "square", 1, true) ~= nil
    local studio = string.find(text, "studio", 1, true) ~= nil
    for _, screen in pairs(machine.screens) do
        local settings = screen.container.user_settings
        settings.xscale, settings.yscale = 1.0, 1.0
        if square and screen.width > 0 and screen.height > 0 then
            local ratio = (screen.width / screen.height) / (4 / 3)
            if ratio < 1 then settings.xscale = ratio else settings.yscale = 1 / ratio end
        end
        screen.container.user_settings = settings
    end
    if #studio_palette > 0 then
        if palette_device == nil then
            for _, device in pairs(machine.palettes) do
                if device.entries >= #studio_palette then
                    palette_device = device
                    break
                end
            end
        end
        if palette_device ~= nil then
            if original_pens == nil then
                original_pens = {}
                for i = 0, #studio_palette - 1 do original_pens[i] = palette_device:pen_color(i) end
            end
            for i = 0, #studio_palette - 1 do
                local color = studio and studio_palette[i + 1] or original_pens[i]
                palette_device:set_pen_color(i, (color >> 16) & 255, (color >> 8) & 255, color & 255)
            end
        end
    end
end

local fast = ready_file ~= ""
local was_throttled, was_frameskip, was_muted = video.throttled, video.frameskip, sound.ui_mute
local hidden, frames, unmute_at = fast, 0, nil
local forwarding = false
if fast then
    video.throttled = false
    video.frameskip = 10
    sound.ui_mute = true
end

-- Emulation controls from the Run menu: speed, pause and frame advance, save
-- states, and an input movie recorded or played from power-on. The studio
-- writes "<serial> <verb> <argument>" lines to the actions file, each serial
-- runs once, and the status file tells the studio where things stand.
local screen = nil
for _, candidate in pairs(machine.screens) do
    screen = candidate
    break
end
-- Frame notifiers also run while the machine is paused (it redraws), so a
-- frame counts when emulated time moved.
local function now_time()
    return machine.time:as_double()
end

local speed = 100           -- percent; 0 runs without a limit
local function apply_speed()
    local unlimited = forwarding or speed == 0
    video.throttled = was_throttled and not unlimited
    video.throttle_rate = (speed > 0 and speed or 100) / 100
    video.frameskip = unlimited and 10 or was_frameskip
    sound.ui_mute = was_muted or unlimited or speed ~= 100
end

-- The pads by mask, every field of a mask (MAME lists some twice).
local pads = {{}, {}}
local pad_ports = {}
for tag, port in pairs(machine.ioport.ports) do
    local index = tonumber(tag:match("^:ctrl(%d):"))
    if index == 1 or index == 2 then
        pad_ports[index] = pad_ports[index] or {}
        table.insert(pad_ports[index], port)
        for _, field in pairs(port.fields) do
            pads[index][field.mask] = pads[index][field.mask] or {}
            table.insert(pads[index][field.mask], field)
        end
    end
end
-- What the game read from a pad during the frame that just ended: a port
-- read in the frame notifier still holds that frame's input, whatever gave
-- it (the studio's keys, a game pad, a movie).
local function pad_mask(index)
    local mask = 0
    for _, port in ipairs(pad_ports[index] or {}) do
        local value = port:read()
        for bit, fields in pairs(pads[index]) do
            local field = fields[1]
            if ((value ~ field.defvalue) & bit) ~= 0 then mask = mask | bit end
        end
    end
    return mask
end
local function set_pads(entry)
    for index = 1, 2 do
        local mask = entry ~= nil and entry[index] or 0
        for bit, fields in pairs(pads[index]) do
            for _, field in ipairs(fields) do
                if mask & bit ~= 0 then field:set_value(1) else field:clear_value() end
            end
        end
    end
end

-- The movie: log[k] is the input of movie frame k, the k-th frame emulated
-- after the script started (before the first frame); done counts the frames
-- emulated so far, and a save state keeps its count beside it.
local log, last_time, done = {}, now_time(), 0
_G.retrolarium_movie = {frame = 0}

-- Rewind: states in memory, near and far. Near, one every rewind_every
-- frames, the last rewind_keep of them (15 seconds): a step back runs at most
-- that many frames again. Far, one every far_every frames, the last far_keep
-- of them (5 minutes), for going back further a second at a time. Plus the
-- pads of every frame since the oldest. Going back to a frame loads the state
-- before it and runs the frames after it again with the same pads,
-- unthrottled, then pauses there. (A state is 2 MB or so: one a frame would
-- not fit.)
local rewind_every, rewind_keep = 20, 45
local far_every, far_keep = 300, 60
local ring, far, inputs, inputs_from = {}, {}, {}, 1
local replay_to = nil
-- A state in memory is only right taken or loaded while the machine is
-- paused (during a frame MAME is inside its scheduler, and a state from there
-- comes back subtly wrong). So a capture pauses for one redraw, unthrottled,
-- and a rewind pauses, loads, and goes on from the next redraw (MAME runs no
-- frames after a load and an unpause in one call). user_paused is the pause
-- someone asked for, which a capture keeps.
local capture_pending, rewind_pending, unpause_next = false, nil, false
local user_paused, stepping = false, false
-- A resume asked for while a rewind or a seek is on its way: the game goes
-- on from where that ends instead of pausing there.
local resume_after = false

-- Over the picture, when the Run menu asks: the pads' buttons of the frame
-- shown, and the game's RAM cells its movie checks watch.
local overlay = [==[@OVERLAY@]==]
local show_input, show_watch = overlay:find("input", 1, true) ~= nil, overlay:find("watch", 1, true) ~= nil
local memory_space = machine.devices[":maincpu"] ~= nil and machine.devices[":maincpu"].spaces["program"] or nil
local watch = {@WATCH@}
-- What a run to a stop may wait for (the TAS editor): the game's own
-- conditions, Lua expressions over `memory`, the main CPU's program space.
local stop_when = {}
for _, item in ipairs({@STOPWHEN@}) do
    local chunk = load("local memory = ... return " .. item[2])
    if chunk ~= nil then stop_when[#stop_when + 1] = {item[1], chunk} end
end
local run_until = nil       -- the stop_when index a run waits for
local event, events, ack = "", 0, 0
-- Each event counts, so the studio sees two saves in a row.
local function set_event(name)
    event, events = name, events + 1
end

local function runs_text(count)
    local lines, i = {}, 1
    while i <= count do
        local entry = log[i]
        local j = i
        while j < count and log[j + 1][1] == entry[1] and log[j + 1][2] == entry[2] do j = j + 1 end
        lines[#lines + 1] = string.format("%d %02x %02x", j - i + 1, entry[1], entry[2])
        i = j + 1
    end
    return table.concat(lines, "\n")
end
local function read_runs(path)
    local file = io.open(path, "r")
    if file == nil then return nil end
    local entries, body, frame = {}, false, nil
    for line in file:lines() do
        if line == "---" then
            body = true
        elseif line:find("^frame %d+$") then
            frame = tonumber(line:match("%d+"))
        elseif body or not line:find("^[#%a]") then
            local count, one, two = line:match("^(%d+) (%x+) (%x+)")
            if count ~= nil then
                for _ = 1, tonumber(count) do entries[#entries + 1] = {tonumber(one, 16), tonumber(two, 16)} end
            end
        end
    end
    file:close()
    return entries, frame
end
local function write_movie()
    if movie_file == "" or movie_mode ~= "record" then return end
    local file = io.open(movie_file .. ".tmp", "w")
    if file == nil then return end
    file:write("# retrolarium movie 1\n", movie_header, "frames ", #log, "\noffset 0\n---\n")
    if #log > 0 then file:write(runs_text(#log), "\n") end
    file:close()
    os.remove(movie_file)
    os.rename(movie_file .. ".tmp", movie_file)
end

if movie_mode == "play" or movie_mode == "resume" then
    log = read_runs(movie_file) or {}
    set_pads(log[1])
end

local function write_status()
    local file = io.open(status_file .. ".tmp", "w")
    if file == nil then return end
    local mode = movie_mode == "resume" and "play" or movie_mode
    file:write(string.format("mode %s\nframe %d\nlength %d\nspeed %d\npaused %d\nack %d\nevent %d %s\n",
        mode == "" and "none" or mode, done, #log, speed,
        -- the pauses a capture or a rewind makes are not the game's
        machine.paused and not capture_pending and rewind_pending == nil and not unpause_next and 1 or 0, ack,
        events, event))
    file:close()
    os.remove(status_file)
    os.rename(status_file .. ".tmp", status_file)
end

-- Save states carry their frame count and the movie up to it beside them
-- (<state>.input), so loading one while recording goes back to that point
-- of the movie.
local pending_save = nil
local pending_osd, osd_wait = nil, 0
table.insert(_G.retrolarium_subscriptions, emu.add_machine_pre_save_notifier(function()
    if _G.retrolarium_generation ~= generation or pending_save == nil then return end
    local count = math.min(done, #log)
    local file = io.open(pending_save .. ".input", "w")
    if file ~= nil then
        file:write("frame ", done, "\n")
        if movie_mode ~= "" and count > 0 then file:write(runs_text(count), "\n") end
        file:close()
    end
    set_event("saved")
    pending_save = nil
end))
local pending_load, latched_load, load_paused = nil, nil, false
table.insert(_G.retrolarium_subscriptions, emu.add_machine_post_load_notifier(function()
    if _G.retrolarium_generation ~= generation or pending_load == nil then return end
    local path = pending_load
    pending_load, last_time = nil, now_time()
    -- The rewind states belong to the frames before the load.
    ring, far, inputs, inputs_from = {}, {}, {}, 1
    local saved, frame = read_runs(path .. ".input")
    local recording = movie_mode == "record"
    local playing = movie_mode == "play" or movie_mode == "resume"
    -- A state carries the input that led to it only when it was saved in a
    -- movie (recorded or played). A recording goes on from such a state,
    -- taking its input; a movie plays on from one of its own frames. Any
    -- other state ends the movie (a recording keeps what it had).
    local own = frame ~= nil and saved ~= nil and #saved == frame
    if playing and own then
        for i = 1, frame do
            if log[i] == nil or log[i][1] ~= saved[i][1] or log[i][2] ~= saved[i][2] then
                own = false
                break
            end
        end
    end
    if (recording or playing) and not own then
        write_movie()
        set_pads(nil)
        movie_mode = ""
        done = frame or 0
        set_event("foreign-state")
        return
    end
    done = frame or 0
    _G.retrolarium_movie.frame = done
    if recording then
        log = saved
        -- The keys take the pads again from the next frame.
        set_pads(nil)
    elseif playing then
        set_pads(log[done + 1])
    else
        set_pads(nil)
    end
    set_event("loaded")
end))
-- The movie is kept when the machine stops (the window closed). The
-- generation already ended in the stop notifier above, and this one only
-- ever sees its own machine stop: the next script drops it before it runs.
table.insert(_G.retrolarium_subscriptions, emu.add_machine_stop_notifier(function()
    write_movie()
end))

-- The pads for the frame after `done`: the movie's while one plays, else
-- the ones that frame had before (a rewind runs it again).
local function pads_of(frame)
    if movie_mode == "play" or movie_mode == "resume" then return log[frame] end
    return inputs[frame]
end
local function replay_pads()
    return pads_of(done + 1)
end

-- Back at the frame a rewind went to: paused, a recording drops what came
-- after it, and the keys or the movie take the pads again.
local function finish_rewind()
    replay_to = nil
    apply_speed()
    if movie_mode == "record" then
        for i = #log, done + 1, -1 do log[i] = nil end
        set_pads(nil)
    elseif movie_mode == "play" or movie_mode == "resume" then
        set_pads(log[done + 1])
    else
        set_pads(nil)
    end
    if resume_after then
        resume_after, user_paused = false, false
    else
        user_paused = true
        emu.pause()
    end
    set_event("rewound")
end

-- Called while paused: the frame `done` just ended.
local function capture_state()
    local entry = {frame = done, state = machine:buffer_save()}
    ring[#ring + 1] = entry
    if #ring > rewind_keep then table.remove(ring, 1) end
    if done % far_every == 0 then
        far[#far + 1] = entry
        if #far > far_keep then table.remove(far, 1) end
    end
    -- The pads from before the oldest state are not run again.
    local oldest = math.min(ring[1].frame, far[1] ~= nil and far[1].frame or ring[1].frame)
    for i = inputs_from, oldest do inputs[i] = nil end
    inputs_from = math.max(inputs_from, oldest + 1)
end

-- The latest state at or before a frame, near or far; the oldest one.
local function state_before(frame)
    local found = nil
    for _, list in ipairs({ring, far}) do
        for i = #list, 1, -1 do
            if list[i].frame <= frame then
                if found == nil or list[i].frame > found.frame then found = list[i] end
                break
            end
        end
    end
    return found
end
local function oldest_state()
    if ring[1] == nil or (far[1] ~= nil and far[1].frame < ring[1].frame) then return far[1] end
    return ring[1]
end

local function rewind(count)
    if latched_load ~= nil or replay_to ~= nil or rewind_pending ~= nil then return end
    local target = math.max(done - count, 0)
    local entry = state_before(target)
    if entry == nil then
        -- As far back as the states go.
        entry = oldest_state()
        if entry == nil or entry.frame >= done then
            user_paused = true
            emu.pause()
            set_event("rewind-limit")
            return
        end
        target = entry.frame
    end
    -- The pads hold the buttons of the state's next frame for one frame
    -- first (a paused machine does not take them in): the first frame after
    -- the load reads those, not the keys held now. That frame then pauses,
    -- and the state loads on the redraw after it.
    rewind_pending, capture_pending = {entry, target, "latch"}, false
    for _, list in ipairs({ring, far}) do
        for i = #list, 1, -1 do
            if list[i].frame > entry.frame then table.remove(list, i) end
        end
    end
    for field in pairs(held) do field:clear_value() end
    held, keys_text = {}, ""
    set_pads(pads_of(entry.frame + 1))
    video.throttled = false
    if machine.paused then emu.unpause() end
end

-- Paused: load the state, then pause there or run on to the frame asked for.
local function rewind_load()
    local entry, target = rewind_pending[1], rewind_pending[2]
    rewind_pending = nil
    machine:buffer_load(entry.state)
    -- The machine's time shows the state's from the next redraw on.
    done, last_time = entry.frame, nil
    _G.retrolarium_movie.frame = done
    for field in pairs(held) do field:clear_value() end
    held, keys_text = {}, ""
    if done >= target then
        finish_rewind()
        return
    end
    replay_to = target
    set_pads(replay_pads())
    video.throttled, video.frameskip = false, 10
    unpause_next = true
end

-- The TAS editor: go to a frame of the movie, back or forward, paused there.
local function seek(target)
    if latched_load ~= nil or replay_to ~= nil or rewind_pending ~= nil then return end
    local playing = movie_mode == "play" or movie_mode == "resume"
    if playing then target = math.min(target, #log) end
    if target < done then
        rewind(done - target)
    elseif target > done and playing then
        replay_to = target
        video.throttled, video.frameskip = false, 10
        if machine.paused then emu.unpause() end
    end
end

-- Run the movie on, unthrottled, until a stop condition holds or it ends.
local function stop_run(name)
    run_until = nil
    apply_speed()
    user_paused = true
    emu.pause()
    set_event(name)
end

local function run_action(verb, argument)
    if verb == "speed" then
        speed = math.max(0, tonumber(argument) or 100)
        apply_speed()
    elseif verb == "pause" then
        user_paused, resume_after = true, false
        if not machine.paused then emu.pause() end
    elseif verb == "resume" then
        user_paused = false
        if replay_to ~= nil or rewind_pending ~= nil or unpause_next then
            resume_after = true
        elseif machine.paused then
            emu.unpause()
        end
    elseif verb == "step" then
        user_paused = true
        if machine.paused then
            stepping = true
            emu.step()
        else
            emu.pause()
        end
    elseif verb == "save" then
        pending_save = argument
        -- Asking MAME to save (or load) lifts its pause; a paused game
        -- stays paused, and the state is still written.
        local paused = machine.paused
        machine:save(argument)
        if paused then emu.pause() end
    elseif verb == "load" then
        local file = io.open(argument, "r")
        if file == nil then
            set_event("no-state")
        else
            file:close()
            -- A state holds the machine, not the pads' latch, which the game
            -- may still read in the frame the state was taken in. So the pads
            -- first hold the buttons of that frame for one frame, and the
            -- state loads at the end of it: the game then reads what it read
            -- when the state was saved, not the keys held at F8.
            local saved, frame = read_runs(argument .. ".input")
            for field in pairs(held) do field:clear_value() end
            held, keys_text = {}, ""
            set_pads(saved ~= nil and frame ~= nil and frame > 0 and saved[frame] or nil)
            latched_load, load_paused = argument, machine.paused
            if machine.paused then emu.step() end
        end
    elseif verb == "stopmovie" then
        write_movie()
        if movie_mode == "play" or movie_mode == "resume" then set_pads(nil) end
        movie_mode = ""
        set_event("stopped")
    elseif verb == "flush" then
        write_movie()
    elseif verb == "exit" then
        -- The studio stops the game: MAME embedded in it gets no close
        -- message of its own, so it ends from here (the movie is written).
        write_movie()
        machine:exit()
    elseif verb == "rewind" then
        rewind(tonumber(argument) or 1)
    elseif verb == "seek" then
        seek(tonumber(argument) or done)
    elseif verb == "until" then
        local index = tonumber(argument)
        if index ~= nil and stop_when[index] ~= nil and replay_to == nil and rewind_pending == nil
                and (movie_mode == "play" or movie_mode == "resume") then
            run_until = index
            video.throttled, video.frameskip = false, 10
            if machine.paused then emu.unpause() end
        end
    elseif verb == "reload" then
        -- The editor changed the movie: its pads from here on.
        if movie_mode == "play" or movie_mode == "resume" then
            log = read_runs(argument) or log
            if not machine.paused then set_pads(log[done + 1]) end
        end
    elseif verb == "record" then
        -- Record from this frame on: the movie's later frames are dropped.
        if movie_mode == "play" or movie_mode == "resume" then
            for i = #log, done + 1, -1 do log[i] = nil end
            movie_mode = "record"
            set_pads(nil)
            write_movie()
            set_event("recording")
        end
    elseif verb == "overlay" then
        show_input = argument:find("input", 1, true) ~= nil
        show_watch = argument:find("watch", 1, true) ~= nil
    elseif verb == "osd" then
        -- A note over the picture ("State 3 saved"), as FCEUX shows one,
        -- shown once a save or load in progress is done: MAME then posts
        -- its own note with the state's whole path, which this replaces.
        pending_osd, osd_wait = argument, 2
    end
end

local function take_actions()
    local file = io.open(actions_file, "r")
    if file == nil then return end
    local text = file:read("a") or ""
    file:close()
    for line in text:gmatch("[^\n]+") do
        local serial, verb, argument = line:match("^(%d+) (%S+) ?(.*)$")
        serial = tonumber(serial)
        if serial ~= nil and serial > ack then
            ack = serial
            run_action(verb, argument)
        end
    end
end

-- The movie's frame over the picture, as Gens shows it: frame/length while
-- it plays, REC and the frame while it records. Drawn in MAME's frame-done
-- callback (a frame notifier is too late), in the screen's pixels.
-- The input overlay draws pad one: the d-pad as a cross, the other buttons
-- held as their names; the watch overlay the RAM cells, one a line.
local directions, buttons = {}, {}
for mask, fields in pairs(pads[1]) do
    local name = fields[1].name:gsub("^P%d+%s+", "")
    local lower = name:lower()
    local direction = lower:match("up") or lower:match("down") or lower:match("left") or lower:match("right")
    if direction ~= nil then
        directions[direction] = mask
    else
        table.insert(buttons, {mask = mask, name = name})
    end
end
table.sort(buttons, function(a, b) return a.mask < b.mask end)
local function draw_input(mask)
    local w, h = screen.width, screen.height
    local x, y = w - 15, h - 15
    local arms = {up = {4, 0}, down = {4, 8}, left = {0, 4}, right = {8, 4}}
    screen:draw_box(x + 4, y + 4, x + 8, y + 8, 0xffa0a0a0, 0x80000000)
    for direction, at in pairs(arms) do
        local held_now = directions[direction] ~= nil and mask & directions[direction] ~= 0
        screen:draw_box(x + at[1], y + at[2], x + at[1] + 4, y + at[2] + 4, 0xffa0a0a0,
            held_now and 0xffffffff or 0x80000000)
    end
    local names = {}
    for _, button in ipairs(buttons) do
        if mask & button.mask ~= 0 then names[#names + 1] = button.name end
    end
    if #names > 0 then screen:draw_text("right", h - 25, table.concat(names, " "), 0xffffffff, 0xc0000000) end
end
local function draw_watch()
    if memory_space == nil then return end
    for i, cell in ipairs(watch) do
        local bytes = {}
        for k = 0, cell[3] - 1 do bytes[#bytes + 1] = string.format("%02X", memory_space:read_u8(cell[2] + k)) end
        screen:draw_text("left", (i - 1) * 9, cell[1] .. " " .. table.concat(bytes), 0xffffffff, 0xc0000000)
    end
end

if screen ~= nil and _G.retrolarium_draw_hook ~= nil then
    _G.retrolarium_draw_hook(function()
        if _G.retrolarium_generation ~= generation then return end
        if show_input then draw_input(inputs[done] ~= nil and inputs[done][1] or 0) end
        if show_watch then draw_watch() end
        if movie_mode == "" then return end
        local text = movie_mode == "record" and string.format("REC %d", done)
            or string.format("%d/%d", done, #log)
        screen:draw_text("left", screen.height - 9, text, 0xffffffff, 0xc0000000)
    end)
end

local status_count = 0
local function control_tick()
    local now = now_time()
    if last_time == nil then last_time = now end
    if machine.paused and now == last_time then
        if unpause_next then
            unpause_next = false
            emu.unpause()
        elseif rewind_pending ~= nil and rewind_pending[3] == "load" then
            -- The frames count again from the state's on the next redraw.
            rewind_load()
            return
        elseif capture_pending then
            capture_pending = false
            capture_state()
            if replay_to == nil and run_until == nil then apply_speed() end
            if not user_paused or replay_to ~= nil or run_until ~= nil then emu.unpause() end
        end
    end
    if now ~= last_time and latched_load ~= nil then
        -- The pads' latch now holds the state's frame: load it (the frame
        -- that just ran is dropped with the rest of this branch).
        last_time = now
        pending_load, latched_load = latched_load, nil
        machine:load(pending_load)
        if load_paused then emu.pause() end
    elseif now ~= last_time then
        last_time = now
        done = done + 1
        _G.retrolarium_movie.frame = done
        inputs[done] = {pad_mask(1), pad_mask(2)}
        if rewind_pending ~= nil then
            -- The frame that took the state's buttons in: load next.
            rewind_pending[3] = "load"
            emu.pause()
        -- A frame advance pauses by itself; its frame is not captured.
        elseif done % rewind_every == 0 and not stepping then
            capture_pending = true
            video.throttled = false
            emu.pause()
        end
        stepping = false
        if movie_mode == "record" then
            log[done] = inputs[done]
            if done % 600 == 0 then write_movie() end
        elseif movie_mode == "play" or movie_mode == "resume" then
            if movie_mode == "play" and movie_stop_at > 0 and done >= movie_stop_at then
                write_status()
                machine:exit()
                return
            end
            if done >= #log then
                set_pads(nil)
                if movie_mode == "resume" then
                    -- The recording goes on from the end of the movie.
                    movie_mode = "record"
                    set_event("recording")
                    user_paused = true
                    emu.pause()
                else
                    movie_mode = ""
                    set_event("ended")
                    if movie_close then
                        write_status()
                        machine:exit()
                        return
                    end
                end
            else
                set_pads(log[done + 1])
            end
        end
        if run_until ~= nil then
            local memory_ok, hit = pcall(stop_when[run_until][2], memory_space)
            if memory_ok and hit then
                stop_run("stopped-" .. run_until)
            elseif movie_mode ~= "play" and movie_mode ~= "resume" then
                -- The movie ended (and said so): the run is over.
                run_until = nil
                apply_speed()
            end
        end
        if replay_to ~= nil then
            if done >= replay_to then
                finish_rewind()
            elseif movie_mode ~= "play" and movie_mode ~= "resume" then
                set_pads(inputs[done + 1])
            end
        end
    end
    -- Actions after the frame is counted: a save then holds that frame, and
    -- a load waits a whole frame for the pads' latch.
    take_actions()
    if pending_osd ~= nil and pending_save == nil and pending_load == nil and latched_load == nil then
        osd_wait = osd_wait - 1
        if osd_wait <= 0 then
            machine:popmessage(pending_osd)
            pending_osd = nil
        end
    end
    status_count = status_count + 1
    if status_count % 4 == 0 then write_status() end
end

on_frame(function()
    frames = frames + 1
    apply_keys(movie_mode ~= "play" and movie_mode ~= "resume" and latched_load == nil and replay_to == nil
        and rewind_pending == nil)
    if frames % 6 == 0 then take_command() end
    if frames == 1 or frames % 15 == 0 then apply_picture() end
    if hidden and ready_check ~= nil then
        ready_frames = ready_check() and ready_frames + 1 or 0
        if ready_frames == 3 then
            print(string.format("Retrolarium: ready after %d frames.", frames))
            local file = io.open(ready_file, "w")
            if file ~= nil then
                file:write("ready")
                file:close()
            end
        end
    end
    if hidden then
        -- A script that never reports would keep the game fast: give up after
        -- a minute of game time.
        if exists(ready_file) or frames > 3600 then
            if frames > 3600 and not exists(ready_file) then
                print("Retrolarium: the game never reported ready; showing it anyway.")
            end
            hidden = false
            video.throttled, video.frameskip = was_throttled, was_frameskip
            -- Sound made while unthrottled is still queued for the speakers;
            -- let it drain silently for a quarter second.
            unmute_at = frames + 15
        end
    elseif unmute_at ~= nil and frames >= unmute_at then
        sound.ui_mute = was_muted
        unmute_at = nil
    end
    if not hidden and fast_forward ~= forwarding then
        forwarding = fast_forward
        apply_speed()
    end
    control_tick()
end)

for _, script in ipairs(game_scripts) do dofile(script) end
)LUA";

// MAME's name for the key of a key event; empty for keys it has no name for.
// Modifiers come from the scan code, so left and right stay apart (Qt
// reports the extended-key flag in bit 8 of the native scan code).
QString mameKey(const QKeyEvent* event) {
    switch (event->nativeScanCode()) {
    case 0x2a: return QStringLiteral("KEYCODE_LSHIFT");
    case 0x36: return QStringLiteral("KEYCODE_RSHIFT");
    case 0x1d: return QStringLiteral("KEYCODE_LCONTROL");
    case 0x11d: return QStringLiteral("KEYCODE_RCONTROL");
    case 0x38: return QStringLiteral("KEYCODE_LALT");
    case 0x138: return QStringLiteral("KEYCODE_RALT");
    default: break;
    }
    const int key = event->key();
    const bool keypad = event->modifiers() & Qt::KeypadModifier;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QStringLiteral("KEYCODE_") + QChar(key);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QStringLiteral("KEYCODE_%1%2").arg(QChar(key)).arg(keypad ? QStringLiteral("_PAD") : QString());
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return QStringLiteral("KEYCODE_F%1").arg(key - Qt::Key_F1 + 1);
    }
    switch (key) {
    case Qt::Key_Up: return QStringLiteral("KEYCODE_UP");
    case Qt::Key_Down: return QStringLiteral("KEYCODE_DOWN");
    case Qt::Key_Left: return QStringLiteral("KEYCODE_LEFT");
    case Qt::Key_Right: return QStringLiteral("KEYCODE_RIGHT");
    case Qt::Key_Return: return QStringLiteral("KEYCODE_ENTER");
    case Qt::Key_Enter: return QStringLiteral("KEYCODE_ENTER_PAD");
    case Qt::Key_Space: return QStringLiteral("KEYCODE_SPACE");
    case Qt::Key_Tab: return QStringLiteral("KEYCODE_TAB");
    case Qt::Key_Backspace: return QStringLiteral("KEYCODE_BACKSPACE");
    case Qt::Key_Escape: return QStringLiteral("KEYCODE_ESC");
    case Qt::Key_Insert: return QStringLiteral("KEYCODE_INSERT");
    case Qt::Key_Delete: return QStringLiteral("KEYCODE_DEL");
    case Qt::Key_Home: return QStringLiteral("KEYCODE_HOME");
    case Qt::Key_End: return QStringLiteral("KEYCODE_END");
    case Qt::Key_PageUp: return QStringLiteral("KEYCODE_PGUP");
    case Qt::Key_PageDown: return QStringLiteral("KEYCODE_PGDN");
    case Qt::Key_Minus: return keypad ? QStringLiteral("KEYCODE_MINUS_PAD") : QStringLiteral("KEYCODE_MINUS");
    case Qt::Key_Plus: return keypad ? QStringLiteral("KEYCODE_PLUS_PAD") : QString();
    case Qt::Key_Asterisk: return keypad ? QStringLiteral("KEYCODE_ASTERISK") : QString();
    case Qt::Key_Slash: return keypad ? QStringLiteral("KEYCODE_SLASH_PAD") : QStringLiteral("KEYCODE_SLASH");
    case Qt::Key_Equal: return QStringLiteral("KEYCODE_EQUALS");
    case Qt::Key_BracketLeft: return QStringLiteral("KEYCODE_OPENBRACE");
    case Qt::Key_BracketRight: return QStringLiteral("KEYCODE_CLOSEBRACE");
    case Qt::Key_Semicolon: return QStringLiteral("KEYCODE_COLON");
    case Qt::Key_Apostrophe: return QStringLiteral("KEYCODE_QUOTE");
    case Qt::Key_Backslash: return QStringLiteral("KEYCODE_BACKSLASH");
    case Qt::Key_Comma: return QStringLiteral("KEYCODE_COMMA");
    case Qt::Key_Period: return keypad ? QStringLiteral("KEYCODE_DEL_PAD") : QStringLiteral("KEYCODE_STOP");
    case Qt::Key_QuoteLeft: return QStringLiteral("KEYCODE_TILDE");
    default: return {};
    }
}

// Keys the studio keeps while the game has the keyboard: playtest controls,
// and frame advance and speed (\ - =, as FCEUX has them).
bool isStudioKey(const QKeyEvent* event) {
    const int key = event->key();
    const bool keypad = event->modifiers() & Qt::KeypadModifier;
    return (key >= Qt::Key_F1 && key <= Qt::Key_F35) || key == Qt::Key_Pause || key == Qt::Key_Backslash ||
           (!keypad && (key == Qt::Key_Minus || key == Qt::Key_Equal));
}

QString luaPath(const QString& path) {
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

// The spec's arguments with the parts the runner owns taken out: its
// autoboot scripts (run inside the control script, in order) and the
// cartridge (a staged copy is mounted instead, marked here by @CART@).
struct Launch {
    QStringList arguments;
    QStringList gameScripts;
    QString cart;
};

Launch parseLaunch(const QStringList& arguments) {
    Launch launch;
    for (int i = 0; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        const bool hasValue = i + 1 < arguments.size();
        if (hasValue && argument == QLatin1String("-autoboot_script")) {
            launch.gameScripts << arguments.at(++i);
        } else if (hasValue && argument == QLatin1String("-autoboot_delay")) {
            ++i;
        } else if (hasValue && argument == QLatin1String("-cart")) {
            launch.cart = arguments.at(++i);
            launch.arguments << argument << QStringLiteral("@CART@");
        } else {
            launch.arguments << argument;
        }
    }
    return launch;
}
}  // namespace

EmulatorSurface::EmulatorSurface(QWidget* parent) : QWidget(parent) {
    // MAME needs a real HWND to attach to.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    // Tells the frame not to open the window menu on Alt or Alt+Space, which
    // are game buttons here.
    setProperty("rlOwnsKeyboard", true);
}

void EmulatorSurface::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    // Ahead of every other filter, so menus and shortcuts never see the keys.
    qApp->installEventFilter(this);
    emit focusChanged(true);
}

void EmulatorSurface::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    qApp->removeEventFilter(this);
    emit focusChanged(false);
}

bool EmulatorSurface::eventFilter(QObject* watched, QEvent* event) {
    if (watched != this && watched != windowHandle()) {
        return false;
    }
    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease) {
        return false;
    }
    auto* key = static_cast<QKeyEvent*>(event);
    if (isStudioKey(key)) {
        return false;
    }
    if (type == QEvent::ShortcutOverride) {
        event->accept();  // no studio shortcut fires on a game key
        return true;
    }
    if (!key->isAutoRepeat()) {
        const QString name = mameKey(key);
        if (!name.isEmpty()) {
            emit keyChanged(name, type == QEvent::KeyPress);
        }
    }
    return true;
}

bool EmulatorSurface::focusNextPrevChild(bool) {
    return false;  // Tab is a key for the game
}

void EmulatorSurface::mousePressEvent(QMouseEvent* event) {
    emit clicked();
    QWidget::mousePressEvent(event);
}

void EmulatorSurface::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

EmulatorRunner::EmulatorRunner(QObject* parent) : QObject(parent), startupTimer_(new QTimer(this)) {
    const QSettings settings;
    squarePixels_ = settings.value(QStringLiteral("playtest/squarePixels"), true).toBool();
    studioColours_ = settings.value(QStringLiteral("playtest/studioColours"), true).toBool();
    startupTimer_->setInterval(kStartupPollMs);
    connect(startupTimer_, &QTimer::timeout, this, &EmulatorRunner::watchStartup);
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(kStatusPollMs);
    connect(statusTimer_, &QTimer::timeout, this, &EmulatorRunner::readStatus);
}

EmulatorRunner::~EmulatorRunner() {
    finished_ = {};
    stopNow();
}

void EmulatorRunner::setPhase(Phase phase) {
    if (phase_ != phase) {
        phase_ = phase;
        emit stateChanged();
    }
}

// MAME draws straight into the surface (-attach_window creates no window of
// its own), so there is nothing to wait for on screen: the plugin names a
// file the game writes when it is up, and without one the emulator counts as
// up after a moment.
void EmulatorRunner::watchStartup() {
    if (!process_) {
        startupTimer_->stop();
        return;
    }
    ++startupChecks_;
    if (!readyFile_.isEmpty()) {
        if (QFile::exists(readyFile_)) {
            startupTimer_->stop();
            setPhase(Phase::Running);
            return;
        }
    }
    if (startupChecks_ >= (readyFile_.isEmpty() ? kQuietStartPolls : kStartupPolls)) {
        startupTimer_->stop();
        setPhase(Phase::Running);
    }
}

void EmulatorRunner::setSurface(EmulatorSurface* surface) {
    if (surface_ == surface) {
        return;
    }
    if (surface_) {
        surface_->disconnect(this);
    }
    surface_ = surface;
    if (surface_) {
        connect(surface_, &EmulatorSurface::clicked, this, [this] { focusEmulator(); });
        connect(surface_, &EmulatorSurface::keyChanged, this, &EmulatorRunner::setKey);
        connect(surface_, &EmulatorSurface::focusChanged, this, [this](bool focused) {
            if (!focused) {
                releaseKeys();
            }
        });
    }
}

void EmulatorRunner::setKey(const QString& key, bool pressed) {
    const bool changed = pressed ? !keys_.contains(key) : keys_.contains(key);
    if (!changed) {
        return;
    }
    if (pressed) {
        keys_.insert(key);
    } else {
        keys_.remove(key);
    }
    writeKeys();
}

void EmulatorRunner::releaseKeys() {
    if (!keys_.isEmpty()) {
        keys_.clear();
        writeKeys();
    }
}

void EmulatorRunner::setPicture(bool squarePixels, bool studioColours) {
    squarePixels_ = squarePixels;
    studioColours_ = studioColours;
    writePicture();
}

void EmulatorRunner::writePicture() {
    if (!stage_ || !stage_->isValid()) {
        return;
    }
    QFile file(stage_->filePath(QStringLiteral("picture.txt")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QStringList words;
        words << (squarePixels_ ? QStringLiteral("square") : QStringLiteral("tv"));
        words << (studioColours_ ? QStringLiteral("studio") : QStringLiteral("emulator"));
        file.write(words.join(QLatin1Char(' ')).toUtf8());
    }
}

namespace {
// Replaces a file the control script reads each frame in one step.
bool replaceFile(const QString& path, const QByteArray& data) {
    QFile file(path + QStringLiteral(".tmp"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(data);
    file.close();
    // MoveFileEx replaces the file in one step. A replace that meets the
    // script's read fails with a sharing violation: try again, or a released
    // key would stay held in the game.
#ifdef Q_OS_WIN
    // The names outlive the call (a pointer into a temporary would not).
    const QString source = QDir::toNativeSeparators(file.fileName());
    const QString target = QDir::toNativeSeparators(path);
    const auto from = reinterpret_cast<const wchar_t*>(source.utf16());
    const auto to = reinterpret_cast<const wchar_t*>(target.utf16());
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING)) {
            return true;
        }
        Sleep(1);
    }
    return false;
#else
    QFile::remove(path);
    return QFile::rename(file.fileName(), path);
#endif
}

// The SHA-1 of the image a run mounts, for its save-state folder.
QString imageKey(const QStringList& arguments) {
    for (int i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("-cart") || arguments.at(i) == QLatin1String("-flop")) {
            QFile file(arguments.at(i + 1));
            if (file.open(QIODevice::ReadOnly)) {
                return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex());
            }
        }
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(arguments.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha1).toHex());
}
}  // namespace

void EmulatorRunner::writeKeys() {
    if (!stage_ || !stage_->isValid()) {
        return;
    }
    QStringList keys = keys_.values();
    keys.sort();
    if (!replaceFile(stage_->filePath(QStringLiteral("keys.txt")), keys.join(QLatin1Char(' ')).toUtf8())) {
        // The script was reading it: a released key must not stay held.
        QTimer::singleShot(kRewriteMs, this, &EmulatorRunner::writeKeys);
    }
}

void EmulatorRunner::writeActions() {
    if (!controls_ || !stage_ || !stage_->isValid() || actions_.isEmpty()) {
        return;
    }
    if (!replaceFile(stage_->filePath(QStringLiteral("actions.txt")),
                     (actions_.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8())) {
        QTimer::singleShot(kRewriteMs, this, &EmulatorRunner::writeActions);
    }
}

void EmulatorRunner::sendAction(const QString& verb, const QString& argument) {
    if (!controls_ || !stage_ || !stage_->isValid()) {
        return;
    }
    actions_ << QStringLiteral("%1 %2 %3").arg(++actionSerial_).arg(verb, argument).trimmed();
    writeActions();
}

void EmulatorRunner::readStatus() {
    if (!controls_ || !stage_ || !stage_->isValid()) {
        return;
    }
    QFile file(stage_->filePath(QStringLiteral("status.txt")));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QHash<QString, QString> status;
    for (const QByteArray& line : file.readAll().split('\n')) {
        const int space = line.indexOf(' ');
        if (space > 0) {
            status.insert(QString::fromUtf8(line.left(space)), QString::fromUtf8(line.mid(space + 1)).trimmed());
        }
    }
    // Actions the script has taken leave the queue.
    const int ack = status.value(QStringLiteral("ack")).toInt();
    while (!actions_.isEmpty() && actions_.first().section(QLatin1Char(' '), 0, 0).toInt() <= ack) {
        actions_.removeFirst();
    }
    const QString mode = status.value(QStringLiteral("mode"), QStringLiteral("none"));
    const int frame = status.value(QStringLiteral("frame")).toInt();
    const int length = status.value(QStringLiteral("length")).toInt();
    if (mode != movieMode_ || frame != movieFrame_ || length != movieLength_) {
        movieMode_ = mode;
        movieFrame_ = frame;
        movieLength_ = length;
        emit emulationChanged();
    }
    // The script pauses by itself when Resume reaches the end of the movie;
    // a pause or resume still on its way is not contradicted.
    const bool scriptPaused = status.value(QStringLiteral("paused")) == QLatin1String("1");
    const bool pending = std::any_of(actions_.cbegin(), actions_.cend(), [](const QString& action) {
        const QString verb = action.section(QLatin1Char(' '), 1, 1);
        return verb == QLatin1String("pause") || verb == QLatin1String("resume") || verb == QLatin1String("step");
    });
    if (!pending && scriptPaused != paused_) {
        paused_ = scriptPaused;
        emit stateChanged();
    }
    const QString event = status.value(QStringLiteral("event"));
    const int serial = event.section(QLatin1Char(' '), 0, 0).toInt();
    if (serial > eventSerial_) {
        eventSerial_ = serial;
        emit emulationEvent(event.section(QLatin1Char(' '), 1));
    }
}

QString EmulatorRunner::statePath(int slot) const {
    return QDir(stateDir_).filePath(QStringLiteral("slot%1.sta").arg(slot));
}

bool EmulatorRunner::slotHasState(int slot) const {
    return !stateDir_.isEmpty() && QFile::exists(statePath(slot));
}

int EmulatorRunner::slotFrame(int slot) const {
    QFile file(statePath(slot) + QStringLiteral(".input"));
    if (stateDir_.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray first = file.readLine().trimmed();
    return first.startsWith("frame ") ? first.mid(6).toInt() : -1;
}

void EmulatorRunner::setSpeed(int percent) {
    speed_ = std::max(0, percent);
    sendAction(QStringLiteral("speed"), QString::number(speed_));
    emit emulationChanged();
}

void EmulatorRunner::frameAdvance() {
    if (!controls_ || !process_) {
        return;
    }
    if (!paused_) {
        // The first press pauses; each one after it runs one frame.
        togglePause();
        return;
    }
    sendAction(QStringLiteral("step"));
}

void EmulatorRunner::setStateSlot(int slot) {
    slot_ = (slot % 10 + 10) % 10;
    emit emulationChanged();
}

void EmulatorRunner::saveState() {
    if (!controls_ || !process_ || stateDir_.isEmpty()) {
        return;
    }
    QDir().mkpath(stateDir_);
    sendAction(QStringLiteral("save"), QDir::fromNativeSeparators(statePath(slot_)));
}

void EmulatorRunner::loadState() {
    if (!controls_ || !process_) {
        return;
    }
    if (!slotHasState(slot_)) {
        emit emulationEvent(QStringLiteral("no-state"));
        return;
    }
    sendAction(QStringLiteral("load"), QDir::fromNativeSeparators(statePath(slot_)));
}

void EmulatorRunner::stopMovie() {
    sendAction(QStringLiteral("stopmovie"));
}

void EmulatorRunner::seek(int frame) {
    if (controls_ && process_) sendAction(QStringLiteral("seek"), QString::number(std::max(0, frame)));
}

void EmulatorRunner::reloadMovie() {
    if (controls_ && process_ && !movie_.file.isEmpty()) {
        sendAction(QStringLiteral("reload"), QDir::fromNativeSeparators(movie_.file));
    }
}

void EmulatorRunner::runUntil(int condition) {
    // The script counts its conditions from 1.
    if (controls_ && process_) sendAction(QStringLiteral("until"), QString::number(condition + 1));
}

void EmulatorRunner::recordFromHere() {
    if (controls_ && process_) sendAction(QStringLiteral("record"));
}

void EmulatorRunner::rewind(int frames) {
    if (!controls_ || !process_) {
        return;
    }
    sendAction(QStringLiteral("rewind"), QString::number(frames));
}

namespace {

QString overlayText(bool input, bool watch) {
    QStringList parts;
    if (input) parts << QStringLiteral("input");
    if (watch) parts << QStringLiteral("watch");
    return parts.join(QLatin1Char(' '));
}

}  // namespace

void EmulatorRunner::setOverlay(bool input, bool watch) {
    showInput_ = input;
    showWatch_ = watch;
    if (controls_ && process_) {
        sendAction(QStringLiteral("overlay"), overlayText(input, watch));
    }
}

bool EmulatorRunner::showMessage(const QString& text) {
    if (!controls_ || !process_) {
        return false;
    }
    sendAction(QStringLiteral("osd"), text.simplified());
    return true;
}

void EmulatorRunner::append(const QString& line) {
    log_.append(line);
    while (log_.size() > kLogLines) {
        log_.removeFirst();
    }
    emit outputLine(line);
}

bool EmulatorRunner::writeControl(const sdk::ProcessSpec& spec, const QStringList& gameScripts,
                                  QString* error) {
    const QString command = luaPath(stage_->filePath(QStringLiteral("command.txt")));
    const QString keys = luaPath(stage_->filePath(QStringLiteral("keys.txt")));
    const QString picture = luaPath(stage_->filePath(QStringLiteral("picture.txt")));
    QStringList pens;
    for (QRgb color : spec.palette) {
        pens << QStringLiteral("0x%1").arg(color & 0xffffff, 6, 16, QLatin1Char('0'));
    }
    hasPalette_ = !spec.palette.isEmpty();
    writePicture();
    QStringList values{luaPath(cart_), command, luaPath(spec.readyFile), keys, spec.readyWhen};
    QStringList scripts;
    for (const QString& script : gameScripts) {
        values << luaPath(script);
        scripts << QStringLiteral("[==[%1]==]").arg(luaPath(script));
    }
    QString environment;
    for (auto it = spec.environment.constBegin(); it != spec.environment.constEnd(); ++it) {
        static const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
        if (!name.match(it.key()).hasMatch()) {
            continue;
        }
        environment += QStringLiteral("    [\"%1\"] = [==[%2]==],\n").arg(it.key(), it.value());
        values << it.value();
    }
    const QString actions = luaPath(stage_->filePath(QStringLiteral("actions.txt")));
    const QString status = luaPath(stage_->filePath(QStringLiteral("status.txt")));
    using Mode = MovieRequest::Mode;
    const QString movieMode = movie_.mode == Mode::Record ? QStringLiteral("record")
                              : movie_.mode == Mode::Play   ? QStringLiteral("play")
                              : movie_.mode == Mode::Resume ? QStringLiteral("resume")
                                                            : QString();
    const QString movieFile = movieMode.isEmpty() ? QString() : luaPath(movie_.file);
    QStringList cells;
    for (const WatchCell& cell : watch_) {
        values << cell.name;
        cells << QStringLiteral("{[==[%1]==], %2, %3}").arg(cell.name).arg(cell.address).arg(cell.length);
    }
    QStringList stops;
    for (const StopCondition& stop : stops_) {
        values << stop.name << stop.lua;
        stops << QStringLiteral("{[==[%1]==], [==[%2]==]}").arg(stop.name, stop.lua);
    }
    values << actions << status << movieFile << movie_.header;
    for (const QString& value : values) {
        if (value.contains(QLatin1String("]==]"))) {
            *error = tr("a path or setting cannot be quoted for Lua");
            return false;
        }
    }
    QFile file(stage_->filePath(QStringLiteral("control.lua")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = file.errorString();
        return false;
    }
    QString text = QString::fromUtf8(kControlScript);
    text.replace(QLatin1String("@CART@"), values.at(0))
        .replace(QLatin1String("@COMMAND@"), values.at(1))
        .replace(QLatin1String("@KEYS@"), values.at(3))
        .replace(QLatin1String("@PICTURE@"), picture)
        .replace(QLatin1String("@PALETTE@"), pens.join(QLatin1Char(',')))
        .replace(QLatin1String("@READYWHEN@"), values.at(4))
        .replace(QLatin1String("@READY@"), values.at(2))
        .replace(QLatin1String("@SCRIPTS@"), scripts.join(QStringLiteral(", ")))
        .replace(QLatin1String("@ACTIONS@"), actions)
        .replace(QLatin1String("@STATUS@"), status)
        .replace(QLatin1String("@MOVIEMODE@"), movieMode)
        .replace(QLatin1String("@MOVIEFILE@"), movieFile)
        .replace(QLatin1String("@MOVIESTOPAT@"), QString::number(movie_.stopAt))
        .replace(QLatin1String("@MOVIECLOSE@"), movie_.closeAtEnd ? QStringLiteral("1") : QString())
        .replace(QLatin1String("@ENVIRONMENT@"), environment)
        .replace(QLatin1String("@OVERLAY@"), overlayText(showInput_, showWatch_))
        .replace(QLatin1String("@WATCH@"), cells.join(QStringLiteral(", ")))
        .replace(QLatin1String("@STOPWHEN@"), stops.join(QStringLiteral(", ")))
        // Last: the header holds the user's own words.
        .replace(QLatin1String("@MOVIEHEADER@"), movie_.header);
    file.write(text.toUtf8());
    return true;
}

QString EmulatorRunner::stageCart(const QString& source, QString* error) {
    const QString path = stage_->filePath(QStringLiteral("cart-%1.%2")
                                              .arg(++cartSerial_)
                                              .arg(QFileInfo(source).suffix()));
    if (!QFile::copy(source, path)) {
        *error = tr("could not copy %1").arg(source);
        return {};
    }
    return path;
}

void EmulatorRunner::start(const sdk::ProcessSpec& requested, const QString& title,
                           std::function<void(const sdk::ProcessResult&)> finished, const MovieRequest& movie) {
    sdk::ProcessSpec spec = requested;
    const Launch launch = parseLaunch(spec.arguments);
    const QStringList signature = QStringList{spec.program, spec.workingDirectory} + launch.arguments;
    const bool movieRun = movie.mode != MovieRequest::Mode::None;
    if (!movieRun && movie_.mode == MovieRequest::Mode::None && process_ && phase_ != Phase::Stopping && stage_ &&
        !cart_.isEmpty() && !launch.cart.isEmpty() && signature == signature_ &&
        reload(spec, launch.cart, launch.gameScripts, title, finished)) {
        return;
    }
    stopNow();
    movie_ = movie;
    actions_.clear();
    actionSerial_ = 0;
    eventSerial_ = 0;
    movieMode_ = QStringLiteral("none");
    movieFrame_ = movieLength_ = 0;
    controls_ = false;
    // Save states belong to the image: a rebuilt game gets its own.
    stateDir_ = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                    .filePath(QStringLiteral("states/") +
                              (movie.stateKey.isEmpty() ? imageKey(spec.arguments) : movie.stateKey));
    if (!surface_) {
        sdk::ProcessResult result;
        result.error = tr("The Playtest view is not open.");
        if (finished) {
            finished(result);
        }
        return;
    }
    title_ = title;
    fastStart_ = false;
    reloading_ = false;
    log_.clear();
    paused_ = false;
    stopping_ = false;
    finished_ = std::move(finished);

    // MAME keeps the cartridge it runs locked; it gets a copy, so the build can
    // replace the original while the game runs.
    stage_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("retrolarium-playtest-XXXXXX")));
    if (spec.readyFile.isEmpty() && !spec.readyWhen.isEmpty() && stage_->isValid()) {
        spec.readyFile = stage_->filePath(QStringLiteral("ready.txt"));
    }
    readyFile_ = spec.readyFile;
    if (!readyFile_.isEmpty()) {
        // A file from the last run would end the wait at once.
        QFile::remove(readyFile_);
    }
    cart_.clear();
    signature_ = signature;
    QStringList arguments = launch.arguments;
    if (!launch.cart.isEmpty()) {
        QString error;
        cart_ = stage_->isValid() ? stageCart(launch.cart, &error) : QString();
        if (cart_.isEmpty()) {
            append(tr("Hot reload is off: %1").arg(error.isEmpty() ? stage_->errorString() : error));
        }
        arguments.replaceInStrings(QStringLiteral("@CART@"), cart_.isEmpty() ? launch.cart : luaPath(cart_));
    }
    // Fit MAME's picture into the panel at the screen's aspect instead of
    // integer steps, and draw into our native surface.
    arguments.removeAll(QStringLiteral("-nounevenstretch"));
    arguments.removeAll(QStringLiteral("-window"));
    arguments << QStringLiteral("-attach_window")
              << QString::number(static_cast<qulonglong>(surface_->winId()))
              << QStringLiteral("-window") << QStringLiteral("-keepaspect")
              << QStringLiteral("-unevenstretch") << QStringLiteral("-keyboardprovider")
              << QStringLiteral("win32")
              // MAME reads input, game pads included, only while it believes
              // its window has focus. The surface belongs to our process, so
              // it never does: without this the pad stays silent.
              << QStringLiteral("-background_input");
    // The control script; without it, the game's own script as given.
    QString error;
    if (stage_->isValid() && writeControl(spec, launch.gameScripts, &error)) {
        arguments << QStringLiteral("-autoboot_delay") << QStringLiteral("0") << QStringLiteral("-autoboot_script")
                  << luaPath(stage_->filePath(QStringLiteral("control.lua")));
        // A game that reports when it is up gets a fast, hidden start.
        fastStart_ = !readyFile_.isEmpty();
        controls_ = true;
        if (speed_ != 100) {
            sendAction(QStringLiteral("speed"), QString::number(speed_));
        }
    } else {
        append(tr("Fast start and hot reload are off: %1")
                   .arg(error.isEmpty() ? stage_->errorString() : error));
        cart_.clear();
        // Without the control script nothing writes the staged ready file:
        // waiting for it would only hold the view in Starting.
        if (stage_->isValid() && readyFile_.startsWith(stage_->path())) {
            readyFile_.clear();
        }
        // MAME runs one script: the last, as it would have taken it anyway.
        if (!launch.gameScripts.isEmpty()) {
            arguments << QStringLiteral("-autoboot_delay") << QStringLiteral("0")
                      << QStringLiteral("-autoboot_script") << launch.gameScripts.last();
        }
    }

    acknowledgeWarnings(spec);

    process_ = new QProcess(this);
    process_->setProgram(spec.program);
    process_->setArguments(arguments);
    process_->setWorkingDirectory(spec.workingDirectory);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = spec.environment.constBegin(); it != spec.environment.constEnd(); ++it) {
        environment.insert(it.key(), it.value());
    }
    process_->setProcessEnvironment(environment);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyRead, this, [this] {
        const QStringList lines = QString::fromLocal8Bit(process_->readAll()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (!line.trimmed().isEmpty()) {
                append(line.trimmed());
            }
        }
    });
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                sdk::ProcessResult result;
                result.started = true;
                result.exitCode = code;
                result.cancelled = stopping_;
                result.crashed = status == QProcess::CrashExit && !stopping_;
                append(stopping_ ? tr("Stopped.")
                                 : tr("Exited with code %1.").arg(code));
                // The script's last word, before its folder goes.
                readStatus();
                statusTimer_->stop();
                controls_ = false;
                movieMode_ = QStringLiteral("none");
                process_->deleteLater();
                process_ = nullptr;
                paused_ = false;
                startupTimer_->stop();
                phase_ = Phase::Idle;
                // MAME has let go of the cartridge copies.
                stage_.reset();
                cart_.clear();
                keys_.clear();
                auto finished = std::move(finished_);
                finished_ = {};
                emit stateChanged();
                if (surface_) {
                    surface_->update();
                }
                if (finished) {
                    finished(result);
                }
            });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !process_) {
            return;
        }
        sdk::ProcessResult result;
        result.error = tr("The emulator could not start: %1").arg(process_->errorString());
        append(result.error);
        process_->deleteLater();
        process_ = nullptr;
        startupTimer_->stop();
        statusTimer_->stop();
        controls_ = false;
        phase_ = Phase::Idle;
        stage_.reset();
        cart_.clear();
        auto finished = std::move(finished_);
        finished_ = {};
        emit stateChanged();
        if (finished) {
            finished(result);
        }
    });
    append(tr("Starting %1").arg(title_));
    phase_ = Phase::Starting;
    startupChecks_ = 0;
    process_->start();
    startupTimer_->start();
    if (controls_) {
        statusTimer_->start();
    }
    emit stateChanged();
}

bool EmulatorRunner::reload(const sdk::ProcessSpec& requested, const QString& cartSource, const QStringList& gameScripts,
                            const QString& title, std::function<void(const sdk::ProcessResult&)>& finished) {
    sdk::ProcessSpec spec = requested;
    if (spec.readyFile.isEmpty() && !spec.readyWhen.isEmpty()) {
        spec.readyFile = stage_->filePath(QStringLiteral("ready.txt"));
    }
    // The copies MAME no longer holds; the mounted one stays locked.
    for (const QString& name : QDir(stage_->path()).entryList({QStringLiteral("cart-*")}, QDir::Files)) {
        QFile::remove(stage_->filePath(name));
    }
    QString error;
    const QString previous = cart_;
    const QString next = stageCart(cartSource, &error);
    if (next.isEmpty()) {
        append(tr("Hot reload failed: %1; restarting.").arg(error));
        return false;
    }
    cart_ = next;
    if (!writeControl(spec, gameScripts, &error)) {
        cart_ = previous;
        append(tr("Hot reload failed: %1; restarting.").arg(error));
        return false;
    }
    if (paused_) {
        togglePause();
    }
    releaseKeys();
    title_ = title;
    readyFile_ = spec.readyFile;
    fastStart_ = !readyFile_.isEmpty();
    reloading_ = true;
    if (!readyFile_.isEmpty()) {
        QFile::remove(readyFile_);
    }
    // The control script picks the command up within a few frames, mounts
    // the new copy and the machine hard-resets into it.
    const QString command = stage_->filePath(QStringLiteral("command.txt"));
    QFile file(command + QStringLiteral(".tmp"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        append(tr("Hot reload failed: %1; restarting.").arg(file.errorString()));
        return false;
    }
    file.write(luaPath(cart_).toUtf8() + '\n');
    file.close();
    QFile::remove(command);
    QFile::rename(file.fileName(), command);
    // The run the reload replaces ends here, as if stopped: whoever started
    // it (a recording waiting for its end) hears about it.
    auto replaced = std::move(finished_);
    finished_ = std::move(finished);
    if (replaced) {
        sdk::ProcessResult result;
        result.started = true;
        result.cancelled = true;
        replaced(result);
    }
    append(tr("Reloading %1").arg(title_));
    phase_ = Phase::Starting;
    startupChecks_ = 0;
    startupTimer_->start();
    emit stateChanged();
    return true;
}

void EmulatorRunner::acknowledgeWarnings(const sdk::ProcessSpec& spec) {
    // MAME's "known problems" screen waits for a key; an embedded MAME never
    // gets one. Mark it as seen in the system's configuration instead.
    const QString system = spec.arguments.value(0);
    if (system.isEmpty() || system.startsWith(QLatin1Char('-'))) {
        return;
    }
    QString error;
    const QVector<mame::Feature> features = mame::warnedFeatures(spec.program, system, &error);
    if (features.isEmpty()) {
        if (!error.isEmpty()) {
            append(tr("MAME warning check: %1").arg(error));
        }
        return;
    }
    const int option = spec.arguments.indexOf(QStringLiteral("-cfg_directory"));
    const QString configDirectory = QDir(spec.workingDirectory)
                                        .filePath(option >= 0 ? spec.arguments.value(option + 1) : QStringLiteral("cfg"));
    QStringList names;
    for (const mame::Feature& feature : features) {
        names << QStringLiteral("%1 %2").arg(feature.status, feature.type);
    }
    if (mame::acknowledgeWarnings(configDirectory, system, features, &error)) {
        append(tr("MAME's known-problems screen for %1 (%2) is marked as seen; emulation is unchanged.")
                   .arg(system, names.join(QStringLiteral(", "))));
    } else {
        append(tr("MAME warning setup: %1").arg(error));
        return;
    }
    // The mark counts only with MAME's skip_warnings option on.
    bool changed = false;
    if (!mame::enableSkipWarnings(spec.workingDirectory, &changed, &error)) {
        append(tr("MAME warning setup: %1").arg(error));
    } else if (changed) {
        append(tr("MAME's skip_warnings option is turned on in its ui.ini, so it honours that mark."));
    }
}

void EmulatorRunner::finishMovie() {
    // MAME embedded in the studio may be ended before its own stop handler
    // runs: a recording is written by the script first, and the studio waits
    // (briefly) until the script says it has done it.
    if (!controls_ || !process_ || movieMode_ != QLatin1String("record")) {
        return;
    }
    sendAction(QStringLiteral("stopmovie"));
    const int serial = actionSerial_;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < kMovieFlushMs) {
        readStatus();
        if (actions_.isEmpty() || actions_.first().section(QLatin1Char(' '), 0, 0).toInt() > serial) {
            return;
        }
        QThread::msleep(20);
    }
    append(tr("The movie may have lost its last seconds: the emulator did not answer."));
}

void EmulatorRunner::stop() {
    if (!process_ || phase_ == Phase::Stopping) {
        return;
    }
    finishMovie();
    stopping_ = true;
    if (paused_) {
        togglePause();
    }
    startupTimer_->stop();
    setPhase(Phase::Stopping);
    // The control script ends MAME at once; a close message would not reach
    // an embedded MAME, which is ended by force when it lingers.
    if (controls_) {
        sendAction(QStringLiteral("exit"));
    } else {
        process_->terminate();
    }
    QPointer<QProcess> process = process_;
    QTimer::singleShot(kStopGraceMs, this, [process] {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
}

void EmulatorRunner::stopNow() {
    if (!process_) {
        return;
    }
    finishMovie();
    stopping_ = true;
    if (paused_) {
        togglePause();
    }
    if (controls_) {
        sendAction(QStringLiteral("exit"));
    } else {
        process_->terminate();
    }
    if (!process_->waitForFinished(1500)) {
        process_->kill();
        process_->waitForFinished(1500);
    }
    if (process_) {
        // It outlived both waits: let it go, so the next launch neither
        // inherits it nor gets its exit later.
        QProcess* stale = process_;
        stale->disconnect(this);
        stale->deleteLater();
        process_ = nullptr;
        paused_ = false;
        startupTimer_->stop();
        statusTimer_->stop();
        controls_ = false;
        movieMode_ = QStringLiteral("none");
        phase_ = Phase::Idle;
        stage_.reset();
        cart_.clear();
        keys_.clear();
        auto finished = std::move(finished_);
        finished_ = {};
        emit stateChanged();
        if (finished) {
            sdk::ProcessResult result;
            result.started = true;
            result.cancelled = true;
            finished(result);
        }
    }
}

bool EmulatorRunner::togglePause() {
    if (!process_) {
        return false;
    }
    if (controls_) {
        // The control script pauses the machine: it still draws, so the
        // picture stays live, and frame advance steps from there.
        sendAction(paused_ ? QStringLiteral("resume") : QStringLiteral("pause"));
        pausedPicture_ = QImage();
        paused_ = !paused_;
        append(paused_ ? tr("Paused.") : tr("Resumed."));
        emit stateChanged();
        return true;
    }
#ifdef Q_OS_WIN
    // MAME's own pause key is not bound in every configuration, so the whole
    // process is suspended instead; the picture freezes on the last frame.
    using ProcessCall = LONG(WINAPI*)(HANDLE);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto suspend = reinterpret_cast<ProcessCall>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtSuspendProcess")));
    const auto resume = reinterpret_cast<ProcessCall>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtResumeProcess")));
    if (!suspend || !resume) {
        return false;
    }
    HANDLE handle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, static_cast<DWORD>(process_->processId()));
    if (!handle) {
        return false;
    }
    // The frame being frozen, kept in colour for screenshots and the pause
    // cover. Taken first: a suspended process hands the compositor nothing.
    const QImage frame = paused_ ? QImage() : picture();
    const LONG status = paused_ ? resume(handle) : suspend(handle);
    CloseHandle(handle);
    if (status != 0) {
        return false;
    }
    pausedPicture_ = frame;
    paused_ = !paused_;
    append(paused_ ? tr("Paused.") : tr("Resumed."));
    emit stateChanged();
    return true;
#else
    return false;
#endif
}

void EmulatorRunner::focusEmulator() {
    // MAME draws into the surface itself; keys go wherever Qt's focus is.
    if (surface_) {
        surface_->setFocus(Qt::MouseFocusReason);
    }
}

#ifdef Q_OS_WIN
namespace {

// The surface's area of the studio window as the compositor shows it.
// PrintWindow with PW_RENDERFULLCONTENT (Windows 8.1+) sees the GPU picture
// MAME presents, which the surface's device context does not, and whatever
// lies over the studio. It works for top-level windows only, so the studio
// window is taken and cropped.
QImage composedArea(HWND surface) {
    constexpr UINT kClientFullContent = 3;  // PW_CLIENTONLY | PW_RENDERFULLCONTENT
    const HWND hwnd = GetAncestor(surface, GA_ROOT);
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT area{};
    GetClientRect(surface, &area);
    MapWindowPoints(surface, hwnd, reinterpret_cast<POINT*>(&area), 2);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || area.right <= area.left || area.bottom <= area.top) {
        return {};
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage image;
    if (dc && bitmap && bits) {
        HGDIOBJ previous = SelectObject(dc, bitmap);
        if (PrintWindow(hwnd, dc, kClientFullContent)) {
            image = QImage(static_cast<const uchar*>(bits), width, height, width * 4, QImage::Format_RGB32)
                        .copy(QRect(QPoint(area.left, area.top), QPoint(area.right - 1, area.bottom - 1)));
        }
        SelectObject(dc, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    ReleaseDC(nullptr, screenDc);
    return image;
}

bool isBlack(const QImage& image) {
    for (int y = 0; y < image.height(); y += 3) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += 3) {
            if ((line[x] & 0xffffff) != 0) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace
#endif

QImage EmulatorRunner::picture() const {
    QScreen* screen = surface_ && surface_->screen() ? surface_->screen() : QGuiApplication::primaryScreen();
    if (!process_ || !screen || !surface_) {
        return {};
    }
    if (paused_ && !pausedPicture_.isNull()) {
        return pausedPicture_;
    }
    QImage image;
#ifdef Q_OS_WIN
    // The compositor now and then hands back an empty frame for MAME's
    // picture: try a few vsyncs before believing the game shows black.
    const HWND surface = reinterpret_cast<HWND>(surface_->winId());
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt > 0) {
            Sleep(20);
        }
        image = composedArea(surface);
        if (!image.isNull() && !isBlack(image)) {
            return image;
        }
    }
    // Still black: the screen itself, when nothing can lie over the studio.
    if (GetForegroundWindow() != GetAncestor(surface, GA_ROOT)) {
        return image;
    }
#endif
    const QPoint origin = surface_->mapToGlobal(QPoint(0, 0)) - screen->geometry().topLeft();
    const QImage grabbed =
        screen->grabWindow(0, origin.x(), origin.y(), surface_->width(), surface_->height()).toImage();
    return grabbed.isNull() ? image : grabbed;
}

QString EmulatorRunner::saveScreenshot(const QString& directory, QString* error) {
    if (!process_ || !surface_) {
        if (error) {
            *error = tr("Nothing is running.");
        }
        return {};
    }
    const QImage image = picture();
    QDir().mkpath(directory);
    // "Flicky · Round 07" becomes "flicky-round-07".
    const QString name = title_.toLower()
                             .replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"))
                             .remove(QRegularExpression(QStringLiteral("^-|-$")));
    const QString path = QDir(directory).filePath(
        QStringLiteral("%1-%2.png")
            .arg(name.isEmpty() ? QStringLiteral("playtest") : name,
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    if (image.isNull() || !image.save(path, "PNG")) {
        if (error) {
            *error = tr("Could not save the screenshot to %1.").arg(path);
        }
        return {};
    }
    append(tr("Screenshot saved to %1").arg(path));
    return path;
}

}  // namespace rl::runtime
