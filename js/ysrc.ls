export test = -> 'ok'
export exec = (arg) -> eval arg.command
try
  r = new rpc "ws+unix://./nsgod.socket", (e) !-> debug "failed: #{e}"
  debug 1
  <-! r.start
  debug 2
  debug "loaded!"
  r.call "ping", {test: 23}, (e, obj) -> debug JSON.stringify obj
  r.call "version", {}, (e, obj) -> debug JSON.stringify obj
  r.on "started", (ev, data) -> debug data
catch e then debug e
