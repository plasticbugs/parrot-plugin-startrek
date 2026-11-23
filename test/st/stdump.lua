-- stdump.lua: run by test/st/mamerun.sh in a headless MAME. Each frame (at
-- the VBLANK start, where MAME's frame_done runs) it applies the inputs the
-- script file gives for that frame and appends the board's state to the
-- dump file, the same records st_host writes:
--   'STFR', frame (u32), RAM 0xc800 (0x800), vector RAM 0xe000 (0x1000),
--   the USB's program RAM (0x1000) and work RAM (0x400), the main CPU's
--   PC SP AF BC DE HL IX IY AF2 BC2 DE2 HL2 WZ as u16, R I IM IFF1 IFF2
--   HALT as u8, then for the USB's 8035 and the speech board's: PC (u16),
--   A, 0 (the PSW isn't in Lua's state), TC TPRE P1 P2, its 64 bytes of RAM
-- ST_SCRIPT, ST_DUMP, ST_FRAMES and ST_SNAPEVERY (a snapshot every Nth
-- frame) come from the environment. Script lines: "frame name value",
-- names coin1 coin2 service start1 start2 b1 b2 b3 b4 (1 pressed) and dial
-- (the spinner's move each frame, -128..127), held from that frame on.
if not _G.st_dump then
  _G.st_dump = true
  local script = os.getenv("ST_SCRIPT")
  local dumpname = os.getenv("ST_DUMP")
  local frames = tonumber(os.getenv("ST_FRAMES") or "600")
  local snapevery = tonumber(os.getenv("ST_SNAPEVERY") or "0")
  local ports = manager.machine.ioport.ports
  local function field(port, mask)
    for _, f in pairs(ports[port].fields) do if f.mask == mask then return f end end
    error("no field " .. port .. " " .. mask)
  end
  local fields = { coin1 = field(":D7D6", 0x01), coin2 = field(":D7D6", 0x10), service = field(":D5D4", 0x01),
                   start1 = field(":FC", 0x01), start2 = field(":FC", 0x02), b2 = field(":FC", 0x04),
                   b1 = field(":FC", 0x08), b3 = field(":FC", 0x10), b4 = field(":FC", 0x20),
                   dial = field(":SPINNER", 0xff) }
  local events = {}
  if script and script ~= "" then
    for line in io.lines(script) do
      local f, name, v = line:match("^(%d+)%s+(%w+)%s+(-?%d+)")
      if f then
        f = tonumber(f)
        events[f] = events[f] or {}
        table.insert(events[f], { name, tonumber(v) })
      end
    end
  end
  local main = manager.machine.devices[":maincpu"]
  local usb = manager.machine.devices[":usbsnd:ourcpu"]
  local sp = manager.machine.devices[":speech:cpu"]
  local mp = main.spaces["program"]
  local shares = manager.machine.memory.shares
  local pgm, work = shares[":usbsnd:pgmram"], shares[":usbsnd:workram"]
  assert(pgm and work, "no USB RAM shares")
  local out = dumpname and assert(io.open(dumpname, "wb"))
  local r16 = { "PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY", "AF2", "BC2", "DE2", "HL2", "WZ" }
  local r8 = { "R", "I", "IM", "IFF1", "IFF2", "HALT" }
  local function share_bytes(s, n)
    local t = {}
    for i = 0, n - 1 do t[#t + 1] = string.char(s:read_u8(i)) end
    return table.concat(t)
  end
  local function regs48(cpu, t)
    local st = cpu.state
    t[#t + 1] = string.pack("<I2BBBBBB", st["PC"].value, st["A"].value, 0, st["TC"].value,
                            st["TPRE"].value, st["P1"].value, st["P2"].value)
    t[#t + 1] = cpu.spaces["data"]:read_range(0, 63, 8)
  end
  local n = 0
  _G.st_sub = emu.register_frame_done(function()
    n = n + 1
    if snapevery > 0 and n % snapevery == 0 then manager.machine.video:snapshot() end
    for _, e in ipairs(events[n] or {}) do
      local v = e[2]
      if e[1] == "dial" then fields.dial:set_value(v & 0xff) else fields[e[1]]:set_value(v ~= 0 and 1 or 0) end
    end
    if out then
      local t = { "STFR", string.pack("<I4", n), mp:read_range(0xc800, 0xcfff, 8), mp:read_range(0xe000, 0xefff, 8),
                  share_bytes(pgm, 0x1000), share_bytes(work, 0x400) }
      for _, r in ipairs(r16) do t[#t + 1] = string.pack("<I2", main.state[r].value) end
      for _, r in ipairs(r8) do t[#t + 1] = string.pack("B", main.state[r].value) end
      regs48(usb, t)
      regs48(sp, t)
      out:write(table.concat(t))
    end
    if n >= frames then
      if out then out:close() end
      manager.machine:exit()
    end
  end)
end
