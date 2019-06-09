export ping = -> 'pong'
export exec = (arg) -> eval arg.command
try
  services.nsgod = r = new rpc "ws+unix://./nsgod.socket", (e) !-> debug "failed to load nsgod: #{e}"
  <-! r.start
  nsgod_loaded = true
  export nsgod = -> "ok"
  r.on "output", event "nsgod.output"
  r.on "started", event "nsgod.started"
  r.on "stopped", event "nsgod.stopped"
  r.on "updated", event "nsgod.updated"
catch e then debug e
