@echo off
rem play.cmd with a hardware write watch on PGR3's lighting table t11 (physical 0ED2D000, texel 16 at AED2D040):
rem the log (out\run\pgr3.log) then names the guest code that writes it, if the CPU does. Session 2 investigation.
set "PGR3_WATCHW=AED2D040"
call "%~dp0play.cmd"
